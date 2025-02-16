# A simple threadpool implementation

How to compile

`make`

## How to use

Create a threadpool with specific threads and queue size by

1. `ThreadPool *pool = threadpool_create(THREAD_AMOUNT, QUEUE_SIZE);`

2. Use `threadpool_job_push_back(poo, CALLBACK, ARGS)` method to add your own task into the pool

Threads will get job from the queue and execute your callback function with args
