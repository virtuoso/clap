// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
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

static thread_local unsigned long submit_failures = 0;

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
 * ? make log_rb_wp atomic
 */
static struct log_entry *log_rb;
static int log_rb_wp;
static int log_rb_sz;
static DECLARE_LIST(log_rb_sinks);

struct rb_sink {
    struct list entry;
    void        (*flush)(struct log_entry *e, void *data);
    void        *data;
    int         fill;
    int         rp;
    int         filter;
};

static notrace void rb_flush_one(struct rb_sink *sink)
{
    int i;

    for (i = (sink->rp + 1) % log_rb_sz; i != log_rb_wp; i = (i + 1) % log_rb_sz)
        if (log_rb[i].msg) {
            if (log_rb[i].level >= sink->filter) {
                /*fprintf(log_rb_output, "[%08lu.%09lu] %s",
                    log_rb[i].ts.tv_sec, log_rb[i].ts.tv_nsec,
                    log_rb[i].msg);*/
                sink->flush(&log_rb[i], sink->data);
                sink->rp = i;
            }
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
    s->rp     = -1;
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

static notrace int rb_space(int readp)
{
    int writep = log_rb_wp;

    if (readp > writep)
        writep += log_rb_sz;

    return writep - readp;
}

static notrace bool rb_needs_flush(struct rb_sink *s)
{
    if (s->rp == -1) {
        s->rp = 0;
        return true;
    }

    if (log_rb[log_rb_wp].msg)
        return true;
    
    if (rb_space(s->rp) >= s->fill)
        return true;

    return false;
}

static notrace void rb_flush(void)
{
    struct rb_sink *sink;
    int rp_min = __INT_MAX__, rp_max = __INT_MAX__, i;

    list_for_each_entry(sink, &log_rb_sinks, entry) {
        if (rb_needs_flush(sink)) {
            rp_min = min(rp_min, sink->rp);
            rb_flush_one(sink);
            rp_max = min(rp_max, sink->rp);
        }
    }

    if (rp_min == __INT_MAX__ || rp_max == __INT_MAX__)
        return;
    for (i = rp_min; i != rp_max; i = (i + 1) % log_rb_sz) {
        mem_free((void *)log_rb[i].msg);
        log_rb[i].msg = NULL;
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
    exit_cleanup(rb_cleanup);

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

    log_rb[log_rb_wp].mod = mod;
    log_rb[log_rb_wp].func = func;
    log_rb[log_rb_wp].line = line;
    log_rb[log_rb_wp].level = level;
    log_rb[log_rb_wp].ts.tv_sec = ts.tv_sec;
    log_rb[log_rb_wp].ts.tv_nsec = ts.tv_nsec;
    log_rb[log_rb_wp].msg = msg;
    log_rb_wp = (log_rb_wp + 1) % LOG_RB_MAX;

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
    WARN
#else
    DBG
#endif
;

static int log_command_handler(struct message *m, void *data)
{
    if (m->cmd.toggle_noise)
        log_floor = log_floor == VDBG ? DBG : VDBG;

    return 0;
}

void notrace log_init(unsigned int flags)
{
    if (log_up)
        return;

    cerr err = subscribe(MT_COMMAND, log_command_handler, NULL);
    if (IS_CERR(err))
        return;

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
        log_init(LOG_FULL);

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
