// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"
#include "librarian.h"
#include "json.h"

#if defined(CONFIG_BROWSER) && 0
static char base_url[PATH_MAX] = "http://ukko.local/clap/";
#else
static char base_url[PATH_MAX] = "./";
#endif

#if defined(CONFIG_BROWSER) && 0
struct fetch_callback_data {
    char *buf;
    int   size;
};

static void fetch_config_onload(void *arg, void *buf, int size)
{
    /*f->buf = buf;
    f->size = size;
    dbg("buf: '%s'\n", buf);*/
}

static void _fetch_config_onload(struct lib_handle *h, void *data)
{
    fetch_config_onload(data, h->buf, h->size);
}

static void fetch_file(const char *name)
{
    emscripten_async_wget_data(name, NULL, fetch_config_onload, NULL);
}
#endif

static void handle_drop(struct ref *ref)
{
    struct lib_handle *h = container_of(ref, struct lib_handle, ref);

    free(h->buf);
}

DECLARE_REFCLASS_DROP(lib_handle, handle_drop);

char *lib_figure_uri(enum res_type type, const char *name)
{
    char *pfx[] = {
        [RES_CONFIG]    = "config",
        [RES_ASSET]     = "asset",
#if defined(CONFIG_GLES)
        [RES_SHADER]     = "asset/glsl-es",
#else
        [RES_SHADER]     = "asset/glsl",
#endif
#ifdef CONFIG_BROWSER
        [RES_STATE]     = "/settings",
#elif defined(_WIN32)
        [RES_STATE]     = getenv("LOCALAPPDATA"),
#else
        [RES_STATE]     = getenv("HOME"),
#endif
    };
    char *uri;
    int ret;

    const char *dot = "";
#if !defined(CONFIG_BROWSER) && !defined(_WIN32)
    if (type == RES_STATE)
        dot = ".";
#endif

    ret = asprintf(&uri, "%s%s/%s%s", type == RES_STATE ? "" : base_url, pfx[type],
                   dot, name);
#ifdef _WIN32
    int i;
    for (i = 0; i < ret; i++)
        if (uri[i] == '/')
            uri[i] = '\\';
#endif /* _WIN32 */
    return ret == -1 ? NULL : uri;
}

#if defined(CONFIG_BROWSER) && 0
static void lib_onload(void *arg, void *buf, int size)
{
    struct lib_handle *h = arg;

    h->buf = buf;
    h->size = size;
    h->state = RES_LOADED;
    ref_put(h);
    h->func(h, h->data);
}

struct lib_handle *lib_request(enum res_type type, const char *name, lib_complete_fn cb, void *data)
{
    struct lib_handle *h;
    LOCAL(char, uri);

    uri = lib_figure_uri(type, name);
    if (!uri)
        return NULL;
    h = ref_new(lib_handle);
    h->name = name;
    h->type = type;
    h->data = data;
    h->func = cb;
    h->state = RES_REQUESTED;

    h = ref_get(h); /* matches ref_put() in lib_onload() */
    emscripten_async_wget_data(uri, h, lib_onload, NULL);

    return h;
}
#else
struct lib_handle *lib_request(enum res_type type, const char *name, lib_complete_fn cb, void *data)
{
    struct lib_handle *h;
    LOCAL(char, uri);
    LOCAL(FILE, f);

    uri = lib_figure_uri(type, name);
    if (!uri)
        return NULL;

    h        = ref_new(lib_handle);
    h->name  = name;
    h->type  = type;
    h->data  = data;
    h->func  = cb;
    h->state = RES_REQUESTED;

    h = ref_get(h); /* matches ref_put() in the callback, which is mandatory */
    f = fopen(uri, "r");
    if (!f) {
        h->state = RES_ERROR;
    } else {
        struct stat st;

        fstat(fileno(f), &st);
        h->buf = malloc(st.st_size + 1);

        if (fread(h->buf, st.st_size, 1, f) != 1) {
            ref_put(h);
            return NULL;
        }

        *((char *)h->buf + st.st_size) = 0;
        h->size = st.st_size;
        h->state = RES_LOADED;
    }
    cb(h, data);

    return h;
}
#endif

void lib_release(struct lib_handle *h)
{
    free(h->data);
    ref_put(h);
}

void cleanup__lib_handlep(lib_handle **h)
{
    if (*h)
        ref_put_last(*h);
}

/*
 * How about:
 *  + first try the local FS, if unsuccessful,
 *  + fetch
 */
struct lib_handle *lib_read_file(enum res_type type, const char *name, void **bufp, size_t *szp)
{
    struct lib_handle *h;
    LOCAL(char, uri);
    LOCAL(FILE, f);
    int ret = 0;

    uri = lib_figure_uri(type, name);
    if (!uri)
        return NULL;

    h = ref_new(lib_handle);
    h->name = name;
    h->type = type;
    h->state = RES_REQUESTED;

    h = ref_get(h); /* matches ref_put() in lib_onload() */
    f = fopen(uri, "r");
    if (!f) {
        ret = -1;
        ref_put(h);
    } else {
        struct stat st;

        fstat(fileno(f), &st);
        h->buf = calloc(1, st.st_size + 1);
        *bufp = h->buf;

        if (fread(h->buf, st.st_size, 1, f) != 1) {
            ref_put(h);
            return NULL;
        }

        h->size = st.st_size;
        *szp = h->size;
        h->state = RES_LOADED;
    }
    ref_put(h);

    return ret ? NULL : h;
}

int librarian_init(const char *dir)
{
    if (dir && strlen(dir))
        strncpy(base_url, dir, PATH_MAX);

    return 0;
}
