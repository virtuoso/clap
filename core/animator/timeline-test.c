// SPDX-License-Identifier: Apache-2.0
/*
 * Element clock tests, built as the standalone animator_tests binary: it
 * links only timeline.c (pure arithmetic) and the shared test harness
 * (test_main() in core/test-harness.c), not the whole engine test blob.
 */
#include <math.h>
#include <stdlib.h>
#include "util.h"
#include "test.h"
#include "timeline.h"

static bool close_to(double a, double b)
{
    return fabs(a - b) < 1e-9;
}

/* a 2 s element at speed 1 runs from 0 to 2 and ends at 2 */
static int timeline_test_elapsed(void)
{
    timeline tl;

    timeline_start(&tl, 10.0, 2.0, 1.0, 0.0);
    if (!close_to(timeline_elapsed(&tl, 10.0), 0.0))        return EXIT_FAILURE;
    if (!close_to(timeline_elapsed(&tl, 11.0), 1.0))        return EXIT_FAILURE;
    if (!close_to(timeline_progress(&tl, 11.0, 2.0), 0.5))  return EXIT_FAILURE;
    if (timeline_ended(&tl, 11.9, 2.0))                     return EXIT_FAILURE;
    if (!timeline_ended(&tl, 12.0, 2.0))                    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* at speed 2 the same element is over in a second; 0 means 1.0 */
static int timeline_test_speed(void)
{
    timeline tl;

    timeline_start(&tl, 10.0, 2.0, 2.0, 0.0);
    if (!close_to(timeline_elapsed(&tl, 10.5), 1.0))        return EXIT_FAILURE;
    if (!close_to(timeline_progress(&tl, 10.5, 2.0), 0.5))  return EXIT_FAILURE;
    if (!timeline_ended(&tl, 11.0, 2.0))                    return EXIT_FAILURE;

    timeline_start(&tl, 10.0, 2.0, 0.0, 0.0);
    if (!close_to(tl.speed, 1.0))                       return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* phase is a fraction of the element, whatever the rate */
static int timeline_test_phase(void)
{
    timeline tl;

    timeline_start(&tl, 10.0, 2.0, 1.0, 0.25);
    if (!close_to(timeline_elapsed(&tl, 10.0), 0.5))    return EXIT_FAILURE;

    timeline_start(&tl, 10.0, 2.0, 2.0, 0.5);
    if (!close_to(timeline_elapsed(&tl, 10.0), 1.0))        return EXIT_FAILURE;
    if (!close_to(timeline_progress(&tl, 10.0, 2.0), 0.5))  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* a rate change jumps the elapsed time; past the end, the element restarts */
static int timeline_test_set_speed(void)
{
    timeline tl;

    timeline_start(&tl, 10.0, 2.0, 1.0, 0.0);
    if (timeline_set_speed(&tl, 10.5, 2.0, 2.0))        return EXIT_FAILURE;
    if (!close_to(timeline_elapsed(&tl, 10.5), 1.0))    return EXIT_FAILURE;

    timeline_start(&tl, 10.0, 2.0, 1.0, 0.0);
    if (!timeline_set_speed(&tl, 11.5, 2.0, 2.0))       return EXIT_FAILURE;
    if (!close_to(timeline_elapsed(&tl, 11.5), 0.0))    return EXIT_FAILURE;
    if (!close_to(tl.speed, 2.0))                       return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "timeline elapsed",   .test = timeline_test_elapsed },
    { .name = "timeline speed",     .test = timeline_test_speed },
    { .name = "timeline phase",     .test = timeline_test_phase },
    { .name = "timeline set speed", .test = timeline_test_set_speed },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
