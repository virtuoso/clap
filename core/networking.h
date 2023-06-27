/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NETWORKING_H__
#define __CLAP_NETWORKING_H__

#include "clap.h"

#if defined(__APPLE__) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

enum mode {
    CLIENT = 0,
    SERVER,
    LISTEN,
};

struct networking_config {
    struct clap_context *clap;
    const char  *server_ip;
    unsigned int server_port;
    unsigned int server_wsport;
    unsigned long   logger  : 1;
    int             timeout;
};

int networking_init(struct networking_config *cfg, enum mode mode);
void networking_poll(void);
void networking_done(void);
void networking_broadcast_restart(void);
void networking_broadcast(int mode, void *data, size_t size);

#endif /* __CLAP_NETWORKING_H__ */
