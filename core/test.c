// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>
#include "ca3d.h"
#include "object.h"
#include "common.h"
#include "messagebus.h"
#include "util.h"

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

DECLARE_REFCLASS_DROP(x0, test_drop);
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

static int __refcount_test3(void)
{
    struct x0 *x0 = ref_new(x0);
    CU(ref) unused struct ref *ref = &x0->ref;

    x0->magic = TEST_MAGIC0;

    return EXIT_SUCCESS;
}

static int refcount_test3(void)
{
    int ret;

    reset_counters();
    ret = __refcount_test3();
    if (ret)
        return ret;
    if (!ok_counters())
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
        entry = calloc(1, sizeof(*entry));
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
        free(e);
    }

    if (!list_empty(&list))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int darray_test0(void)
{
    darray(int, da);
    int i;

    darray_init(&da);
    for (i = 0; i < 10; i++) {
        int *x = darray_add(&da.da);
        *x = i;
    }

    if (da.da.nr_el != 10)
        return EXIT_FAILURE;

    if (da.x[5] != 5)
        return EXIT_FAILURE;

    darray_clearout(&da.da);

    if (da.da.nr_el)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int darray_test1(void)
{
    darray(int, da);
    int i;

    darray_init(&da);
    for (i = 0; i < 10; i++) {
        int *x = darray_add(&da.da);
        *x = i;
    }

    int *x = darray_insert(&da.da, 3);
    *x = -1;

    if (da.da.nr_el != 11)
        return EXIT_FAILURE;

    if (da.x[3] != -1)
        return EXIT_FAILURE;

    if (da.x[10] != 9)
        return EXIT_FAILURE;

    darray_clearout(&da.da);

    if (da.da.nr_el)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int darray_test2(void)
{
    darray(int, da);
    int i;

    darray_init(&da);
    for (i = 0; i < 10; i++) {
        int *x = darray_add(&da.da);
        *x = i;
    }

    darray_delete(&da.da, 3);

    if (da.x[3] != 4 || da.x[8] != 9)
        return EXIT_FAILURE;

    if (da.da.nr_el != 9)
        return EXIT_FAILURE;

    darray_delete(&da.da, -1);

    if (da.da.nr_el != 8)
        return EXIT_FAILURE;

    if (da.x[7] != 8)
        return EXIT_FAILURE;

    darray_clearout(&da.da);

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
    unsigned long value = (unsigned long)item;

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
        hashmap_insert(&hm, i, (void *)i);
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

        if (!xyzarray_count(xyz))
            ret = EXIT_FAILURE;

        free(xyz);
    }

    return ret;
}

static struct test {
    const char	*name;
    int			(*test)(void);
} tests[] = {
    { .name = "refcount basic", .test = refcount_test0 },
    { .name = "refcount get/put", .test = refcount_test1 },
    { .name = "refcount static", .test = refcount_test2 },
    { .name = "refcount cleanup", .test = refcount_test3 },
    { .name = "list_for_each", .test = list_test0 },
    { .name = "list_for_each_iter", .test = list_test1 },
    { .name = "darray basic", .test = darray_test0 },
    { .name = "darray insert", .test = darray_test1 },
    { .name = "darray delete", .test = darray_test2 },
    { .name = "hashmap basic", .test = hashmap_test0 },
    { .name = "hashmap for each", .test = hashmap_test1 },
    { .name = "bitmap basic", .test = bitmap_test0 },
    { .name = "ca3d basic", .test = ca3d_test0 },
};

int main()
{
    int ret, i;

    messagebus_init();

    for (i = 0; i < array_size(tests); i++) {
        failcount = 0;
        ret = tests[i].test();
        msg("test %-40s: %s\n", tests[i].name, ret ? "FAILED" : "PASSED");
        if (ret)
            break;
    }

    messagebus_done();

    return ret;
}

