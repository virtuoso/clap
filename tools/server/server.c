// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>

#include "clap.h"
#include "config.h"
#include "messagebus.h"
#include "networking.h"

static volatile sig_atomic_t exit_requested;

static void sigint_handler(int sig)
{
    exit_requested = 1;
}

static bool exit_server_loop = false;
static bool restart_server   = false;

static void server_run(void)
{
    while (!exit_server_loop && !exit_requested)
        networking_poll();
}

static int handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    if (m->cmd.restart) {
        exit_server_loop = true;
        restart_server   = true;
    }
    if (m->cmd.status) {
        /* on the server, connected game clients are the NET_SERVER-mode nodes */
        networking_broadcast(NET_SERVER, &m->cmd, sizeof(m->cmd));
    }
    return 0;
}

static struct option long_options[] = {
    { "restart",    no_argument,        0, 'R' },
    { "server",     required_argument,  0, 'S'},
    { "port",       required_argument,  0, 'p' },
    { "wsport",     required_argument,  0, 'w' },
    {}
};

static const char short_options[] = "RS:p:w:";

static unsigned int parse_port(const char *str)
{
    char *end;
    unsigned long port = strtoul(str, &end, 0);

    if (*end || !port || port > 65535) {
        fprintf(stderr, "invalid port '%s'\n", str);
        exit(EXIT_FAILURE);
    }

    return port;
}

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug = 1,
    };
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = CONFIG_SERVER_PORT,
        .server_wsport = CONFIG_SERVER_WSPORT,
        .timeout       = 100,
    };
    int c, option_index, do_restart = 0;
    int ret = EXIT_SUCCESS;

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'R':
            do_restart++;
            break;
        case 'S':
            ncfg.server_ip = optarg;
            break;
        case 'p':
            ncfg.server_port = parse_port(optarg);
            break;
        case 'w':
            ncfg.server_wsport = parse_port(optarg);
            break;
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);
        }
    }

    signal(SIGINT, sigint_handler);
    cresp(clap_context) clap_res = clap_init(&cfg, argc, argv, envp);
    if (IS_CERR(clap_res)) {
        err("failed to initialize clap\n");
        return EXIT_FAILURE;
    }

    ncfg.clap = clap_res.val;

    cerr err;

    if (do_restart) {
        err = networking_init(clap_res.val, &ncfg, NET_CLIENT);
        if (IS_CERR(err)) {
            err_cerr(err, "Failed to initialize client connection for restarting\n");
            ret = EXIT_FAILURE;
            goto exit_clap;
        }
        /* wait for the connection to come up before broadcasting */
        int sent = 0;
        for (int i = 0; i < 20 && !sent; i++) {
            networking_poll();
            sent = networking_broadcast_restart();
        }

        networking_poll();
        networking_done();
        if (!sent) {
            err("no server to restart at %s\n", ncfg.server_ip);
            ret = EXIT_FAILURE;
        }
        goto exit_clap;
    }

    err = networking_init(clap_res.val, &ncfg, NET_SERVER);
    if (IS_CERR(err)) {
        err_cerr(err, "Failed to initialize server\n");
        ret = EXIT_FAILURE;
        goto exit_clap;
    }

    subscribe(clap_res.val, MT_COMMAND, handle_command, NULL);
    server_run();
    networking_done();
    if (restart_server) {
        dbg("### restarting server ###\n");
        cres(int) res = clap_restart(clap_res.val);
        /* XXX: on failure, exits either way, but let it fallthrough */
        if (IS_CERR(res))
            return EXIT_FAILURE;
    }

exit_clap:
    clap_done(clap_res.val, 0);

    return ret;
}
