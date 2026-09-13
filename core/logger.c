// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include "config.h"
#include "messagebus.h"
#include "memory.h"
#include "logger.h"
#include "util.h"

/**********************************************************
 * Logging subsystem                                      *
 **********************************************************/

/*
 * TODOs
 *  * move timestamping to the logging core from rb
 *  * think about filtering (by level, module, etc)
 *  * ftrace needs to be either more (elf symbol lookup) or go away
 *  * make rb atomic update/thread safe
 *  * make stdio logger also a file logger
 *  * allow multiple instances of the same logger
 *  * allow 'parameters'/filtering on instances
 */

unsigned int abort_on_error;

#if defined(__has_feature)
# if __has_feature(address_sanitizer)
void __asan_error_report()
{
    if (abort_on_error)
        abort();
}
# endif /* __has_feature(address_sanitizer) */
#endif /* __has_feature */

static thread_local __unused unsigned long submit_failures = 0;

/*
 * Chain multiple log sinks together, so that we can have
 * multiple independent outputs, if needed.
 * These include:
 *  + stdio: print stuff to stdout/stderr
 *  + rb:    store stuff in a ring buffer
 */
struct logger {
    const char  *name;
    cerr        (*init)(void);
    cerr        (*log)(int level, const char *mod, int line, const char *func, const char *msg);
    struct logger *next;
};

static notrace cerr
stdio_log(int level, const char *mod, int line, const char *func, const char *msg)
{
    FILE *output = stdout;

    if (level < VDBG)
        return CERR_OK;

    if (level != NORMAL)
        output = stderr;

    if (level < 0 || level >= WARN)
        fprintf(output, "[%s:%d @%s] ", str_basename(mod), line, func);
    fputs(msg, output);

    return CERR_OK;
}

static struct logger logger_stdio = {
    .name   = "stdio",
    .log    = stdio_log,
};

/*
 * TODO we do actually want this to be thread-safe
 * ? make log_rb_seq atomic
 */
static struct log_entry *log_rb;
static unsigned int log_rb_sz;
/* Sequence number of the next entry to write; entry N lives in slot N % log_rb_sz */
static unsigned long log_rb_seq;
/* Sequence number of the oldest entry that hasn't been freed yet */
static unsigned long log_rb_done;
static DECLARE_LIST(log_rb_sinks);

struct rb_sink {
    struct list         entry;
    void                (*flush)(struct log_entry *e, void *data);
    void                *data;
    unsigned long       seq;    /* next entry to flush */
    int                 fill;
    int                 filter;
};

/* Sequence number of the oldest entry still in the ring */
static notrace unsigned long rb_oldest(void)
{
    return log_rb_seq > log_rb_sz ? log_rb_seq - log_rb_sz : 0;
}

static notrace void rb_flush_one(struct rb_sink *sink)
{
    /* Anything a lap behind has been overwritten by now */
    sink->seq = max(sink->seq, rb_oldest());

    for (; sink->seq != log_rb_seq; sink->seq++) {
        struct log_entry *e = &log_rb[sink->seq % log_rb_sz];

        if (e->msg && e->level >= sink->filter)
            sink->flush(e, sink->data);
    }
}

cerr rb_sink_add(void (*flush)(struct log_entry *e, void *data), void *data, int filter, int fill)
{
    struct rb_sink *s;

    s = mem_alloc(sizeof(*s), .zero = 1);
    if (!s)
        return CERR_NOMEM;

    s->flush  = flush;
    s->filter = filter;
    s->fill   = fill;
    s->data   = data;
    s->seq    = rb_oldest();
    list_append(&log_rb_sinks, &s->entry);

    return CERR_OK;
}

void rb_sink_del(void *data)
{
    struct rb_sink *s;

    list_for_each_entry(s, &log_rb_sinks, entry) {
        if (s->data == data) {
            list_del(&s->entry);
            mem_free(s);
            return;
        }
    }
}

static notrace bool rb_needs_flush(struct rb_sink *s)
{
    unsigned long pending = log_rb_seq - s->seq;
    unsigned long fill = s->fill > 0 ? s->fill : 0;

    /* A full lap of pending entries means the next write overwrites one of them */
    return pending >= fill || pending >= log_rb_sz;
}

static notrace void rb_flush(void)
{
    struct rb_sink *sink;
    unsigned long seq_min = ULONG_MAX;

    list_for_each_entry(sink, &log_rb_sinks, entry) {
        if (rb_needs_flush(sink))
            rb_flush_one(sink);
        seq_min = min(seq_min, sink->seq);
    }

    /* Without a sink, everything stays in the ring for one that comes later */
    if (seq_min == ULONG_MAX)
        return;

    /* Entries every sink is past are done with; older ones went with their slots */
    for (log_rb_done = max(log_rb_done, rb_oldest()); log_rb_done < seq_min; log_rb_done++) {
        struct log_entry *e = &log_rb[log_rb_done % log_rb_sz];

        mem_free((void *)e->msg);
        e->msg = NULL;
    }
}

static void rb_cleanup(int status)
{
    rb_flush();
}

static notrace cerr rb_init(void)
{
    log_rb = mem_alloc(sizeof(struct log_entry), .nr = LOG_RB_MAX);
    if (!log_rb)
        return CERR_NOMEM;

    log_rb_sz = LOG_RB_MAX;
    cerr err = exit_cleanup(rb_cleanup);
    if (IS_CERR(err))
        err_cerr(err, "couldn't install rb_cleanup()\n");

    return CERR_OK;
}

static notrace cerr rb_log(int level, const char *mod, int line, const char *func, const char *msg)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_REALTIME, &ts);

    msg = strdup(msg);
    if (!msg)
        return CERR_NOMEM;

    rb_flush();

    struct log_entry *e = &log_rb[log_rb_seq % log_rb_sz];

    /*
     * Once the ring has come around, the slot holds the oldest entry, and it
     * goes whether or not every sink has flushed it: the forced flush in
     * rb_needs_flush() is a sink's last chance to see it.
     */
    mem_free((void *)e->msg);

    e->mod = mod;
    e->func = func;
    e->line = line;
    e->level = level;
    e->ts.tv_sec = ts.tv_sec;
    e->ts.tv_nsec = ts.tv_nsec;
    e->msg = msg;
    log_rb_seq++;

    return CERR_OK;
}

static struct logger logger_rb = {
    .name   = "ring buffer",
    .init   = rb_init,
    .log    = rb_log,
};

static int log_up;
static struct logger *logger;

static notrace void logger_append(struct logger *lg)
{
    struct logger **lastp;

    for (lastp = &logger; *lastp; lastp = &((*lastp)->next))
        ;

    cerr err = CERR_OK;
    if (lg->init)
        err = lg->init();

    if (!IS_CERR(err))
        *lastp = lg;
}

static int log_floor =
#ifdef CONFIG_FINAL
    NORMAL
#else
    DBG
#endif
;

void log_floor_set(int level)
{
    log_floor = level;
}

static int log_command_handler(struct clap_context *ctx, struct message *m, void *data)
{
    if (m->cmd.toggle_noise)
        log_floor = log_floor == VDBG ? DBG : VDBG;

    return 0;
}

static bool log_cmd_subscribed;

void notrace log_init(struct clap_context *ctx, unsigned int flags)
{
    if (ctx && !log_cmd_subscribed) {
        CERR_RET(subscribe(ctx, MT_COMMAND, log_command_handler, NULL), log_cmd_subscribed = true);
        if (log_up) return;
    }

    if (flags & LOG_STDIO)
        logger_append(&logger_stdio);

    if (flags & LOG_RB)
        logger_append(&logger_rb);

    if (flags & LOG_QUIET)
        log_floor = NORMAL;

    log_up++;
    dbg("logger initialized, build %s\n", build_date);
}

static notrace void log_submit(int level, const char *mod, int line, const char *func, const char *msg)
{
    struct logger *lg;

    for (lg = logger; lg; lg = lg->next) {
        cerr err = lg->log(level, mod, line, func, msg);
        if (IS_CERR(err))
            submit_failures++;
    }
}

notrace void vlogg(int level, const char *mod, int line, const char *func, const char *fmt, va_list va)
{
    LOCAL(char, buf);

    if (unlikely(!log_up))
        log_init(NULL, LOG_FULL);

    /* Filter noisy stuff on the way in */
    if (level < log_floor)
        return;

    cres(int) res = mem_vasprintf(&buf, fmt, va);

    if (IS_CERR(res))
        return;

    log_submit(level, mod, line, func, buf);
}

notrace void logg(int level, const char *mod, int line, const char *func, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    vlogg(level, mod, line, func, fmt, va);
    va_end(va);
}

#define ROW_MAX 16
#define ROW_LEN (ROW_MAX * 3 + 1 + 3 + ROW_MAX)
void hexdump(unsigned char *buf, size_t size)
{
    char row[ROW_LEN];
    size_t done;
    int i, s = 0;

    for (done = 0; done < size; done++) {
        i = done % ROW_MAX;
        if (!i) {
            // s += sprintf(row + s, "  |");
            // s += sprintf(row + s, "|");
            row[ROW_MAX * 3] = ' ';
            s = 0;
            if (done)
                dbg("XD: %s\n", row);
        }
        s += snprintf(row + s, ROW_LEN - s, "%02x ", buf[done]);
        row[ROW_LEN - ROW_MAX - 3 + i] = isalnum(buf[done]) ? buf[done] : '.';
    }

    if (s)
        dbg("XD: %s\n", row);
}

notrace void __cyg_profile_func_enter(void *this_fn,
                              void *call_site)
{
    logg(FTRACE, NULL, 0, NULL, "%p <- %p\n", this_fn, call_site);
}

notrace void __cyg_profile_func_exit(void *this_fn,
                             void *call_site)
{
    logg(FTRACE, NULL, 0, NULL, "%p <- %p\n", this_fn, call_site);
}
