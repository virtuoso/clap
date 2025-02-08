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

struct clap_context *clap;

static void sigint_handler(int sig)
{
    fprintf(stderr, "## SIGINT\n");
    networking_done();
    clap_done(clap, 0);
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
        networking_broadcast(CLIENT, &m->cmd, sizeof(m->cmd));
    }
    return 0;
}

static struct option long_options[] = {
    { "restart",    no_argument,        0, 'R' },
    { "server",     required_argument,  0, 'S'},
    {}
};

static const char short_options[] = "RS:";

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug = 1,
    };
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = 21044,
        .server_wsport = 21045,
        .timeout       = 100,
    };
    int c, option_index, do_restart = 0;

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

    if (do_restart) {
        networking_init(&ncfg, CLIENT);
        networking_poll();
        networking_poll();
        networking_broadcast_restart();
        networking_poll();
        networking_done();
        clap_done(clap_res.val, 0);
        return EXIT_SUCCESS;
    }

    networking_init(&ncfg, SERVER);
    subscribe(MT_COMMAND, handle_command, NULL);
    server_run();
    networking_done();
    if (restart_server) {
        dbg("### restarting server ###\n");
        cres(int) res = clap_restart(clap_res.val);
        if (IS_CERR(res))
            return EXIT_FAILURE;
    }
    clap_done(clap_res.val, 0);

    return EXIT_SUCCESS;
}
