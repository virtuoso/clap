/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NETWORKING_H__
#define __CLAP_NETWORKING_H__

#include <errno.h>
#include "clap.h"

#if defined(__APPLE__) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

/**
 * enum net_mode - what this end of a connection is
 * @NET_CLIENT:     a game client streaming its logs to the server
 * @NET_SERVER:     the server's end of one accepted client connection
 * @NET_LISTEN:     a listening socket producing NET_SERVER children
 */
enum net_mode {
    NET_CLIENT = 0,
    NET_SERVER,
    NET_LISTEN,
};

/**
 * struct networking_config - networking_init() parameters
 * @clap:           clap context
 * @server_ip:      address to connect to (client) or bind to (server)
 * @server_port:    raw TCP port used by native clients
 * @server_wsport:  websocket port used by browser clients
 * @logger:         stream the log ring buffer to the server
 * @timeout:        poll timeout in milliseconds
 */
struct networking_config {
    struct clap_context *clap;
    const char  *server_ip;
    unsigned int server_port;
    unsigned int server_wsport;
    unsigned long   logger  : 1;
    int             timeout;
};

#ifdef CONFIG_NETWORKING
/**
 * networking_init() - set up the networking client or server
 * @ctx:        clap context
 * @cfg:        configuration; duplicated, the caller keeps ownership
 * @mode:       NET_CLIENT for the engine, NET_SERVER for tools/server
 *
 * Return: CERR_OK or the failure reason; nothing is left behind on failure.
 */
cerr_check networking_init(struct clap_context *ctx, struct networking_config *cfg, enum net_mode mode);

/**
 * networking_poll() - run one round of connection upkeep
 *
 * Accepts, reads, writes and reaps as the sockets allow, blocking for at
 * most the configured timeout; a client redials the server when its
 * connection is gone.
 */
void networking_poll(void);

/**
 * networking_done() - flush and tear down all connections
 */
void networking_done(void);

/**
 * networking_broadcast_restart() - tell every connected peer to restart
 *
 * Return: the number of nodes the restart was queued to.
 */
int networking_broadcast_restart(void);

/**
 * networking_broadcast() - queue a message to every running node of a mode
 * @mode:       which nodes to address (their own mode, not the peer's)
 * @data:       message bytes, duplicated for each recipient
 * @size:       message length
 *
 * Return: the number of nodes the message was queued to.
 */
int networking_broadcast(enum net_mode mode, void *data, size_t size);
#else
static inline cerr_check networking_init(struct clap_context *ctx, struct networking_config *cfg, enum net_mode mode) { return CERR_NOT_SUPPORTED; }
static inline void networking_poll(void) {}
static inline void networking_done(void) {}
static inline int networking_broadcast_restart(void) { return 0; }
static inline int networking_broadcast(enum net_mode mode, void *data, size_t size) { return 0; }
#endif /* CONFIG_NETWORKING */

#endif /* __CLAP_NETWORKING_H__ */
