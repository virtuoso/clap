// SPDX-License-Identifier: Apache-2.0
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <limits.h>
#include <inttypes.h>
#include <poll.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include "common.h" /* XXX: for EM_ASM(): factor out */
#include "clap.h"
#include "util.h"
#include "logger.h"
#include "object.h"
#include "networking.h"
#include "messagebus.h"
#include "sha1.c" /* XXX */
#include "base64.h"
#include "websocket.h"

static DECLARE_LIST(nodes);

enum {
    ST_INIT = 0,
    ST_HANDSHAKE,
    ST_SYNC,
    ST_RUNNING,
    ST_ERROR,
};

/*
 * Data queued for sending on the node, could use a better name
 */
struct queued {
    void        *out;
    size_t      outsz;
    /* how much of @out has been sent so far */
    size_t      off;
    struct list entry;
};

/*
 * Client or server or listener.
 */
struct network_node {
    struct ref             ref;
    struct list            entry;
    struct network_node    *parent;
    struct message_source  *src;
    struct sockaddr_in     sa;
    /*
     * Timestamps are mostly for the future if someone wants to
     * implement multiplayer or somesuch.
     */
    struct timespec        local_time;
    struct timespec        remote_time;
    struct timespec        remote_delta;

    /*
     * Buffered input data, in case of partial packets
     */
    void                   *input;
    size_t                 inputsz;
    void                   *wsinput;
    size_t                 wsinputsz;

    socklen_t              addrlen;
    /*
     * requires websocket encoding/decoding
     */
    bool                   websocket;
    /* close frame received; drop once the close echo flushes */
    bool                   closing;
    /* chain of struct queued */
    struct list            out_queue;
    /* mode is who we are, not who we're talking to */
    enum mode              mode;
    int                    fd;
    int                    state;
    /* file for dumping remote's log messages */
    FILE                   *log_f;
    /* poll(2) */
    short                  events;
    /* carry out websocket handshake */
    int (*handshake)(struct network_node *n, const uint8_t *buf);
};


static struct networking_config  *_ncfg;
static struct clap_context       *clap_ctx;
static struct pollfd *pollfds;
static unsigned int  nr_nodes;
static unsigned int  need_polling_alloc;
static enum mode     _mode;
/* a restart command arrived; acted upon once the poll loop unwinds */
static bool          restart_requested;
/* networking_done() in progress: no reconnecting */
static bool          shutting_down;

static void queue_outmsg(struct network_node *n, void *data, size_t size);

static void log_flush(struct log_entry *e, void *data)
{
    struct network_node *n = data;
    struct message_command *mcmd;
    struct message_log *mlog;
    const char *modfile;
    struct timespec ts;
    size_t size;
    void *buf;

    modfile = strrchr(e->mod, '/');
    if (!modfile)
        modfile = e->mod;
    else
        modfile++;

    size = sizeof(*mcmd) + sizeof(*mlog) + strlen(e->msg) + 1;
    size += strlen(modfile) + strlen(e->func) + 7 + 5;
    buf = mem_alloc(size, .nr = 1, .fatal_fail = 1);
    mcmd = buf;
    mlog = buf + sizeof(*mcmd);
    mcmd->log_follows = 1;
    if (n->state < ST_RUNNING) {
        mcmd->connect = 1; /* XXX */
        n->state = ST_RUNNING;
    } else {
        mcmd->connect = 1;
        dbg_once("### n->state: %d\n", n->state);
    }
    mlog->ts.tv_sec = e->ts.tv_sec;
    mlog->ts.tv_nsec = e->ts.tv_nsec;
    mlog->length = snprintf(mlog->msg, size - sizeof(*mcmd) - sizeof(*mlog),
                            "[%s:%d @%s] %s", modfile, e->line, e->func, e->msg) + 1;
    size = mlog->length + sizeof(*mcmd) + sizeof(*mlog);
    clock_gettime(CLOCK_REALTIME, &ts);
    timespec_to_64(&ts, &mcmd->time);
    queue_outmsg(n, buf, size);
    //fprintf(stderr, "===> %s <===\n", e->msg);
}

static void polling_alloc()
{
    struct network_node *n;

    nr_nodes = 0;
    list_for_each_entry(n, &nodes, entry) {
        nr_nodes++;
    }

    dbg("pollfds: %d\n", nr_nodes);
    mem_free(pollfds);
    pollfds = NULL;

    if (nr_nodes)
        pollfds = mem_alloc(sizeof(struct pollfd), .nr = nr_nodes, .fatal_fail = 1);
    need_polling_alloc = 0;
}

static void polling_update(void)
{
    struct network_node *n;
    unsigned int        i = 0;

    if (need_polling_alloc)
        polling_alloc();

    list_for_each_entry(n, &nodes, entry) {
        if (i == nr_nodes) {
            err("i > nr_nodes: %d %d\n", i, nr_nodes);
            return;
        }
        pollfds[i].events = n->events;
        pollfds[i].fd     = n->fd;
        i++;
    }
}

DEFINE_REFCLASS_INIT_OPTIONS(network_node,
    struct network_node *parent;
    const char          *ip;
    unsigned int        port;
    int                 mode;
);

static cerr network_node_make(struct ref *ref, void *_opts)
{
    rc_init_opts(network_node) *opts = _opts;
    int mode = opts->parent ? opts->parent->mode : opts->mode;

    struct network_node *n = container_of(ref, struct network_node, ref);

    n->mode = mode;
    n->events = POLLIN | POLLHUP | POLLNVAL | POLLOUT;
    n->state  = ST_INIT;
    n->fd     = -1;
    list_init(&n->out_queue);

    if (opts->parent) {
        n->parent            = opts->parent;
        n->handshake         = opts->parent->handshake;
        n->addrlen           = opts->parent->addrlen;
        memcpy(&n->sa, &opts->parent->sa, n->addrlen);
    } else if (opts->ip) {
        n->addrlen = sizeof(n->sa);
        memset(&n->sa, 0, sizeof(struct sockaddr_in));
        n->sa.sin_family = AF_INET;
        n->sa.sin_port   = htons(opts->port);
        if (inet_pton(AF_INET, opts->ip, &n->sa.sin_addr) != 1) {
            err("bad server address '%s'\n", opts->ip);
            return CERR_INVALID_ARGUMENTS;
        }

        n->fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (n->fd < 0)
            return errno_to_cerr(errno);

        if (mode == LISTEN) {
            int val = 1;
            setsockopt(n->fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
        }
        fcntl(n->fd, F_SETFL, O_NONBLOCK);
    }

    /* a failed make is not followed by drop: nothing above may fail past here */
    list_append(&nodes, &n->entry);

    return CERR_OK;
}

static void network_node_drop(struct ref *ref)
{
    struct network_node *n = container_of(ref, struct network_node, ref);
    struct queued *qd, *_qd;

    if (n->log_f) {
        fprintf(n->log_f, " --- connection closed ---\n");
        fclose(n->log_f);
    }

    if (n->mode == CLIENT)
        rb_sink_del(n);

    list_for_each_entry_iter(qd, _qd, &n->out_queue, entry) {
        list_del(&qd->entry);
        mem_free(qd->out);
        mem_free(qd);
    }

    mem_free(n->input);
    mem_free(n->wsinput);
    if (n->src) {
        mem_free(n->src->name);
        mem_free(n->src);
    }
    list_del(&n->entry);
    if (n->fd >= 0) {
        shutdown(n->fd, SHUT_RDWR);
        close(n->fd);
    }
    need_polling_alloc++;
}

DEFINE_REFCLASS2(network_node);
DECLARE_REFCLASS(network_node);

static cerr network_node_listen(struct network_node *n)
{
    if (bind(n->fd, (struct sockaddr *)&n->sa, n->addrlen))
        return CERR_SOCK_BIND_FAILED;

    if (listen(n->fd, 1))
        return CERR_SOCK_LISTEN_FAILED;

    need_polling_alloc++;

    return CERR_OK;
}

static int network_node_connect(struct network_node *n)
{
    int ret;
   
    //dbg("connecting: state: %d\n", n->state);
    ret = connect(n->fd, (struct sockaddr *)&n->sa, n->addrlen);
    //dbg("connect: %d %m state: %d\n", ret, n->state);
    if (!ret)
        n->state = ST_HANDSHAKE;

    return ret;
}

static cresp(network_node) network_node_accept(struct network_node *n)
{
    cresp(network_node) res = ref_new_checked(network_node, .parent = n);
    if (IS_CERR(res))
        return res;

    struct network_node *child = res.val;
    child->mode         = SERVER;
    child->fd = accept(n->fd, (struct sockaddr *)&n->sa, &n->addrlen);
    if (child->fd < 0) {
        ref_put(child);
        return cresp_error(network_node, CERR_SOCK_ACCEPT_FAILED);
    }

    fcntl(child->fd, F_SETFL, O_NONBLOCK);

    child->src = mem_alloc(sizeof(*child->src), .zero = 1);
    if (!child->src) {
        ref_put(child);
        return cresp_error(network_node, CERR_NOMEM);
    }

    child->src->type = MST_CLIENT;
    cres(int) name_res = mem_asprintf(&child->src->name, "%s", inet_ntoa(n->sa.sin_addr));
    if (IS_CERR(name_res)) {
        ref_put(child);
        return cresp_error_cerr(network_node, name_res);
    }

    dbg("new client '%s'\n", child->src->name);
    child->src->desc = "remote client";
    child->state     = ST_HANDSHAKE;
    need_polling_alloc++;
    return res;
}

static const char *wsguid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static int websocket_parse(struct network_node *n, const uint8_t *_buf)
{
    const char *buf = (const char *)_buf, *end = buf + strlen(buf);
    char       h[21], bh[64];
    char       *out;
    size_t     klen, vlen;

    const char *k = http_header_find(buf, end, "Sec-WebSocket-Key", &klen);
    if (!k || !klen)                                    return -1;

    const char *ver = http_header_find(buf, end, "Sec-WebSocket-Version", &vlen);
    if (!ver || vlen != 2 || strncmp(ver, "13", 2))     return -1;

    /* the client is entitled to have one of its offered subprotocols echoed */
    size_t plen = 0;
    const char *proto = http_header_find(buf, end, "Sec-WebSocket-Protocol", &plen);
    if (proto) {
        size_t i;

        for (i = 0; i < plen && proto[i] != ',' && proto[i] != ' ' && proto[i] != '\t'; i++)
            ;
        plen = i;
        if (!plen)
            proto = NULL;
    }

    LOCAL_SET(char, key) = mem_alloc(klen + strlen(wsguid) + 1, .zero = 1, .fatal_fail = 1);
    memcpy(key, k, klen);
    strcpy(key + klen, wsguid);
    SHA1(h, key, strlen(key));

    base64_encode(bh, sizeof(bh), h, sizeof(h) - 1);
    cres(int) res;
    if (proto)
        res = mem_asprintf((char **)&out,
                           "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                           "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n"
                           "Sec-WebSocket-Protocol: %.*s\r\n\r\n",
                           bh, (int)plen, proto);
    else
        res = mem_asprintf((char **)&out,
                           "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                           "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
                           bh);
    if (IS_CERR(res) || !res.val)
        return -1;

    queue_outmsg(n, out, res.val);

    return 0;
}

static cresp(network_node) server_setup(const char *server_ip, unsigned int port)
{
    cresp(network_node) res = ref_new_checked(network_node, .ip = server_ip, .port = port, .mode = LISTEN);
    if (IS_CERR(res))
        return res;

    cerr err = network_node_listen(res.val);
    if (IS_CERR(err))
        return cresp_error_cerr(network_node, err);
    need_polling_alloc++;

    return res;
}

static cresp(network_node) _client_setup(struct networking_config *cfg, unsigned int port)
{
    struct network_node *n;
    cerr err = CERR_OK;

    cresp(network_node) res = ref_new_checked(network_node, .ip = cfg->server_ip, .port = port, .mode = CLIENT);
    if (IS_CERR(res))
        return res;

    n = res.val;
    network_node_connect(n);
    if (cfg->logger)
        err = rb_sink_add(log_flush, n, VDBG, 1);

    if (IS_CERR(err)) {
        ref_put(n);
        return cresp_error_cerr(network_node, err);
    }

    need_polling_alloc++;

    return res;
}

static cresp(network_node) client_setup(struct networking_config *cfg)
{
#ifdef __EMSCRIPTEN__
    return _client_setup(cfg, cfg->server_wsport);
#else
    return _client_setup(cfg, cfg->server_port);
#endif
}

static const char *node_name(struct network_node *n)
{
    if (n->src)
        return n->src->name;

    switch (n->mode) {
    case CLIENT:
        return "<client>";
    case SERVER:
        return "<server>";
    default:
        break;
    }

    return "<unknown>";
}

void networking_broadcast(int mode, void *data, size_t size)
{
    struct network_node *n;

    dbg("sending broadcast\n");
    list_for_each_entry(n, &nodes, entry) {
        void *out;

        if (n->mode == mode && n->state == ST_RUNNING) {
            dbg("sending to node '%s'\n", node_name(n));
            out = memdup(data, size);
            queue_outmsg(n, out, size);
            n->events |= POLLOUT;
        }
    }
}

void networking_broadcast_restart(void)
{
    struct message_command mcmd;

    memset(&mcmd, 0, sizeof(mcmd));
    mcmd.restart = 1;
    networking_broadcast(SERVER, &mcmd, sizeof(mcmd));
    networking_broadcast(CLIENT, &mcmd, sizeof(mcmd));
}

static ssize_t handle_client_input(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command *mcmd;

    if (size < sizeof(*mcmd)) {
        //n->state = ST_ERROR;
        dbg("short read: %zu <> %lu\n", size, sizeof(*mcmd));
        return -1;
    }

    mcmd = (void *)buf;
    if (mcmd->restart)
        restart_requested = true;

    return sizeof(*mcmd);
}

static void log_f_open(struct network_node *n)
{
    char log_name[PATH_MAX];
    char buf[128];
    struct tm t;

    localtime_r(&n->remote_time.tv_sec, &t);

    strftime(buf, sizeof(buf), "%F_%T", &t);
    snprintf(log_name, PATH_MAX, "/tmp/clap-%s-%s.%09" "ld",
         n->src->name, buf, n->remote_time.tv_nsec);
    dbg("using log file '%s'\n", log_name);
    n->log_f = fopen(log_name, "w");
}

/**
 * MAX_WIRE_LOG_LENGTH - cap on the text length of one wire log message
 *
 * A wire log message is one formatted logger line; a length beyond any
 * plausible line means a hostile or corrupt stream, not something to
 * keep buffering input for.
 */
#define MAX_WIRE_LOG_LENGTH 65536

/**
 * log_message_write() - validate one wire log message and write it out
 * @n:      node the message arrived on; its log file receives the text
 * @buf:    start of the message_log
 * @size:   bytes available at @buf
 *
 * Return: bytes consumed; -1 to wait for more data; 0 with @n put in
 * ST_ERROR on a hostile length.
 */
static ssize_t log_message_write(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_log *ml = (void *)buf;

    if (size < sizeof(*ml))
        return -1;

    if (ml->length > MAX_WIRE_LOG_LENGTH) {
        err("bogus log message length %u from '%s'\n", ml->length, node_name(n));
        n->state = ST_ERROR;
        return 0;
    }

    if (sizeof(*ml) + ml->length > size)
        return -1;

    if (n->log_f && ml->length)
        fprintf(n->log_f, "[%" PRItvsec ".%09" PRItvsec "] %.*s",
                ml->ts.tv_sec, ml->ts.tv_nsec, (int)ml->length, ml->msg);

    return sizeof(*ml) + ml->length;
}

static ssize_t handle_server_handshake(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command mcmd;

    if (size < sizeof(mcmd)) {
        /* not an error, cache and wait for more */
        // n->state = ST_ERROR;
        dbg("short handshake: %zu <> %lu\n", size, sizeof(mcmd));
        return -1;
    }

    memcpy(&mcmd, buf, sizeof(mcmd));
    if (!mcmd.connect) {
        /* is this an actual error? protocol is not really defined */
        n->state = ST_ERROR;
        dbg("connect not set: %d\n", mcmd.connect);
        return sizeof(mcmd);
    }

    timespec_from_64(&n->remote_time, &mcmd.time);
    clock_gettime(CLOCK_REALTIME, &n->local_time);
    timespec_diff(&n->local_time, &n->remote_time, &n->remote_delta);
    n->state = ST_RUNNING;
    log_f_open(n);
    dbg("local time: %" PRItime ".%09" "ld" " client time: %" PRItime ".%09" "ld" " delta: %" PRItime ".%09" "ld" "\n",
        n->local_time.tv_sec, n->local_time.tv_nsec,
        n->remote_time.tv_sec, n->remote_time.tv_nsec,
        n->remote_delta.tv_sec, n->remote_delta.tv_nsec);
    if (n->log_f)
        setbuf(n->log_f, NULL);
    if (mcmd.restart) {
        fprintf(stderr, "broadcasting restart right away\n");
        networking_broadcast_restart();
    }
    if (mcmd.log_follows) {
        ssize_t consumed = log_message_write(n, buf + sizeof(mcmd), size - sizeof(mcmd));

        if (consumed <= 0)  return consumed;

        return sizeof(mcmd) + consumed;
    }

    return sizeof(mcmd);
}

/*
 * Handle one command message; return number of bytes consumed.
 */
static ssize_t handle_server_command(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command mcmd;
    struct message         m;
    size_t ret = sizeof(mcmd);

    if (size < sizeof(mcmd)) {
        /* short read is not an error, cache and wait for more */
        //n->state = ST_ERROR;
        return -1;
    }

    memcpy(&mcmd, buf, sizeof(mcmd));
    if (mcmd.restart) {
        fprintf(stderr, "broadcasting restart\n");
        networking_broadcast_restart();
        ret = sizeof(mcmd);
    }
    if (mcmd.log_follows) {
        ssize_t consumed = log_message_write(n, buf + sizeof(mcmd), size - sizeof(mcmd));

        if (consumed <= 0)  return consumed;

        ret = sizeof(mcmd) + consumed;
    }

    /*
     * This is for the future; the condition needs to be
     * "if (acceptable_external_command(&mcmd)) ..."
     */
    if (!mcmd.log_follows) {
        m.type   = MT_COMMAND;
        m.source = n->src;
        memcpy(&m.cmd, &mcmd, sizeof(m.cmd));
        message_send(clap_ctx, &m);
    }

    return ret;
}

static ssize_t handle_server_input(struct network_node *n, uint8_t *buf, size_t size)
{
    switch (n->state) {
    case ST_HANDSHAKE:
        return handle_server_handshake(n, buf, size);
    case ST_SYNC:
        break;
    case ST_RUNNING:
        return handle_server_command(n, buf, size);
    }
    return 0;
}

static ssize_t handle_input(struct network_node *n, uint8_t *buf, size_t size)
{
    dbg("got input on '%s'(sz=%zu): %d/%d\n", node_name(n), size, n->mode, n->state);
    if (n->mode == CLIENT) {
        return handle_client_input(n, buf, size);
    }

    return handle_server_input(n, buf, size);
}

static void queue_frame(struct network_node *n, void *data, size_t size)
{
    struct queued *qd = mem_alloc(sizeof(*qd), .zero = 1, .fatal_fail = 1);

    qd->out    = data;
    qd->outsz  = size;
    list_append(&n->out_queue, &qd->entry);

    n->events |= POLLOUT;
}

static void queue_outmsg(struct network_node *n, void *data, size_t size)
{
    if (!size)
        size = strlen(data) + 1;

    if (n->websocket) {
        uint8_t *frame;
        size_t  framesz;

        CHECK0(ws_encode(data, size, &frame, &framesz, WSOP_BIN));

        mem_free(data); /* XXX: unless it's static; don't queue static stuff */
        data = frame;
        size = framesz;
    }

    queue_frame(n, data, size);
}

static void network_node_cache(struct network_node *n, size_t off)
{
    memmove(n->input, n->input + off, n->inputsz - off);
    n->inputsz -= off;
}

static size_t network_node_uncache(struct network_node *n, uint8_t *buf, size_t size)
{
    CHECK(n->input = realloc(n->input, n->inputsz + size));
    memcpy(n->input + n->inputsz, buf, size);
    n->inputsz += size;
    return n->inputsz;
}

static void network_node_wscache(struct network_node *n, size_t off)
{
    memmove(n->wsinput, n->wsinput + off, n->wsinputsz - off);
    n->wsinputsz -= off;
}

static size_t network_node_wsuncache(struct network_node *n, uint8_t *buf, size_t size)
{
    CHECK(n->wsinput = realloc(n->wsinput, n->wsinputsz + size));
    memcpy(n->wsinput + n->wsinputsz, buf, size);
    n->wsinputsz += size;
    return n->wsinputsz;
}

static int process_input(struct network_node *n, uint8_t *buf, size_t size)
{
    size_t total, handled = 0;
    size_t xsz, done = 0;
    int    op = -1;
    uint8_t *x;

    /*
     * This is kind of a pipeline:
     * stage 0: socket -> ws data
     *          - short read -> more data from socket
     * stage 1: ws data -> messages
     *          - short read -> more ws data
     * If socket is not ws, only one stage.
     */

    if (n->websocket)
        total = network_node_wsuncache(n, buf, size);
    else
        total = size;

    /* decode all complete frames */
    while (total > 0) {
        x = n->input;
        xsz = total;
        if (n->websocket) {
            cres(size_t) dres = ws_decode(n->wsinput + done, total, &x, &xsz, &op,
                                          n->mode == SERVER);
            /* incomplete packet: cache what we have for the next round */
            if (IS_CERR_CODE(dres, CERR_BUFFER_INCOMPLETE)) {
                network_node_wscache(n, done);
                goto out_short;
            }

            /* protocol violation: fail the connection */
            if (IS_CERR(dres)) {
                n->state = ST_ERROR;
                return 0;
            }

            size = dres.val;

            if (op == WSOP_CLOSE) {
                uint8_t *frame;
                size_t  framesz;

                dbg("close from '%s'\n", node_name(n));
                /* echo the close, status payload included, drop once it flushes */
                if (!ws_encode(x, xsz, &frame, &framesz, WSOP_CLOSE))
                    queue_frame(n, frame, framesz);
                mem_free(x);
                n->closing = true;

                /* nothing after a close frame is worth reading */
                mem_free(n->wsinput);
                n->wsinput = NULL;
                n->wsinputsz = 0;
                return 0;
            }

            if (op == WSOP_PING || op == WSOP_PONG) {
                if (op == WSOP_PING) {
                    uint8_t *frame;
                    size_t  framesz;

                    if (!ws_encode(x, xsz, &frame, &framesz, WSOP_PONG))
                        queue_frame(n, frame, framesz);
                }
                /* control frames stay out of the message stream */
                mem_free(x);
                total -= size;
                done += size;
                continue;
            }

            /* paste what we decoded with what was cached from before */
            network_node_uncache(n, x, xsz);
        } else {
            network_node_uncache(n, buf, size);
        }

        handled = 0;

        bool cached = false;

        /*
         * making it handle multiple messages, if in fact, one WS frame
         * contains multiple messages
         */
        do {
            ssize_t xhandled;

            xhandled = handle_input(n, n->input + handled, n->inputsz - handled);

            /*
             * XXX: what does a data short look like?
             * if it comes wrapped in a ws frame;
             * messages shouldn't span multiple ws frames
             */
            dbg("<== handled / xhandled: %zu / %zd\n", handled, xhandled);
            if (xhandled < 0) {
                /* partial message: keep the tail for the next read */
                network_node_cache(n, handled);
                cached = true;
                break;
            }
            if (n->state == ST_ERROR) {
                // ref_put(n);
                return 0;
            }
            handled += xhandled;
        } while (handled > 0 && handled < n->inputsz);

        if (!cached && handled == n->inputsz) {
            mem_free(n->input);
            n->input = NULL;
            n->inputsz = 0;
        }
        total -= size;
        done += size;

        if (n->websocket)
            mem_free(x);
    }

    if (n->websocket) {
        mem_free(n->wsinput);
        n->wsinput = NULL;
        n->wsinputsz = 0;
    }

    return 0;

out_short:
    return 1;
}

void networking_poll(void)
{
    struct network_node *n, *it;
    int                 i = 0, events;
    uint8_t             buf[4096];
    ssize_t             ret;

    if (!_ncfg)
        return;

    memset(buf, 0, sizeof(buf));
    polling_update();
    if (nr_nodes == 0 && _mode == CLIENT && !shutting_down) {
        cresp(network_node) res = client_setup(_ncfg);
        if (IS_CERR(res)) {
            err_cerr(res, "Couldn't start client\n");
            return;
        }

        n = res.val;
    }

    ret = poll(pollfds, nr_nodes, _ncfg->timeout);
    if (ret <= 0)
        goto state;

    //dbg("polled: %d\n", ret);
    list_for_each_entry_iter(n, it, &nodes, entry) {
        /* nodes appended mid-walk have no pollfd slot yet */
        if (i == nr_nodes)
            break;

        int slot = i++;

        events = pollfds[slot].revents;
        // dbg("pollfd[%d]: %x\n", slot, events);
        pollfds[slot].revents = 0;
        /* Sockets in error state are done for */
        if (events & (POLLERR | POLLNVAL)) {
            int soerr = 0;
            socklen_t solen = sizeof(soerr);

            getsockopt(n->fd, SOL_SOCKET, SO_ERROR, &soerr, &solen);
            dbg("pollfd[%d] error on '%s': %s\n", slot, node_name(n), strerror(soerr));
            ref_put(n);
            continue;
        }
        /* First, new incoming connections */
        if (n->mode == LISTEN && (events & POLLIN)) {
            cresp(network_node) res = network_node_accept(n);
            if (IS_CERR(res)) {
                err_cerr(res, "Couldn't accept connection\n");
                continue;
            }

            dbg("accepted client connection\n");
            continue;
        }
        /* Second, new data on existing connections */
        if (events & POLLIN) {
            memset(buf, 0, sizeof(buf));
            ret = recvfrom(n->fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&n->sa, &n->addrlen);
            if (!ret) {
                dbg("pollfd[%d]: shutting down\n", slot);
                ref_put(n);
                continue;
            }
            if (ret > 0) {
                buf[ret] = 0;
                dbg("new data on %d: %zd bytes (+ %zu/%zu bytes left over):\n", slot, ret, n->inputsz, n->wsinputsz);

                if (n->handshake) {
                    if (n->handshake(n, buf) < 0) {
                        ref_put(n);
                        continue;
                    }

                    n->events |= POLLOUT;
                    continue;
                }

                if (process_input(n, buf, ret))
                    continue;

                if (n->state == ST_ERROR) {
                    ref_put(n);
                    continue;
                }
            } else if (ret < 0) {
                err("recvfrom[%d] returned %zd: %m // %x\n", slot, ret, events);
            }
        }
        /* Third, hangups */
        if (events & POLLHUP) {
            ref_put(n);
            continue;
        }
        /* Fourth, send out queued data */
        if (events & POLLOUT) {
            struct queued *qd, *_qd;

            if (n->state == ST_INIT) {
                int soerr = 0;
                socklen_t solen = sizeof(soerr);

                /* the first POLLOUT on a connecting socket is the connect() verdict */
                if (!getsockopt(n->fd, SOL_SOCKET, SO_ERROR, &soerr, &solen) && soerr) {
                    dbg("connect to '%s' failed: %s\n", node_name(n), strerror(soerr));
                    ref_put(n);
                    continue;
                }

                n->state = ST_HANDSHAKE;
            }
            list_for_each_entry_iter(qd, _qd, &n->out_queue, entry) {
                //dbg("sending[%d]: <-- %zd\n", slot, qd->outsz);
                ret = sendto(n->fd, qd->out + qd->off, qd->outsz - qd->off, MSG_NOSIGNAL,
                             (struct sockaddr *)&n->sa, n->addrlen);
                if (ret < 0) {
                    /* wait for the next POLLOUT on a full socket buffer */
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;

                    err("send to '%s' failed: %m\n", node_name(n));
                    n->state = ST_ERROR;
                    break;
                }

                if ((size_t)ret < qd->outsz - qd->off) {
                    /* short write: keep the tail queued */
                    qd->off += ret;
                    break;
                }

                mem_free(qd->out);

                list_del(&qd->entry);
                mem_free(qd);

                //n->outsz = mem_asprintf(&n->out, "PING");
                if (n->handshake) {
                    n->websocket = true;
                    n->handshake = NULL;
                }
            }

            /* keep POLLOUT armed until the queue drains */
            if (list_empty(&n->out_queue))
                n->events &= ~POLLOUT;

            /* a dead peer, or the close echo has flushed: drop the node */
            if (n->state == ST_ERROR || (n->closing && list_empty(&n->out_queue))) {
                ref_put(n);
                continue;
            }
        }
    }

state:
    list_for_each_entry(n, &nodes, entry) {
        //dbg_on(n->mode == CLIENT, "n->state: %d\n", n->state);
        if (n->mode == CLIENT && n->state == ST_HANDSHAKE) {
            struct message_command *mcmd;
            struct timespec ts;

            dbg("server '%s:%d' handshake\n", inet_ntoa(n->sa.sin_addr), ntohs(n->sa.sin_port));
            mcmd = mem_alloc(sizeof(*mcmd), .zero = 1, .fatal_fail = 1);
            mcmd->connect = 1;
            clock_gettime(CLOCK_REALTIME, &ts);
            timespec_to_64(&ts, &mcmd->time);
            queue_outmsg(n, mcmd, sizeof(*mcmd));
            n->state = ST_RUNNING;
        }
    }

    if (restart_requested) {
        restart_requested = false;
#ifdef __EMSCRIPTEN__
        networking_done();
        EM_ASM({ location.reload(); });
#else
        clap_restart(_ncfg->clap);
#endif /* __EMSCRIPTEN__ */
    }
}

#ifdef __EMSCRIPTEN__
static void socket_callback(int fd, void *data)
{
    dbg("socket callback on fd %d\n", fd);
    networking_poll();
}
#endif /* __EMSCRIPTEN__ */

cerr networking_init(struct clap_context *ctx, struct networking_config *cfg, enum mode mode)
{
    struct network_node *n;
    cresp(network_node) res;

    clap_ctx = ctx;
    _ncfg = memdup(cfg, sizeof(*cfg));
    _mode = mode;
    switch (mode) {
    case CLIENT:
        res = client_setup(_ncfg);
        if (IS_CERR(res))
            return cerr_error_cres(res);
        break;
    case SERVER:
        res = server_setup(_ncfg->server_ip, _ncfg->server_port);
        if (IS_CERR(res))
            return cerr_error_cres(res);

        res = server_setup(_ncfg->server_ip, _ncfg->server_wsport);
        if (IS_CERR(res))
            return cerr_error_cres(res);

        n            = res.val;
        n->handshake = websocket_parse;
        need_polling_alloc++;
        break;
    default:
        break;
    }
#ifdef __EMSCRIPTEN__
    emscripten_set_socket_open_callback(NULL, socket_callback);
#endif /* __EMSCRIPTEN__ */
    dbg("networking initialized\n");

    return CERR_OK;
}

void networking_done(void)
{
    struct network_node *n, *it;

    shutting_down = true;

    /* flush the stragglers, but a dead peer doesn't get to hold the exit hostage */
    for (int rounds = 0; rounds < 10; rounds++) {
        bool pending = false;

        networking_poll();
        list_for_each_entry(n, &nodes, entry)
            if (!list_empty(&n->out_queue))
                pending = true;

        if (!pending)
            break;
    }

    list_for_each_entry_iter(n, it, &nodes, entry)
        ref_put(n);

    mem_free(pollfds);
    pollfds  = NULL;
    nr_nodes = 0;
    mem_free(_ncfg);
    _ncfg = NULL;
    shutting_down = false;
}
