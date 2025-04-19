// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <time.h>
#include "cpio.h"
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
    uint16_t    inode;
    bool        reverse;
    bool        writing;
    bool        alloc;
    bool        opened;
} cpio_context;

static inline uint16_t cpio_val(cpio_context *ctx, cpio_field s)
{
    return ctx->reverse ?
        ((uint16_t)s[0] << 8 | s[1]) :
        ((uint16_t)s[1] << 8 | s[0]);
}

static inline void cpio_set_val(cpio_context *ctx, cpio_field s, uint16_t val)
{
    if (ctx->reverse) {
        s[0] = bitmask_field(val, 0xff00);
        s[1] = bitmask_field(val, 0x00ff);
    } else {
        s[0] = bitmask_field(val, 0x00ff);
        s[1] = bitmask_field(val, 0xff00);
    }
}

#define ALIGN2(x) round_up((x), 2)

DEFINE_CLEANUP(cpio_context, if (*p) cpio_close(*p))

cpio_context *_cpio_open(const cpio_params *params)
{
    if (params->buf && (params->file_name || params->file))
        return NULL;

    if (params->file_name && params->file)
        return NULL;

    LOCAL_SET(cpio_context, ctx) = mem_alloc(sizeof(*ctx), .zero = 1);

    if (!params->buf) {
        ctx->f = params->file_name ?
            fopen(params->file_name, params->write ? FOPEN_WB : FOPEN_RB) :
            params->file;
        if (!ctx->f)
            return NULL;

        ctx->opened = !!params->file_name;

        if (!params->write) {
            /* can't stream from stdin yet */
            if (ctx->f == stdin)
                return NULL;

            struct stat st;

            if (fstat(fileno(ctx->f), &st))
                return NULL;

            ctx->alloc = true;
            ctx->start = mem_alloc(st.st_size);
            if (fread(ctx->start, st.st_size, 1, ctx->f) != 1)
                return NULL;

            ctx->size = st.st_size;
        }

        ctx->writing = params->write;
        compat_set_binary(ctx->f);
    } else {
        ctx->start  = params->buf;
        ctx->size   = params->buf_size;
    }

    ctx->cursor         = ctx->start;
    ctx->add_file       = params->add_file;
    ctx->callback_data  = params->callback_data;

    return NOCU(ctx);
}

void cpio_close(cpio_context *ctx)
{
    if (ctx->alloc)
        mem_free(ctx->start);

    if (ctx->f) {
        cpio_write(ctx, TRAILER, NULL, 0);
        if (ctx->opened)
            fclose(ctx->f);
    }

    mem_free(ctx);
}

cerr cpio_read(cpio_context *ctx)
{
    if (ctx->writing)
        return CERR_INVALID_OPERATION;

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

        if (namesize == sizeof(TRAILER) &&
            !strncmp(name, TRAILER, sizeof(TRAILER)))
            return CERR_OK;

        ctx->cursor = (uint8_t *)name + ALIGN2(namesize);

        __unused uint8_t *body = ctx->cursor;
        ctx->cursor += ALIGN2(size);

        /* skip non-regular files (directories, devices, pipes, sockets) */
        if (!S_ISREG(mode))
            continue;

        if (ctx->add_file)
            ctx->add_file(ctx->callback_data, name, body, size);
    }
    return CERR_OK;
}

cerr cpio_write(cpio_context *ctx, const char *name, void *buf, size_t size)
{
    mode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
    uint16_t namesize = strlen(name) + 1;
    uint32_t timestamp = time(NULL);
    cpio_header h;

    if (!ctx->writing)
        return CERR_INVALID_OPERATION;

    if (size > UINT_MAX)
        return CERR_TOO_LARGE;

    /* buf and size can either be both set or both unset */
    if (!!size != !!buf)
        return CERR_INVALID_ARGUMENTS;

    if (size)
        mode |= S_IFREG;
    else
        mode |= S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;

    memset(&h, 0, sizeof(h));
    cpio_set_val(ctx, h.h_magic, MAGIC);
    cpio_set_val(ctx, h.h_filesize_1, bitmask_field(size, 0xfffful << 16));
    cpio_set_val(ctx, h.h_filesize_2, bitmask_field(size, 0xfffful));
    cpio_set_val(ctx, h.h_mtime_1, bitmask_field(timestamp, 0xfffful << 16));
    cpio_set_val(ctx, h.h_mtime_2, bitmask_field(timestamp, 0xfffful));
    cpio_set_val(ctx, h.h_namesize, namesize);
    cpio_set_val(ctx, h.h_mode, mode);
    cpio_set_val(ctx, h.h_ino, ctx->inode++);
    cpio_set_val(ctx, h.h_uid, 0);
    cpio_set_val(ctx, h.h_gid, 0);
    cpio_set_val(ctx, h.h_nlink, size ? 1 : 2);
    fwrite(&h, sizeof(h), 1, ctx->f);
    fwrite(name, namesize, 1, ctx->f);
    if (namesize & 1)
        fputc('\0', ctx->f);

    if (!buf)
        return CERR_OK;

    fwrite(buf, size, 1, ctx->f);
    if (size & 1)
        fputc('\0', ctx->f);

    return CERR_OK;
}
