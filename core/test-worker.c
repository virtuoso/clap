// SPDX-License-Identifier: Apache-2.0
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "logger.h"
#include "test.h"
#include "worker.h"

/*
 * The tests are the pool's owner: they poll from here and never wait on a
 * worker directly, the way the engine's frame loop would. Every wait is
 * bounded, so a pool that goes wrong fails a test rather than hangs it.
 */

/** WAIT_STEPS - 100 us sleeps before a wait gives up: 10 s, room for a sanitizer build on a busy box */
#define WAIT_STEPS 100000

/* Poll until @counter, which the callbacks bump, reaches @target */
static bool poll_until(worker_pool *pool, atomic_uint *counter, unsigned int target)
{
    for (unsigned int i = 0; i < WAIT_STEPS; i++) {
        if (atomic_load_explicit(counter, memory_order_acquire) >= target)  return true;
        worker_pool_poll(pool, 0);
        usleep(100);
    }

    return false;
}

/* Wait for the workers to bring @counter to @target, without delivering anything */
static bool wait_until(atomic_uint *counter, unsigned int target)
{
    for (unsigned int i = 0; i < WAIT_STEPS; i++) {
        if (atomic_load_explicit(counter, memory_order_acquire) >= target)  return true;
        usleep(100);
    }

    return false;
}

/* Wait for the other side to set @flag */
static bool wait_for(atomic_bool *flag)
{
    for (unsigned int i = 0; i < WAIT_STEPS; i++) {
        if (atomic_load_explicit(flag, memory_order_acquire))   return true;
        usleep(100);
    }

    return false;
}

/* One worker: the jobs run in submission order */
static cresp(worker_pool) single_pool(void)
{
    return worker_pool_new(.name = "test", .nr_threads = 1);
}

/****************************************************************************
 * results and completions
 ****************************************************************************/

/** NR_JOBS - jobs the basic test submits */
#define NR_JOBS 32

typedef struct counted_job {
    atomic_uint     ran;
    atomic_uint     results;
    atomic_uint     done;
    unsigned int    index;
    bool            failed;
} counted_job;

static atomic_uint nr_done;

static cerr counted_run(job *j)
{
    counted_job *cj = job_get_data(j);

    atomic_fetch_add_explicit(&cj->ran, 1, memory_order_relaxed);

    return job_yield(j, (void *)(uintptr_t)cj->index);
}

static void counted_result(job *j, void *result)
{
    counted_job *cj = job_get_data(j);

    /* results carry what run() yielded and precede the completion */
    if ((uintptr_t)result != cj->index)                         cj->failed = true;
    if (atomic_load_explicit(&cj->done, memory_order_relaxed))  cj->failed = true;
    atomic_fetch_add_explicit(&cj->results, 1, memory_order_relaxed);
}

static void counted_done(job *j, cerr err)
{
    counted_job *cj = job_get_data(j);

    if (IS_CERR(err))                       cj->failed = true;
    if (job_get_state(j) != JOB_COMPLETE)   cj->failed = true;
    atomic_fetch_add_explicit(&cj->done, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&nr_done, 1, memory_order_release);
}

static int worker_basic_test(void)
{
    static counted_job jobs[NR_JOBS];
    job *handles[NR_JOBS];
    int ret = EXIT_SUCCESS;

    LOCAL_SET(worker_pool, pool) = CRES_RET(
        worker_pool_new(.name = "test", .nr_threads = 2),
        return EXIT_FAILURE
    );
    if (worker_pool_nr_threads(pool) != 2)  return EXIT_FAILURE;

    atomic_store(&nr_done, 0);
    for (unsigned int i = 0; i < NR_JOBS; i++) {
        jobs[i] = (counted_job){ .index = i };
        handles[i] = CRES_RET(
            job_submit(.pool = pool, .run = counted_run, .result = counted_result, .done = counted_done,
                       .data = &jobs[i], .name = "counted"),
            { while (i--) ref_put(handles[i]); return EXIT_FAILURE; }
        );
    }

    if (!poll_until(pool, &nr_done, NR_JOBS))   ret = EXIT_FAILURE;

    for (unsigned int i = 0; i < NR_JOBS; i++) {
        if (jobs[i].failed || jobs[i].ran != 1 || jobs[i].results != 1 || jobs[i].done != 1)    ret = EXIT_FAILURE;
        if (job_get_state(handles[i]) != JOB_DELIVERED)                                         ret = EXIT_FAILURE;
        /* the pool's reference went with the completion */
        ref_put(handles[i]);
    }

    return ret;
}

/****************************************************************************
 * delivery order
 ****************************************************************************/

/** NR_YIELDS - results each ordered job yields */
#define NR_YIELDS 3

static unsigned int next_result;
static bool order_failed;
static atomic_uint nr_ordered_ran;
static atomic_uint nr_ordered_done;

static cerr ordered_run(job *j)
{
    unsigned int index = (uintptr_t)job_get_data(j);

    for (unsigned int k = 0; k < NR_YIELDS; k++)
        CERR_RET_CERR(job_yield(j, (void *)(uintptr_t)(index * NR_YIELDS + k)));

    atomic_fetch_add_explicit(&nr_ordered_ran, 1, memory_order_release);

    return CERR_OK;
}

/* one worker runs the jobs in submission order; each job's results arrive in yield order */
static void ordered_result(job *j, void *result)
{
    if ((uintptr_t)result != next_result)   order_failed = true;
    next_result++;
}

static void ordered_done(job *j, cerr err)
{
    unsigned int index = (uintptr_t)job_get_data(j);

    /* all of this job's results, none of the next one's */
    if (next_result != (index + 1) * NR_YIELDS) order_failed = true;
    atomic_fetch_add_explicit(&nr_ordered_done, 1, memory_order_release);
}

static int worker_order_test(void)
{
    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    next_result = 0;
    order_failed = false;
    atomic_store(&nr_ordered_done, 0);
    for (uintptr_t i = 0; i < 8; i++) {
        job *j = CRES_RET(
            job_submit(.pool = pool, .run = ordered_run, .result = ordered_result, .done = ordered_done,
                       .data = (void *)i),
            return EXIT_FAILURE
        );
        ref_put(j);
    }

    if (!poll_until(pool, &nr_ordered_done, 8)) return EXIT_FAILURE;

    return order_failed || next_result != 8 * NR_YIELDS ? EXIT_FAILURE : EXIT_SUCCESS;
}

/****************************************************************************
 * cancellation
 ****************************************************************************/

typedef struct gate_job {
    cerr        err;
    atomic_uint done;
    /* run() sets it once it is in, or has reported */
    atomic_bool started;
    /* run() goes on once the test sets it */
    atomic_bool release;
    /* a second gate, for a run() that reports twice */
    atomic_bool proceed;
    atomic_bool ran;
} gate_job;

static cerr gate_run(job *j)
{
    gate_job *gj = job_get_data(j);

    atomic_store_explicit(&gj->started, true, memory_order_release);
    atomic_store_explicit(&gj->ran, true, memory_order_relaxed);
    wait_for(&gj->release);

    return CERR_OK;
}

static void gate_done(job *j, cerr err)
{
    gate_job *gj = job_get_data(j);

    gj->err = err;
    atomic_fetch_add_explicit(&gj->done, 1, memory_order_release);
}

/* A queued job that's cancelled never runs, and completes with CERR_CANCELLED */
static int worker_cancel_queued_test(void)
{
    static gate_job blocker, victim;
    int ret = EXIT_SUCCESS;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    blocker = (gate_job){};
    victim = (gate_job){};

    LOCAL_SET(job, b) = CRES_RET(
        job_submit(.pool = pool, .run = gate_run, .done = gate_done, .data = &blocker),
        return EXIT_FAILURE
    );
    if (!wait_for(&blocker.started))    ret = EXIT_FAILURE;

    /* the one worker is busy: this one waits in the queue */
    LOCAL_SET(job, v) = CRES_RET(
        job_submit(.pool = pool, .run = gate_run, .done = gate_done, .data = &victim),
        { atomic_store(&blocker.release, true); return EXIT_FAILURE; }
    );
    if (job_get_state(v) != JOB_QUEUED) ret = EXIT_FAILURE;
    job_cancel(v);
    if (!job_is_cancelled(v))           ret = EXIT_FAILURE;

    atomic_store_explicit(&blocker.release, true, memory_order_release);
    if (!poll_until(pool, &blocker.done, 1))        ret = EXIT_FAILURE;
    if (!poll_until(pool, &victim.done, 1))         ret = EXIT_FAILURE;
    if (IS_CERR(blocker.err))                       ret = EXIT_FAILURE;
    if (!IS_CERR_CODE(victim.err, CERR_CANCELLED))  ret = EXIT_FAILURE;
    if (atomic_load(&victim.ran))                   ret = EXIT_FAILURE;
    if (job_get_state(v) != JOB_DELIVERED)          ret = EXIT_FAILURE;

    return ret;
}

static cerr cancellable_run(job *j)
{
    gate_job *gj = job_get_data(j);

    atomic_store_explicit(&gj->started, true, memory_order_release);
    while (!job_is_cancelled(j))    usleep(100);

    return CERR_OK;
}

/* A running job sees the cancellation and finishes on its own terms */
static int worker_cancel_running_test(void)
{
    static gate_job gj;
    int ret = EXIT_SUCCESS;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    gj = (gate_job){};
    LOCAL_SET(job, j) = CRES_RET(
        job_submit(.pool = pool, .run = cancellable_run, .done = gate_done, .data = &gj),
        return EXIT_FAILURE
    );
    if (!wait_for(&gj.started))             ret = EXIT_FAILURE;
    if (job_get_state(j) != JOB_RUNNING)    ret = EXIT_FAILURE;

    job_cancel(j);
    if (!poll_until(pool, &gj.done, 1))     ret = EXIT_FAILURE;
    if (IS_CERR(gj.err))                    ret = EXIT_FAILURE;
    if (job_get_state(j) != JOB_DELIVERED)  ret = EXIT_FAILURE;

    return ret;
}

/****************************************************************************
 * progress
 ****************************************************************************/

/* Reports halfway once let go, and done once let go again */
static cerr progress_run(job *j)
{
    gate_job *gj = job_get_data(j);

    wait_for(&gj->release);
    job_set_progress(j, 0.5f);
    atomic_store_explicit(&gj->started, true, memory_order_release);
    wait_for(&gj->proceed);
    job_set_progress(j, 1.0f);

    return CERR_OK;
}

static int worker_progress_test(void)
{
    static gate_job gj;
    int ret = EXIT_SUCCESS;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    gj = (gate_job){};
    LOCAL_SET(job, j) = CRES_RET(
        job_submit(.pool = pool, .run = progress_run, .done = gate_done, .data = &gj),
        return EXIT_FAILURE
    );
    if (job_get_progress(j) != 0.0f)    ret = EXIT_FAILURE;

    atomic_store_explicit(&gj.release, true, memory_order_release);
    if (!wait_for(&gj.started))         ret = EXIT_FAILURE;
    if (job_get_progress(j) != 0.5f)    ret = EXIT_FAILURE;

    atomic_store_explicit(&gj.proceed, true, memory_order_release);
    if (!poll_until(pool, &gj.done, 1)) ret = EXIT_FAILURE;
    if (job_get_progress(j) != 1.0f)    ret = EXIT_FAILURE;

    return ret;
}

/****************************************************************************
 * teardown
 ****************************************************************************/

static cerr sleepy_run(job *j)
{
    atomic_uint *ran = job_get_data(j);

    usleep(1000);
    atomic_fetch_add_explicit(ran, 1, memory_order_relaxed);

    return CERR_OK;
}

static void sleepy_done(job *j, cerr err)
{
    atomic_fetch_add_explicit(&nr_done, 1, memory_order_relaxed);
}

/* Dropping the pool runs what's queued and delivers it before returning */
static int worker_drain_test(void)
{
    static atomic_uint ran;

    atomic_store(&ran, 0);
    atomic_store(&nr_done, 0);

    worker_pool *pool = CRES_RET(single_pool(), return EXIT_FAILURE);
    for (int i = 0; i < 8; i++) {
        job *j = CRES_RET(
            job_submit(.pool = pool, .run = sleepy_run, .done = sleepy_done, .data = &ran),
            { ref_put_last(pool); return EXIT_FAILURE; }
        );
        ref_put(j);
    }

    ref_put_last(pool);

    return atomic_load(&ran) != 8 || atomic_load(&nr_done) != 8 ? EXIT_FAILURE : EXIT_SUCCESS;
}

/****************************************************************************
 * pacing, yielding into nothing, the CPU budget
 ****************************************************************************/

static unsigned int paced_delivered;

static void paced_result(job *j, void *result)
{
    /* every message in production order, however many polls it takes */
    if ((uintptr_t)result != paced_delivered)   order_failed = true;
    paced_delivered++;
}

static void paced_done(job *j, cerr err)
{
    unsigned int index = (uintptr_t)job_get_data(j);

    if (paced_delivered != (index + 1) * NR_YIELDS) order_failed = true;
    atomic_fetch_add_explicit(&nr_ordered_done, 1, memory_order_release);
}

/* A capped poll delivers that many and keeps the rest, in order, for the next one */
static int worker_poll_max_test(void)
{
    int ret = EXIT_SUCCESS;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    paced_delivered = 0;
    order_failed = false;
    atomic_store(&nr_ordered_ran, 0);
    atomic_store(&nr_ordered_done, 0);
    for (uintptr_t i = 0; i < 4; i++) {
        job *j = CRES_RET(
            job_submit(.pool = pool, .run = ordered_run, .result = paced_result, .done = paced_done,
                       .data = (void *)i),
            return EXIT_FAILURE
        );
        ref_put(j);
    }

    /* 4 x (NR_YIELDS results + a completion) pile up behind the one worker before any poll */
    if (!wait_until(&nr_ordered_ran, 4))    ret = EXIT_FAILURE;
    if (worker_pool_poll(pool, 3) != 3)     ret = EXIT_FAILURE;

    for (unsigned int i = 0; i < WAIT_STEPS && atomic_load(&nr_ordered_done) < 4; i++) {
        unsigned int nr = worker_pool_poll(pool, 3);

        if (nr > 3) ret = EXIT_FAILURE;
        if (!nr)    usleep(100);
    }

    if (atomic_load(&nr_ordered_done) != 4) ret = EXIT_FAILURE;
    if (worker_pool_poll(pool, 0))          ret = EXIT_FAILURE;
    if (order_failed)                       ret = EXIT_FAILURE;
    if (paced_delivered != 4 * NR_YIELDS)   ret = EXIT_FAILURE;

    return ret;
}

static cerr child_run(job *j)
{
    return CERR_OK;
}

static cerr yield_into_nothing_run(job *j)
{
    /* no result callback to receive it: refused rather than leaked */
    return job_yield(j, j);
}

static int worker_yield_unreceived_test(void)
{
    static gate_job gj;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    gj = (gate_job){};
    job *j = CRES_RET(
        job_submit(.pool = pool, .run = yield_into_nothing_run, .done = gate_done, .data = &gj),
        return EXIT_FAILURE
    );
    ref_put(j);
    if (!poll_until(pool, &gj.done, 1)) return EXIT_FAILURE;

    return IS_CERR_CODE(gj.err, CERR_INVALID_OPERATION) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* A pool with a context gets what clap_cpus_reserve() grants */
static int worker_budget_test(void)
{
    LOCAL_SET(worker_pool, pool) = CRES_RET(
        worker_pool_new(.ctx = test_ctx, .name = "test", .nr_threads = TEST_CPU_BUDGET + 6),
        return EXIT_FAILURE
    );

    return worker_pool_nr_threads(pool) == TEST_CPU_BUDGET ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int worker_invalid_test(void)
{
    auto no_threads = worker_pool_new(.name = "test", .nr_threads = 0);
    auto no_name = worker_pool_new(.nr_threads = 1);
    if (!IS_CERR(no_threads) || !IS_CERR(no_name))  return EXIT_FAILURE;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    auto no_run = job_submit(.pool = pool);
    auto no_pool = job_submit(.run = child_run);
    if (!IS_CERR(no_run) || !IS_CERR(no_pool))      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/****************************************************************************
 * thread-local storage
 ****************************************************************************/

/* The owner sets it before the job runs, and the job sets its own */
static thread_local int tls_value;

typedef struct tls_job {
    atomic_uint done;
    /* the worker's tls_value before and after run() sets it */
    int         before;
    int         after;
} tls_job;

static cerr tls_run(job *j)
{
    tls_job *tj = job_get_data(j);

    tj->before = tls_value;
    tls_value = 2;
    tj->after = tls_value;

    return CERR_OK;
}

static void tls_done(job *j, cerr err)
{
    tls_job *tj = job_get_data(j);

    atomic_fetch_add_explicit(&tj->done, 1, memory_order_release);
}

/* A worker's thread_local variable is its own: it neither sees nor changes the owner's */
static int worker_thread_local_test(void)
{
    static tls_job tj;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);

    tls_value = 1;
    tj = (tls_job){};
    job *j = CRES_RET(job_submit(.pool = pool, .run = tls_run, .done = tls_done, .data = &tj), return EXIT_FAILURE);
    ref_put(j);

    bool done = poll_until(pool, &tj.done, 1);
    int ret = tj.before == 0 && tj.after == 2 && tls_value == 1 ? EXIT_SUCCESS : EXIT_FAILURE;

    tls_value = 0;

    return done ? ret : EXIT_FAILURE;
}

/****************************************************************************
 * logging from jobs
 ****************************************************************************/

/** LOG_PROBE_MAX - lines the log probe keeps */
#define LOG_PROBE_MAX 8

/* The tests' "log-" lines, in the order the ring hands them to its sinks */
typedef struct log_probe {
    pthread_t       owner;
    char            lines[LOG_PROBE_MAX][16];
    unsigned int    nr;
    /* a line came out on a thread other than the owner's */
    bool            off_owner;
} log_probe;

static log_probe probe;

static void log_probe_flush(struct log_entry *e, void *data)
{
    log_probe *lp = data;

    if (strncmp(e->msg, "log-", 4))                 return;
    if (!pthread_equal(pthread_self(), lp->owner))  lp->off_owner = true;
    if (lp->nr < LOG_PROBE_MAX)                     strncpy(lp->lines[lp->nr], e->msg, sizeof(lp->lines[0]) - 1);
    lp->nr++;
}

static cerr log_probe_start(void)
{
    probe = (log_probe){ .owner = pthread_self() };

    return rb_sink_add(log_probe_flush, &probe, VDBG, 1);
}

/* Sinks are flushed on the way into the ring: a line of its own brings out the ones before it */
static unsigned int log_probe_fence(void)
{
    msg("fence\n");

    return probe.nr;
}

/* The probe saw @expected, in order, all of it on the owner's thread */
static int log_probe_check(const char **expected, unsigned int nr)
{
    rb_sink_del(&probe);

    if (probe.off_owner || probe.nr != nr)  return EXIT_FAILURE;

    for (unsigned int i = 0; i < nr; i++)
        if (strcmp(probe.lines[i], expected[i]))    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

typedef struct log_job {
    atomic_uint done;
    /* run() is past its last line */
    atomic_bool ended;
    /* the owner has logged its own line: run() may return */
    atomic_bool go;
} log_job;

static cerr log_order_run(job *j)
{
    log_job *lj = job_get_data(j);

    msg("log-one\n");
    CERR_RET_CERR(job_yield(j, (void *)1));
    msg("log-two\n");
    CERR_RET_CERR(job_yield(j, (void *)2));
    msg("log-three\n");
    atomic_store_explicit(&lj->ended, true, memory_order_release);

    return CERR_OK;
}

static void log_order_result(job *j, void *result)
{
    msg("log-R%u\n", (unsigned int)(uintptr_t)result);
}

static void log_job_done(job *j, cerr err)
{
    log_job *lj = job_get_data(j);

    msg("log-D\n");
    atomic_fetch_add_explicit(&lj->done, 1, memory_order_release);
}

/* What a job logs comes out on the owner's thread, each line ahead of the callback it led up to */
static int worker_log_order_test(void)
{
    static const char *order[] = { "log-one\n", "log-R1\n", "log-two\n", "log-R2\n", "log-three\n", "log-D\n" };
    static log_job lj;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);
    CERR_RET(log_probe_start(), return EXIT_FAILURE);

    lj = (log_job){};
    job *j = CRES_RET(
        job_submit(.pool = pool, .run = log_order_run, .result = log_order_result, .done = log_job_done,
                   .data = &lj),
        { rb_sink_del(&probe); return EXIT_FAILURE; }
    );
    ref_put(j);

    /* the job has logged all it will, and nothing has polled: none of it is out */
    bool ended = wait_for(&lj.ended);
    unsigned int early = log_probe_fence();
    bool done = poll_until(pool, &lj.done, 1);
    log_probe_fence();

    int ret = log_probe_check(order, array_size(order));

    return ended && !early && done ? ret : EXIT_FAILURE;
}

static cerr log_drop_run(job *j)
{
    msg("log-drop\n");

    return CERR_OK;
}

/* A pool's drop prints what its jobs logged, without a poll */
static int worker_log_drop_test(void)
{
    static const char *order[] = { "log-drop\n", "log-D\n" };
    static log_job lj;

    worker_pool *pool = CRES_RET(single_pool(), return EXIT_FAILURE);
    CERR_RET(log_probe_start(), { ref_put_last(pool); return EXIT_FAILURE; });

    lj = (log_job){};
    job *j = CRES_RET(
        job_submit(.pool = pool, .run = log_drop_run, .done = log_job_done, .data = &lj),
        { rb_sink_del(&probe); ref_put_last(pool); return EXIT_FAILURE; }
    );
    ref_put(j);

    ref_put_last(pool);
    log_probe_fence();

    return log_probe_check(order, array_size(order));
}

static cerr log_unreceived_run(job *j)
{
    msg("log-before\n");
    cerr err = job_yield(j, j);
    msg("log-after\n");

    return err;
}

/* A yield that fails leaves what was logged before it to the completion */
static int worker_log_unreceived_test(void)
{
    static const char *order[] = { "log-before\n", "log-after\n", "log-D\n" };
    static log_job lj;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);
    CERR_RET(log_probe_start(), return EXIT_FAILURE);

    lj = (log_job){};
    job *j = CRES_RET(
        job_submit(.pool = pool, .run = log_unreceived_run, .done = log_job_done, .data = &lj),
        { rb_sink_del(&probe); return EXIT_FAILURE; }
    );
    ref_put(j);

    bool done = poll_until(pool, &lj.done, 1);
    log_probe_fence();

    int ret = log_probe_check(order, array_size(order));

    return done ? ret : EXIT_FAILURE;
}

static cerr log_owner_run(job *j)
{
    log_job *lj = job_get_data(j);

    msg("log-job\n");
    atomic_store_explicit(&lj->ended, true, memory_order_release);
    wait_for(&lj->go);

    return CERR_OK;
}

/* The owner's own lines go out as they come while a job holds its */
static int worker_log_owner_test(void)
{
    static const char *order[] = { "log-owner\n", "log-job\n", "log-D\n" };
    static log_job lj;

    LOCAL_SET(worker_pool, pool) = CRES_RET(single_pool(), return EXIT_FAILURE);
    CERR_RET(log_probe_start(), return EXIT_FAILURE);

    lj = (log_job){};
    job *j = CRES_RET(
        job_submit(.pool = pool, .run = log_owner_run, .done = log_job_done, .data = &lj),
        { rb_sink_del(&probe); return EXIT_FAILURE; }
    );
    ref_put(j);

    /* the job holds its line and waits for the owner's, which goes out on its own */
    bool ended = wait_for(&lj.ended);
    msg("log-owner\n");
    unsigned int owner = log_probe_fence();
    atomic_store_explicit(&lj.go, true, memory_order_release);

    bool done = poll_until(pool, &lj.done, 1);
    log_probe_fence();

    int ret = log_probe_check(order, array_size(order));

    return ended && owner == 1 && done ? ret : EXIT_FAILURE;
}

static const struct test tests[] = {
    { .name = "worker results and completions",   .test = worker_basic_test },
    { .name = "worker delivery order",            .test = worker_order_test },
    { .name = "worker cancel queued",             .test = worker_cancel_queued_test },
    { .name = "worker cancel running",            .test = worker_cancel_running_test },
    { .name = "worker progress",                  .test = worker_progress_test },
    { .name = "worker drain on drop",             .test = worker_drain_test },
    { .name = "worker poll cap",                  .test = worker_poll_max_test },
    { .name = "worker yield without a receiver",  .test = worker_yield_unreceived_test },
    { .name = "worker CPU budget",                .test = worker_budget_test },
    { .name = "worker invalid arguments",         .test = worker_invalid_test },
    { .name = "worker thread_local per thread",   .test = worker_thread_local_test },
    { .name = "worker log order",                 .test = worker_log_order_test },
    { .name = "worker log on drop",               .test = worker_log_drop_test },
    { .name = "worker log past a failed yield",   .test = worker_log_unreceived_test },
    { .name = "worker log beside the owner's",    .test = worker_log_owner_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
