// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <sys/stat.h>
#include "cpio.h"
#include "librarian.h"
#include "error.h"
#include "memory.h"
#include "util.h"

/*
 * CPIO implementation, derived from an implementation that was derived from
 * PAX in NetBSD
 */
#define MAGIC   070707
#define TRAILER "TRAILER!!!"

typedef uint8_t cpio_field[2];

typedef struct cpio_header {
    cpio_field  h_magic;
    cpio_field  h_dev;
    cpio_field  h_ino;
    cpio_field  h_mode;
    cpio_field  h_uid;
    cpio_field  h_gid;
    cpio_field  h_nlink;
    cpio_field  h_rdev;
    cpio_field  h_mtime_1;
    cpio_field  h_mtime_2;
    cpio_field  h_namesize;
    cpio_field  h_filesize_1;
    cpio_field  h_filesize_2;
} cpio_header;

typedef struct cpio_context {
    uint8_t     *start;
    uint8_t     *cursor;
    size_t      size;
    FILE        *f;
    add_file    add_file;
    void        *callback_data;
    bool        reverse;
    bool        writing;
    bool        alloc;
} cpio_context;

static inline uint16_t cpio_val(cpio_context *ctx, cpio_field s)
{
    return ctx->reverse ?
        ((uint16_t)s[0] << 8 | s[1]) :
        ((uint16_t)s[1] << 8 | s[0]);
}

#define ALIGN2(x) round_up((x), 2)

static DEFINE_CLEANUP(cpio_context, mem_free(*p))

cpio_context *_cpio_open(const cpio_params *params)
{
    if (params->buf && params->file_name)
        return NULL;

    LOCAL_SET(cpio_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);

    if (params->file_name) {
        if (!params->write) {
            ctx->alloc = true;
            err("implement me\n");
            enter_debugger();
        }

        ctx->f = fopen(params->file_name, "w");
        if (!ctx->f)
            return NULL;

        ctx->writing = true;
    } else {
        ctx->start          = params->buf;
        ctx->cursor         = ctx->start;
        ctx->size           = params->buf_size;
        ctx->add_file       = params->add_file;
        ctx->callback_data  = params->callback_data;
    }

    return NOCU(ctx);
}

void cpio_close(cpio_context *ctx)
{
    if (ctx->alloc)
        mem_free(ctx->start);
    if (ctx->f)
        fclose(ctx->f);

    mem_free(ctx);
}

cerr cpio_read(cpio_context *ctx)
{
    for (ctx->cursor = ctx->start; ctx->cursor - ctx->start < ctx->size;) {
        cpio_header *h = (cpio_header *)ctx->cursor;
        uint16_t magic = cpio_val(ctx, h->h_magic);
        if (magic != MAGIC) {
            ctx->reverse = true;
            magic = cpio_val(ctx, h->h_magic);
            if (magic != MAGIC)
                return CERR_PARSE_FAILED;
        }

        size_t size = cpio_val(ctx, h->h_filesize_1) << 16 |
                      cpio_val(ctx, h->h_filesize_2);
        mode_t mode = cpio_val(ctx, h->h_mode);

        uint16_t namesize = cpio_val(ctx, h->h_namesize);
        char *name = (char *)ctx->cursor + sizeof(*h);

        if (namesize == sizeof(TRAILER) - 1 &&
            !strncmp(name, TRAILER, sizeof(TRAILER)))
            break;

        ctx->cursor = (uint8_t *)name + ALIGN2(namesize);

        unused uint8_t *body = ctx->cursor;
        ctx->cursor += ALIGN2(size);

        /* skip non-regular files (directories, devices, pipes, sockets) */
        if (!S_ISREG(mode))
            continue;

        if (ctx->add_file)
            ctx->add_file(ctx->callback_data, name, body, size);
    }
    return CERR_OK;
}
