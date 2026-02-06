// SPDX-License-Identifier: Apache-2.0
#include <getopt.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include "ca2d.h"
#include "ca3d.h"
#include "cpio.h"
#include "fs-ops.h"
#include "object.h"
#include "common.h"
#include "messagebus.h"
#include "util.h"
#include "xyarray.h"

struct clap_context {
    struct messagebus mb;
};

struct messagebus *clap_get_messagebus(struct clap_context *ctx)
{
    return &ctx->mb;
}

#define TEST_MAGIC0 0xdeadbeef

static unsigned int verbose;

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

static int fs_test_teardown(void)
{
    CERR_RET(fs_remove_dir(fs_test_ops, fs_test_dir), return EXIT_FAILURE);
    return EXIT_SUCCESS;
}

static struct test {
    const char	*name;
    int			(*test)(void);
} tests[] = {
    { .name = "refcount basic", .test = refcount_test0 },
    { .name = "refcount get/put", .test = refcount_test1 },
    { .name = "refcount static", .test = refcount_test2 },
    { .name = "list_for_each", .test = list_test0 },
    { .name = "list_for_each_iter", .test = list_test1 },
    { .name = "darray basic", .test = darray_test0 },
    { .name = "darray insert", .test = darray_test1 },
    { .name = "darray delete", .test = darray_test2 },
    { .name = "str_endswith", .test = str_endswith_test0 },
    { .name = "str_endswith_nocase", .test = str_endswith_nocase_test0 },
    { .name = "str_trim_slashes", .test = str_trim_slashes_test0 },
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
    { .name = "fs test teardown", .test = fs_test_teardown },
};

static struct option long_options[] = {
    { "verbose",    no_argument,        0, 'v' },
    {}
};

static const char short_options[] = "v";

int main(int argc, char **argv, char **envp)
{
    int ret, i, c, option_index;
    struct clap_context ctx = {};

    cerr err = messagebus_init(&ctx);
    if (IS_CERR(err))
        return EXIT_FAILURE;

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'v':
            verbose++;
            break;
        default:
            err("invalid command line option %x\n", c);
            ret = EXIT_FAILURE;
            goto out;
        }
    }

    for (i = 0; i < array_size(tests); i++) {
        failcount = 0;
        ret = tests[i].test();
        msg("test %-40s: %s\n", tests[i].name, ret ? "FAILED" : "PASSED");
        if (ret)
            break;
    }

out:
    messagebus_done(&ctx);

    return ret;
}

