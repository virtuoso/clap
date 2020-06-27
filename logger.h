#ifndef __CLAP_LOGGER_H__
#define __CLAP_LOGGER_H__

#ifndef MODNAME
#define MODNAME __BASE_FILE__
#endif

#define LOG_RB_MAX 512
#define LOG_STDIO   1
#define LOG_RB      2
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

void hexdump(unsigned char *buf, size_t size);
void log_init(unsigned int flags);
void logg(int level, const char *mod, const char *func, char *fmt, ...);
#define trace(args...) \
    logg(VDBG, MODNAME, __func__, ## args);
#define dbg(args...) \
    logg(DBG, MODNAME, __func__, ## args);
#define msg(args...) \
    logg(NORMAL, MODNAME, __func__, ## args);
#define warn(args...) \
    logg(WARN, MODNAME, __func__, ## args);
#define err(args...) \
    logg(ERR, MODNAME, __func__, ## args);

#endif /* __CLAP_LOGGER_H__ */
