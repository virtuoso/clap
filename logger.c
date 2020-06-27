#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include "config.h"
#include "messagebus.h"
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

struct log_entry {
    struct timespec ts;     /* timestamp */
    const char      *mod;   /* module */
    const char      *func;  /* function */
    char            *msg;   /* payload */
    int             level;
};

/*
 * Chain multiple log sinks together, so that we can have
 * multiple independent outputs, if needed.
 * These include:
 *  + stdio: print stuff to stdout/stderr
 *  + rb:    store stuff in a ring buffer
 */
struct logger {
    const char  *name;
    int         (*init)(void);
    int         (*log)(int level, const char *mod, const char *func, char *msg);
    struct logger *next;
};

static notrace int stdio_log(int level, const char *mod, const char *func, char *msg)
{
    FILE *output = stdout;

    if (level < VDBG)
        return 0;

    if (level != NORMAL)
        output = stderr;

    if (level < 0)
        fprintf(output, "[%s @%s] ", mod, func);
    fputs(msg, output);

    return 0;
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
static FILE *log_rb_output;

static notrace void rb_flush(void)
{
    int i, filter = VDBG;

    for (i = (log_rb_wp + 1) % log_rb_sz; i != log_rb_wp; i = (i + 1) % log_rb_sz)
        if (log_rb[i].msg) {
            if (log_rb[i].level >= filter) {
                fprintf(log_rb_output, "[%08lu.%09lu] %s",
                        log_rb[i].ts.tv_sec, log_rb[i].ts.tv_nsec,
                        log_rb[i].msg);
            }
            free(log_rb[i].msg);
            log_rb[i].msg = NULL;
        }
}

static void rb_cleanup(int status)
{
    rb_flush();
}

static notrace int rb_init(void)
{
    log_rb = calloc(LOG_RB_MAX, sizeof(struct log_entry));
    if (!log_rb)
        return -ENOMEM;

    log_rb_output = stdout;
    log_rb_sz = LOG_RB_MAX;
    exit_cleanup(rb_cleanup);

    return 0;
}

static notrace int rb_log(int level, const char *mod, const char *func, char *msg)
{
    struct timespec ts;

    /* XXX not dealing with absence of clock_gettime() */
    (void)clock_gettime(CLOCK_REALTIME, &ts);

    msg = strdup(msg);
    if (!msg)
        return -ENOMEM; /* XXX error codes */

    if (log_rb[log_rb_wp].msg)
        rb_flush();

    log_rb[log_rb_wp].mod = mod;
    log_rb[log_rb_wp].func = func;
    log_rb[log_rb_wp].level = level;
    log_rb[log_rb_wp].ts.tv_sec = ts.tv_sec;
    log_rb[log_rb_wp].ts.tv_nsec = ts.tv_nsec;
    log_rb[log_rb_wp].msg = msg;
    log_rb_wp = (log_rb_wp + 1) % LOG_RB_MAX;

    return 0;
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

    *lastp = lg;
    if (lg->init)
        lg->init();
}

static int log_floor = DBG;

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

    if (flags & LOG_STDIO)
        logger_append(&logger_stdio);

    if (flags & LOG_RB)
        logger_append(&logger_rb);

    subscribe(MT_COMMAND, log_command_handler, NULL);
    log_up++;
    dbg("logger initialized, build %s\n", CONFIG_BUILDDATE);
}

static notrace void log_submit(int level, const char *mod, const char *func, char *msg)
{
    struct logger *lg;

    for (lg = logger; lg; lg = lg->next) {
        //fprintf(stderr, "# sending '%s' to '%s'\n", msg, lg->name);
        lg->log(level, mod, func, msg);
    }
}

notrace void logg(int level, const char *mod, const char *func, char *fmt, ...)
{
    LOCAL(char, buf);
    va_list va;
    int ret;

    if (unlikely(!log_up))
        log_init(LOG_FULL);

    /* Filter noisy stuff on the way in */
    if (level < log_floor)
        return;

    va_start(va, fmt);
    ret = vasprintf(&buf, fmt, va);
    va_end(va);

    if (ret < 0)
        return;

    log_submit(level, mod, func, buf);
}

#define ROW_MAX 16
#define ROW_LEN (ROW_MAX * 3 + 1)
void hexdump(unsigned char *buf, size_t size)
{
    char row[ROW_LEN];
    size_t done;
    int i, s = 0;

    for (done = 0; done < size; done++) {
        i = done % ROW_MAX;
        if (!i) {
            s = 0;
            if (done)
                dbg("XD: %s\n", row);
        }
        s += snprintf(row + s, ROW_LEN - s, "%02x ", buf[done]);
    }
    if (s)
        dbg("XD: %s\n", row);
}

notrace void __cyg_profile_func_enter(void *this_fn,
                              void *call_site)
{
    logg(FTRACE, NULL, NULL, "%p <- %p\n", this_fn, call_site);
}

notrace void __cyg_profile_func_exit(void *this_fn,
                             void *call_site)
{
    logg(FTRACE, NULL, NULL, "%p <- %p\n", this_fn, call_site);
}
