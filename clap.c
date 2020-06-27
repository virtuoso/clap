#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "common.h"
#include "input.h"
#include "messagebus.h"
#include "librarian.h"

static void clap_config_init(struct clap_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
}

static bool clap_config_is_valid(struct clap_config *cfg)
{
    return true;
}

int clap_init(struct clap_config *cfg)
{
    unsigned int log_flags = LOG_DEFAULT;
    struct clap_config config;

    if (cfg) {
        if (!clap_config_is_valid(cfg))
            return -EINVAL;
        memcpy(&config, cfg, sizeof(config));
    } else {
        clap_config_init(&config);
    }

    if (config.debug)
        log_flags = LOG_FULL;

    log_init(log_flags);
    (void)input_init(); /* XXX: error handling */
    (void)librarian_init();

    return 0;
}

void clap_done(int status)
{
    exit_cleanup_run(status);
}
