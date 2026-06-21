// SPDX-License-Identifier: Apache-2.0
//
// FFI wrappers over the UI: create and dismiss on-screen text. A script owns the
// widgets it creates; they are this module's private per-script state
// (CLAP_SCRIPT_PRIV), released when the script is dropped.

#include "clap.h"
#include "memory.h"
#include "script/ffi.h"
#include "script/internal.h"
#include "ui.h"

/* A ui_widget created by ui_text(), tracked so it is released on teardown. */
struct script_ui_text {
    struct list         entry;
    struct ui_widget    *widget;
};

CLAP_SCRIPT_PRIV(struct list ui_texts)

CLAP_SCRIPT_TYPE("type UiText* = struct { _h: uint }")

CLAP_SCRIPT_FN(ffi_ui_text, "fn ui_text*(text: str): UiText")
void ffi_ui_text(UmkaStackSlot *params, UmkaStackSlot *result)
{
    script *s = ffi_script(result);
    const char *text = ffi_str(params, 0);
    struct ui *ui = clap_get_ui(script_context(s));
    struct ui_widget *w = NULL;

    if (ui && text) {
        w = ui_text_new(ui, NULL, text);
        if (w) {
            struct script_ui_text *node = mem_alloc(sizeof(*node), .zero = 1);
            if (node) {
                node->widget = w;
                list_append(&s->priv.ui_texts, &node->entry);
            } else {
                ref_put(w);
                w = NULL;
            }
        }
    }

    ffi_ret_ptr(params, result, w);
}

CLAP_SCRIPT_METHOD(ffi_ui_delete, "fn (t: UiText) delete*()")
void ffi_ui_delete(UmkaStackSlot *params, UmkaStackSlot *result)
{
    script *s = ffi_script(result);
    struct ui_widget *w = ffi_ptr(params, 0);
    if (!w)     return;

    struct script_ui_text *node, *iter;
    list_for_each_entry_iter(node, iter, &s->priv.ui_texts, entry)
        if (node->widget == w) {
            list_del(&node->entry);
            ref_put(node->widget);
            mem_free(node);
            return;
        }
}

CLAP_SCRIPT_DONE(script_ui_texts_done)
void script_ui_texts_done(script *s)
{
    struct script_ui_text *node, *iter;
    list_for_each_entry_iter(node, iter, &s->priv.ui_texts, entry) {
        list_del(&node->entry);
        if (node->widget)   ref_put(node->widget);
        mem_free(node);
    }
}
