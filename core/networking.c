// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
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
#include "common.h" /* XXX: for EM_ASM(): factor out */
#include "clap.h"
#include "util.h"
#include "logger.h"
#include "object.h"
#include "networking.h"
#include "messagebus.h"
#include "sha1.c" /* XXX */
#include "base64.c"
#include "base64.h"

/*static const char server_ip[] = "192.168.1.114"; //"ukko.local";
static const unsigned int server_port = 21044;*/
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
    void                   *data;
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
static struct pollfd *pollfds;
static unsigned int  nr_nodes;
static unsigned int  need_polling_alloc;

static void queue_outmsg(struct network_node *n, void *data, size_t size);

#ifndef SERVER_STANDALONE
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
    CHECK(buf = calloc(1, size));
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
#endif

static void polling_alloc()
{
    struct network_node *n;

    nr_nodes = 0;
    list_for_each_entry(n, &nodes, entry) {
        nr_nodes++;
    }

    dbg("pollfds: %d\n", nr_nodes);
    free(pollfds);
    pollfds = NULL;

    if (nr_nodes)
        CHECK(pollfds = calloc(nr_nodes, sizeof(struct pollfd)));
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

static void network_node_drop(struct ref *ref)
{
    struct network_node *n = container_of(ref, struct network_node, ref);

    if (n->log_f) {
        fprintf(n->log_f, " --- connection closed ---\n");
        fclose(n->log_f);
    }

    if (n->mode == CLIENT)
        rb_sink_del(n);

    free(n->input);
    free(n->src);
    list_del(&n->entry);
    close(n->fd);
    shutdown(n->fd, SHUT_RDWR);
    need_polling_alloc++;
}

DECLARE_REFCLASS(network_node);

static struct network_node *network_node_new(int mode)
{
    struct network_node *n;

    CHECK(n = ref_new(network_node));
    n->mode = mode;
    n->events = POLLIN | POLLHUP | POLLNVAL | POLLOUT;
    n->state  = ST_INIT;
    list_append(&nodes, &n->entry);
    list_init(&n->out_queue);

    return n;
}

static struct network_node *network_node_new_parent(struct network_node *parent)
{
    struct network_node *n = network_node_new(parent->mode);
    n->parent            = parent;
    n->handshake         = parent->handshake;
    n->data              = parent->data;
    n->addrlen           = parent->addrlen;
    memcpy(&n->sa, &parent->sa, n->addrlen);
    return n;
}

static struct network_node *network_node_new_socket(const char *ip, unsigned int port, int mode)
{
    struct network_node *n = network_node_new(mode);

    n->addrlen = sizeof(n->sa);
    memset(&n->sa, 0, sizeof(struct sockaddr_in));
    n->fd            = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    n->sa.sin_family = AF_INET;
    n->sa.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &n->sa.sin_addr);
    if (mode == LISTEN) {
        int val = 1;
        setsockopt(n->fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    }
    fcntl(n->fd, F_SETFL, O_NONBLOCK);

    return n;
}

static int network_node_listen(struct network_node *n)
{
    CHECK0(bind(n->fd, (struct sockaddr *)&n->sa, n->addrlen));
    CHECK0(listen(n->fd, 1));
    need_polling_alloc++;

    return 0;
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

static struct network_node *network_node_accept(struct network_node *n)
{
    struct network_node *child = network_node_new_parent(n);
    child->mode         = SERVER;
    CHECK(child->fd = accept(n->fd, (struct sockaddr *)&n->sa, &n->addrlen));
    CHECK(child->src = calloc(1, sizeof(*child->src)));
    child->src->type = MST_CLIENT;
    asprintf(&child->src->name, "%s", inet_ntoa(n->sa.sin_addr));
    dbg("new client '%s'\n", child->src->name);
    child->src->desc = "remote client";
    child->state     = ST_HANDSHAKE;
    need_polling_alloc++;
    return child;
}

struct wsheader {
    char *key;
    int  version;
};

const char *wsguid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static int websocket_parse(struct network_node *n, const uint8_t *_buf)
{
    const char      *p, *c, *buf = (const char *)_buf;
    size_t          len = strlen(buf), wslen = 0, outsz;
    struct wsheader *wsh = n->data;
    char            h[21], bh[64];
    char            *key, *out;

    for (p = buf; p - buf < len;) {
        c = skip_to_new_line(p);
        if (!strncmp(p, "Sec-WebSocket-Key", 17)) {
            p        = skip_nonspace(p);
            p        = skip_space(p);
            wslen    = c - p - 2; /* XXX: \r\n */
            wsh->key = strndup(p, wslen);
        }
        p = c;
    }
    if (!wsh->key)
        return -1;

    CHECK(key = calloc(1, wslen + strlen(wsguid) + 1));
    strcpy(key, wsh->key);
    strcpy(key + wslen, wsguid);
    SHA1(h, key, strlen(key));

    base64_encode(bh, sizeof(bh), h, sizeof(h) - 1);
    outsz = asprintf((char **)&out,
                     "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                     "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
                     bh);
    /*
     * XXX: wrapping format strings with CHECK*() doesn't work, because
     * err_on_cond() will try to paste the whole stringified expression
     * with its own format string. Not a huge inconvenience, but would
     * be nice to eventually fix this.
     */
    CHECK(outsz);
    queue_outmsg(n, out, outsz);
    //free(out);
    free(key);

    return 0;
}

enum {
    WSOP_CONT = 0x0,
    WSOP_TEXT,
    WSOP_BIN,
    WSOP_FIN = 0x8,
};

struct ws_header {
    unsigned opcode : 4;
    unsigned rsv    : 3;
    unsigned fin    : 1;
    unsigned length : 7;
    unsigned mask   : 1;
};

static size_t ws_decode(uint8_t *input, size_t size, uint8_t **poutput, size_t *poutsz, int *popcode)
{
    struct ws_header *h = (void *)input;
    uint8_t          *mask = NULL;
    size_t           len, off = 2;
    uint8_t          *output;
    int              op;

    if (size < sizeof(*h))
        return -2;
    dbg("ws_header: fin=%d opcode=%d mask=%d length=%d (%zu left)\n",
        h->fin, h->opcode, h->mask, h->length, size);
    len = h->length;
    op  = h->opcode;

    if (len <= 125) {
        off = 2;
    } else if (len == 126) {
        len = input[off++] << 8;
        len |= input[off++];
    } else if (len == 127) {
        len  = input[off++] << 24;
        len |= input[off++] << 16;
        len |= input[off++] << 8;
        len |= input[off++];
    }
    if (h->mask) {
        mask = &input[off];
        off += 4;
    }

    if (len + off > size) {
        dbg("## packet larger than input buffer: %zu/%zu\n", len, size);
        return -2;
    }

    CHECK(output = calloc(1, len + 1));
    if (!h->mask) {
        memcpy(output, &input[off], len);
    } else {
        int i;

        for (i = 0; i < len; i++)
            output[i] = input[off + i] ^ mask[i % 4];
    }
    output[len] = 0;

    *poutput = output;
    *poutsz  = len;
    *popcode = op;

    return off + len;
}

static int ws_encode(uint8_t *input, size_t inputsz, uint8_t **poutput, size_t *poutsz)
{
    size_t            length = inputsz + 2, off = 2;
    uint8_t           *output = NULL;
    struct ws_header  *h;
    uint16_t          len16;
    uint64_t          len64;
    int               lensz;

    if (inputsz < 125) {
        lensz = 0;
        //length += 2;
        //dbg("header: %02x\n", output[0]);
    } else if (inputsz < UINT16_MAX) {
        lensz = 2;
        len16 = (uint16_t)inputsz;
    } else {
        lensz = 8;
        len64 = (uint64_t)inputsz;
    }
    length += lensz;
    off += lensz;

    CHECK(output = calloc(1, length));
    h         = (void *)output;
    h->fin    = 1;
    h->opcode = WSOP_BIN;
    switch (lensz) {
    case 0:
        h->length = (uint8_t)inputsz;
        break;
    case 2:
        h->length = 126;
        memcpy(&output[2], &len16, lensz);
        break;
    case 8:
        h->length = 127;
        memcpy(&output[2], &len64, lensz);
        break;
    }
    memcpy(&output[off], input, inputsz);

    *poutput = output;
    *poutsz  = length;
    return 0;
}

static struct network_node *server_setup(const char *server_ip, unsigned int port)
{
    struct network_node *n;

    CHECK(n = network_node_new_socket(server_ip, port, LISTEN));
    CHECK0(network_node_listen(n));
    need_polling_alloc++;

    return n;
}

static struct network_node *_client_setup(const char *server_ip, unsigned int port)
{
    struct network_node *n;

    CHECK(n = network_node_new_socket(server_ip, port, CLIENT));
    network_node_connect(n);
#ifndef SERVER_STANDALONE
    rb_sink_add(log_flush, n, VDBG, 1);
#endif
    need_polling_alloc++;

    return n;
}

static struct network_node *client_setup(struct networking_config *cfg)
{
#ifdef __EMSCRIPTEN__
    return _client_setup(cfg->server_ip, cfg->server_wsport);
#else
    return _client_setup(cfg->server_ip, cfg->server_port);
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

static void broadcast_command(struct message_command *mcmd)
{
    struct network_node *n;

    dbg("sending restart\n");
    list_for_each_entry (n, &nodes, entry) {
        void *out;

        if (n->mode != LISTEN && n->state == ST_RUNNING) {
            dbg("sending to client '%s'\n", node_name(n));
            out = memdup(mcmd, sizeof(*mcmd));
            queue_outmsg(n, out, sizeof(*mcmd));
            n->events |= POLLOUT;
        }
    }
}

void networking_broadcast_restart(void)
{
    struct message_command mcmd;

    memset(&mcmd, 0, sizeof(mcmd));
    mcmd.restart = 1;
    broadcast_command(&mcmd);
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
    if (mcmd->restart) {
#ifdef __EMSCRIPTEN__
        networking_done();
        EM_ASM({ location.reload(); });
#else
        clap_restart();
#endif
    }
    return sizeof(*mcmd);
}

static void log_f_open(struct network_node *n)
{
    char log_name[PATH_MAX];
    char buf[BUFSIZ];
    struct tm t;

    localtime_r(&n->remote_time.tv_sec, &t);

    strftime(buf, sizeof(buf), "%F_%T", &t);
    snprintf(log_name, PATH_MAX, "/tmp/clap-%s-%s.%09" "ld",
         n->src->name, buf, n->remote_time.tv_nsec);
    dbg("using log file '%s'\n", log_name);
    n->log_f = fopen(log_name, "w");
}

static ssize_t handle_server_handshake(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command *mcmd;

    if (size < sizeof(*mcmd)) {
        /* not an error, cache and wait for more */
        // n->state = ST_ERROR;
        dbg("short handshake: %zu <> %lu\n", size, sizeof(*mcmd));
        return -1;
    }

    mcmd = (void *)buf;
    if (!mcmd->connect) {
        /* is this an actual error? protocol is not really defined */
        n->state = ST_ERROR;
        dbg("connect not set: %d\n", mcmd->connect);
        return sizeof(*mcmd);
    }

    timespec_from_64(&n->remote_time, &mcmd->time);
    clock_gettime(CLOCK_REALTIME, &n->local_time);
    timespec_diff(&n->local_time, &n->remote_time, &n->remote_delta);
    n->state = ST_RUNNING;
    log_f_open(n);
    dbg("local time: %" "ld" ".%09" "ld" " client time: %" "ld" ".%09" "ld" " delta: %" "ld" ".%09" "ld" "\n",
        n->local_time.tv_sec, n->local_time.tv_nsec,
        n->remote_time.tv_sec, n->remote_time.tv_nsec,
        n->remote_delta.tv_sec, n->remote_delta.tv_nsec);
    if (n->log_f)
        setbuf(n->log_f, NULL);
    if (mcmd->restart) {
        fprintf(stderr, "broadcasting restart right away\n");
        networking_broadcast_restart();
    }
    if (mcmd->log_follows) {
        struct message_log *ml;
        size_t ret;

        ml = (void *)buf + sizeof(*mcmd);
        ret = sizeof(*mcmd) + sizeof(*ml) + ml->length;
        if (ret > size)
            return -1;

        if (n->log_f && ml->length) {
            fprintf(n->log_f, "[%" PRIu64 ".%09" PRIu64 "] %-*s", ml->ts.tv_sec, ml->ts.tv_nsec, ml->length, ml->msg);
        }
        return ret;
    }

    return sizeof(*mcmd);
}

/*
 * Handle one command message; return number of bytes consumed.
 */
static ssize_t handle_server_command(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command *mcmd;
    struct message         m;
    size_t ret = sizeof(*mcmd);

    if (size < sizeof(*mcmd)) {
        /* short read is not an error, cache and wait for more */
        //n->state = ST_ERROR;
        return -1;
    }

    mcmd = (void *)buf;
    if (mcmd->restart) {
        fprintf(stderr, "broadcasting restart\n");
        networking_broadcast_restart();
        ret = sizeof(*mcmd);
    }
    if (mcmd->log_follows) {
        struct message_log *ml;

        if (size < sizeof(*mcmd) + sizeof(*ml))
            return -1;

        ml = (void *)buf + sizeof(*mcmd);
        ret = sizeof(*mcmd) + sizeof(*ml) + ml->length;
        if (ret > size)
            return -1;

        if (n->log_f && ml->length) {
            fprintf(n->log_f, "[%" PRIu64 ".%09" PRIu64 "] %-*s", ml->ts.tv_sec, ml->ts.tv_nsec, ml->length, ml->msg);
        }
    }

    /*
     * This is for the future; the condition needs to be
     * "if (acceptable_external_command(&mcmd)) ..."
     */
    if (!mcmd->log_follows) {
        m.type   = MT_COMMAND;
        m.source = n->src;
        memcpy(&m.cmd, mcmd, sizeof(m.cmd));
        message_send(&m);
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

struct wsheader _wsh;

#ifdef SERVER_STANDALONE
#define TIMEOUT 100
#else
#define TIMEOUT 0
#endif

static void queue_outmsg(struct network_node *n, void *data, size_t size)
{
    struct queued *qd;

    if (!size)
        size = strlen(data) + 1;

    CHECK(qd = calloc(1, sizeof(*qd)));

    if (n->websocket) {
        uint8_t *frame;
        size_t  framesz;

        CHECK0(ws_encode(data, size, &frame, &framesz));

        free(data); /* XXX: unless it's static; don't queue static stuff */
        data = frame;
        size = framesz;
    }

    qd->out    = (void *)data;
    qd->outsz  = size;
    list_append(&n->out_queue, &qd->entry);

    n->events |= POLLOUT;
}

static void network_node_cache(struct network_node *n, size_t off)
{
    void *xx;
    CHECK(xx = memdup(n->input + off, n->inputsz - off));
    free(n->input);
    n->input = xx;
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
    void *xx;
    CHECK(xx = memdup(n->wsinput + off, n->wsinputsz - off));
    free(n->wsinput);
    n->wsinput = xx;
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
    ssize_t ret;
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
            ret = ws_decode(n->wsinput + done, total, &x, &xsz, &op);
            /* incomplete packet: cache what we have for the next round */
            if (ret < 0) {
                network_node_wscache(n, done);
                goto out_short;
            }

            /* paste what we decoded with what was cached from before */
            network_node_uncache(n, x, xsz);
            size = ret;
        } else {
            network_node_uncache(n, buf, size);
        }

        handled = 0;

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
                network_node_cache(n, handled);
                break;
            }
            if (n->state == ST_ERROR) {
                ref_put(n);
                return 0;
            }
            handled += xhandled;
        } while (handled > 0 && handled < n->inputsz);

        if (handled == n->inputsz) {
            free(n->input);
            n->input = NULL;
            n->inputsz = 0;
        }
        total -= size;
        done += size;

        if (n->websocket)
            free(x);
    }

    if (n->websocket) {
        free(n->wsinput);
        n->wsinput = NULL;
        n->wsinputsz = 0;
    }

    return 0;

out_short:
    if (n->websocket) {
        if (op == WSOP_FIN) {
            ref_put(n);
        }
    }

    return 1;
}

#ifdef SERVER_STANDALONE
static void all_queue_outmsg(void *data, size_t size)
{
    struct network_node *n;

    list_for_each_entry(n, &nodes, entry) {
        if (n->mode == CLIENT && n->state == ST_RUNNING)
            queue_outmsg(n, data, size);
    }
}
#endif /* SERVER_STANDALONE */

void networking_poll(void)
{
    struct network_node *n, *it, *child;
    int                 i = 0, events;
    uint8_t             buf[4096];
    ssize_t             ret;

    memset(buf, 0, sizeof(buf));
    polling_update();
    if (nr_nodes == 0) {
        n = client_setup(_ncfg);
    }

    ret = poll(pollfds, nr_nodes, TIMEOUT);
    if (ret <= 0)
        goto state;

    //dbg("polled: %d\n", ret);
    list_for_each_entry_iter(n, it, &nodes, entry) {
        events = pollfds[i].revents;
        // dbg("pollfd[%d]: %x\n", i, events);
        pollfds[i].revents = 0;
        /* First, new incoming connections */
        if (n->mode == LISTEN && (events & POLLIN)) {
            CHECK(child = network_node_accept(n));
            dbg("accepted client connection\n");
            ret = recvfrom(child->fd, buf, sizeof(buf), 0, (struct sockaddr *)&child->sa, &child->addrlen);
            //dbg("Handshake: '%s'\n", buf);
            if (ret < 0)
                continue;
            if (child->handshake)
                child->handshake(child, buf);
            else
                process_input(child, buf, ret);
            child->events |= POLLOUT;

            continue;
        }
        /* Second, new data on existing connections */
        if (events & POLLIN) {
            memset(buf, 0, sizeof(buf));
            ret = recvfrom(n->fd, buf, sizeof(buf), 0, (struct sockaddr *)&n->sa, &n->addrlen);
            if (!ret) {
                dbg("pollfd[%d]: shutting down\n", i);
                ref_put(n);
                continue;
            }
            if (ret > 0) {
                dbg("new data on %d: %zd bytes (+ %zu/%zu bytes left over):\n", i, ret, n->inputsz, n->wsinputsz);

                if (process_input(n, buf, ret))
                    goto next;
            } else if (ret < 0) {
                err("recvfrom[%d] returned %zd: %m // %x\n", i, ret, events);
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

            if (n->state == ST_INIT)
                n->state = ST_HANDSHAKE;
            list_for_each_entry_iter(qd, _qd, &n->out_queue, entry) {
                //dbg("sending[%d]: <-- %zd\n", i, qd->outsz);
                ret = sendto(n->fd, qd->out, qd->outsz, MSG_NOSIGNAL, (struct sockaddr *)&n->sa, n->addrlen);
                free(qd->out);

                list_del(&qd->entry);
                free(qd);

                //n->outsz = asprintf(&n->out, "PING");
                if (n->handshake) {
                    n->websocket = true;
                    n->handshake = NULL;
                }
            }
            n->events &= ~POLLOUT;
        }

next:
        i++;
    }

state:
    list_for_each_entry(n, &nodes, entry) {
        //dbg_on(n->mode == CLIENT, "n->state: %d\n", n->state);
        if (n->mode == CLIENT && n->state == ST_HANDSHAKE) {
            struct message_command *mcmd;
            struct timespec ts;

            dbg("server '%s:%d' handshake\n", inet_ntoa(n->sa.sin_addr), ntohs(n->sa.sin_port));
            CHECK(mcmd = calloc(1, sizeof(*mcmd)));
            mcmd->connect = 1;
            clock_gettime(CLOCK_REALTIME, &ts);
            timespec_to_64(&ts, &mcmd->time);
            queue_outmsg(n, mcmd, sizeof(*mcmd));
            n->state = ST_RUNNING;
        }
    }
}

#ifdef __EMSCRIPTEN__
static void socket_callback(int fd, void *data)
{
    dbg("socket callback on fd %d\n", fd);
    networking_poll();
}
#endif /* __EMSCRIPTEN__ */

int networking_init(struct networking_config *cfg, enum mode mode)
{
    struct network_node *n;

    _ncfg = memdup(cfg, sizeof(*cfg));
    switch (mode) {
    case CLIENT:
        CHECK(n = client_setup(_ncfg));
        break;
    case SERVER:
        CHECK(n = server_setup(_ncfg->server_ip, _ncfg->server_port));
        CHECK(n = server_setup(_ncfg->server_ip, _ncfg->server_wsport));
        n->data      = &_wsh;
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

    return 0;
}

void networking_done(void)
{
    struct network_node *n, *it;

    networking_broadcast_restart();
not_empty:
    networking_poll();

    list_for_each_entry_iter(n, it, &nodes, entry) {
        if (!list_empty(&n->out_queue))
            goto not_empty;
        ref_put(n);
    }
    free(_ncfg);
}

#ifdef SERVER_STANDALONE
int platform_input_init(void)
{
    return 0;
}

static void sigint_handler(int sig)
{
    fprintf(stderr, "## SIGINT\n");
    networking_done();
    clap_done(0);
    exit(0);
}

static bool exit_server_loop = false;
static bool restart_server   = false;

void server_run(void)
{
    while (!exit_server_loop)
        networking_poll();
}

static int handle_command(struct message *m, void *data)
{
    if (m->cmd.restart) {
        exit_server_loop = true;
        restart_server   = true;
    }
    if (m->cmd.status) {
        all_queue_outmsg(&m->cmd, sizeof(m->cmd));
    }
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug = 1,
    };
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = 21044,
        .server_wsport = 21045,
    };

    signal(SIGINT, sigint_handler);
    clap_init(&cfg, argc, argv, envp);
    networking_init(&ncfg, SERVER);
    subscribe(MT_COMMAND, handle_command, NULL);
    server_run();
    networking_done();
    clap_done(0);
    if (restart_server) {
        dbg("### restarting server ###\n");
        return clap_restart();
    }

    return EXIT_SUCCESS;
}
#endif /* SERVER_STANDALONE */
