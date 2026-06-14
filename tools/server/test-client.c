// SPDX-License-Identifier: Apache-2.0

/*
 * Minimal networking client for the end-to-end test: connect to a test
 * server, stream some log lines at it, disconnect cleanly.
 */
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

#include "clap.h"
#include "config.h"
#include "networking.h"

static struct option long_options[] = {
    { "server",     required_argument,  0, 'S' },
    { "port",       required_argument,  0, 'p' },
    {}
};

static const char short_options[] = "S:p:";

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug = 1,
    };
    struct networking_config ncfg = {
        .server_ip   = CONFIG_SERVER_IP,
        .server_port = CONFIG_SERVER_PORT,
        .logger      = 1,
        .timeout     = 10,
    };
    int c, option_index;

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'S':
            ncfg.server_ip = optarg;
            break;
        case 'p':
            ncfg.server_port = strtoul(optarg, NULL, 0);
            break;
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);
        }
    }

    cresp(clap_context) clap_res = clap_init(&cfg, argc, argv, envp);
    if (IS_CERR(clap_res))
        return EXIT_FAILURE;

    ncfg.clap = clap_res.val;

    cerr err = networking_init(clap_res.val, &ncfg, NET_CLIENT);
    if (IS_CERR(err)) {
        err_cerr(err, "failed to initialize networking\n");
        clap_done(clap_res.val, 0);
        return EXIT_FAILURE;
    }

    msg("net-e2e marker: the client was here\n");

    /* a second of polls: connect, stream the ring buffer, settle */
    for (int i = 0; i < 100; i++)
        networking_poll();

    networking_done();
    clap_done(clap_res.val, 0);

    return EXIT_SUCCESS;
}
