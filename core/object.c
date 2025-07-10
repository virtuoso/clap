// SPDX-License-Identifier: Apache-2.0
#include <stdarg.h>
#include "common.h"
#include "object.h"

static DECLARE_LIST(ref_classes);

#if !defined(CONFIG_FINAL) && !defined(CLAP_TESTS)
#include "ui-debug.h"

void memory_debug(void)
{
    debug_module *dbgm = ui_igBegin_name(DEBUG_MEMORY, ImGuiWindowFlags_AlwaysAutoResize, "memory");

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        ui_igTableHeader("refclasses", (const char *[]) { "refclass", "objects"}, 2);

        unsigned int total = 0;
        struct ref_class *rc;
        list_for_each_entry(rc, &ref_classes, entry) {
            ui_igTableCell(true, "%s", rc->name);
            ui_igTableCell(false, "%lu", rc->nr_active);
            total++;
        }

        igEndTable();

        igText("total: %u", total);
    }

    ui_igEnd(DEBUG_MEMORY);
}
#endif /* CONFIG_FINAL && CLAP_TESTS */

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
}

void ref_class_unuse(struct ref *ref)
{
    /* not deleting the class itself */
    ref->refclass->nr_active--;
}

void _ref_drop(struct ref *ref)
{
    ref_class_unuse(ref);
    ref->refclass->drop(ref);
    ref_free(ref);
}
