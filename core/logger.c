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
 *  * color warnings and errors in Windows consoles too: they don't set TERM,
 *    so stdio_init() leaves them plain, and only take ANSI escapes after
 *    SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
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

/* A line log_hold() keeps for log_held_print() */
struct log_held_line {
    struct log_held_line    *next;
    const char              *mod;
    const char              *func;
    char                    *msg;
    int                     line;
    int                     level;
};

/* Where a thread that holds its lines (log_hold()) collects them */
static thread_local log_held *log_holder;

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

/* stdio_log() prints warnings and errors in bold red, see stdio_init() */
static bool stdio_color;

/*
 * Color goes to a terminal that takes ANSI escapes: stderr is a terminal,
 * TERM names one that isn't "dumb", and NO_COLOR (https://no-color.org) is
 * unset or empty. The browser's console isn't one, whatever isatty() says.
 */
static notrace cerr stdio_init(void)
{
#ifndef CONFIG_BROWSER
    const char *term = getenv("TERM"), *no_color = getenv("NO_COLOR");

    stdio_color = isatty(STDERR_FILENO) && term && strcmp(term, "dumb") && !(no_color && *no_color);
#endif /* !CONFIG_BROWSER */

    return CERR_OK;
}

static notrace cerr
stdio_log(int level, const char *mod, int line, const char *func, const char *msg)
{
    FILE *output = stdout;
    bool color = stdio_color && level >= WARN;

    if (level < VDBG)
        return CERR_OK;

    if (level != NORMAL)
        output = stderr;

    if (color)  fputs("\033[1;31m", output);
    if (level < 0 || level >= WARN)
        fprintf(output, "[%s:%d @%s] ", str_basename(mod), line, func);
    fputs(msg, output);
    if (color)  fputs("\033[0m", output);

    return CERR_OK;
}

static struct logger logger_stdio = {
    .name   = "stdio",
    .init   = stdio_init,
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

/* the floor that log_floor_set() pinned, which LOG_QUIET leaves alone */
static bool log_floor_pinned;

void log_floor_set(int level)
{
    log_floor = level;
    log_floor_pinned = true;
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

    if ((flags & LOG_QUIET) && !log_floor_pinned)
        log_floor = NORMAL;

    log_up++;
    dbg("logger initialized, build %s\n", build_date);
}

/* Bring the logger up on first use, and drop what's under the noise floor */
static notrace bool log_is_filtered(int level)
{
    if (unlikely(!log_up))  log_init(nullptr, LOG_FULL);

    return level < log_floor;
}

static notrace void log_submit(int level, const char *mod, int line, const char *func, const char *msg,
                               const struct logger *skip)
{
    struct logger *lg;

    for (lg = logger; lg; lg = lg->next) {
        if (lg == skip) continue;

        cerr err = lg->log(level, mod, line, func, msg);
        if (IS_CERR(err))
            submit_failures++;
    }
}

/* Format a line for log_held_print() instead of logging it */
static notrace void log_held_add(log_held *held, int level, const char *mod, int line, const char *func,
                                 const char *fmt, va_list va)
{
    struct log_held_line *l = mem_alloc(sizeof(*l), .zero = 1);
    if (!l) {
        submit_failures++;
        return;
    }

    cres(int) res = mem_vasprintf(&l->msg, fmt, va);
    if (IS_CERR(res)) {
        mem_free(l);
        submit_failures++;
        return;
    }

    l->mod   = mod;
    l->func  = func;
    l->line  = line;
    l->level = level;

    /* it may be the thread's last word: err_on() under -E, a Box3D assertion */
    if (level >= ERR)   stdio_log(level, mod, line, func, l->msg);

    if (held->last) held->last->next = l;
    else            held->first = l;
    held->last = l;
}

notrace void log_hold(log_held *held)
{
    log_holder = held;
}

notrace void log_held_print(log_held *held)
{
    struct log_held_line *l = held->first;

    *held = (log_held){};

    while (l) {
        struct log_held_line *next = l->next;

        /* stdio has had an error line already */
        if (!log_is_filtered(l->level))
            log_submit(l->level, l->mod, l->line, l->func, l->msg, l->level >= ERR ? &logger_stdio : nullptr);

        mem_free(l->msg);
        mem_free(l);
        l = next;
    }
}

notrace void vlogg(int level, const char *mod, int line, const char *func, const char *fmt, va_list va)
{
    LOCAL(char, buf);

    /* A holding thread reads no logger state: the floor applies when its lines print */
    if (log_holder) {
        log_held_add(log_holder, level, mod, line, func, fmt, va);
        return;
    }

    /* Filter noisy stuff on the way in */
    if (log_is_filtered(level)) return;

    cres(int) res = mem_vasprintf(&buf, fmt, va);

    if (IS_CERR(res))
        return;

    log_submit(level, mod, line, func, buf, nullptr);
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
