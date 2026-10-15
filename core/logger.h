/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LOGGER_H__
#define __CLAP_LOGGER_H__

#include <sys/types.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include "common.h"
#include "error.h"

#define LOG_RB_MAX 512
#define LOG_STDIO   1
#define LOG_RB      2
#define LOG_QUIET   4
#define LOG_DEFAULT  (LOG_STDIO)
#define LOG_FULL     (LOG_STDIO | LOG_RB)

enum {
    FTRACE = -3,
    VDBG = -2,
    DBG,
    NORMAL = 0,
    WARN,
    ERR,
};

extern unsigned int abort_on_error;

struct log_entry {
    struct timespec ts;     /* timestamp */
    const char      *mod;   /* module */
    const char      *func;  /* function */
    const char      *msg;   /* payload */
    int             line;
    int             level;
};

cerr rb_sink_add(void (*flush)(struct log_entry *e, void *data), void *data, int filter, int fill)
    __nonnull_params((1,2));
void rb_sink_del(void *data) __nonnull_params();

void hexdump(unsigned char *buf, size_t size) __nonnull_params();
struct clap_context;
void log_init(struct clap_context *ctx, unsigned int flags);
void vlogg(int level, const char *mod, int line, const char *func, const char *fmt, va_list va)
    __nonnull_params((5));
void logg(int level, const char *mod, int line, const char *func, const char *fmt, ...)
    __printf(5, 6) __nonnull_params((5));

/**
 * log_floor_set() - set the log floor
 * @level:  the lowest level that gets logged: the enum above (VDBG shows
 *          trace(), DBG shows dbg(), ...)
 *
 * Messages below @level are dropped on the way in.
 */
void log_floor_set(int level);

struct log_held_line;

/**
 * struct log_held - log lines one thread holds for another to print
 * @first:  the oldest line, nullptr for none
 * @last:   the newest line
 *
 * One thread fills it and another prints it; handing it over between them
 * (in a worker job's message, say) is what orders their accesses.
 */
typedef struct log_held {
    struct log_held_line    *first;
    struct log_held_line    *last;
} log_held;

/**
 * log_hold() - hold the calling thread's log lines instead of logging them
 * @held:   where the lines collect, oldest first; nullptr logs them as they come again
 *
 * The ring buffer and its sinks (the network log stream among them) run on
 * the thread that logs and aren't thread-safe: they belong to the main
 * thread. Another thread that logs holds its lines, formatted, for the main
 * thread's log_held_print(), and reads no logger state meanwhile: the noise
 * floor applies when they print. A line at %ERR also goes to stderr right
 * away, since it may be the last thing the thread does (err_on() under -E,
 * a Box3D assertion).
 * Context: the thread whose lines are held
 */
void log_hold(log_held *held);

/**
 * log_held_print() - log the lines another thread held, and free them
 * @held:   the lines log_hold() collected; empty on return
 *
 * Each line goes through the noise floor and the loggers on the calling
 * thread, with the module, line and function it was logged from; a line that
 * went to stderr already skips stdio.
 * Context: a thread that isn't holding its own lines: the main thread
 */
void log_held_print(log_held *held);

#define trace(args...) \
    logg(VDBG, MODNAME, __LINE__, __func__, ## args);
#define trace_on(_c, args...) do { if ((_c)) trace("condition '" # _c "': " args); } while (0)
#define dbg(args...) \
    logg(DBG, MODNAME, __LINE__, __func__, ## args);

#define dbg_on(_c, args...) do { if ((_c)) dbg("condition '" # _c "': " args); } while (0)
#define dbg_once(args...) do { static int __printed = 0; if (!__printed++) dbg(args) } while (0)
#define msg(args...) \
    logg(NORMAL, MODNAME, __LINE__, __func__, ## args);
/**
 * msg_once() - log a normal-level message the first time this call site runs
 * @args:   the format string and its arguments, as for msg()
 */
#define msg_once(args...) do { static int __printed = 0; if (!__printed++) msg(args) } while (0)
#define warn(args...) \
    logg(WARN, MODNAME, __LINE__, __func__, ## args);
#define warn_on(_c, args...) do { if ((_c)) warn("condition '" # _c "': " args); } while (0)
#define warn_once(args...) do { static int __printed = 0; if (!__printed++) warn(args) } while (0)
#define err(args...) \
    logg(ERR, MODNAME, __LINE__, __func__, ## args);
#define err_cerr(_cerr, _fmt, args...) do { \
    if (!IS_CERR_CODE((_cerr), CERR_REQUEST_EXIT)) { \
        char __buf[512]; \
        cerr_strbuf(__buf, sizeof(__buf), &(_cerr)); \
        logg(ERR, MODNAME, __LINE__, __func__, "[%s] " _fmt, __buf, ## args); \
    } \
} while (0)

/*
 * err_on_cond() ignores @_cc, because sometimes format strings end up there,
 * which subsequently get pasted with the actuall error format string. I'm too
 * lazy to untangle this right now; source line information is already plenty
 * to go on
 */
#define err_on_cond(_c, _cc, args...) do { if ((_c)) { err("error: " args); if (abort_on_error) enter_debugger(); } } while (0)
//void err_on_cond(bool cond, const char *condstr, const char *fmt, ...);
#define err_on(_c, args...) err_on_cond(_c, __stringify(_c), ## args)

#endif /* __CLAP_LOGGER_H__ */
