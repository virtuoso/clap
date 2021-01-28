#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
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

struct queued {
    void        *out;
    size_t      outsz;
    struct list entry;
};

struct network_node {
    struct ref             ref;
    struct list            entry;
    struct network_node    *parent;
    struct message_source  *src;
    struct sockaddr_in     sa;
    struct timespec        local_time;
    struct timespec        remote_time;
    struct timespec        remote_delta;
    socklen_t              addrlen;
    bool                   websocket;
    void                   *data;
    struct list            out_queue;
    enum mode              mode;
    int                    fd;
    int                    state;
    short                  events;
    int (*handshake)(struct network_node *n, const uint8_t *buf);
};

static struct networking_config  *_ncfg;
static struct pollfd *pollfds;
static unsigned int  nr_nodes;

static void polling_alloc()
{
    struct network_node *n;

    nr_nodes = 0;
    list_for_each_entry(n, &nodes, entry) {
        nr_nodes++;
    }

    dbg("pollfds: %d\n", nr_nodes);
    free(pollfds);
    CHECK(pollfds = calloc(nr_nodes, sizeof(struct pollfd)));
}

static void polling_update()
{
    struct network_node *n;
    unsigned int        i = 0;

    if (!nr_nodes && !list_empty(&nodes))
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

static void polling_setup()
{
    polling_alloc();
    polling_update();
}

static void network_node_drop(struct ref *ref)
{
    struct network_node *n = container_of(ref, struct network_node, ref);

    free(n->src);
    list_del(&n->entry);
    close(n->fd);
    shutdown(n->fd, SHUT_RDWR);
    free(n);
}

static struct network_node *network_node_new(int mode)
{
    struct network_node *n;

    CHECK(n = ref_new(struct network_node, ref, network_node_drop));
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
    polling_setup();

    return 0;
}

static int network_node_connect(struct network_node *n)
{
    int ret;
   
    //dbg("connecting: state: %d\n", n->state);
    ret = connect(n->fd, (struct sockaddr *)&n->sa, n->addrlen);
    //dbg("connect: %d %m state: %d\n", ret, n->state);
    //if (!ret)
    //    n->state = ST_HANDSHAKE;

    return ret;
}

static struct network_node *network_node_accept(struct network_node *n)
{
    struct network_node *child = network_node_new_parent(n);
    child->mode         = SERVER;
    CHECK(child->fd = accept(n->fd, (struct sockaddr *)&n->sa, &n->addrlen));
    CHECK(child->src = calloc(1, sizeof(*child->src)));
    child->src->type = MST_CLIENT;
    asprintf(&child->src->name, "%s:%d", inet_ntoa(n->sa.sin_addr), ntohs(n->sa.sin_port));
    dbg("new client '%s'\n", child->src->name);
    child->src->desc = "remote client";
    child->state     = ST_HANDSHAKE;
    polling_setup();
    return child;
}

struct wsheader {
    char *key;
    int  version;
};

static void queue_outmsg(struct network_node *n, void *data, size_t size);

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
    CHECK(key = calloc(1, wslen + strlen(wsguid) + 1));
    strcpy(key, wsh->key);
    strcpy(key + wslen, wsguid);
    SHA1(h, key, strlen(key));

    base64_encode(bh, sizeof(bh), h, sizeof(h) - 1);
    CHECK(outsz = asprintf((char **)&out,
                           "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                           "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
                           bh));
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

struct blah {
    void *test;
    int  also_test;
    char ye;
};

static int ws_decode(uint8_t *input, size_t size, uint8_t **poutput, size_t *poutsz)
{
    struct ws_header *h = (void *)input;
    uint8_t          *mask = NULL;
    size_t           len, off = 2;
    uint8_t          *output;
    int              op;

    hexdump(input, size);
    dbg("ws_header: fin=%d opcode=%d mask=%d length=%d\n",
        h->fin, h->opcode, h->mask, h->length);
    len = h->length;
    op  = h->opcode;

    if (len <= 125) {
        off = 2;
    } else if (len == 126) {
        len = input[off++];
        len |= input[off++] << 8;
    } else if (len == 127) {
        len = input[off++];
        len |= input[off++] << 8;
        len |= input[off++] << 16;
        len |= input[off++] << 24;
    }
    if (h->mask) {
        mask = &input[off];
        off += 4;
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

    return op;
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

    return n;
}

static struct network_node *_client_setup(const char *server_ip, unsigned int port)
{
    struct network_node *n;

    CHECK(n = network_node_new_socket(server_ip, port, CLIENT));
    network_node_connect(n);

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

    list_for_each_entry (n, &nodes, entry) {
        void *out;

        if (n->mode != LISTEN && n->state == ST_RUNNING) {
            dbg("sending to client '%s:%d'\n", node_name(n));
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

static void handle_client_input(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command *mcmd;

    if (size < sizeof(*mcmd)) {
        n->state = ST_ERROR;
        dbg("size mismatch: %d <> %d\n", size, sizeof(*mcmd));
        return;
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
}

static void handle_server_handshake(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command *mcmd;

    if (size < sizeof(*mcmd)) {
        n->state = ST_ERROR;
        dbg("size mismatch: %d <> %d\n", size, sizeof(*mcmd));
        return;
    }

    mcmd = (void *)buf;
    if (!mcmd->connect) {
        n->state = ST_ERROR;
        dbg("connect not set: %d\n", mcmd->connect);
        return;
    }

    timespec_from_64(&n->remote_time, &mcmd->time);
    clock_gettime(CLOCK_REALTIME, &n->local_time);
    timespec_diff(&n->local_time, &n->remote_time, &n->remote_delta);
    n->state = ST_RUNNING;
    dbg("local time: %lu.%lu client time: %lu.%lu delta: %lu.%lu\n",
        n->local_time.tv_sec, n->local_time.tv_nsec,
        n->remote_time.tv_sec, n->remote_time.tv_nsec,
        n->remote_delta.tv_sec, n->remote_delta.tv_nsec);
}

static void handle_server_command(struct network_node *n, uint8_t *buf, size_t size)
{
    struct message_command *mcmd;
    struct message         m;

    if (size < sizeof(*mcmd)) {
        n->state = ST_ERROR;
        return;
    }

    mcmd = (void *)buf;
    if (mcmd->restart)
        networking_broadcast_restart();
    m.type   = MT_COMMAND;
    m.source = n->src;
    memcpy(&m.cmd, mcmd, sizeof(m.cmd));
    message_send(&m);
}

static void handle_server_input(struct network_node *n, uint8_t *buf, size_t size)
{
    switch (n->state) {
    case ST_HANDSHAKE:
        handle_server_handshake(n, buf, size);
        break;
    case ST_SYNC:
        break;
    case ST_RUNNING:
        handle_server_command(n, buf, size);
        break;
    }
}

static void handle_input(struct network_node *n, uint8_t *buf, size_t size)
{
    dbg("got input on '%s'(sz=%zu): %d/%d\n", node_name(n), size, n->mode, n->state);
    if (n->mode == CLIENT) {
        handle_client_input(n, buf, size);
    } else {
        handle_server_input(n, buf, size);
    }
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
        hexdump(frame, framesz);

        free(data); /* XXX: unless it's static; don't queue static stuff */
        data = frame;
        size = framesz;
    }

    qd->out    = (void *)data;
    qd->outsz  = size;
    list_append(&n->out_queue, &qd->entry);

    n->events |= POLLOUT;
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

void networking_poll()
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

    dbg("polled: %d\n", ret);
    list_for_each_entry_iter(n, it, &nodes, entry) {
        events = pollfds[i].revents;
        dbg("pollfd[%d]: %x\n", i, events);
        pollfds[i].revents = 0;
        if (n->mode == LISTEN && (events & POLLIN)) {
            CHECK(child = network_node_accept(n));
            dbg("accepted client connection\n");
            ret = recvfrom(child->fd, buf, sizeof(buf), 0, (struct sockaddr *)&child->sa, &child->addrlen);
            //dbg("Handshake: '%s'\n", buf);
            if (child->handshake)
                child->handshake(child, buf);
            else
                handle_input(child, buf, ret);
            child->events |= POLLOUT;

            continue;
        }
        if (events & POLLIN) {
            memset(buf, 0, sizeof(buf));
            ret = recvfrom(n->fd, buf, sizeof(buf), 0, (struct sockaddr *)&n->sa, &n->addrlen);
            if (!ret) {
                dbg("pollfd[%d]: shutting down\n", i);
                ref_put(&n->ref);
                polling_setup();
                continue;
            }
            if (ret > 0) {
                uint8_t *x = buf;
                size_t xsz = ret;
                int    op = -1;
                dbg("new data on %d:\n", i);
                if (n->websocket)
                    op = ws_decode(buf, ret, &x, &xsz);

                if (op == WSOP_FIN) {
                    ref_put(&n->ref);
                    polling_setup();
                    continue;
                }
                handle_input(n, x, xsz);
                if (x != buf)
                    free(x);
            } else if (ret < 0) {
                err("recvfrom[%d] returned %d: %m // %x\n", i, ret, events);
            }
        }
        if (events & POLLHUP) {
            ref_put(&n->ref);
            polling_setup();
            continue;
        }
        if (events & POLLOUT) {
            struct queued *qd, *_qd;

            if (n->state == ST_INIT)
                n->state = ST_HANDSHAKE;
            list_for_each_entry_iter(qd, _qd, &n->out_queue, entry) {
                dbg("sending[%d]: <-- %zd\n", i, qd->outsz);
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

static void log_flush(struct log_entry *e)
{
    //fprintf(stderr, "===> %s <===\n", e->msg);
}

int networking_init(struct networking_config *cfg, enum mode mode)
{
    struct network_node *n;

    _ncfg = memdup(cfg, sizeof(*cfg));
    switch (mode) {
    case CLIENT:
        CHECK(n = client_setup(_ncfg));
        rb_sink_add(log_flush, VDBG, 1);
        break;
    case SERVER:
        CHECK(n = server_setup(_ncfg->server_ip, _ncfg->server_port));
        CHECK(n = server_setup(_ncfg->server_ip, _ncfg->server_wsport));
        n->data      = &_wsh;
        n->handshake = websocket_parse;
        polling_setup();
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
    networking_poll();

    list_for_each_entry_iter(n, it, &nodes, entry) {
        ref_put(&n->ref);
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
    networking_done();
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
