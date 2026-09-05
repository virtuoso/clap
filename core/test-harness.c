// SPDX-License-Identifier: Apache-2.0
/*
 * Shared test harness: the runner and the stub engine context that every
 * test binary links. Each subsystem's test file provides its own tests[]
 * and a trivial main() that forwards to test_main().
 */
#include <getopt.h>
#include <stdlib.h>
#include "clap.h"
#include "common.h"
#include "error.h"
#include "logger.h"
#include "messagebus.h"
#include "test.h"
#include "util.h"

/*
 * The test binaries link a stripped-down engine, so provide the messagebus
 * accessor the shared code expects instead of the full clap_context.
 */
struct clap_context {
    struct messagebus mb;
};

struct messagebus *clap_get_messagebus(struct clap_context *ctx)
{
    return &ctx->mb;
}

/*
 * The CPU budget worker pools draw on: the real one sizes itself by
 * the machine, the tests want a known cap to check the clamping against.
 */
unsigned int clap_cpus_reserve(struct clap_context *ctx, unsigned int want)
{
    return min(want, TEST_CPU_BUDGET);
}

void clap_cpus_release(struct clap_context *ctx, unsigned int nr) {}

/*
 * logger.c prints build_date on init. The real value comes from the generated
 * builddate.c, which the engine regenerates on every build; the tests don't
 * care about the timestamp, so stub it instead of compiling that file.
 */
const char *build_date = "test";

unsigned int verbose;
struct clap_context *test_ctx;

static struct option long_options[] = {
    { "verbose",    no_argument,        0, 'v' },
    {}
};

static const char short_options[] = "v";

int test_main(int argc, char **argv, char **envp, const struct test *tests, size_t nr)
{
    int ret = EXIT_SUCCESS, c, option_index;
    struct clap_context ctx = {};

    cerr err = messagebus_init(&ctx);
    if (IS_CERR(err))   return EXIT_FAILURE;

    test_ctx = &ctx;

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)    break;

        switch (c) {
        case 'v':
            verbose++;
            break;
        default:
            err("invalid command line option %x\n", c);
            ret = EXIT_FAILURE;
            goto out;
        }
    }

    for (size_t i = 0; i < nr; i++) {
        ret = tests[i].test();
        msg("test %-40s: %s\n", tests[i].name, ret ? "FAILED" : "PASSED");
        if (ret)        break;
    }

out:
    messagebus_done(&ctx);
    return ret;
}
