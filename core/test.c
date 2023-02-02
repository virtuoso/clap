// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <stdbool.h>
#include "object.h"
#include "common.h"
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

    if (da.x[3] != 4)
        return EXIT_FAILURE;

    if (da.da.nr_el != 9)
        return EXIT_FAILURE;

    darray_clearout(&da.da);

    if (da.da.nr_el)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
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
};

int main()
{
    int ret, i;

    for (i = 0; i < array_size(tests); i++) {
        failcount = 0;
        ret = tests[i].test();
        msg("test %-40s: %s\n", tests[i].name, ret ? "FAILED" : "PASSED");
        if (ret)
            break;
    }

#ifdef CONFIG_BROWSER
    exit_cleanup_run(ret);
#endif
    return ret;
}

