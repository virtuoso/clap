/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_TEST_H__
#define __CLAP_TEST_H__

#include <stddef.h>

/* One test case: a name for reporting and a function returning EXIT_SUCCESS. */
struct test {
    const char  *name;
    int         (*test)(void);
};

/**
 * test_main() - Run a test binary's cases
 * @argc:   argument count, as passed to main()
 * @argv:   argument vector; -v/--verbose bumps @verbose
 * @envp:   environment, as passed to main()
 * @tests:  test cases to run in order
 * @nr:     number of entries in @tests
 *
 * Owns the messagebus lifetime around the run, prints PASSED/FAILED for
 * each case and stops at the first failure. Each subsystem's test binary
 * provides its own tests[] and a main() that forwards here.
 *
 * Return: EXIT_SUCCESS if every case passed, EXIT_FAILURE otherwise
 */
int test_main(int argc, char **argv, char **envp, const struct test *tests, size_t nr);

/* -v count; tests may read it to be chatty. */
extern unsigned int verbose;

/* The harness' stub engine context, for APIs that take one. */
extern struct clap_context *test_ctx;

/** TEST_CPU_BUDGET - what the harness' clap_cpus_reserve() grants at most */
#define TEST_CPU_BUDGET 2

#endif /* __CLAP_TEST_H__ */
