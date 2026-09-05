// SPDX-License-Identifier: Apache-2.0
#include <stdatomic.h>
#include "clap.h"
#include "logger.h"
#include "memory.h"
#include "thread.h"
#include "util.h"
#include "worker.h"

/*
 * Jobs travel from the owner to the workers through a mutex-protected
 * FIFO: a submit appends and signals, a worker pops. The owner only ever
 * holds that mutex for the append, so the longest it can be kept from
 * its frame is a worker's pop. The way back is a lock-free stack of
 * messages (results and completions) that the workers push and the
 * poller takes wholesale into a FIFO of its own, to deliver at its own
 * pace; nothing pops single entries off the stack, so there is no ABA to
 * guard against.
 */

/* a result or a completion on its way to the poller */
struct job_msg {
    struct job_msg  *next;
    job             *j;
    void            *result;
    cerr            err;
    bool            done;
};

struct job {
    struct ref      ref;
    worker_pool     *pool;
    job_run_fn      run;
    job_result_fn   result;
    job_done_fn     done;
    void            *data;
    const char      *name;
    /* worker_pool::queue, while queued */
    struct list     entry;
    /* the completion: no allocation that could fail stands between run() and done() */
    struct job_msg  done_msg;
    atomic_int      state;
    _Atomic float   progress;
    atomic_bool     cancelled;
};

struct worker_pool {
    struct ref                  ref;
    struct clap_context         *ctx;
    const char                  *name;
    thread_t                    *threads;
    /* messages from the workers, newest first; the poller takes them all at once */
    _Atomic(struct job_msg *)   inbox;
    /* taken from the inbox, oldest first, waiting for a poll to deliver them */
    struct job_msg              *pending;
    struct job_msg              *pending_last;
    /* jobs waiting for a worker, oldest first; lock covers it and stopping */
    struct list                 queue;
    mutex_t                     lock;
    condvar_t                   cond;
    /* threads started, and reserved with ctx */
    unsigned int                nr_threads;
    /* the workers finish the queue and exit */
    bool                        stopping;
};

static const char *job_state_names[] = {
    [JOB_QUEUED]    = "queued",
    [JOB_RUNNING]   = "running",
    [JOB_COMPLETE]  = "complete",
    [JOB_DELIVERED] = "delivered",
};

const char *job_state_name(job_state state)
{
    if ((unsigned int)state >= array_size(job_state_names)) return "unknown";

    return job_state_names[state];
}

static void inbox_push(worker_pool *pool, struct job_msg *msg)
{
    struct job_msg *head = atomic_load_explicit(&pool->inbox, memory_order_relaxed);

    do {
        msg->next = head;
    } while (!atomic_compare_exchange_weak_explicit(&pool->inbox, &head, msg,
                                                    memory_order_release, memory_order_relaxed));
}

/****************************************************************************
 * job
 ****************************************************************************/

void *job_get_data(job *j)
{
    return j->data;
}

const char *job_get_name(job *j)
{
    return j->name;
}

job_state job_get_state(job *j)
{
    return atomic_load_explicit(&j->state, memory_order_acquire);
}

cerr job_yield(job *j, void *result)
{
    /* a result nobody receives is a leak */
    if (!j->result) return CERR_INVALID_OPERATION;

    struct job_msg *msg = mem_alloc(sizeof(*msg));
    if (!msg)       return CERR_NOMEM;

    *msg = (struct job_msg){ .j = j, .result = result };
    inbox_push(j->pool, msg);

    return CERR_OK;
}

void job_set_progress(job *j, float progress)
{
    /* a lone value; nothing is published along with it */
    atomic_store_explicit(&j->progress, progress, memory_order_relaxed);
}

float job_get_progress(job *j)
{
    return atomic_load_explicit(&j->progress, memory_order_relaxed);
}

void job_cancel(job *j)
{
    atomic_store_explicit(&j->cancelled, true, memory_order_relaxed);
}

bool job_is_cancelled(job *j)
{
    return atomic_load_explicit(&j->cancelled, memory_order_relaxed);
}

/*
 * On a worker: run the job, unless cancelled, and send its completion.
 * The push is the worker's last access to the job: once the poller sees
 * the completion it runs done() and drops the pool's reference, which may
 * be the last.
 */
static void job_run(job *j)
{
    atomic_store_explicit(&j->state, JOB_RUNNING, memory_order_relaxed);
    j->done_msg.err = job_is_cancelled(j) ? CERR_CANCELLED : j->run(j);
    atomic_store_explicit(&j->state, JOB_COMPLETE, memory_order_relaxed);
    /* publishes done_msg and the state with it */
    inbox_push(j->pool, &j->done_msg);
}

static cerr job_make(struct ref *ref, void *_opts)
{
    rc_init_opts(job) *opts = _opts;

    if (!opts->pool || !opts->run)  return CERR_INVALID_ARGUMENTS;

    job *j = container_of(ref, job, ref);

    j->pool     = opts->pool;
    j->run      = opts->run;
    j->result   = opts->result;
    j->done     = opts->done;
    j->data     = opts->data;
    j->name     = opts->name ? : "job";
    j->done_msg = (struct job_msg){ .j = j, .done = true };
    atomic_init(&j->state, JOB_QUEUED);
    atomic_init(&j->progress, 0.0f);
    atomic_init(&j->cancelled, false);

    worker_pool *pool = opts->pool;

    mutex_lock(&pool->lock);
    if (pool->stopping) {
        mutex_unlock(&pool->lock);
        return CERR_INVALID_OPERATION;
    }

    /* the pool's reference, released once done() has run */
    ref_get(j);
    list_append(&pool->queue, &j->entry);
    mutex_unlock(&pool->lock);
    condvar_signal(&pool->cond);

    return CERR_OK;
}

/* the owner's data is the owner's; the completion lives in the job itself */
static void job_drop(struct ref *ref) {}

DEFINE_REFCLASS2(job);
DEFINE_CLEANUP(job, if (*p) ref_put(*p))

/****************************************************************************
 * worker pool
 ****************************************************************************/

static void *worker_thread(thread_t *self)
{
    worker_pool *pool = thread_get_data(self);

    for (;;) {
        mutex_lock(&pool->lock);
        while (list_empty(&pool->queue) && !pool->stopping) condvar_wait(&pool->cond, &pool->lock);

        /* stopping: the queue is finished first */
        if (list_empty(&pool->queue)) {
            mutex_unlock(&pool->lock);
            break;
        }

        job *j = list_first_entry(&pool->queue, job, entry);
        list_del(&j->entry);
        mutex_unlock(&pool->lock);

        job_run(j);
    }

    return NULL;
}

/* Move the inbox, newest first, to the end of the pending FIFO in production order */
static void inbox_take(worker_pool *pool)
{
    struct job_msg *msg = atomic_exchange_explicit(&pool->inbox, NULL, memory_order_acquire);
    struct job_msg *fifo = NULL, *last = msg;

    while (msg) {
        struct job_msg *next = msg->next;

        msg->next = fifo;
        fifo = msg;
        msg = next;
    }

    if (!fifo)  return;

    if (pool->pending_last)     pool->pending_last->next = fifo;
    else                        pool->pending = fifo;
    pool->pending_last = last;
}

/* Deliver up to @max pending messages; the caller keeps the pool alive */
static unsigned int pool_deliver(worker_pool *pool, unsigned int max)
{
    inbox_take(pool);

    unsigned int nr = 0;
    while (pool->pending && (!max || nr < max)) {
        struct job_msg *msg = pool->pending;
        job *j = msg->j;

        pool->pending = msg->next;
        if (!pool->pending)     pool->pending_last = NULL;

        if (msg->done) {
            if (j->done)    j->done(j, msg->err);
            atomic_store_explicit(&j->state, JOB_DELIVERED, memory_order_release);
            /* the pool's reference; the message is part of the job */
            ref_put(j);
        } else {
            if (j->result)  j->result(j, msg->result);
            mem_free(msg);
        }

        nr++;
    }

    return nr;
}

unsigned int worker_pool_poll(worker_pool *pool, unsigned int max)
{
    /* a done() may put the owner's last reference: the drop then waits for the loop */
    ref_get(pool);
    unsigned int nr = pool_deliver(pool, max);
    ref_put(pool);

    return nr;
}

unsigned int worker_pool_nr_threads(worker_pool *pool)
{
    return pool->nr_threads;
}

/* Have the workers finish the queue and exit, and wait for them */
static void worker_pool_stop(worker_pool *pool)
{
    mutex_lock(&pool->lock);
    pool->stopping = true;
    mutex_unlock(&pool->lock);
    condvar_broadcast(&pool->cond);

    for (unsigned int i = 0; i < pool->nr_threads; i++) thread_deinit(&pool->threads[i]);
}

static cerr worker_pool_make(struct ref *ref, void *_opts)
{
    rc_init_opts(worker_pool) *opts = _opts;

    if (!opts->name || !opts->nr_threads)   return CERR_INVALID_ARGUMENTS;

    worker_pool *pool = container_of(ref, worker_pool, ref);
    unsigned int nr = opts->ctx ? clap_cpus_reserve(opts->ctx, opts->nr_threads) : opts->nr_threads;

    if (!nr)
        return CERR_NOT_SUPPORTED_REASON(
            .fmt    = "no threads left for pool '%s'",
            .arg0   = opts->name
        );

    pool->ctx  = opts->ctx;
    pool->name = opts->name;
    list_init(&pool->queue);
    atomic_init(&pool->inbox, NULL);

    cerr err;
    CERR_RET(mutex_init(&pool->lock), { err = __cerr; goto err_reserve; });
    CERR_RET(condvar_init(&pool->cond), { err = __cerr; goto err_lock; });

    pool->threads = mem_alloc(sizeof(*pool->threads), .nr = nr);
    if (!pool->threads) {
        err = CERR_NOMEM;
        goto err_cond;
    }

    for (; pool->nr_threads < nr; pool->nr_threads++)
        CERR_RET(
            thread_init(&pool->threads[pool->nr_threads], worker_thread, pool),
            { err = __cerr; goto err_threads; }
        );

    return CERR_OK;

err_threads:
    worker_pool_stop(pool);
    mem_free(pool->threads);
err_cond:
    condvar_destroy(&pool->cond);
err_lock:
    mutex_destroy(&pool->lock);
err_reserve:
    if (opts->ctx)  clap_cpus_release(opts->ctx, nr);

    return err;
}

static void worker_pool_drop(struct ref *ref)
{
    worker_pool *pool = container_of(ref, worker_pool, ref);
    unsigned int nr = pool->nr_threads;

    worker_pool_stop(pool);
    /* what the workers finished on their way out */
    pool_deliver(pool, 0);

    mem_free(pool->threads);
    condvar_destroy(&pool->cond);
    mutex_destroy(&pool->lock);
    if (pool->ctx)  clap_cpus_release(pool->ctx, nr);
}

DEFINE_REFCLASS2(worker_pool);
DEFINE_CLEANUP(worker_pool, if (*p) ref_put(*p))
