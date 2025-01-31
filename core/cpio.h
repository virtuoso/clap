#ifndef __CLAP_CPIO_H__
#define __CLAP_CPIO_H__

#include <sys/types.h>
#include <stdbool.h>
#include "error.h"

typedef struct cpio_context cpio_context;
typedef void (*add_file)(void *callback_data, const char *name, void *data, size_t size);

typedef struct cpio_params {
    void            *buf;
    const char      *file_name;
    size_t          buf_size;
    void            *callback_data;
    add_file        add_file;
    bool            write;
} cpio_params;

#define cpio_open(args...) \
    _cpio_open(&(cpio_params){ args })
cpio_context *_cpio_open(const cpio_params *params);
void cpio_close(cpio_context *ctx);
cerr cpio_read(cpio_context *ctx);

#endif /* __CLAP_CPIO_H__ */
