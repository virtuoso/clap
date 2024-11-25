// SPDX-License-Identifier: Apache-2.0
#include <stdarg.h>
#include "common.h"
#include "object.h"
#include "json.h"

static DECLARE_LIST(ref_classes);
static char ref_classes_string[4096];
static bool ref_classes_updated;

static void ref_classes_update(void)
{
    size_t size, total = 0;
    struct ref_class *rc;
    unsigned long counter = 0;

    list_for_each_entry(rc, &ref_classes, entry) {
        size = snprintf(&ref_classes_string[total], sizeof(ref_classes_string) - total,
                        " -> '%s': %lu\n", rc->name, rc->nr_active);
        if (total + size >= sizeof(ref_classes_string))
            break;
        total += size;
        counter++;
    }

    size = snprintf(&ref_classes_string[total], sizeof(ref_classes_string) - total,
                    " total: %lu", counter);
    ref_classes_string[total+size] = 0;
    ref_classes_updated = false;
}

const char *ref_classes_get_string(void)
{
    if (ref_classes_updated)
        ref_classes_update();
    return ref_classes_string;
}

static bool ref_class_needs_init(struct ref_class *rc)
{
    return list_empty(&rc->entry);
}

static void ref_class_init_lazy(struct ref_class *rc)
{
    list_append(&ref_classes, &rc->entry);
}

void ref_class_add(struct ref *ref)
{
    struct ref_class *rc = ref->refclass;

    bug_on(!ref->refclass);

    if (ref_class_needs_init(rc))
        ref_class_init_lazy(rc);

    if (!ref_is_static(ref))
        rc->nr_active++;
    ref_classes_updated = true;
}

static void ref_class_unuse(struct ref *ref)
{
    /* not deleting the class itself */
    ref->refclass->nr_active--;
    ref_classes_updated = true;
}

void _ref_drop(struct ref *ref)
{
    ref_class_unuse(ref);
    ref->refclass->drop(ref);
    ref_free(ref);
}
