#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include "cpio.h"
#include "memory.h"

static cpio_context *ctx;

static void sigint_handler(int sig)
{
    fprintf(stderr, "## SIGINT\n");
    cpio_close(ctx);
    exit(0);
}

static struct option long_options[] = {
    { "create",     no_argument,        0, 'o' },
    {}
};

static const char short_options[] = "o";

int main(int argc, char **argv, char **envp)
{
    int c, option_index, do_create = 0;

    fprintf(stderr, "### starting argc: %d\n", argc);
    for (c = 0; c < argc; c++)
        fprintf(stderr, "### => '%s'\n", argv[c]);

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'o':
            do_create++;
            break;
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);
        }
    }

    if (!do_create) {
        fprintf(stderr, "%s can only be invoked with -o option\n", argv[0]);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "### opening stdout\n");
    ctx = cpio_open(.write = true, .file = stdout);
    if (!ctx) {
        fprintf(stderr, "can't open cpio output\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, sigint_handler);

    fprintf(stderr, "### opening reading stdin\n");
    while (!feof(stdin)) {
        char name[PATH_MAX];
        struct stat st;
        FILE *f;

        if (!fgets(name, sizeof(name), stdin))
            continue;

        str_chomp(name);

        fprintf(stderr, "### got '%s'\n", name);
        int err = stat(name, &st);
        if (err)
            continue;

        fprintf(stderr, "### size: %zu, isreg: %d\n", st.st_size, S_ISREG(st.st_mode));
        if (!S_ISREG(st.st_mode)) {
            cpio_write(ctx, name, NULL, 0);
            continue;
        }

        f = fopen(name, "r");
        fprintf(stderr, "### opening: %p\n", f);
        if (!f)
            continue;

        void *buf = mem_alloc(st.st_size);
        fprintf(stderr, "### buf: %p\n", buf);
        if (fread(buf, st.st_size, 1, f) == 1) {
            cerr err = cpio_write(ctx, name, buf, st.st_size);
            fprintf(stderr, "### err: %d\n", err);
        }
        mem_free(buf);
        fclose(f);
    }

    cpio_close(ctx);

    return EXIT_SUCCESS;
}
