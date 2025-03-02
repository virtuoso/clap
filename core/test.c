// SPDX-License-Identifier: Apache-2.0
#include <getopt.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include "ca2d.h"
#include "ca3d.h"
#include "cpio.h"
#include "object.h"
#include "common.h"
#include "messagebus.h"
#include "util.h"
#include "xyarray.h"

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

static int hashmap_test0(void)
{
    struct hashmap hm;
    char *value;

    hashmap_init(&hm, 256);
    hashmap_insert(&hm, 0, "zero");
    hashmap_insert(&hm, 256, "one");

    value = hashmap_find(&hm, 0);
    if (strcmp(value, "zero"))
        return EXIT_FAILURE;

    value = hashmap_find(&hm, 256);
    if (strcmp(value, "one"))
        return EXIT_FAILURE;

    hashmap_done(&hm);

    if (!list_empty(&hm.list) || hm.nr_buckets)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
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
    long i;

    hashmap_init(&hm, 256);

    for (i = 1; i < 1024; i++)
        hashmap_insert(&hm, i, (void *)(uintptr_t)i);
    hashmap_for_each(&hm, hashmap_cb, &ctx);

    hashmap_done(&hm);

    if (ctx.broken)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
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

    bitmap_set(&b1, 0);
    bitmap_set(&b1, 2);
    if (!bitmap_includes(&b0, &b1))
        return EXIT_FAILURE;

    if (bitmap_includes(&b1, &b0))
        return EXIT_FAILURE;

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
    { .name = "hashmap basic", .test = hashmap_test0 },
    { .name = "hashmap for each", .test = hashmap_test1 },
    { .name = "bitmap basic", .test = bitmap_test0 },
    { .name = "ca2d basic", .test = ca2d_test0 },
    { .name = "ca3d basic", .test = ca3d_test0 },
    { .name = "cpio basic", .test = cpio_test0 },
};

static struct option long_options[] = {
    { "verbose",    no_argument,        0, 'v' },
    {}
};

static const char short_options[] = "v";

int main(int argc, char **argv, char **envp)
{
    int ret, i, c, option_index;

    cerr err = messagebus_init();
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
    messagebus_done();

    return ret;
}

