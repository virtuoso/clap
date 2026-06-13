/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NETWORKING_H__
#define __CLAP_NETWORKING_H__

#include <errno.h>
#include "clap.h"

#if defined(__APPLE__) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

enum net_mode {
    NET_CLIENT = 0,
    NET_SERVER,
    NET_LISTEN,
};

struct networking_config {
    struct clap_context *clap;
    const char  *server_ip;
    unsigned int server_port;
    unsigned int server_wsport;
    unsigned long   logger  : 1;
    int             timeout;
};

#ifdef CONFIG_NETWORKING
cerr_check networking_init(struct clap_context *ctx, struct networking_config *cfg, enum net_mode mode);
void networking_poll(void);
void networking_done(void);
int networking_broadcast_restart(void);
int networking_broadcast(enum net_mode mode, void *data, size_t size);
#else
static inline cerr_check networking_init(struct clap_context *ctx, struct networking_config *cfg, enum net_mode mode) { return CERR_NOT_SUPPORTED; }
static inline void networking_poll(void) {}
static inline void networking_done(void) {}
static inline int networking_broadcast_restart(void) { return 0; }
static inline int networking_broadcast(enum net_mode mode, void *data, size_t size) { return 0; }
#endif /* CONFIG_NETWORKING */

#endif /* __CLAP_NETWORKING_H__ */
