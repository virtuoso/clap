/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_DEBUG_FS_H__
#define __CLAP_UI_DEBUG_FS_H__

#include <limits.h>
#include <stdbool.h>
#include "error.h"
#include "util.h"
#include "fs-ops.h"

/**
 * enum fs_select_mode - selection mode for filesystem dialog
 * @FS_SELECT_FILE:   files can be accepted
 * @FS_SELECT_DIR:    directories can be accepted
 */
enum ui_debug_fs_select_mode {
    UI_DEBUG_FS_SELECT_FILE = 0,
    UI_DEBUG_FS_SELECT_DIR,
};

/**
 * struct ui_debug_fs_config - filesystem dialog configuration
 * @title:            window title (defaults to "Filesystem")
 * @modal:            render as modal popup when true
 * @input_field:      text input field name or NULL to hide
 * @action_label:     label for the action button (defaults to "Open")
 * @select_mode:      selection mode (files or directories)
 * @start_dir:        initial directory (NULL => current working directory)
 * @extensions:       optional NULL-terminated list of file extensions (".ext")
 * @can_select:       optional custom validator for enabling the action button
 * @draw_right_panel: optional callback to draw a right-hand panel
 * @on_accept:        callback fired when the action button is pressed
 * @data:             opaque pointer passed to callbacks
 */
struct ui_debug_fs_config {
    const char  *title;
    bool        modal;
    const char  *input_field;
    const char  *action_label;
    enum ui_debug_fs_select_mode select_mode;
    const char  *start_dir;
    const char * const *extensions;
    bool (*can_select)(const char *path, bool is_dir, void *data);
    void (*draw_right_panel)(const char *cwd, const char *selected_name, bool selected_is_dir, void *data);
    void (*on_accept)(const char *cwd, const char *selected_name, bool selected_is_dir, void *data);
    void *data;
};

/**
 * struct ui_debug_fs_dialog - filesystem dialog state (owned by the caller)
 * @active:            dialog is currently open
 * @modal:             render as modal popup
 * @has_parent:        current directory has a parent that can be entered
 * @selection_is_dir:  true when the current selection is a directory
 * @ops:               filesystem vtable in use
 * @cfg:               dialog configuration copy
 * @cwd:               current directory
 * @selection:         selected entry name (no path)
 * @dirs:              cached directories
 * @files:             cached files
 */
typedef struct ui_debug_fs_dialog {
    bool                    active;
    bool                    modal;
    bool                    has_parent;
    bool                    selection_is_dir;
    bool                    can_accept;
    const struct fs_ops     *ops;
    struct ui_debug_fs_config cfg;
    char                    cwd[PATH_MAX];
    char                    selection[PATH_MAX];
    darray(struct fs_dirent, dirs);
    darray(struct fs_dirent, files);
} ui_debug_fs_dialog;

#ifndef CONFIG_FINAL
cerr_check ui_debug_fs_open(ui_debug_fs_dialog *dlg, const struct ui_debug_fs_config *cfg,
                            const struct fs_ops *ops);
void ui_debug_fs_draw(ui_debug_fs_dialog *dlg);
#else
static inline cerr ui_debug_fs_open(__unused ui_debug_fs_dialog *dlg,
                                    __unused const struct ui_debug_fs_config *cfg,
                                    __unused const struct fs_ops *ops)
{
    return CERR_NOT_SUPPORTED;
}
static inline void ui_debug_fs_draw(__unused ui_debug_fs_dialog *dlg) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_UI_DEBUG_FS_H__ */
