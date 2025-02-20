#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "threadpool.h"

typedef struct Job
{
    void *mArgs;
    threadpool_job_func_t mFunc;
} Job;

struct JobQueue
{
    pthread_mutex_t mQueueLock;
    struct Job *mJobs;
    ThreadPool *mThreadPool;
    pthread_cond_t mReadable;
    pthread_cond_t mWritable;
    int32_t mQueueSize; // Size of queue
    int32_t mQueueHead; // Head of queue
    int32_t mQueueTail; // Tail of queue
};

struct ThreadPool
{
    int32_t mNumOfThread;               // number of threads in the threadpool
    pthread_t *mThreads;                // pointer to threads
    volatile int32_t mShutDown;         // if shut down threadpool
    JobQueue *mJobQueue;                // job queue
};

static void            *_threadpool_thread(void *threadpool);
static void             _threadpool_free(ThreadPool *pool);
static struct JobQueue *_threadpool_create_job_queue(const ThreadPool * const threadpool, const int32_t queue_size);
static void             _threadpool_destroy_job_queue(struct JobQueue * const queue);
static Job             *_threadpool_thread_get_job(ThreadPool * thread_pool);


/*
* @brief the worker thread, which will
* @param[in] threadpool      the threadpool which own the thread
* @returns None
*/
static void *_threadpool_thread(void *threadpool)
{
    ThreadPool *pool = (ThreadPool *) threadpool;
    THREAD_POOL_LOG("started");
    while( !pool->mShutDown ) {
        //THREAD_POOL_LOG("pool->mShutDown: %d", pool->mShutDown);
        // _threadpool_thread_get_job() will not return until it gets a jobs from queue or timeout
        Job *job = _threadpool_thread_get_job(pool);
        if ( !job )
        {
            THREAD_POOL_LOG("Time out or error during get job from queue");
            continue;
        }
        //THREAD_POOL_LOG("thread got a job from job queue");
        (*job->mFunc)(job->mArgs);
        free(job);
        //THREAD_POOL_LOG("job done")
    }
    THREAD_POOL_LOG("pool->mShutDown: %d", pool->mShutDown);
    THREAD_POOL_LOG("exit");
    pthread_exit(NULL);
    return(NULL);
}

static struct JobQueue *_threadpool_create_job_queue(const ThreadPool * const threadpool, const int32_t queue_size)
{
    struct JobQueue *queue = (struct JobQueue *) malloc( sizeof(struct JobQueue) );
    if ( !queue )
    {
        THREAD_POOL_LOG("malloc queue");
        return NULL;
    }
    queue->mJobs = (Job *) malloc( sizeof(Job) * queue_size);
    if ( !queue->mJobs )
    {
        THREAD_POOL_LOG("malloc queue->mJobs");
        free(queue);
        return NULL;
    }
    queue->mQueueHead = 0;
    queue->mQueueTail = 0;
    queue->mQueueSize = queue_size;
    queue->mThreadPool = (ThreadPool *) threadpool;
    if ( pthread_mutex_init(&(queue->mQueueLock), NULL) != 0 )
    {
        THREAD_POOL_LOG("pthread_mutex_init queue->mQueueLock");
        free(queue->mJobs);
        free(queue);
        return NULL;
    }
    if ( pthread_cond_init(&(queue->mReadable), NULL) != 0 )
    {
        THREAD_POOL_LOG("pthread_cond_init queue->mReadable");
        pthread_mutex_destroy(&(queue->mQueueLock));
        free(queue->mJobs);
        free(queue);
        return NULL;
    }
    if ( pthread_cond_init(&(queue->mWritable), NULL) != 0 )
    {
        THREAD_POOL_LOG("pthread_cond_init queue->mWritable");
        pthread_mutex_destroy(&(queue->mQueueLock));
        pthread_cond_destroy(&(queue->mReadable));
        free(queue->mJobs);
        free(queue);
        return NULL;
    }
    return queue;
}

/*
* @brief Release all resources allocated for the jobqueue. This function should only called after jobqueue is initialized successfully
* @param[in] queue     a pointer point to struct JobQueue object
* @returns None
*/
static void _threadpool_destroy_job_queue(struct JobQueue * const queue)
{
    if ( !queue )
    {
        return;
    }
    if ( queue->mJobs )
    {
        free(queue->mJobs);
    }

    /* 
        According to man page it shall be safe to destroy an initialized mutex that is unlocked. 
        Attempting to destroy a locked mutex results in undefined behavior. 
    */
    pthread_mutex_destroy(&(queue->mQueueLock));
    pthread_cond_destroy(&(queue->mReadable));
    pthread_cond_destroy(&(queue->mWritable));
    free(queue);
}

/*
* @brief Terminate all threads in the thread pool and release resources allocated
* @param[in] threadpool     a pointer point to ThreadPool object
* @returns None
*/
void threadpool_destroy(ThreadPool *threadpool)
{

    if( !threadpool ) {
        THREAD_POOL_LOG("NULL ptr, threadpool");
        return;
    }

    /* Already shutting down */
    if( threadpool->mShutDown ) {
        THREAD_POOL_LOG("");
        goto end;
    }

    threadpool->mShutDown = 1;

    /* Wake up all worker threads */
    if( pthread_cond_broadcast(&(threadpool->mJobQueue->mReadable)) != 0) 
    {
        THREAD_POOL_LOG("pthread_cond_broadcast fail");
        goto end;
    }
    /* Join all worker thread */
    for(int32_t i = 0; i < threadpool->mNumOfThread; i++) 
    {
        THREAD_POOL_LOG("");
        if( pthread_join(threadpool->mThreads[i], NULL) != 0 ) 
        {
            THREAD_POOL_LOG("pthread_join(threadpool->mThreads[%d] fail", i);
        }
    }
end:
    THREAD_POOL_LOG("before threadpool_free");
    _threadpool_free(threadpool);
}

/*
* @brief Release all resources allocated for the threadpool. This function should only called after threads are joined or not launch yet
* @param[in] threadpool     a pointer point to ThreadPool object
* @returns None
*/
static void _threadpool_free(ThreadPool *threadpool)
{
    if( !threadpool ) {
        return;
    }

    if( threadpool->mThreads ) 
    {
        free(threadpool->mThreads);
    }
    if ( threadpool->mJobQueue )
    {
        _threadpool_destroy_job_queue(threadpool->mJobQueue);
    }

    free(threadpool);    
}

/*
* @brief Create a threadpool with thread_count threads
* @param[in] thread_count     numbers of thread in the pool
* @param[in] job_queue_size   size of job queue
* @returns ThreadPool * or NULL if error
*/
ThreadPool *threadpool_create(const int32_t thread_count, const int32_t job_queue_size)
{
    ThreadPool *pool = (ThreadPool *) malloc( sizeof(ThreadPool) );
    if ( !pool )
    {
        THREAD_POOL_LOG("malloc pool");
        return NULL;
    }
    pool->mNumOfThread = thread_count;
    pool->mShutDown = 0;
    pool->mThreads = (pthread_t *) malloc( sizeof(pthread_t) * thread_count );
    if ( !pool->mThreads )
    {
        THREAD_POOL_LOG("malloc pool->mThreads");
        _threadpool_free(pool);
        return NULL;
    }

    pool->mJobQueue = _threadpool_create_job_queue(pool, job_queue_size);
    if ( !pool->mJobQueue )
    {
        THREAD_POOL_LOG("_threadpool_create_job_queue");
        _threadpool_free(pool);
        return NULL;
    }
    
    /* Start worker threads */
    for( int32_t i = 0; i < thread_count; i++ ) {
        if( pthread_create(&(pool->mThreads[i]), NULL, _threadpool_thread, (void *) pool) != 0 ) {
            threadpool_destroy(pool);
            THREAD_POOL_LOG("pthread_create fail");
            return NULL;
        }
    }
    return pool;
}

/*
* @brief Push a job to the tail of the queue
* @param[in] threadpool     a pointer point to ThreadPool object
* @param[in] func           a pointer point to threadpool_job_func_t function
* @param[in] args           a pointer point to the arguments for threadpool_job_func_t function
* @returns THREADPOOL_ERROR, THREADPOOL_SUCCESS
*/
int32_t threadpool_job_push_back(const ThreadPool * const threadpool, threadpool_job_func_t func, const void * const args)
{
    if ( !threadpool ) 
    {
        THREAD_POOL_LOG("NULL ptr, thread_pool");
        return THREADPOOL_ERROR;
    }

    if ( !func )
    {
        THREAD_POOL_LOG("NULL ptr, threadpool_job_func_t");
        return THREADPOOL_ERROR;
    }
    
    struct JobQueue *job_queue = threadpool->mJobQueue;
    if ( !job_queue )
    {
        THREAD_POOL_LOG("NULL ptr, job_queue");
        return THREADPOOL_ERROR;
    }
    
    if ( pthread_mutex_lock(&(job_queue->mQueueLock)) != 0 ) 
    {
        THREAD_POOL_LOG("pthread_mutex_lock fail");
        return THREADPOOL_ERROR;
    }

    int32_t next = (job_queue->mQueueTail + 1) % job_queue->mQueueSize;

    if ( threadpool->mShutDown )
    {
        pthread_mutex_unlock(&(job_queue->mQueueLock));
        return THREADPOL_SHUTDOWN;
    }
    // If queue is full, wait until it becomes writable
    while ( (job_queue->mQueueTail + 1) % job_queue->mQueueSize == job_queue->mQueueHead )
    {
        THREAD_POOL_LOG("job queue full");
        pthread_cond_wait(&job_queue->mWritable, &job_queue->mQueueLock);
    }

    // Add job to the queue
    job_queue->mJobs[job_queue->mQueueTail].mFunc = func;
    job_queue->mJobs[job_queue->mQueueTail].mArgs = (void *) args;
    job_queue->mQueueTail++;
    job_queue->mQueueTail %= job_queue->mQueueSize;

    // signal to all idle threads in the pools
    if ( pthread_cond_signal(&(job_queue->mReadable)) != 0 ) 
    {
        THREAD_POOL_LOG("pthread_cond_signal fail");
        return THREADPOOL_ERROR;
    }

    if ( pthread_mutex_unlock(&(job_queue->mQueueLock)) != 0 )
    {
        THREAD_POOL_LOG("pthread_mutex_unlock fail");
        return THREADPOOL_ERROR;
    }
    return THREADPOOL_SUCCESS;
}

/*
* @brief Get a Job object from the head of queue
* @param[in] thread_pool     numbers of thread in the pool
* @returns pointer point to Job or NULL if error
*/
static Job *_threadpool_thread_get_job(ThreadPool * thread_pool)
{
    if ( !thread_pool )
    {
        THREAD_POOL_LOG("NULL ptr, thread_pool");
        return NULL;
    }
    
    struct JobQueue *job_queue = thread_pool->mJobQueue;
    if ( !job_queue )
    {
        THREAD_POOL_LOG("NULL ptr, job_queue");
        return NULL;
    }

    pthread_mutex_lock(&job_queue->mQueueLock);

    // wait for new job
    while ( (job_queue->mQueueTail - job_queue->mQueueHead) == 0 && !thread_pool->mShutDown)
    {
        THREAD_POOL_LOG("Wait queue become readable...");
        struct timespec t;
        threadpool_get_lock_timout_timespec(&t);
        if ( pthread_cond_timedwait(&job_queue->mReadable, &job_queue->mQueueLock, &t) < 0 )
        {
            THREAD_POOL_LOG("pthread_cond_timedwait fail, errno: %d", errno);
            return NULL;
        }
    }
    if ( thread_pool->mShutDown )
    {
        THREAD_POOL_LOG("Shut down");
        pthread_mutex_unlock(&job_queue->mQueueLock);
        return NULL;
    }
    // Run the job
    Job *new_job = (Job *) malloc( sizeof(Job) );
    if ( !new_job )
    {
        pthread_mutex_unlock(&job_queue->mQueueLock);
        THREAD_POOL_LOG("NULL ptr, new_job");
        return NULL;
    }
    
    // Copy the job out from the queue
    memcpy(new_job, &(job_queue->mJobs[job_queue->mQueueHead]), sizeof(Job));

    job_queue->mQueueHead++;
    job_queue->mQueueHead %= job_queue->mQueueSize;
    pthread_cond_signal(&job_queue->mWritable);
    pthread_mutex_unlock(&job_queue->mQueueLock);
    return new_job;
}
