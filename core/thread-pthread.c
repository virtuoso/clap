// SPDX-License-Identifier: Apache-2.0
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include "common.h"
#include "error.h"
#include "memory.h"

#define IMPLEMENTOR
#include "thread.h"
#undef IMPLEMENTOR

/****************************************************************************
 * mutex
 ****************************************************************************/

cerr mutex_init(mutex_t *mutex)
{
    pthread_mutexattr_t attr;

    int err = pthread_mutexattr_init(&attr);
    if (err)    return CERR_NOMEM;
#ifdef DEBUG
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#else /* !DEBUG */
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
#endif /* !DEBUG */

    err = pthread_mutex_init(&mutex->mutex, &attr);
    if (err) {
        pthread_mutexattr_destroy(&attr);
        return errno_to_cerr(err);
    }

    pthread_mutexattr_destroy(&attr);

    return CERR_OK;
}

void mutex_destroy(mutex_t *mutex)
{
    pthread_mutex_destroy(&mutex->mutex);
}

void mutex_lock(mutex_t *mutex)
{
    int err = pthread_mutex_lock(&mutex->mutex);
    if (err)    err("correctness bug: %s (%d)\n", strerror(err), err);
}

bool mutex_trylock(mutex_t *mutex)
{
    return !pthread_mutex_trylock(&mutex->mutex);
}

void mutex_unlock(mutex_t *mutex)
{
    int err = pthread_mutex_unlock(&mutex->mutex);
    if (err)    err("correctness bug: %s (%d)\n", strerror(err), err);
}

/****************************************************************************
 * condvar
 ****************************************************************************/

cerr condvar_init(condvar_t *var)
{
    int err = pthread_cond_init(&var->cond, NULL);
    return err ? errno_to_cerr(err) : CERR_OK;
}

void condvar_wait(condvar_t *var, mutex_t *mutex)
{
    pthread_cond_wait(&var->cond, &mutex->mutex);
}

void condvar_signal(condvar_t *var)
{
    pthread_cond_signal(&var->cond);
}

void condvar_broadcast(condvar_t *var)
{
    pthread_cond_broadcast(&var->cond);
}

void condvar_destroy(condvar_t *var)
{
    pthread_cond_destroy(&var->cond);
}

/****************************************************************************
 * event
 ****************************************************************************/

cerr event_init(event_t *evt)
{
    atomic_init(&evt->event, 0);
    CERR_RET_CERR(mutex_init(&evt->mutex));
    CERR_RET(condvar_init(&evt->cond), { mutex_destroy(&evt->mutex); return __cerr; });
#ifndef CONFIG_FINAL
    atomic_init(&evt->fast_wait, 0);
    atomic_init(&evt->slow_wait, 0);
#endif /* !CONFIG_FINAL */
    return CERR_OK;
}

void event_destroy(event_t *evt)
{
    mutex_destroy(&evt->mutex);
    condvar_destroy(&evt->cond);
}

void event_wait(event_t *evt, long spin_before_sleep)
{
    for (; spin_before_sleep > 0; spin_before_sleep--)
        if (atomic_load_explicit(&evt->event, memory_order_acquire)) {
#ifndef CONFIG_FINAL
            atomic_fetch_add_explicit(&evt->fast_wait, 1, memory_order_relaxed);
#endif /* !CONFIG_FINAL */
            goto consume;
        }

    mutex_lock(&evt->mutex);

    while (!atomic_load_explicit(&evt->event, memory_order_acquire))
        condvar_wait(&evt->cond, &evt->mutex);

#ifndef CONFIG_FINAL
    atomic_fetch_add_explicit(&evt->slow_wait, 1, memory_order_relaxed);
#endif /* !CONFIG_FINAL */

    mutex_unlock(&evt->mutex);

consume:
    atomic_store_explicit(&evt->event, 0, memory_order_release);
}

void event_post(event_t *evt)
{
    atomic_store_explicit(&evt->event, 1, memory_order_release);
    condvar_signal(&evt->cond);
}

/****************************************************************************
 * semaphore
 ****************************************************************************/

cerr semaphore_init(semaphore_t *sem, int value)
{
    CERR_RET_CERR(mutex_init(&sem->mutex));
    CERR_RET(condvar_init(&sem->cond), { mutex_destroy(&sem->mutex); return __cerr; });
    sem->count = sem->init = value;
    return CERR_OK;
}

void semaphore_destroy(semaphore_t *sem)
{
    mutex_destroy(&sem->mutex);
    condvar_destroy(&sem->cond);
    err_on(sem->count != sem->init, "unbalanced: %d vs %d\n", sem->count, sem->init);
}

void semaphore_wait(semaphore_t *sem, int timeout)
{
    mutex_lock(&sem->mutex);

    while (!sem->count)
        condvar_wait(&sem->cond, &sem->mutex);

    sem->count--;

    mutex_unlock(&sem->mutex);
}

void semaphore_release(semaphore_t *sem)
{
    mutex_lock(&sem->mutex);

    sem->count++;
    condvar_signal(&sem->cond);

    mutex_unlock(&sem->mutex);
}

/****************************************************************************
 * thread
 ****************************************************************************/

static void *thread_wrap(void *data)
{
    thread_t *t = data;

    /*
     * pthread_cancel() is deliberately not used by this abstraction,
     * in favor of a dedicated state flag, which gives the thread more
     * control over where its cancellation points are and removes the
     * burden of testing all possible system calls for being cancelled
     *
     * The below PTHREAD_CANCEL_DISABLE is a guardrail and a statement
     * of intent documented here.
     */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    void *ret = t->func(t);
    t->exited = true;

    return ret;
}

void *thread_get_data(thread_t *t)
{
    return t->data;
}

bool thread_should_exit(thread_t *t)
{
    return atomic_load_explicit(&t->should_exit, memory_order_acquire);
}

const char *thread_get_name(thread_t *t)
{
    return t->name;
}

void thread_sleep(thread_t *t, long spin_before_sleep)
{
    event_wait(&t->wakeup, spin_before_sleep);
}

void thread_wakeup(thread_t *t)
{
    event_post(&t->wakeup);
}

void thread_request_exit(thread_t *t)
{
    atomic_store_explicit(&t->should_exit, 1, memory_order_release);
    thread_wakeup(t);
}

cerr_check _thread_init(thread_t *t, thread_fn func, void *data, thread_options *opts)
{
    if (!!opts->stack_addr != !!opts->stack_size)   return CERR_INVALID_ARGUMENTS;

    CERR_RET_CERR(event_init(&t->wakeup));

    t->name = opts->name ? : "generic clap thread";
    t->func = func;
    t->data = data;
    t->exited = false;

    atomic_init(&t->should_exit, 0);

    cerr err;
    pthread_attr_t attr;
    CERR_RET(errno_to_cerr(pthread_attr_init(&attr)), { err = __cerr; goto err_event; });

    CERR_RET(errno_to_cerr(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE)), { err = __cerr; goto err_attr; });
    CERR_RET(errno_to_cerr(pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)), { err = __cerr; goto err_attr; });
    CERR_RET(errno_to_cerr(pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)), { err = __cerr; goto err_attr; });
    CERR_RET(
        errno_to_cerr(pthread_attr_setschedpolicy(&attr, SCHED_RR)),
        if (!IS_CERR_CODE(__cerr, CERR_NOT_SUPPORTED) &&
            !IS_CERR_CODE(__cerr, CERR_PERMISSION_DENIED)) { err = __cerr; goto err_attr; }
    );

    if (opts->stack_addr && opts->stack_size)
        CERR_RET(errno_to_cerr(pthread_attr_setstack(&attr, opts->stack_addr, opts->stack_size)), { err = __cerr; goto err_attr; });

    CERR_RET(errno_to_cerr(pthread_create(&t->thread, &attr, thread_wrap, t)), { err = __cerr; goto err_attr; });

    pthread_attr_destroy(&attr);

    return CERR_OK;

err_attr:
    pthread_attr_destroy(&attr);

err_event:
    event_destroy(&t->wakeup);
    return err;
}

DEFINE_CLEANUP(thread_t, if (*p) mem_free(*p));

cresp_check(thread_t) _thread_new(thread_fn func, void *data, thread_options *opts)
{
    LOCAL_SET(thread_t, t) = mem_alloc(sizeof(*t));

    CERR_RET_T(_thread_init(t, func, data, opts), thread_t);

    return cresp_val(thread_t, NOCU(t));
}

void *thread_deinit(thread_t *t)
{
    void *ret;

    pthread_join(t->thread, &ret);
    event_destroy(&t->wakeup);

    return ret;
}

void *thread_destroy(thread_t *t)
{
    void *ret = thread_deinit(t);
    mem_free(t);
    return ret;
}
