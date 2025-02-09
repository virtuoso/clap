#ifndef __CLAP_CPIO_H__
#define __CLAP_CPIO_H__

#include <sys/types.h>
#include <stdbool.h>
#include "error.h"
#include "util.h"

typedef struct cpio_context cpio_context;
typedef void (*add_file)(void *callback_data, const char *name, void *data, size_t size);

DECLARE_CLEANUP(cpio_context);

typedef struct cpio_params {
    void            *buf;
    const char      *file_name;
    FILE            *file;
    size_t          buf_size;
    void            *callback_data;
    add_file        add_file;
    bool            write;
} cpio_params;

#define cpio_open(args...) \
    _cpio_open(&(cpio_params){ args })
cpio_context *_cpio_open(const cpio_params *params) __nonnull_params();
void cpio_close(cpio_context *ctx) __nonnull_params();
cerr cpio_read(cpio_context *ctx) __nonnull_params();
cerr cpio_write(cpio_context *ctx, const char *name, void *buf, size_t size) __nonnull_params((1, 2));

#endif /* __CLAP_CPIO_H__ */
