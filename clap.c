#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include "common.h"
#include "input.h"
#include "messagebus.h"
#include "librarian.h"

#if defined(__has_feature)
# if __has_feature(address_sanitizer)
const char *__asan_default_options() {
    /*
     * https://github.com/google/sanitizers/wiki/AddressSanitizerFlags
     * https://github.com/google/sanitizers/wiki/SanitizerCommonFlags
     * malloc_context_size=20
     */
    return "verbosity=1:check_initialization_order=true:detect_stack_use_after_return=true:"
	   "alloc_dealloc_mismatch=true:strict_string_checks=true";
}
# endif /* __has_feature(address_sanitizer) */
#endif /* __has_feature */
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
