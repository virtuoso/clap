/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_WORKER_H__
#define __CLAP_WORKER_H__

#include <stdbool.h>
#include "error.h"
#include "object.h"

struct clap_context;

/*
 * Worker pools run jobs on threads of their own and hand the outcome back
 * on the thread that owns the pool: the owner posts a job ("write this
 * frame out", "parse this scene") and carries on; the job's results and
 * its completion arrive through its callbacks from worker_pool_poll(),
 * never from a worker, so they are free to call into the renderer and the
 * rest of the engine. Nothing here waits for a job to finish: the owner
 * decides when to poll, how much to take and what to do with it.
 *
 * The pool, the job handles and the polling belong to one thread, the
 * owner's: reference counts aren't atomic, and neither is most of the
 * engine. A worker sees its job through job_get_data(), job_yield(),
 * job_set_progress() and job_is_cancelled(), and nothing else of the
 * engine's: no ref_get()/ref_put(), no renderer, pipeline or scene; the
 * memory allocator, the file system and pure computation are fine. In a
 * browser, what a job proxies to the main thread (MAIN_THREAD_EM_ASM)
 * runs while that thread may be inside the pool's lock, so it keeps away
 * from the pool as well. Work that another thread hands in (a GPU
 * completion callback, say) goes through a list of its own that the
 * owner drains and submits from.
 *
 * Pools are specialized, one per kind of work; each one's thread count is
 * a request that clap_cpus_reserve() cuts down to what the machine has left.
 */

typedef struct worker_pool worker_pool;
typedef struct job job;

DECLARE_REFCLASS(worker_pool);
DECLARE_REFCLASS(job);
DECLARE_CLEANUP(worker_pool);
DECLARE_CLEANUP(job);

DEFINE_REFCLASS_INIT_OPTIONS(worker_pool,
    /* for the CPU budget; NULL runs the requested number of threads */
    struct clap_context *ctx;
    const char          *name;
    unsigned int        nr_threads;
);

/**
 * define worker_pool_new - start a worker pool
 * @args:   worker_pool_init_options: .ctx, .name, .nr_threads
 *
 * Starts .nr_threads worker threads, or as many as clap_cpus_reserve()
 * grants; none left is an error, not a pool. Dropping the last reference
 * stops the pool: the workers finish the jobs still queued (job_cancel()
 * the ones that shouldn't run), their threads are joined, and everything
 * they left is delivered on the dropping thread, so no job's done() goes
 * missing; a job submitted meanwhile is refused.
 * Context: the owner's thread
 * Return: cresp(worker_pool)
 */
#define worker_pool_new(args...) ref_new_checked(worker_pool, args)

/**
 * worker_pool_poll() - deliver what the workers have finished
 * @pool:   the pool
 * @max:    at most this many callbacks, 0 for everything
 *
 * Runs the result() and done() callbacks of the jobs that progressed
 * since the last poll, in the order the workers produced them, a job's
 * results ahead of its done(); what @max leaves undelivered waits for the
 * next poll, in order. Never blocks on the workers.
 * Context: the owner's thread
 * Return: the number of callbacks run
 */
unsigned int worker_pool_poll(worker_pool *pool, unsigned int max);

/**
 * worker_pool_nr_threads() - how many threads a pool got
 * @pool:   the pool
 * Return: the number of worker threads
 */
unsigned int worker_pool_nr_threads(worker_pool *pool);

/**
 * enum job_state - where a job is in its life
 * @JOB_QUEUED:     waiting for a worker
 * @JOB_RUNNING:    run() in progress on a worker
 * @JOB_COMPLETE:   run() returned; the completion is waiting for a poll
 * @JOB_DELIVERED:  done() has run
 */
typedef enum job_state {
    JOB_QUEUED,
    JOB_RUNNING,
    JOB_COMPLETE,
    JOB_DELIVERED,
} job_state;

/**
 * job_state_name() - a job state's name, for logs and debug views
 * @state:  the state
 * Return: "queued", "running", "complete" or "delivered"; "unknown" for
 *         anything else
 */
const char *job_state_name(job_state state);

/**
 * typedef job_run_fn - a job's work, on a worker thread
 * @j:  the job
 *
 * Long-running work checks job_is_cancelled() now and then and bails out
 * when told to; partial results go to the owner through job_yield().
 * Return: CERR_OK, or the error the job's done() receives
 */
typedef cerr (*job_run_fn)(job *j);

/**
 * typedef job_result_fn - receive one job_yield()ed result
 * @j:         the job
 * @result:    what run() yielded; the owner's to free from here on
 *
 * Runs from worker_pool_poll(); also for a job cancelled after the
 * yield, which job_is_cancelled() tells.
 */
typedef void (*job_result_fn)(job *j, void *result);

/**
 * typedef job_done_fn - a job's completion
 * @j:      the job
 * @err:    what run() returned, or CERR_CANCELLED for a job that never ran
 *
 * Runs from worker_pool_poll(), once per job, after its last result().
 * The pool's reference goes away right after, so the owner holds one of
 * its own if it still needs the handle.
 */
typedef void (*job_done_fn)(job *j, cerr err);

DEFINE_REFCLASS_INIT_OPTIONS(job,
    worker_pool     *pool;
    job_run_fn      run;
    /* NULL if run() never yields */
    job_result_fn   result;
    /* NULL if the owner doesn't care when the job is over */
    job_done_fn     done;
    void            *data;
    const char      *name;
);

/**
 * define job_submit - queue a job on a pool
 * @args:   job_init_options: .pool, .run, .result, .done, .data, .name
 *
 * The pool keeps a reference until done() has run, so the returned
 * handle can be put right away; keeping it allows job_get_state(),
 * job_get_progress() and job_cancel(). Fails with CERR_INVALID_OPERATION
 * once the pool is being dropped.
 * Context: the owner's thread
 * Return: cresp(job)
 */
#define job_submit(args...) ref_new_checked(job, args)

/**
 * job_get_data() - the owner's data pointer
 * @j:  the job
 * Return: job_init_options::data
 */
void *job_get_data(job *j);

/**
 * job_get_name() - the job's name
 * @j:  the job
 * Return: job_init_options::name
 */
const char *job_get_name(job *j);

/**
 * job_get_state() - where a job is in its life
 * @j:  the job
 * Return: the state, as of this call
 */
job_state job_get_state(job *j);

/**
 * job_yield() - hand a result to the owner
 * @j:         the job, from inside its run()
 * @result:    the result; the owner's result() gets it on a later poll
 *
 * Return: CERR_OK, CERR_INVALID_OPERATION if the job has no result()
 *         to receive it, CERR_NOMEM if the result couldn't be queued; on
 *         an error it stays the caller's to free
 */
cerr_check job_yield(job *j, void *result);

/**
 * job_set_progress() - report how far a job has come
 * @j:          the job, from inside its run()
 * @progress:   0.0 to 1.0
 */
void job_set_progress(job *j, float progress);

/**
 * job_get_progress() - how far a job has come
 * @j:  the job
 * Return: the last value run() reported, 0.0 before it reports any
 */
float job_get_progress(job *j);

/**
 * job_cancel() - ask a job to stop
 * @j:  the job
 *
 * A job that hasn't started doesn't: its done() gets CERR_CANCELLED. A
 * running one goes on until its run() notices job_is_cancelled(); what it
 * yielded before is still delivered, and done() runs either way.
 */
void job_cancel(job *j);

/**
 * job_is_cancelled() - has the owner asked the job to stop?
 * @j:  the job
 * Return: true after job_cancel()
 */
bool job_is_cancelled(job *j);

#endif /* __CLAP_WORKER_H__ */
