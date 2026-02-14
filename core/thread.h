/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_THREAD_H__
#define __CLAP_THREAD_H__

#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include "error.h"
#include "typedef.h"

TYPE(mutex,
    pthread_mutex_t mutex;
);

/**
 * mutex_init() - initialize a mutex
 * @mutex: pointer to the mutex structure
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check mutex_init(mutex_t *mutex);

/**
 * mutex_destroy() - destroy a mutex
 * @mutex: pointer to the mutex structure
 */
void mutex_destroy(mutex_t *mutex);

/**
 * mutex_lock() - lock a mutex
 * @mutex: pointer to the mutex structure
 *
 * Blocks until the mutex can be acquired.
 */
void mutex_lock(mutex_t *mutex);

/**
 * mutex_trylock() - try to lock a mutex
 * @mutex: pointer to the mutex structure
 *
 * Return: true if the mutex was acquired, false otherwise.
 */
bool mutex_trylock(mutex_t *mutex);

/**
 * mutex_unlock() - unlock a mutex
 * @mutex: pointer to the mutex structure
 */
void mutex_unlock(mutex_t *mutex);

TYPE(lock,
    union {
        mutex_t     mutex;
        atomic_uint lock;
    };
    bool            sleeps;
);

TYPE(condvar,
    pthread_cond_t  cond;
);

/**
 * condvar_init() - initialize a condition variable
 * @var: pointer to the condition variable structure
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr condvar_init(condvar_t *var);

/**
 * condvar_wait() - wait on a condition variable
 * @var:   pointer to the condition variable structure
 * @mutex: mutex to hold while waiting
 *
 * Atomically releases the mutex and waits on the condition variable.
 * Reacquires the mutex before returning.
 */
void condvar_wait(condvar_t *var, mutex_t *mutex);

/**
 * condvar_signal() - signal a condition variable
 * @var: pointer to the condition variable structure
 *
 * Wakes up at least one thread waiting on the condition variable.
 */
void condvar_signal(condvar_t *var);

/**
 * condvar_broadcast() - broadcast a condition variable
 * @var: pointer to the condition variable structure
 *
 * Wakes up all threads waiting on the condition variable.
 */
void condvar_broadcast(condvar_t *var);

/**
 * condvar_destroy() - destroy a condition variable
 * @var: pointer to the condition variable structure
 */
void condvar_destroy(condvar_t *var);

TYPE(event,
    mutex_t     mutex;
    condvar_t   cond;
    atomic_uint event;
#ifndef CONFIG_FINAL
    atomic_ulong fast_wait;
    atomic_ulong slow_wait;
#endif /* !CONFIG_FINAL */
);

/**
 * event_init() - initialize an event
 * @evt: pointer to the event structure
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr event_init(event_t *evt);

/**
 * event_destroy() - destroy an event
 * @evt: pointer to the event structure
 */
void event_destroy(event_t *evt);

/**
 * event_wait() - wait for an event
 * @evt:               pointer to the event structure
 * @spin_before_sleep: number of spin loops before sleeping
 *
 * Blocks until the event is signaled.
 */
void event_wait(event_t *evt, long spin_before_sleep);

/**
 * event_post() - signal an event
 * @evt: pointer to the event structure
 *
 * Wakes up threads waiting for the event.
 */
void event_post(event_t *evt);

TYPE(semaphore,
    mutex_t     mutex;
    condvar_t   cond;
    int         count;
    int         init;
);

/**
 * semaphore_init() - initialize a semaphore
 * @sem:   pointer to the semaphore structure
 * @value: initial value
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr semaphore_init(semaphore_t *sem, int value);

/**
 * semaphore_destroy() - destroy a semaphore
 * @sem: pointer to the semaphore structure
 */
void semaphore_destroy(semaphore_t *sem);

/**
 * semaphore_wait() - wait on a semaphore
 * @sem:     pointer to the semaphore structure
 * @timeout: timeout in milliseconds (or negative for no timeout)
 *
 * Decrements the semaphore count. Blocks if count is zero.
 */
void semaphore_wait(semaphore_t *sem, int timeout);

/**
 * semaphore_release() - release a semaphore
 * @sem: pointer to the semaphore structure
 *
 * Increments the semaphore count.
 */
void semaphore_release(semaphore_t *sem);

TYPE_FORWARD(thread);

typedef void *(*thread_fn)(thread_t *self);

typedef struct thread_options {
    const char  *name;
    void        *stack_addr;
    size_t      stack_size;
} thread_options;

TYPE(thread,
    pthread_t   thread;
    event_t     wakeup;
    atomic_uint should_exit;
    thread_fn   func;
    void        *data;
    const char  *name;
    bool        exited;
);

cresp_ret(thread_t);

/**
 * define thread_init - initialize a thread structure
 * @_t:    pointer to the thread structure
 * @_func: thread entry point
 * @_data: argument passed to thread function
 * @...:   optional parameters
 *
 * Initializes but does not start the thread.
 */
#define thread_init(_t, _func, _data, ...) \
    _thread_init((_t), (_func), (_data), &(thread_options) { .name = __stringify(_func), __VA_ARGS__ })

/**
 * _thread_init() - internal thread initialization
 * @t:    pointer to the thread structure
 * @func: thread entry point
 * @data: argument passed to thread function
 * @opts: thread options
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check _thread_init(thread_t *t, thread_fn func, void *data, thread_options *opts);

/**
 * define thread_new - create and start a new thread
 * @_func: thread entry point
 * @_data: argument passed to thread function
 * @...:   optional parameters
 *
 * Return: Thread handle on success, error code otherwise.
 */
#define thread_new(_func, _data, ...) \
    _thread_new((_tctx), (_func), (_data), &(thread_options) { .name = __stringify(_func), __VA_ARGS__ })

/**
 * _thread_new() - internal thread creation
 * @func: thread entry point
 * @data: argument passed to thread function
 * @opts: thread options
 *
 * Return: Thread handle on success, error code otherwise.
 */
cresp_check(thread_t) _thread_new(thread_fn func, void *data, thread_options *opts);

/**
 * thread_deinit() - deinitialize a thread structure
 * @t: pointer to the thread structure
 *
 * Waits for the thread to finish and releases resources.
 *
 * Return: Thread exit code.
 */
void *thread_deinit(thread_t *t);

/**
 * thread_destroy() - destroy a thread
 * @t: pointer to the thread structure
 *
 * Waits for the thread to finish, releases resources, and frees the structure.
 *
 * Return: Thread exit code.
 */
void *thread_destroy(thread_t *t);

/**
 * thread_get_data() - get thread data
 * @t: pointer to the thread structure
 *
 * Return: The data pointer passed to thread_init or thread_new.
 */
void *thread_get_data(thread_t *t);

/**
 * thread_wakeup() - wake up a sleeping thread
 * @t: pointer to the thread structure
 *
 * Signals the thread's internal event.
 */
void thread_wakeup(thread_t *t);

/**
 * thread_request_exit() - request a thread to exit
 * @t: pointer to the thread structure
 *
 * Sets the should_exit flag and wakes up the thread.
 */
void thread_request_exit(thread_t *t);

/**
 * thread_should_exit() - check if thread should exit
 * @t: pointer to the thread structure
 *
 * Return: true if exit has been requested, false otherwise.
 */
bool thread_should_exit(thread_t *t);

/**
 * thread_get_name() - get thread name
 * @t: pointer to the thread structure
 *
 * Return: Thread name string.
 */
const char *thread_get_name(thread_t *t);

/**
 * thread_sleep() - sleep the current thread
 * @t:                 pointer to the thread structure
 * @spin_before_sleep: number of spin loops before sleeping
 *
 * Waits for the thread's wakeup event.
 */
void thread_sleep(thread_t *t, long spin_before_sleep);

#endif /* __CLAP_THREAD_H__ */
