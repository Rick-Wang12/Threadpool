#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define THREADPOOL_SUCCESS     0
#define THREADPOOL_ERROR      -1
#define THREADPOL_SHUTDOWN     1

#define THREAD_POOL_LOG(msg, ...)  \
{ \
    printf("#### %s@%d, " msg "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__); \
} 

typedef struct ThreadPool ThreadPool;
typedef struct JobQueue JobQueue; 
typedef void (*threadpool_job_func_t)(void * args);

ThreadPool *threadpool_create(const int32_t thread_count, const int32_t job_queue_size);
void        threadpool_destroy(ThreadPool *pool);
int32_t     threadpool_job_push_back(ThreadPool * const threadpool, 
                                     threadpool_job_func_t func, 
                                     const void * const args
                                    );

#endif
/*__THREADPOOL_H__*/