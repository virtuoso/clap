// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include "ca2d.h"
#include "ca3d.h"
#include "clap.h"
#include "cpio.h"
#include "datatypes.h"
#include "draw.h"
#include "font.h"
#include "fs-ops.h"
#include "object.h"
#include "render.h"
#include "str.h"
#include "common.h"
#include "messagebus.h"
#include "interp.h"
#include "util.h"
#include "websocket.h"
#include "xyarray.h"
#include "test.h"

#define TEST_MAGIC0 0xdeadbeef

struct x0 {
    struct ref	ref;
    unsigned long magic;
};

static unsigned int failcount, dropcount;

static void reset_counters(void)
{
    failcount = 0;
    dropcount = 0;
}

static bool ok_counters(void)
{
    return !failcount && dropcount == 1;
}

static void test_drop(struct ref *ref)
{
    struct x0 *x0 = container_of(ref, struct x0, ref);

    if (x0->magic != TEST_MAGIC0) {
        failcount++;
        return;
    }

    dropcount++;
}

DEFINE_REFCLASS_INIT_OPTIONS(x0);
DEFINE_REFCLASS_DROP(x0, test_drop);
DECLARE_REFCLASS(x0);

static int refcount_test0(void)
{
    struct x0 *x0 = ref_new(x0);

    reset_counters();
    x0->magic = TEST_MAGIC0;
    ref_put(x0);
    if (!ok_counters())
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int refcount_test1(void)
{
    struct x0 *x0 = ref_new(x0);

    reset_counters();
    x0->magic = TEST_MAGIC0;
    x0 = ref_get(x0);
    if (x0->magic != TEST_MAGIC0)
        return EXIT_FAILURE;

    ref_put(x0);
    ref_put(x0);
    if (!ok_counters())
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int refcount_test2(void)
{
    static struct x0 xS = { .ref = REF_STATIC(x0), .magic = TEST_MAGIC0 };

    reset_counters();
    ref_put(&xS);
    if (dropcount || failcount)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

struct list_entry {
    struct list entry;
    unsigned int i;
};

#define LIST_MAX 10
static int list_test0(void)
{
    struct list_entry entries[LIST_MAX], *e;
    struct list       list;
    int               i;

    list_init(&list);
    for (i = 0; i < 10; i++) {
        entries[i].i = i;
        list_append(&list, &entries[i].entry);
    }

    i = 0;
    list_for_each_entry(e, &list, entry) {
        if (i != e->i)
            return EXIT_FAILURE;
        i++;
    }
    if (i != 10)
        return EXIT_FAILURE;

    e = list_first_entry(&list, struct list_entry, entry);
    if (e->i != 0)
        return EXIT_FAILURE;

    e = list_last_entry(&list, struct list_entry, entry);
    if (e->i != LIST_MAX - 1)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int list_test1(void)
{
    struct list_entry *entry, *e, *ie;
    struct list       list;
    int               i;

    list_init(&list);
    for (i = 0; i < 10; i++) {
        entry = mem_alloc(sizeof(*entry), .zero = 1);
        entry->i = i;
        list_append(&list, &entry->entry);
    }

    i = 0;
    list_for_each_entry_iter(e, ie, &list, entry) {
        if (i != e->i) {
            msg("%d <> %d\n", i, e->i);
            return EXIT_FAILURE;
        }
        i++;
    }
    if (i != 10)
        return EXIT_FAILURE;

    e = list_first_entry(&list, struct list_entry, entry);
    if (e->i != 0)
        return EXIT_FAILURE;

    e = list_last_entry(&list, struct list_entry, entry);
    if (e->i != LIST_MAX - 1)
        return EXIT_FAILURE;

    list_for_each_entry_iter(e, ie, &list, entry) {
        list_del(&e->entry);
        mem_free(e);
    }

    if (!list_empty(&list))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int darray_test0(void)
{
    darray(int, da);
    int i;

    darray_init(da);
    for (i = 0; i < 10; i++) {
        int *x = darray_add(da);
        *x = i;
    }

    if (da.da.nr_el != 10)
        return EXIT_FAILURE;

    if (da.x[5] != 5)
        return EXIT_FAILURE;

    darray_clearout(da);

    if (da.da.nr_el)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int darray_test1(void)
{
    darray(int, da);
    int i;

    darray_init(da);
    for (i = 0; i < 10; i++) {
        int *x = darray_add(da);
        *x = i;
    }

    int *x = darray_insert(da, 3);
    *x = -1;

    if (da.da.nr_el != 11)
        return EXIT_FAILURE;

    if (da.x[3] != -1)
        return EXIT_FAILURE;

    if (da.x[10] != 9)
        return EXIT_FAILURE;

    darray_clearout(da);

    if (da.da.nr_el)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int darray_test2(void)
{
    darray(int, da);
    int i;

    darray_init(da);
    for (i = 0; i < 10; i++) {
        int *x = darray_add(da);
        *x = i;
    }

    darray_delete(da, 3);

    if (da.x[3] != 4 || da.x[8] != 9)
        return EXIT_FAILURE;

    if (da.da.nr_el != 9)
        return EXIT_FAILURE;

    darray_delete(da, -1);

    if (da.da.nr_el != 8)
        return EXIT_FAILURE;

    if (da.x[7] != 8)
        return EXIT_FAILURE;

    darray_clearout(da);

    if (da.da.nr_el)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int darray_test3(void)
{
    darray(int, da);

    darray_init(da);
    for (int i = 0; i < 100; i++) {
        int *x = darray_add(da);
        *x = i;
    }

    /* the allocation runs ahead of the count, in doublings */
    if (da.da.nr_el != 100)             return EXIT_FAILURE;
    if (da.da.cap < da.da.nr_el)        return EXIT_FAILURE;

    size_t cap = da.da.cap;

    /* shrinking keeps the room */
    darray_resize(da, 10);
    if (da.da.nr_el != 10)              return EXIT_FAILURE;
    if (da.da.cap != cap)               return EXIT_FAILURE;

    /* and reused room comes back zeroed */
    darray_resize(da, 20);
    if (da.da.cap != cap)               return EXIT_FAILURE;
    if (da.x[9] != 9 || da.x[15])       return EXIT_FAILURE;

    /* trimming gives the unused room back */
    darray_trim(da);
    if (da.da.nr_el != 20)              return EXIT_FAILURE;
    if (da.da.cap != da.da.nr_el)       return EXIT_FAILURE;
    if (da.x[9] != 9 || da.x[15])       return EXIT_FAILURE;

    /* growing a trimmed array still works */
    int *x = darray_add(da);
    *x = 42;
    if (da.da.nr_el != 21)              return EXIT_FAILURE;
    if (da.da.cap < da.da.nr_el)        return EXIT_FAILURE;
    if (da.x[20] != 42)                 return EXIT_FAILURE;

    /* an emptied array only gives its allocation back when trimmed */
    darray_resize(da, 0);
    if (!da.x)                          return EXIT_FAILURE;

    darray_trim(da);
    if (da.x || da.da.cap)              return EXIT_FAILURE;

    darray_clearout(da);

    return EXIT_SUCCESS;
}

static int strtobool_test0(void)
{
    if (!strtobool("on"))   return EXIT_FAILURE;
    if (!strtobool("ON"))   return EXIT_FAILURE;
    if (!strtobool("yes"))  return EXIT_FAILURE;
    if (!strtobool("YES"))  return EXIT_FAILURE;
    if (!strtobool("true")) return EXIT_FAILURE;
    if (!strtobool("TRUE")) return EXIT_FAILURE;
    if (!strtobool("42"))   return EXIT_FAILURE;
    if (strtobool("0"))     return EXIT_FAILURE;
    if (strtobool("blah"))  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}


static int str_endswith_test0(void)
{
    if (!str_endswith("foo.txt", ".txt"))
        return EXIT_FAILURE;

    if (str_endswith("foo.TXT", ".txt"))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int str_endswith_nocase_test0(void)
{
    if (!str_endswith_nocase("foo.TXT", ".txt"))
        return EXIT_FAILURE;

    if (str_endswith_nocase("foo.txt", ".bin"))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int str_trim_slashes_test0(void)
{
    /* XXX: this will fail on windows, anyone is free to fix */
#ifndef _WIN32
    char buf[64] = "foo/bar///";

    str_trim_slashes(buf);
    if (strcmp(buf, "foo/bar"))
        return EXIT_FAILURE;

    strcpy(buf, "/");
    str_trim_slashes(buf);
    if (strcmp(buf, "/"))
        return EXIT_FAILURE;

    strcpy(buf, "//");
    str_trim_slashes(buf);
    if (strcmp(buf, "/"))
        return EXIT_FAILURE;
#endif /* _WIN32 */

    return EXIT_SUCCESS;
}

static int string_view_test0(void)
{
    const char test_const_arr[] = "const array", *test_const_ptr = "const pointer";
    char *empty = "";

    if (!str_is_const(test_const_ptr) ||
        !str_is_const(test_const_arr) ||
        !str_is_const("blah") ||
        str_is_const(empty))                        return EXIT_FAILURE;

    declare_sv(test_const_arr);
    if (sv(test_const_arr).end != strlen(test_const_arr) ||
        sv(test_const_arr).cap != 0)
        return EXIT_FAILURE;

    auto res = sv_append(&sv(test_const_arr), "clobber");
    if (!IS_CERR_CODE(res, CERR_INVALID_OPERATION)) return EXIT_FAILURE;

    declare_sv(test_const_ptr);
    if (sv(test_const_ptr).end != strlen(test_const_ptr) ||
        sv(test_const_ptr).cap != 0)
        return EXIT_FAILURE;

    res = sv_append(&sv(test_const_ptr), "clobber");
    if (!IS_CERR_CODE(res, CERR_INVALID_OPERATION)) return EXIT_FAILURE;

    declare_sv(empty);
    res = sv_append(&sv(empty), "clobber");
    if (!IS_CERR_CODE(res, CERR_TOO_LARGE))         return EXIT_FAILURE;

    char test_arr[] = "array";
    declare_sv(test_arr);
    if (sv(test_arr).end != strlen(test_arr) ||
        sv(test_arr).cap != sizeof(test_arr))       return EXIT_FAILURE;

    res = sv_append(&sv(test_arr), "ARRGH");
    if (!IS_CERR_CODE(res, CERR_TOO_LARGE))         return EXIT_FAILURE;

    sv_reset(&sv(test_arr));
    res = sv_append(&sv(test_arr), "ARRGH");
    if (IS_CERR(res) || strcmp(test_arr, "ARRGH"))  return EXIT_FAILURE;

    char *test_ptr = "pointer";
    declare_sv(test_ptr);
    if (sv(test_ptr).end != strlen(test_ptr) ||
        sv(test_ptr).cap != strlen(test_ptr) + 1)   return EXIT_FAILURE;

    res = sv_append(&sv(test_ptr), "ptr");
    if (!IS_CERR_CODE(res, CERR_TOO_LARGE))         return EXIT_FAILURE;

    // test_ptr is not const, but still points to rodata, therefore
    // not attempting a write that'd otherwise succeed

    LOCAL_SET(char, test_dyn) = strdup(test_ptr);
    declare_sv(test_dyn);
    if (sv(test_dyn).end != strlen(test_dyn) ||
        sv(test_dyn).cap != strlen(test_dyn) + 1)
        return EXIT_FAILURE;

    res = sv_append(&sv(test_dyn), "ptr");
    if (!IS_CERR_CODE(res, CERR_TOO_LARGE))         return EXIT_FAILURE;

    sv_reset(&sv(test_dyn));
    res = sv_append(&sv(test_dyn), "ptr");
    if (IS_CERR(res) || strcmp(test_dyn, "ptr"))    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int path_join_test0(void)
{
    /* XXX: this will fail on windows, anyone is free to fix */
#ifndef _WIN32
    char buf[64];

    cerr err = path_join(buf, sizeof(buf), "foo", "bar", "baz");
    if (IS_CERR(err))
        return EXIT_FAILURE;

    /* XXX: this will fail on windows, anyone is free to fix */
    if (strcmp(buf, "foo/bar/baz"))
        return EXIT_FAILURE;

    err = path_join(buf, sizeof(buf),  "/", "foo", "bar");
    if (IS_CERR(err))
        return EXIT_FAILURE;

    if (strcmp(buf, "/foo/bar"))
        return EXIT_FAILURE;
#endif /* _WIN32 */

    return EXIT_SUCCESS;
}

static int path_has_parent_test0(void)
{
    /* XXX: this will fail on windows, anyone is free to fix */
#ifndef _WIN32
    if (path_has_parent("foo"))
        return EXIT_FAILURE;

    if (!path_has_parent("foo/bar"))
        return EXIT_FAILURE;

    if (path_has_parent("/"))
        return EXIT_FAILURE;

    if (!path_has_parent("/foo"))
        return EXIT_FAILURE;

    /* trailing slashes are ignored */
    if (!path_has_parent("foo/bar/"))
        return EXIT_FAILURE;
#endif /* _WIN32 */

    return EXIT_SUCCESS;
}

static int path_parent_test0(void)
{
    /* XXX: this will fail on windows, anyone is free to fix */
#ifndef _WIN32
    char buf[64];
    cerr err;

    /* Basic parent calculation */
    CERR_RET(path_parent(buf, sizeof(buf), "foo/bar"), return EXIT_FAILURE);
    if (strcmp(buf, "foo"))
        return EXIT_FAILURE;

    /* Root should not have a parent */
    err = path_parent(buf, sizeof(buf), "/");
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))
        return EXIT_FAILURE;

    /* Simple file should not have a parent */
    err = path_parent(buf, sizeof(buf), "foo");
    if (!IS_CERR_CODE(err, CERR_NOT_FOUND))
        return EXIT_FAILURE;

    /* Trailing slashes should be ignored */
    CERR_RET(path_parent(buf, sizeof(buf), "foo/bar/"), return EXIT_FAILURE);
    if (strcmp(buf, "foo"))
        return EXIT_FAILURE;

    /* Absolute path parent */
    CERR_RET(path_parent(buf, sizeof(buf), "/foo"), return EXIT_FAILURE);
    if (strcmp(buf, "/"))
        return EXIT_FAILURE;

    /* Buffer too small (implementation copies full path first) */
    err = path_parent(buf, 4, "/foo");
    if (!IS_CERR_CODE(err, CERR_TOO_LARGE))
        return EXIT_FAILURE;
#endif /* _WIN32 */

    return EXIT_SUCCESS;
}

static int hashmap_test0(void)
{
    int ret = EXIT_FAILURE;
    struct hashmap hm;
    char *value;

    CERR_RET(hashmap_init(&hm, 256), return EXIT_FAILURE);
    CERR_RET(hashmap_insert(&hm, 0, "zero"), goto out);
    CERR_RET(hashmap_insert(&hm, 256, "one"), goto out);

    value = CRES_RET(hashmap_find(&hm, 0), goto out);
    if (strcmp(value, "zero"))
        goto out;

    value = CRES_RET(hashmap_find(&hm, 256), goto out);
    if (strcmp(value, "one"))
        goto out;

    ret = EXIT_SUCCESS;
out:
    hashmap_done(&hm);

    if (!list_empty(&hm.list) || hm.nr_buckets)
        return EXIT_FAILURE;

    return ret;
}

struct hashmap_test_data {
    unsigned long   value;
    bool            broken;
};

static void hashmap_cb(void *item, void *data)
{
    struct hashmap_test_data *pctx = data;
    unsigned long value = (uintptr_t)item;

    if (pctx->value + 1 != value)
        pctx->broken = true;
    pctx->value = value;
}

static int hashmap_test1(void)
{
    struct hashmap_test_data ctx = {};
    struct hashmap hm;
    long i, ret = EXIT_FAILURE;

    CERR_RET(hashmap_init(&hm, 256), return EXIT_FAILURE);

    for (i = 1; i < 1024; i++)
        CERR_RET(hashmap_insert(&hm, i, (void *)(uintptr_t)i), goto out);
    hashmap_for_each(&hm, hashmap_cb, &ctx);

    ret = EXIT_SUCCESS;
out:
    hashmap_done(&hm);

    if (ctx.broken)
        return EXIT_FAILURE;

    return ret;
}

static int bitmap_test0(void)
{
    struct bitmap b0, b1;

    bitmap_init(&b0, 64);
    if (b0.size != 64 / BITS_PER_LONG)
        return EXIT_FAILURE;

    bitmap_init(&b1, 128);
    if (b1.size != 128 / BITS_PER_LONG)
        return EXIT_FAILURE;

    bitmap_set(&b0, 0);
    bitmap_set(&b0, 1);
    bitmap_set(&b0, 2);
    if (!bitmap_is_set(&b0, 0) ||
        !bitmap_is_set(&b0, 1) ||
        !bitmap_is_set(&b0, 2))
        return EXIT_FAILURE;

    auto pos = CRES_RET(bitmap_set_lowest(&b0), return EXIT_FAILURE);
    if (pos != 3 || !bitmap_is_set(&b0, 3)) return EXIT_FAILURE;

    bitmap_set(&b1, 0);
    bitmap_set(&b1, 2);
    if (!bitmap_includes(&b0, &b1))
        return EXIT_FAILURE;

    if (bitmap_includes(&b1, &b0))
        return EXIT_FAILURE;

    pos = CRES_RET(bitmap_find_first_unset(&b1), return EXIT_FAILURE);
    if (pos != 1)   return EXIT_FAILURE;

    bitmap_clear(&b1, 0);
    bitmap_clear(&b1, 2);
    if (bitmap_is_set(&b1, 0) || bitmap_is_set(&b1, 2))
        return EXIT_FAILURE;

    bitmap_set(&b1, 120);

    pos = CRES_RET(bitmap_find_first_set(&b1), return EXIT_FAILURE);
    if (pos != 120)   return EXIT_FAILURE;

    bitmap_done(&b0);
    bitmap_done(&b1);
    return EXIT_SUCCESS;
}

static int ca3d_test0(void)
{
    static_assert(CA_RANGE(2, 4) == 12, "CA_RANGE() macro is broken");

    int ret = EXIT_SUCCESS;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand48(ts.tv_nsec);

    int i;
    for (i = 0; i < CA3D_MAX && ret == EXIT_SUCCESS; i++) {
        struct xyzarray *xyz = ca3d_make(16, 8, 4);
        ca3d_run(xyz, ca_coral, 4);
        if (verbose)
            xyzarray_print(xyz);

        if (!xyzarray_count(xyz))
            ret = EXIT_FAILURE;

        mem_free(xyz);
    }

    return ret;
}

static int ca2d_test0(void)
{
#define CA2D_SIDE 16
    const struct cell_automaton ca_test = {
        .name      = "test",
        .born_mask = 3 << 2,
        .surv_mask = 3 << 7,
        .nr_states = 4,
        .decay     = true,
        .neigh_2d  = ca2d_neigh_m1,
    };
    int ret = EXIT_SUCCESS;
    unsigned char *map;

    map = ca2d_generate(&ca_test, CA2D_SIDE, 5);
    if (verbose)
        xyarray_print(map);
    int x, y, count;
    for (y = 0, count = 0; y < CA2D_SIDE; y++)
        for (x = 0; x < CA2D_SIDE; x++)
            count += xyarray_get(map, x, y);

    if (!count)
        ret = EXIT_FAILURE;

    xyarray_free(map);

    return ret;
}

#define CPIO_TEST_FILE "cpio_test_file"
#define CPIO_TEST_STRING "cpio test string"
struct cpio_test_cb_data {
    int count;
};

static void __cpio_add_file(void *data, const char *name, void *buf, size_t size)
{
    struct cpio_test_cb_data *cbd = data;

    if (!strcmp(name, "cpio_test0")) {
        LOCAL_SET(char, str) = strndup(buf, size);
        if (!strcmp(str, "cpio_test0"))
            cbd->count++;
    } else if (!strcmp(name, CPIO_TEST_FILE)) {
        LOCAL_SET(char, str) = strndup(buf, size);
        if (!strcmp(str, CPIO_TEST_STRING))
            cbd->count++;
    }
}

static int cpio_test0(void)
{
    LOCAL_SET(FILE, f) = tmpfile();
    if (!f)
        return EXIT_FAILURE;

    LOCAL_SET(cpio_context, ctx) = cpio_open(.write = true, .file = f);
    if (!ctx)
        return EXIT_FAILURE;

    cerr err = cpio_write(ctx, __func__, (void *)__func__, sizeof(__func__));
    if (IS_CERR(err))
        return EXIT_FAILURE;

    err = cpio_write(ctx, CPIO_TEST_FILE, CPIO_TEST_STRING, sizeof(CPIO_TEST_STRING));
    if (IS_CERR(err))
        return EXIT_FAILURE;

    cpio_close(ctx);
    ctx = NULL;
    fseek(f, 0, SEEK_SET);

    struct cpio_test_cb_data cbd = {};
    ctx = cpio_open(.file = f, .add_file = __cpio_add_file, .callback_data = &cbd );
    if (!ctx)
        return EXIT_FAILURE;

    err = cpio_read(ctx);
    if (IS_CERR(err))
        return EXIT_FAILURE;

    return cbd.count == 2 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const struct fs_ops *fs_test_ops = &fs_ops_posix;
static char fs_test_dir[PATH_MAX];

static int fs_test_setup(void)
{
    char cwd[PATH_MAX];
    CERR_RET(fs_get_cwd(fs_test_ops, cwd), return EXIT_FAILURE);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand48(ts.tv_nsec);

    char test_file[PATH_MAX];
    snprintf(test_file, sizeof(test_file), "fs_test_%ld", lrand48());
    path_join(fs_test_dir, sizeof(fs_test_dir), cwd, test_file);

    {
        cerr err = fs_remove_dir(fs_test_ops, fs_test_dir);
        if (!IS_CERR(err)) return EXIT_FAILURE;
    }
    CERR_RET(fs_make_dir(fs_test_ops, fs_test_dir), return EXIT_FAILURE);
    return EXIT_SUCCESS;
}

static int fs_test_dir_iter(void)
{
    LOCAL_SET(fs_dir, dir) = CRES_RET(fs_open_dir(fs_test_ops, fs_test_dir), return EXIT_FAILURE);
    struct fs_dirent ent;
    int count = 0;
    for (;;) {
        cerr r = fs_read_dir(dir, &ent);
        if (IS_CERR_CODE(r, CERR_EOF)) break;
        CERR_RET(r, return EXIT_FAILURE);
        count++;
    }
    return EXIT_SUCCESS;
}

static int fs_test_text_rw(void)
{
    char file[PATH_MAX];
    path_join(file, sizeof(file), fs_test_dir, "test_text.txt");
    const char *text = "Hello World\nLine 2";

    /* Write text */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_WRITE, true, false, false), return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, text, strlen(text)), return EXIT_FAILURE) != strlen(text)) return EXIT_FAILURE;
    }

    /* Read text */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_READ, false, false, false), return EXIT_FAILURE);
        char buf[64] = {0};
        size_t sz = CRES_RET(fs_read(f, buf, sizeof(buf) - 1), return EXIT_FAILURE);
        if (sz != strlen(text)) return EXIT_FAILURE;
        if (strcmp(buf, text)) return EXIT_FAILURE;
    }
    remove(file);
    return EXIT_SUCCESS;
}

static int fs_test_append(void)
{
    char file[PATH_MAX];
    path_join(file, sizeof(file), fs_test_dir, "test_append.txt");
    const char *text = "Base";
    const char *app = "Append";

    /* Write base */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_WRITE, true, false, false), return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, text, strlen(text)), return EXIT_FAILURE) != strlen(text)) return EXIT_FAILURE;
    }
    /* Append */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_APPEND, false, false, false), return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, app, strlen(app)), return EXIT_FAILURE) != strlen(app)) return EXIT_FAILURE;
    }
    /* Read verify */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_READ, false, false, false), return EXIT_FAILURE);
        char buf[64] = {0};
        size_t sz = CRES_RET(fs_read(f, buf, sizeof(buf) - 1), return EXIT_FAILURE);
        if (sz != strlen(text) + strlen(app)) return EXIT_FAILURE;
        if (strncmp(buf, "BaseAppend", 10)) return EXIT_FAILURE;
    }
    remove(file);
    return EXIT_SUCCESS;
}

static int fs_test_binary_rw(void)
{
    char file[PATH_MAX];
    path_join(file, sizeof(file), fs_test_dir, "test_bin.bin");
    unsigned char data[] = { 0x00, 0xFF, 0x10, 0x0A, 0x0D };

    /* Write binary */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_WRITE, true, false, true), return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, data, sizeof(data)), return EXIT_FAILURE) != sizeof(data)) return EXIT_FAILURE;
    }
    /* Read binary */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_READ, false, false, true), return EXIT_FAILURE);
        unsigned char buf[64];
        size_t sz = CRES_RET(fs_read(f, buf, sizeof(buf)), return EXIT_FAILURE);
        if (sz != sizeof(data)) return EXIT_FAILURE;
        if (memcmp(buf, data, sizeof(data))) return EXIT_FAILURE;
    }
    remove(file);
    return EXIT_SUCCESS;
}

static int fs_test_seek(void)
{
    char file[PATH_MAX];
    path_join(file, sizeof(file), fs_test_dir, "test_seek.txt");

    /* Write and seek */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_BOTH, true, false, false), return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, "0123456789", 10), return EXIT_FAILURE) != 10) return EXIT_FAILURE;

        CERR_RET(fs_seek(f, 0, FS_SEEK_SET), return EXIT_FAILURE);
        char buf[5];
        if (CRES_RET(fs_read(f, buf, 5), return EXIT_FAILURE) != 5) return EXIT_FAILURE;
        if (strncmp(buf, "01234", 5)) return EXIT_FAILURE;

        CERR_RET(fs_seek(f, 2, FS_SEEK_SET), return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, "AB", 2), return EXIT_FAILURE) != 2) return EXIT_FAILURE;
    }
    /* Read verify */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_READ, false, false, false), return EXIT_FAILURE);
        char buf[64] = {0};
        CRES_RET(fs_read(f, buf, sizeof(buf) - 1), return EXIT_FAILURE);
        if (strncmp(buf, "01AB456789", 10)) return EXIT_FAILURE;
    }
    remove(file);
    return EXIT_SUCCESS;
}

#ifdef HAVE_FSWATCH
/* Poll (without sleeping) for up to ~2s, OR-ing the flags reported for @name; @nr counts its events */
static unsigned int fs_test_watch_drain(fs_watch *w, const char *name, int *nr)
{
    unsigned int seen = 0;

    for (int i = 0; i < 20; i++) {
        cerr pr = fs_watch_poll(&w, 1, 100);
        if (IS_CERR_CODE(pr, CERR_EOF)) {
            /* a batch came through and nothing more is queued; otherwise keep waiting */
            if (seen)   break;
            continue;
        }
        if (IS_CERR(pr))    break;

        struct fs_watch_event ev;
        for (;;) {
            cerr rr = fs_watch_read(w, &ev);
            if (IS_CERR_CODE(rr, CERR_EOF))     break;
            if (IS_CERR(rr))                    return seen;
            if (strcmp(ev.name, name))          continue;

            seen |= ev.flags;
            if (nr)     (*nr)++;
        }
    }

    return seen;
}

static int fs_test_watch(void)
{
    LOCAL_SET(fs_watch, w) = CRES_RET(fs_watch_open(&fs_watch_ops_native, fs_test_dir),
                                      return EXIT_FAILURE);

    char file[PATH_MAX];
    path_join(file, sizeof(file), fs_test_dir, "watched.txt");

    unsigned int seen = 0;

    /* create */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_WRITE, true, false, false),
                                         return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, "a", 1), return EXIT_FAILURE) != 1)    return EXIT_FAILURE;
    }
    int nr = 0;
    seen |= fs_test_watch_drain(w, "watched.txt", &nr);
#ifdef __linux__
    /* inotify: the create and the close-write are queued together and come out merged */
    if (nr != 1)                        return EXIT_FAILURE;
#endif /* __linux__ */

    /* modify */
    {
        LOCAL_SET(fs_file, f) = CRES_RET(fs_open(fs_test_ops, file, FS_APPEND, false, false, false),
                                         return EXIT_FAILURE);
        if (CRES_RET(fs_write(f, "b", 1), return EXIT_FAILURE) != 1)    return EXIT_FAILURE;
    }
    seen |= fs_test_watch_drain(w, "watched.txt", NULL);

    /* delete */
    remove(file);
    seen |= fs_test_watch_drain(w, "watched.txt", NULL);

    /* Backends coalesce, so assert the union of what was observed */
    if (!(seen & FS_WATCH_CREATED))     return EXIT_FAILURE;
    if (!(seen & FS_WATCH_MODIFIED))    return EXIT_FAILURE;
    if (!(seen & FS_WATCH_DELETED))     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
#endif /* HAVE_FSWATCH */

static int fs_test_teardown(void)
{
    CERR_RET(fs_remove_dir(fs_test_ops, fs_test_dir), return EXIT_FAILURE);
    return EXIT_SUCCESS;
}

static bool vec4_eq(vec4 a, vec4 b, float eps)
{
    for (int i = 0; i < 4; i++)
        if (fabsf(a[i] - b[i]) > eps)
            return false;
    return true;
}

#define VEC4_EQ_F32(a, b) vec4_eq(a, b, 1e-5f)
#define VEC4_EQ_U8(a, b)  vec4_eq(a, b, 1.0f / 255.0f + 1e-5f)

static int canvas_test_new(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 4, 4), return EXIT_FAILURE);
    if (c->width != 4 || c->height != 4)
        return EXIT_FAILURE;
    if (canvas_size(c) != 4 * 4 * 4)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int canvas_test_write_read_rgba8(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 4, 4), return EXIT_FAILURE);
    vec4 color = { 1.0f, 0.5f, 0.25f, 1.0f };
    canvas_write(c, .x = 2, .y = 3, .color = color);

    vec4 out = {};
    canvas_read(c, 2, 3, out);
    if (!VEC4_EQ_U8(color, out))
        return EXIT_FAILURE;

    /* untouched pixel should be zero */
    canvas_read(c, 0, 0, out);
    vec4 zero = {};
    if (!VEC4_EQ_U8(zero, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_write_read_rgba32f(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 4, 4), return EXIT_FAILURE);
    vec4 color = { 2.0f, 0.5f, 0.125f, 1.0f };
    canvas_write(c, .x = 1, .y = 1, .color = color);

    vec4 out = {};
    canvas_read(c, 1, 1, out);
    if (!VEC4_EQ_F32(color, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_fill(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 3, 3), return EXIT_FAILURE);
    vec4 color = { 0.0f, 1.0f, 0.0f, 1.0f };
    canvas_fill(c, .color = color);

    for (unsigned int y = 0; y < 3; y++)
        for (unsigned int x = 0; x < 3; x++) {
            vec4 out = {};
            canvas_read(c, x, y, out);
            if (!VEC4_EQ_U8(color, out))
                return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

static int canvas_test_fill_rgba32f(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 3, 3), return EXIT_FAILURE);
    vec4 color = { 3.0f, 1.5f, 0.0f, 1.0f };
    canvas_fill(c, .color = color);

    for (unsigned int y = 0; y < 3; y++)
        for (unsigned int x = 0; x < 3; x++) {
            vec4 out = {};
            canvas_read(c, x, y, out);
            if (!VEC4_EQ_F32(color, out))
                return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

static int canvas_test_blit(void)
{
    LOCAL_SET(canvas, dst) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 8, 8), return EXIT_FAILURE);
    LOCAL_SET(canvas, src) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 2, 2), return EXIT_FAILURE);

    vec4 red = { 1.0f, 0.0f, 0.0f, 1.0f };
    canvas_fill(src, .color = red);
    canvas_blit(dst, src, .x = 3, .y = 3);

    /* blitted region should be red */
    for (unsigned int y = 3; y < 5; y++)
        for (unsigned int x = 3; x < 5; x++) {
            vec4 out = {};
            canvas_read(dst, x, y, out);
            if (!VEC4_EQ_U8(red, out))
                return EXIT_FAILURE;
        }

    /* outside the blitted region should be zero */
    vec4 zero = {};
    vec4 out = {};
    canvas_read(dst, 0, 0, out);
    if (!VEC4_EQ_U8(zero, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_blit_color(void)
{
    LOCAL_SET(canvas, dst) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 4, 4), return EXIT_FAILURE);
    LOCAL_SET(canvas, src) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 2, 2), return EXIT_FAILURE);

    vec4 white = { 1.0f, 1.0f, 1.0f, 1.0f };
    canvas_fill(src, .color = white);

    float tint[] = { 1.0f, 0.5f, 0.0f, 1.0f };
    canvas_blit(dst, src, .color = tint);

    vec4 expected = { 1.0f, 0.5f, 0.0f, 1.0f };
    vec4 out = {};
    canvas_read(dst, 0, 0, out);
    if (!VEC4_EQ_U8(expected, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_blit_cross_format(void)
{
    LOCAL_SET(canvas, dst) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 4, 4), return EXIT_FAILURE);
    LOCAL_SET(canvas, src) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 2, 2), return EXIT_FAILURE);

    vec4 color = { 0.5f, 0.25f, 0.75f, 1.0f };
    canvas_fill(src, .color = color);
    canvas_blit(dst, src, .x = 1, .y = 1);

    vec4 out = {};
    canvas_read(dst, 1, 1, out);
    if (!VEC4_EQ_U8(color, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_write_read_rgba16f(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA16F, 4, 4), return EXIT_FAILURE);
    vec4 color = { 2.0f, 0.5f, 0.125f, 1.0f };
    canvas_write(c, .x = 1, .y = 2, .color = color);

    vec4 out = {};
    canvas_read(c, 1, 2, out);
    if (!VEC4_EQ_F32(color, out))
        return EXIT_FAILURE;

    /* untouched pixel should be zero */
    canvas_read(c, 0, 0, out);
    vec4 zero = {};
    if (!VEC4_EQ_F32(zero, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_fill_rgba16f(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA16F, 3, 3), return EXIT_FAILURE);
    vec4 color = { 1.5f, 0.25f, 0.0f, 1.0f };
    canvas_fill(c, .color = color);

    for (unsigned int y = 0; y < 3; y++)
        for (unsigned int x = 0; x < 3; x++) {
            vec4 out = {};
            canvas_read(c, x, y, out);
            if (!VEC4_EQ_F32(color, out))
                return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

static int canvas_test_blit_rgba16f_to_rgba8(void)
{
    LOCAL_SET(canvas, dst) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 4, 4), return EXIT_FAILURE);
    LOCAL_SET(canvas, src) = CRES_RET(canvas_new(TEX_FMT_RGBA16F, 2, 2), return EXIT_FAILURE);

    vec4 color = { 0.5f, 0.25f, 0.75f, 1.0f };
    canvas_fill(src, .color = color);
    canvas_blit(dst, src);

    vec4 out = {};
    canvas_read(dst, 0, 0, out);
    if (!VEC4_EQ_U8(color, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_blit_rgba8_to_rgba16f(void)
{
    LOCAL_SET(canvas, dst) = CRES_RET(canvas_new(TEX_FMT_RGBA16F, 4, 4), return EXIT_FAILURE);
    LOCAL_SET(canvas, src) = CRES_RET(canvas_new(TEX_FMT_RGBA8, 2, 2), return EXIT_FAILURE);

    vec4 color = { 1.0f, 0.0f, 0.5f, 1.0f };
    canvas_fill(src, .color = color);
    canvas_blit(dst, src, .x = 1, .y = 1);

    vec4 out = {};
    canvas_read(dst, 1, 1, out);
    if (!VEC4_EQ_U8(color, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_blit_rgba16f_to_rgba32f(void)
{
    LOCAL_SET(canvas, dst) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 4, 4), return EXIT_FAILURE);
    LOCAL_SET(canvas, src) = CRES_RET(canvas_new(TEX_FMT_RGBA16F, 2, 2), return EXIT_FAILURE);

    vec4 color = { 2.0f, 0.5f, 0.125f, 1.0f };
    canvas_fill(src, .color = color);
    canvas_blit(dst, src);

    vec4 out = {};
    canvas_read(dst, 0, 0, out);
    if (!VEC4_EQ_F32(color, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_blit_rgba32f_to_rgba16f(void)
{
    LOCAL_SET(canvas, dst) = CRES_RET(canvas_new(TEX_FMT_RGBA16F, 4, 4), return EXIT_FAILURE);
    LOCAL_SET(canvas, src) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 2, 2), return EXIT_FAILURE);

    vec4 color = { 1.5f, 0.25f, 0.75f, 1.0f };
    canvas_fill(src, .color = color);
    canvas_blit(dst, src);

    vec4 out = {};
    canvas_read(dst, 0, 0, out);
    if (!VEC4_EQ_F32(color, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_blend_write(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 2, 2), return EXIT_FAILURE);

    /* write opaque red background */
    vec4 bg = { 1.0f, 0.0f, 0.0f, 1.0f };
    canvas_write(c, .color = bg);

    /* blend 50% alpha green on top: src*src_alpha + dst*(1-src_alpha) */
    vec4 fg = { 0.0f, 1.0f, 0.0f, 0.5f };
    canvas_write(c, .color = fg,
        .src_blend = BLEND_SRC_ALPHA, .dst_blend = BLEND_ONE_MINUS_SRC_ALPHA);

    vec4 expected = { 0.5f, 0.5f, 0.0f, 0.75f };
    vec4 out = {};
    canvas_read(c, 0, 0, out);
    if (!VEC4_EQ_F32(expected, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int canvas_test_blend_fill(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 3, 3), return EXIT_FAILURE);

    /* fill with opaque blue */
    vec4 bg = { 0.0f, 0.0f, 1.0f, 1.0f };
    canvas_fill(c, .color = bg);

    /* blend-fill with 50% alpha white */
    vec4 fg = { 1.0f, 1.0f, 1.0f, 0.5f };
    canvas_fill(c, .color = fg,
        .src_blend = BLEND_SRC_ALPHA, .dst_blend = BLEND_ONE_MINUS_SRC_ALPHA);

    vec4 expected = { 0.5f, 0.5f, 1.0f, 0.75f };
    for (unsigned int y = 0; y < 3; y++)
        for (unsigned int x = 0; x < 3; x++) {
            vec4 out = {};
            canvas_read(c, x, y, out);
            if (!VEC4_EQ_F32(expected, out))
                return EXIT_FAILURE;
        }

    return EXIT_SUCCESS;
}

static int canvas_test_blend_none(void)
{
    LOCAL_SET(canvas, c) = CRES_RET(canvas_new(TEX_FMT_RGBA32F, 2, 2), return EXIT_FAILURE);

    /* write red, then overwrite with green (no blending) */
    vec4 red = { 1.0f, 0.0f, 0.0f, 1.0f };
    canvas_write(c, .color = red);

    vec4 green = { 0.0f, 1.0f, 0.0f, 1.0f };
    canvas_write(c, .color = green);

    vec4 out = {};
    canvas_read(c, 0, 0, out);
    if (!VEC4_EQ_F32(green, out))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int tex_fmt_needs_alpha_test(void)
{
    if (!texture_format_needs_alpha(TEX_FMT_RGB8))      return EXIT_FAILURE;
    if (!texture_format_needs_alpha(TEX_FMT_RGB8_SRGB)) return EXIT_FAILURE;
    if (!texture_format_needs_alpha(TEX_FMT_RGB16F))    return EXIT_FAILURE;
    if (!texture_format_needs_alpha(TEX_FMT_RGB32F))    return EXIT_FAILURE;
    if (texture_format_needs_alpha(TEX_FMT_RGBA8))      return EXIT_FAILURE;
    if (texture_format_needs_alpha(TEX_FMT_R8))         return EXIT_FAILURE;
    if (texture_format_needs_alpha(TEX_FMT_DEPTH32F))   return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int tex_fmt_add_alpha_test(void)
{
    if (texture_format_add_alpha(TEX_FMT_RGB8)      != TEX_FMT_RGBA8)      return EXIT_FAILURE;
    if (texture_format_add_alpha(TEX_FMT_RGB8_SRGB) != TEX_FMT_RGBA8_SRGB) return EXIT_FAILURE;
    if (texture_format_add_alpha(TEX_FMT_RGB16F)    != TEX_FMT_RGBA16F)    return EXIT_FAILURE;
    if (texture_format_add_alpha(TEX_FMT_RGB32F)    != TEX_FMT_RGBA32F)    return EXIT_FAILURE;
    if (texture_format_add_alpha(TEX_FMT_RGBA8)     != TEX_FMT_RGBA8)      return EXIT_FAILURE;
    if (texture_format_add_alpha(TEX_FMT_R8)        != TEX_FMT_R8)         return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int tex_rgb_to_rgba_8bit_test(void)
{
    uint8_t rgb[] = { 0xFF, 0x00, 0x80, 0x10, 0x20, 0x30 }; /* 2 pixels */
    size_t out_size;
    LOCAL_SET(void, rgba) = texture_rgb_to_rgba(TEX_FMT_RGB8, rgb, 2, &out_size);
    if (!rgba || out_size != 8)                     return EXIT_FAILURE;
    uint8_t *p = rgba;
    if (p[0] != 0xFF || p[1] != 0x00 || p[2] != 0x80 || p[3] != 0xFF) return EXIT_FAILURE;
    if (p[4] != 0x10 || p[5] != 0x20 || p[6] != 0x30 || p[7] != 0xFF) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int tex_rgb_to_rgba_32f_test(void)
{
    float rgb[] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.2f, 0.3f }; /* 2 pixels */
    size_t out_size;
    LOCAL_SET(void, rgba) = texture_rgb_to_rgba(TEX_FMT_RGB32F, rgb, 2, &out_size);
    if (!rgba || out_size != 32)                    return EXIT_FAILURE;
    float *p = rgba;
    if (p[0] != 1.0f || p[1] != 0.5f || p[2] != 0.25f || p[3] != 1.0f) return EXIT_FAILURE;
    if (p[4] != 0.1f || p[5] != 0.2f || p[6] != 0.3f  || p[7] != 1.0f) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int tex_rgb_to_rgba_16f_test(void)
{
    /* 0x3C00 = half-float 1.0, 0x0000 = 0.0, 0x3800 = 0.5 */
    uint16_t rgb[] = { 0x3C00, 0x0000, 0x3800, 0x3C00, 0x3C00, 0x3C00 }; /* 2 pixels */
    size_t out_size;
    LOCAL_SET(void, rgba) = texture_rgb_to_rgba(TEX_FMT_RGB16F, rgb, 2, &out_size);
    if (!rgba || out_size != 16)                    return EXIT_FAILURE;
    uint16_t *p = rgba;
    if (p[0] != 0x3C00 || p[1] != 0x0000 || p[2] != 0x3800 || p[3] != 0x3C00) return EXIT_FAILURE;
    if (p[4] != 0x3C00 || p[5] != 0x3C00 || p[6] != 0x3C00 || p[7] != 0x3C00) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

static int ua_parse_desktop_test(void)
{
    clap_os_info osi = {};

    /* Safari on macOS */
    clap_os_parse_user_agent(&osi,
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 "
        "(KHTML, like Gecko) Version/17.4 Safari/605.1.15");
    if (osi.os != CLAP_OS_MACOS || osi.browser != CLAP_BROWSER_SAFARI)  return EXIT_FAILURE;
    if (osi.os_version.major != 10 || osi.os_version.minor != 15 ||
        osi.os_version.patch != 7)                                      return EXIT_FAILURE;
    if (osi.browser_version.major != 17 || osi.browser_version.minor != 4) return EXIT_FAILURE;
    if (osi.webkit_version.major != 605 || osi.webkit_version.minor != 1 ||
        osi.webkit_version.patch != 15)                                 return EXIT_FAILURE;
    if (osi.mobile)                                                     return EXIT_FAILURE;

    /* Chrome on Linux: not Safari despite the trailing Safari/ token */
    osi = (clap_os_info){};
    clap_os_parse_user_agent(&osi,
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.6478.61 Safari/537.36");
    if (osi.os != CLAP_OS_LINUX || osi.browser != CLAP_BROWSER_CHROME)  return EXIT_FAILURE;
    if (osi.browser_version.major != 126 || osi.browser_version.patch != 6478) return EXIT_FAILURE;
    if (osi.webkit_version.major != 537 || osi.webkit_version.minor != 36) return EXIT_FAILURE;

    /* Firefox on Windows */
    osi = (clap_os_info){};
    clap_os_parse_user_agent(&osi,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) "
        "Gecko/20100101 Firefox/128.0");
    if (osi.os != CLAP_OS_WINDOWS || osi.browser != CLAP_BROWSER_FIREFOX) return EXIT_FAILURE;
    if (osi.os_version.major != 10 || osi.os_version.minor != 0)        return EXIT_FAILURE;
    if (osi.browser_version.major != 128)                               return EXIT_FAILURE;
    if (osi.webkit_version.major)                                       return EXIT_FAILURE;

    /* Edge: Chromium-derived, but not reported as Chrome */
    osi = (clap_os_info){};
    clap_os_parse_user_agent(&osi,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36 Edg/126.0.2592.56");
    if (osi.browser != CLAP_BROWSER_OTHER)                              return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ua_parse_mobile_test(void)
{
    clap_os_info osi = {};

    /* Safari on iPhone */
    clap_os_parse_user_agent(&osi,
        "Mozilla/5.0 (iPhone; CPU iPhone OS 17_4_1 like Mac OS X) "
        "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4.1 "
        "Mobile/15E148 Safari/604.1");
    if (osi.os != CLAP_OS_IOS || osi.browser != CLAP_BROWSER_SAFARI)    return EXIT_FAILURE;
    if (osi.os_version.major != 17 || osi.os_version.minor != 4 ||
        osi.os_version.patch != 1)                                      return EXIT_FAILURE;
    if (!osi.mobile)                                                    return EXIT_FAILURE;

    /* Chrome on Android */
    osi = (clap_os_info){};
    clap_os_parse_user_agent(&osi,
        "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.6478.71 Mobile Safari/537.36");
    if (osi.os != CLAP_OS_ANDROID || osi.browser != CLAP_BROWSER_CHROME) return EXIT_FAILURE;
    if (osi.os_version.major != 14)                                     return EXIT_FAILURE;
    if (!osi.mobile)                                                    return EXIT_FAILURE;

    /* garbage and NULL don't misdetect or crash */
    osi = (clap_os_info){};
    clap_os_parse_user_agent(&osi, NULL);
    clap_os_parse_user_agent(&osi, "curl/8.5.0");
    if (osi.os != CLAP_OS_UNKNOWN || osi.browser != CLAP_BROWSER_OTHER) return EXIT_FAILURE;
    if (osi.mobile)                                                     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

#ifdef CONFIG_NETWORKING
static int http_header_find_test(void)
{
    static const char req[] =
        "GET /chat HTTP/1.1\r\n"
        "Host: server.example.com\r\n"
        "Upgrade: websocket\r\n"
        "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Protocol: binary, base64\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    const char *end = req + sizeof(req) - 1;
    size_t vlen;
    const char *v;

    /* name match is case-insensitive */
    v = http_header_find(req, end, "Sec-WebSocket-Key", &vlen);
    if (!v || vlen != 24 || strncmp(v, "dGhlIHNhbXBsZSBub25jZQ==", vlen))   return EXIT_FAILURE;

    /* the value ends at its own line ending even for the last header */
    v = http_header_find(req, end, "Sec-WebSocket-Version", &vlen);
    if (!v || vlen != 2 || strncmp(v, "13", vlen))                          return EXIT_FAILURE;

    v = http_header_find(req, end, "Sec-WebSocket-Protocol", &vlen);
    if (!v || vlen != 14 || strncmp(v, "binary, base64", vlen))             return EXIT_FAILURE;

    /* name prefixes and absent headers don't match */
    if (http_header_find(req, end, "Sec-WebSocket", &vlen))                 return EXIT_FAILURE;
    if (http_header_find(req, end, "Origin", &vlen))                        return EXIT_FAILURE;

    /* a name truncated by the buffer end doesn't match either */
    static const char cut[] = "GET / HTTP/1.1\r\nSec-WebSocket-Key";
    if (http_header_find(cut, cut + sizeof(cut) - 3, "Sec-WebSocket-Key", &vlen))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int http_headers_end_test(void)
{
    static const char req[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\nBODY";
    const uint8_t *p = (const uint8_t *)req;

    const uint8_t *e = http_headers_end(p, sizeof(req) - 1);
    if (!e || memcmp(e, "BODY", 4))             return EXIT_FAILURE;

    /* incomplete block: no verdict yet */
    if (http_headers_end(p, sizeof(req) - 6))   return EXIT_FAILURE;
    if (http_headers_end(p, 3))                 return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_codec_roundtrip(size_t size)
{
    LOCAL_SET(uchar, payload) = mem_alloc(size + 1, .zero = 1);
    LOCAL(uchar, frame);
    LOCAL(uchar, out);
    size_t framesz, outsz;
    int op = -1;

    if (!payload)   return EXIT_FAILURE;

    for (size_t i = 0; i < size; i++)
        payload[i] = (uchar)(i * 7 + 1);

    ws_encode(payload, size, &frame, &framesz, WSOP_BIN);

    cres(size_t) dres = ws_decode(frame, framesz, &out, &outsz, &op, false);
    if (IS_CERR(dres) || dres.val != framesz)           return EXIT_FAILURE;
    if (outsz != size || op != WSOP_BIN)                return EXIT_FAILURE;
    if (size && memcmp(out, payload, size))             return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_codec_roundtrip_test(void)
{
    static const size_t sizes[] = { 0, 1, 2, 4, 63, 124, 125, 126, 300,
                                    4096, 65535, 65536, 70000 };

    for (int i = 0; i < array_size(sizes); i++)
        if (ws_codec_roundtrip(sizes[i]))   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_decode_masked_test(void)
{
    static const uint8_t mask[] = { 0x11, 0x22, 0x33, 0x44 };
    static const char msg[] = "clap";
    size_t msglen = sizeof(msg) - 1;
    uint8_t frame[2 + sizeof(mask) + sizeof(msg) - 1];
    LOCAL(uchar, out);
    size_t outsz;
    int op = -1;

    frame[0] = 0x82;                /* FIN | binary opcode */
    frame[1] = 0x80 | msglen;       /* MASK | payload length */
    memcpy(&frame[2], mask, sizeof(mask));
    for (size_t i = 0; i < msglen; i++)
        frame[2 + sizeof(mask) + i] = msg[i] ^ mask[i % sizeof(mask)];

    cres(size_t) dres = ws_decode(frame, sizeof(frame), &out, &outsz, &op, true);
    if (IS_CERR(dres) || dres.val != sizeof(frame))     return EXIT_FAILURE;
    if (outsz != msglen || op != WSOP_BIN)              return EXIT_FAILURE;
    if (memcmp(out, msg, msglen))                       return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_decode_short_one(uint8_t *frame, size_t size)
{
    uint8_t *out = NULL;
    size_t outsz;
    int op = -1;

    cres(size_t) dres = ws_decode(frame, size, &out, &outsz, &op, false);
    return IS_CERR_CODE(dres, CERR_BUFFER_INCOMPLETE) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int ws_decode_short_test(void)
{
    uint8_t claims5[]  = { 0x82, 0x05, 'c' };           /* 5 payload bytes promised, 1 present */
    uint8_t len16cut[] = { 0x82, 126, 0x01 };           /* 16-bit length cut short */
    uint8_t len64cut[] = { 0x82, 127, 0, 0, 0 };        /* 64-bit length cut short */
    uint8_t maskcut[]  = { 0x82, 0x81, 0x11, 0x22 };    /* masking key cut short */

    if (ws_decode_short_one(claims5, sizeof(claims5)))      return EXIT_FAILURE;
    if (ws_decode_short_one(claims5, 1))                    return EXIT_FAILURE;
    if (ws_decode_short_one(len16cut, sizeof(len16cut)))    return EXIT_FAILURE;
    if (ws_decode_short_one(len64cut, sizeof(len64cut)))    return EXIT_FAILURE;
    if (ws_decode_short_one(maskcut, sizeof(maskcut)))      return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_encode_wire_test(void)
{
    static const uint8_t be70000[8] = { 0, 0, 0, 0, 0, 0x01, 0x11, 0x70 };
    LOCAL_SET(uchar, payload) = mem_alloc(70000, .zero = 1);
    LOCAL(uchar, frame);
    size_t framesz;

    if (!payload)   return EXIT_FAILURE;

    /* 125 still fits the single-byte length */
    ws_encode(payload, 125, &frame, &framesz, WSOP_BIN);
    if (framesz != 2 + 125 || (frame[1] & 0x7f) != 125) return EXIT_FAILURE;
    mem_free(frame);
    frame = NULL;

    /* 300 takes the 16-bit form, big-endian on the wire */
    ws_encode(payload, 300, &frame, &framesz, WSOP_BIN);
    if (framesz != 2 + 2 + 300)                         return EXIT_FAILURE;
    if ((frame[1] & 0x7f) != 126)                       return EXIT_FAILURE;
    if (frame[2] != 0x01 || frame[3] != 0x2c)           return EXIT_FAILURE;
    mem_free(frame);
    frame = NULL;

    /* 65535 is the top of the 16-bit form */
    ws_encode(payload, 65535, &frame, &framesz, WSOP_BIN);
    if (framesz != 2 + 2 + 65535)                       return EXIT_FAILURE;
    if ((frame[1] & 0x7f) != 126)                       return EXIT_FAILURE;
    if (frame[2] != 0xff || frame[3] != 0xff)           return EXIT_FAILURE;
    mem_free(frame);
    frame = NULL;

    /* 70000 takes the 64-bit form */
    ws_encode(payload, 70000, &frame, &framesz, WSOP_BIN);
    if (framesz != 2 + 8 + 70000)                       return EXIT_FAILURE;
    if ((frame[1] & 0x7f) != 127)                       return EXIT_FAILURE;
    if (memcmp(&frame[2], be70000, 8))                  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_decode_coalesced_test(void)
{
    uint8_t buf[] = { 0x82, 0x02, 'h', 'i', 0x82, 0x01, '!' };
    LOCAL(uchar, first);
    LOCAL(uchar, second);
    size_t outsz;
    int op = -1;

    cres(size_t) dres = ws_decode(buf, sizeof(buf), &first, &outsz, &op, false);
    if (IS_CERR(dres) || dres.val != 4)             return EXIT_FAILURE;
    if (outsz != 2 || memcmp(first, "hi", 2))       return EXIT_FAILURE;

    /* a complete 3-byte frame at the tail decodes on its own */
    dres = ws_decode(buf + 4, sizeof(buf) - 4, &second, &outsz, &op, false);
    if (IS_CERR(dres) || dres.val != 3)             return EXIT_FAILURE;
    if (outsz != 1 || second[0] != '!')             return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_control_frames_test(void)
{
    static const uint8_t mask[] = { 0xa5, 0x5a, 0xff, 0x00 };
    static const uint8_t status[] = { 0x03, 0xe8 };     /* 1000, normal closure */
    uint8_t frame[2 + sizeof(mask) + sizeof(status)];
    LOCAL(uchar, out);
    LOCAL(uchar, pong);
    size_t outsz, pongsz;
    int op = -1;

    frame[0] = 0x88;                    /* FIN | close */
    frame[1] = 0x80 | sizeof(status);   /* MASK | payload length */
    memcpy(&frame[2], mask, sizeof(mask));
    for (size_t i = 0; i < sizeof(status); i++)
        frame[2 + sizeof(mask) + i] = status[i] ^ mask[i % sizeof(mask)];

    cres(size_t) dres = ws_decode(frame, sizeof(frame), &out, &outsz, &op, true);
    if (IS_CERR(dres) || dres.val != sizeof(frame))     return EXIT_FAILURE;
    if (op != WSOP_CLOSE || outsz != sizeof(status))    return EXIT_FAILURE;
    if (memcmp(out, status, sizeof(status)))            return EXIT_FAILURE;

    /* a pong echoing the ping payload carries the pong opcode */
    ws_encode(out, outsz, &pong, &pongsz, WSOP_PONG);
    if (pong[0] != (0x80 | WSOP_PONG))  return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int ws_decode_reject_one(uint8_t b0, uint8_t b1, bool require_mask)
{
    uint8_t frame[8] = { b0, b1 };
    uint8_t *out = NULL;
    size_t outsz;
    int op = -1;

    cres(size_t) dres = ws_decode(frame, sizeof(frame), &out, &outsz, &op, require_mask);
    return IS_CERR_CODE(dres, CERR_INVALID_FORMAT) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int ws_decode_reject_test(void)
{
    if (ws_decode_reject_one(0x02, 0x01, false))    return EXIT_FAILURE;    /* fin clear */
    if (ws_decode_reject_one(0x80, 0x01, false))    return EXIT_FAILURE;    /* continuation */
    if (ws_decode_reject_one(0xc2, 0x01, false))    return EXIT_FAILURE;    /* reserved bit set */
    if (ws_decode_reject_one(0x88, 126, false))     return EXIT_FAILURE;    /* extended-length control */
    if (ws_decode_reject_one(0x82, 0x01, true))     return EXIT_FAILURE;    /* unmasked to a server */
    if (ws_decode_reject_one(0x83, 0x01, false))    return EXIT_FAILURE;    /* reserved data opcode 3 */
    if (ws_decode_reject_one(0x87, 0x01, false))    return EXIT_FAILURE;    /* reserved data opcode 7 */
    if (ws_decode_reject_one(0x8b, 0x01, false))    return EXIT_FAILURE;    /* reserved control opcode 0xb */
    if (ws_decode_reject_one(0x8f, 0x01, false))    return EXIT_FAILURE;    /* reserved control opcode 0xf */

    return EXIT_SUCCESS;
}
#endif /* CONFIG_NETWORKING */

/* Log @nr messages into the ring with the test's stdio parked on /dev/null */
static int log_rb_spam(int nr)
{
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull < 0)    return EXIT_FAILURE;

    fflush(stdout);
    fflush(stderr);
    int saved_out = dup(STDOUT_FILENO);
    int saved_err = dup(STDERR_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);

    for (int i = 0; i < nr; i++)
        dbg("ring buffer spam %d\n", i);

    fflush(stdout);
    fflush(stderr);
    dup2(saved_out, STDOUT_FILENO);
    dup2(saved_err, STDERR_FILENO);
    close(saved_out);
    close(saved_err);
    close(devnull);

    return EXIT_SUCCESS;
}

static int log_rb_wrap_test(void)
{
    /*
     * Come around the ring with room to spare: an entry that gets overwritten
     * without being freed is a leak that LeakSanitizer reports at exit.
     */
    return log_rb_spam(LOG_RB_MAX + 8);
}

struct log_rb_probe {
    int     count;
    char    first[32];
};

static void log_rb_probe_flush(struct log_entry *e, void *data)
{
    struct log_rb_probe *probe = data;

    if (!probe->count++)
        strncpy(probe->first, e->msg, sizeof(probe->first) - 1);
}

static int log_rb_late_sink_test(void)
{
    struct log_rb_probe probe = {};

    /* A full ring: a sink attached now is due all of it, oldest entry first */
    if (log_rb_spam(LOG_RB_MAX + 8))    return EXIT_FAILURE;

    cerr err = rb_sink_add(log_rb_probe_flush, &probe, VDBG, 1);
    if (IS_CERR(err))                   return EXIT_FAILURE;

    /* Sinks are flushed on the way into the ring, so the trigger itself doesn't count */
    int ret = log_rb_spam(1);
    rb_sink_del(&probe);
    if (ret)                            return ret;

    if (probe.count != LOG_RB_MAX) {
        err("late sink got %d entries, expected %d\n", probe.count, LOG_RB_MAX);
        return EXIT_FAILURE;
    }

    if (strcmp(probe.first, "ring buffer spam 8\n")) {
        err("late sink started at '%s'\n", probe.first);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int tex_next_match_test(void)
{
    texture_format_iter it = 0;
    auto fmt = CRES_RET(texture_format_next_match(&it, 0, 4, DT_MASK(DT_BYTE), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RGBA8)   return EXIT_FAILURE;
    it = 0;
    fmt = CRES_RET(texture_format_next_match(&it, 16, 2, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RG16F)   return EXIT_FAILURE;
    it = 0;
    fmt = CRES_RET(texture_format_next_match(&it, 16, 4, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RGBA16F) return EXIT_FAILURE;
    it = 0;
    fmt = CRES_RET(texture_format_next_match(&it, 32, 2, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RG32F)   return EXIT_FAILURE;
    it = 0;
    fmt = CRES_RET(texture_format_next_match(&it, 32, 4, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RGBA32F) return EXIT_FAILURE;

    /*
     * Resumption: each call returns the next-best unvisited candidate
     * (the WebGL2 shape: RGB16F unsupported must yield RGBA16F next,
     * not skip to the 32-bit tier), and exhaustion errors out
     */
    it = 0;
    fmt = CRES_RET(texture_format_next_match(&it, 16, 3, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RGB16F)  return EXIT_FAILURE;
    fmt = CRES_RET(texture_format_next_match(&it, 16, 3, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RGBA16F) return EXIT_FAILURE;
    fmt = CRES_RET(texture_format_next_match(&it, 16, 3, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RGB32F)  return EXIT_FAILURE;
    fmt = CRES_RET(texture_format_next_match(&it, 16, 3, DT_MASK(DT_FLOAT), false, false), return EXIT_FAILURE);
    if (fmt != TEX_FMT_RGBA32F) return EXIT_FAILURE;
    auto res = texture_format_next_match(&it, 16, 3, DT_MASK(DT_FLOAT), false, false);
    if (!IS_CERR(res))          return EXIT_FAILURE;

    /* an integer format doesn't sneak into a byte|float accept-set */
    it = 0;
    fmt = CRES_RET(
        texture_format_next_match(&it, 32, 4, DT_MASK(DT_BYTE) | DT_MASK(DT_FLOAT), false, false),
        return EXIT_FAILURE
    );
    if (fmt != TEX_FMT_RGBA32F) return EXIT_FAILURE;
    res = texture_format_next_match(&it, 32, 4, DT_MASK(DT_BYTE) | DT_MASK(DT_FLOAT), false, false);
    if (!IS_CERR(res))          return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int interp_easing_test(void)
{
    if (ease_in_cubicf(0.0f) != 0.0f || ease_in_cubicf(1.0f) != 1.0f)   return EXIT_FAILURE;
    if (ease_out_cubicf(0.0f) != 0.0f || ease_out_cubicf(1.0f) != 1.0f) return EXIT_FAILURE;

    /* in accelerates, out decelerates, both stay within [0, 1] */
    if (ease_in_cubicf(0.5f) >= 0.5f || ease_out_cubicf(0.5f) <= 0.5f)  return EXIT_FAILURE;

    for (int i = 1; i <= 10; i++) {
        float x = i / 10.0f, px = (i - 1) / 10.0f;

        if (ease_in_cubicf(x) <= ease_in_cubicf(px))    return EXIT_FAILURE;
        if (ease_out_cubicf(x) <= ease_out_cubicf(px))  return EXIT_FAILURE;
    }

    /* approach: nothing at dt 0, everything at dt >> 1/rate */
    if (approachf(10.0f, 0.0f) != 0.0f)                 return EXIT_FAILURE;
    if (fabsf(approachf(10.0f, 10.0f) - 1.0f) > 1e-6f)  return EXIT_FAILURE;

    vec3 cur = { 0, 0, 0 };
    vec3_approach(cur, (vec3){ 1, 2, 3 }, 100.0f, 1.0f);
    if (fabsf(cur[0] - 1.0f) > 1e-4f || fabsf(cur[2] - 3.0f) > 1e-4f)   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int quat_mat4x4_roundtrip_test(void)
{
    quat cases[] = {
        { 0, 0, 0, 1 },                                             /* identity */
        { 1, 0, 0, 0 },                                             /* 180 around X */
        { 0.7071068f, 0, 0, 0.7071068f },                           /* 90 around X */
        { 0, -0.7071068f, 0, 0.7071068f },                          /* -90 around Y */
        { 0.5f, -0.5f, 0.5f, -0.5f },
        { 0.18257419f, 0.36514837f, -0.54772256f, 0.73029674f },
    };

    for (size_t i = 0; i < array_size(cases); i++) {
        mat4x4 m;
        quat out;

        mat4x4_from_quat(m, cases[i]);
        quat_from_mat4x4(out, m);

        /* q and -q encode the same rotation */
        float sign = quat_inner_product(out, cases[i]) < 0.f ? -1.f : 1.f;
        for (int k = 0; k < 4; k++)
            if (fabsf(out[k] * sign - cases[i][k]) > 1e-5f) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "user-agent parse desktop", .test = ua_parse_desktop_test },
    { .name = "user-agent parse mobile", .test = ua_parse_mobile_test },
    { .name = "refcount basic", .test = refcount_test0 },
    { .name = "refcount get/put", .test = refcount_test1 },
    { .name = "refcount static", .test = refcount_test2 },
    { .name = "list_for_each", .test = list_test0 },
    { .name = "list_for_each_iter", .test = list_test1 },
    { .name = "darray basic", .test = darray_test0 },
    { .name = "darray insert", .test = darray_test1 },
    { .name = "darray delete", .test = darray_test2 },
    { .name = "darray capacity", .test = darray_test3 },
    { .name = "strtobool", .test = strtobool_test0 },
    { .name = "str_endswith", .test = str_endswith_test0 },
    { .name = "str_endswith_nocase", .test = str_endswith_nocase_test0 },
    { .name = "str_trim_slashes", .test = str_trim_slashes_test0 },
    { .name = "string_view basic", .test = string_view_test0 },
    { .name = "path_join", .test = path_join_test0 },
    { .name = "path_has_parent", .test = path_has_parent_test0 },
    { .name = "path_parent", .test = path_parent_test0 },
    { .name = "hashmap basic", .test = hashmap_test0 },
    { .name = "hashmap for each", .test = hashmap_test1 },
    { .name = "bitmap basic", .test = bitmap_test0 },
    { .name = "ca2d basic", .test = ca2d_test0 },
    { .name = "ca3d basic", .test = ca3d_test0 },
    { .name = "cpio basic", .test = cpio_test0 },
    { .name = "fs test setup", .test = fs_test_setup },
    { .name = "fs test dir iter", .test = fs_test_dir_iter },
    { .name = "fs test text rw", .test = fs_test_text_rw },
    { .name = "fs test append", .test = fs_test_append },
    { .name = "fs test binary rw", .test = fs_test_binary_rw },
    { .name = "fs test seek", .test = fs_test_seek },
#ifdef HAVE_FSWATCH
    { .name = "fs test watch", .test = fs_test_watch },
#endif /* HAVE_FSWATCH */
    { .name = "fs test teardown", .test = fs_test_teardown },
    { .name = "canvas new", .test = canvas_test_new },
    { .name = "canvas write/read rgba8", .test = canvas_test_write_read_rgba8 },
    { .name = "canvas write/read rgba32f", .test = canvas_test_write_read_rgba32f },
    { .name = "canvas fill rgba8", .test = canvas_test_fill },
    { .name = "canvas fill rgba32f", .test = canvas_test_fill_rgba32f },
    { .name = "canvas blit", .test = canvas_test_blit },
    { .name = "canvas blit color", .test = canvas_test_blit_color },
    { .name = "canvas blit cross format", .test = canvas_test_blit_cross_format },
    { .name = "canvas write/read rgba16f", .test = canvas_test_write_read_rgba16f },
    { .name = "canvas fill rgba16f", .test = canvas_test_fill_rgba16f },
    { .name = "canvas blit rgba16f->rgba8", .test = canvas_test_blit_rgba16f_to_rgba8 },
    { .name = "canvas blit rgba8->rgba16f", .test = canvas_test_blit_rgba8_to_rgba16f },
    { .name = "canvas blit rgba16f->rgba32f", .test = canvas_test_blit_rgba16f_to_rgba32f },
    { .name = "canvas blit rgba32f->rgba16f", .test = canvas_test_blit_rgba32f_to_rgba16f },
    { .name = "canvas blend write", .test = canvas_test_blend_write },
    { .name = "canvas blend fill", .test = canvas_test_blend_fill },
    { .name = "canvas blend none", .test = canvas_test_blend_none },
    { .name = "tex fmt needs alpha", .test = tex_fmt_needs_alpha_test },
    { .name = "tex fmt add alpha", .test = tex_fmt_add_alpha_test },
    { .name = "tex rgb to rgba 8bit", .test = tex_rgb_to_rgba_8bit_test },
    { .name = "tex rgb to rgba 32f", .test = tex_rgb_to_rgba_32f_test },
    { .name = "tex rgb to rgba 16f", .test = tex_rgb_to_rgba_16f_test },
    { .name = "tex next match iterator test", .test = tex_next_match_test },
    { .name = "log ring buffer wrap", .test = log_rb_wrap_test },
    { .name = "log ring buffer late sink", .test = log_rb_late_sink_test },
#ifdef CONFIG_NETWORKING
    { .name = "http header find", .test = http_header_find_test },
    { .name = "http headers end", .test = http_headers_end_test },
    { .name = "websocket codec roundtrip", .test = ws_codec_roundtrip_test },
    { .name = "websocket masked decode", .test = ws_decode_masked_test },
    { .name = "websocket encode wire format", .test = ws_encode_wire_test },
    { .name = "websocket short frame", .test = ws_decode_short_test },
    { .name = "websocket coalesced frames", .test = ws_decode_coalesced_test },
    { .name = "websocket control frames", .test = ws_control_frames_test },
    { .name = "websocket protocol rejects", .test = ws_decode_reject_test },
#endif /* CONFIG_NETWORKING */
    { .name = "interp easing", .test = interp_easing_test },
    { .name = "quat from mat4x4 roundtrip", .test = quat_mat4x4_roundtrip_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
