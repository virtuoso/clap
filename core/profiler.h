/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROFILER_H__
#define __CLAP_PROFILER_H__

#include <sys/time.h>
#include <time.h>

struct profile {
    struct timespec ts, diff;
    struct profile  *next;
    const char      *name;
};

#ifndef CONFIG_FINAL

#define PROFILER

#define PROF_NAME(_n) prof_ ## _n

#define PROF_PTR(_n) (&(prof_ ## _n))

#define DECLARE_PROF(_n) \
    struct profile PROF_NAME(_n) = { .name = __stringify(_n) }

#define PROF_FIRST(_n) \
    DECLARE_PROF(_n); \
    clock_gettime(CLOCK_MONOTONIC, &PROF_PTR(_n)->ts);

#define PROF_STEP(_n, _prev) \
    DECLARE_PROF(_n); \
    clock_gettime(CLOCK_MONOTONIC, &PROF_PTR(_n)->ts); \
    PROF_PTR(_prev)->next = &prof_ ## _n; \
    timespec_diff(&PROF_PTR(_prev)->ts, &PROF_PTR(_n)->ts, &PROF_PTR(_n)->diff);

#define PROF_SHOW(_n) \
    dbg("PROFILER: '%s': %lu.%09lu\n", __stringify(_n), prof_ ## _n.diff.tv_sec, prof_ ## _n.diff.tv_nsec);

void profiler_show(struct profile *first);

#else
#define DECLARE_PROF(x)
#define PROF_NAME(x)
#define PROF_PTR(x) NULL
#define PROF_FIRST(x)
#define PROF_STEP(x,y)
#define PROF_SHOW(x)
#define PROF_STRUCT(x)
static inline void profiler_show(struct profile *first) {}
#endif

#endif /* __CLAP_PROFILER_H__ */
