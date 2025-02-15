/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LIBRARIAN_H__
#define __CLAP_LIBRARIAN_H__

#include "object.h"

enum res_type {
    RES_CONFIG = 0,
    RES_ASSET,
    RES_STATE,
    RES_SHADER,
};

enum res_state {
    RES_REQUESTED = 0,
    RES_LOADED,
    RES_ERROR,
};

struct lib_handle;
typedef void (*lib_complete_fn)(struct lib_handle *, void *);

typedef struct lib_handle {
    const char      *name;
    void            *buf;
    size_t          size;
    void            *data;
    struct ref      ref;
    enum res_type   type;
    enum res_state  state;
    lib_complete_fn func;
    bool            builtin;
} lib_handle;

DECLARE_CLEANUP(lib_handle);

int librarian_init(const char *dir);
struct lib_handle *
lib_request(enum res_type type, const char *name, lib_complete_fn cb, void *data);
char *lib_figure_uri(enum res_type type, const char *name);
struct lib_handle *lib_read_file(enum res_type type, const char *name, void **buf, size_t *szp);

#endif /* __CLAP_LIBRARIAN_H__ */
