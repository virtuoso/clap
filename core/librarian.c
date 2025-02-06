// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"
#include "cpio.h"
#include "librarian.h"
#include "librarian-file.h"
#include "json.h"
#include "memory.h"

extern const unsigned int nr_builtin_shaders;
extern const struct builtin_file builtin_shaders[];
static darray(struct builtin_file, builtin_assets);

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

    if (h->builtin)
        return;

    mem_free(h->buf);
}

DECLARE_REFCLASS_DROP(lib_handle, handle_drop);

/* XXX: horrible name, horrible hack */
static void windows_reslash(char *dest, const char *src, ssize_t len, bool forward)
{
    if (len < 0)
        len = strlen(src);

#ifdef _WIN32
    int i;
    for (i = 0; i < len; i++)
        if (!forward && src[i] == '/')
            dest[i] = '\\';
        else if (forward && src[i] == '\\')
            dest[i] = '/';
        else
            dest[i] = src[i];
    dest[i] = 0;
#else
    if (dest != src)
        strncpy(dest, src, len + 1);
#endif /* _WIN32 */
}

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

    const char *dot = "";
#if !defined(CONFIG_BROWSER) && !defined(_WIN32)
    if (type == RES_STATE)
        dot = ".";
#endif

    cres(int) res = mem_asprintf(&uri, "%s%s/%s%s", type == RES_STATE ? "" : base_url, pfx[type],
                                 dot, name);
    if (IS_CERR(res))
        return NULL;

    /* uri is both char *dest and const char *src, but the compiler doesn't know */
    windows_reslash(uri, uri, res.val, false);

    return uri;
}

static const char *builtin_file_contents(enum res_type type, const char *name, size_t *psize)
{
    int i;

    /* only for shaders at the moment */
    if (type == RES_SHADER) {
        const char *_name = str_basename(name);
        for (i = 0; i < nr_builtin_shaders; i++)
            if (!strcmp(_name, builtin_shaders[i].name))
                return builtin_shaders[i].contents;
    }

    struct builtin_file *file;
    char _name[PATH_MAX];
    windows_reslash(_name, name, -1, true);
    darray_for_each(file, builtin_assets)
        if (!strcmp(_name, file->name)) {
            *psize = file->size;
            return file->contents;
        }

    return NULL;
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
    size_t size;

    uri = lib_figure_uri(type, name);
    if (!uri)
        return NULL;

    char *_uri = uri + strlen(base_url);
    const char *builtin_contents = builtin_file_contents(type, _uri, &size);

    if (builtin_contents) {
        h = ref_new(lib_handle);
        h->name = name;
        h->data = data;
        h->type = type;
        h->state = RES_LOADED;
        h->buf = mem_alloc(size + 1, .zero = 1);
        memcpy(h->buf, builtin_contents, size);
        h->size = size ? size : strlen(builtin_contents);
        h->builtin = false;
        h = ref_get(h);
        cb(h, data);
        return h;
    }

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
        h->buf = mem_alloc(st.st_size + 1, .zero = 1);

        if (!h->buf || fread(h->buf, st.st_size, 1, f) != 1) {
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
    size_t size;
    int ret = 0;

    uri = lib_figure_uri(type, name);
    if (!uri)
        return NULL;

    char *_uri = uri + strlen(base_url);
    const char *builtin_contents = builtin_file_contents(type, _uri, &size);

    if (builtin_contents) {
        h = ref_new(lib_handle);
        h->name = name;
        h->type = type;
        h->state = RES_LOADED;
        h->buf = (void *)builtin_contents;
        *bufp = h->buf;
        h->size = size ? size : strlen(builtin_contents);
        *szp = h->size;
        h->builtin = true;
        return h;
    }

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
        h->buf = mem_alloc(st.st_size + 1, .zero = 1);
        *bufp = h->buf;

        if (!h->buf || fread(h->buf, st.st_size, 1, f) != 1) {
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

char rodata weak clap_asset_cpio_start;
char rodata weak clap_asset_cpio_end;

static void builtin_add_file(void *callback_data, const char *name, void *data, size_t size)
{
    struct darray *da = callback_data;
    struct builtin_file *new_file = _darray_add(da);

    new_file->name = name;
    new_file->size = size;
    new_file->contents = data;
}

int librarian_init(const char *dir)
{
    darray_init(builtin_assets);

    if (dir && strlen(dir))
        strncpy(base_url, dir, sizeof(base_url) - 1);

    if (&clap_asset_cpio_end - &clap_asset_cpio_start > 1) {
        cpio_context *cpio = cpio_open(.buf = &clap_asset_cpio_start,
                                       .buf_size = &clap_asset_cpio_end - &clap_asset_cpio_start,
                                       .add_file = builtin_add_file,
                                       .callback_data = &builtin_assets.da);
        cerr err = cpio_read(cpio);
        err_on(err != CERR_OK, "cpio_read() failed: %d\n", err);
        cpio_close(cpio);
    }

    return 0;
}
