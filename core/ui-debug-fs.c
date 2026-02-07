// SPDX-License-Identifier: Apache-2.0
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "ui-debug.h"
#include "ui-debug-fs.h"
#include "fs-ops.h"

static void clear_selection(ui_debug_fs_dialog *dlg)
{
    dlg->selection[0] = 0;
    dlg->selection_is_dir = false;
    dlg->can_accept = false;
}

static void set_selection(ui_debug_fs_dialog *dlg, const char *name, bool is_dir)
{
    snprintf(dlg->selection, sizeof(dlg->selection), "%s", name);
    dlg->selection_is_dir = is_dir;
}

static cerr load_directory(ui_debug_fs_dialog *dlg, const char *path)
{
    struct fs_dirent ent;

    if (!path || !*path)
        return CERR_INVALID_ARGUMENTS;

    int n = snprintf(dlg->cwd, sizeof(dlg->cwd), "%s", path);
    if (n < 0 || (size_t)n >= sizeof(dlg->cwd))
        return CERR_TOO_LARGE;
    str_trim_slashes(dlg->cwd);

    darray_resize(dlg->dirs, 0);
    darray_resize(dlg->files, 0);

    void *dir = CRES_RET_CERR(dlg->ops->open_dir(dlg->cwd));

    for (;;) {
        cerr r = dlg->ops->read_dir(dir, &ent);
        if (IS_CERR_CODE(r, CERR_EOF))
            break;

        CERR_RET(r, { dlg->ops->close_dir(dir); return __cerr; });

        if (!strcmp(ent.name, ".") || !strcmp(ent.name, ".."))
            continue;

        struct fs_dirent *dst = _darray_add(ent.is_dir ? &dlg->dirs.da : &dlg->files.da);
        if (!dst) {
            dlg->ops->close_dir(dir);
            return CERR_NOMEM;
        }

        *dst = ent;
    }

    dlg->ops->close_dir(dir);

    qsort(dlg->dirs.x, darray_count(dlg->dirs), sizeof(*dlg->dirs.x), dlg->ops->dirent_cmp);
    qsort(dlg->files.x, darray_count(dlg->files), sizeof(*dlg->files.x), dlg->ops->dirent_cmp);

    dlg->has_parent = path_has_parent(dlg->cwd);
    clear_selection(dlg);

    return CERR_OK;
}

static bool match_extensions(const char *name, const char * const *exts)
{
    if (!exts)
        return true;

    for (; *exts; exts++) {
        if (str_endswith_nocase(name, *exts))
            return true;
    }

    return false;
}

static bool update_can_accept(ui_debug_fs_dialog *dlg)
{
    if (!dlg->selection[0] || !dlg->cfg.on_accept) {
        dlg->can_accept = false;
        return false;
    }

    if (!strcmp(dlg->selection, "..")) {
        dlg->can_accept = false;
        return false;
    }

    char full[PATH_MAX];
    cerr err = path_join(full, sizeof(full), dlg->cwd, dlg->selection);
    if (IS_CERR(err)) {
        dlg->can_accept = false;
        return false;
    }

    if (dlg->cfg.can_select) {
        dlg->can_accept = dlg->cfg.can_select(full, dlg->selection_is_dir, dlg->cfg.data);
        return dlg->can_accept;
    }

    if (dlg->cfg.select_mode == UI_DEBUG_FS_SELECT_DIR) {
        dlg->can_accept = dlg->selection_is_dir;
        return dlg->can_accept;
    }

    dlg->can_accept = !dlg->selection_is_dir &&
                      match_extensions(dlg->selection, dlg->cfg.extensions);
    return dlg->can_accept;
}

static void cleanup(ui_debug_fs_dialog *dlg)
{
    darray_clearout(dlg->dirs);
    darray_clearout(dlg->files);
    dlg->active = false;
}

static void do_accept(ui_debug_fs_dialog *dlg)
{
    if (!dlg->can_accept)
        return;

    dlg->cfg.on_accept(dlg->cwd, dlg->selection, dlg->selection_is_dir, dlg->cfg.data);

    cleanup(dlg);
}

static cerr load_parent(ui_debug_fs_dialog *dlg)
{
    char parent[PATH_MAX];

    CERR_RET_CERR(path_parent(parent, sizeof(parent), dlg->cwd));

    return load_directory(dlg, parent);
}

static void draw_entries(ui_debug_fs_dialog *dlg)
{
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;

    if (!dlg->has_parent)
        flags |= ImGuiSelectableFlags_Disabled;

    if (igSelectable_Bool("..", false, flags, (ImVec2){}) && dlg->has_parent) {
        set_selection(dlg, "..", true);
        update_can_accept(dlg);
        if (igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left))
            (void)load_parent(dlg);
    }

    for (size_t i = 0; i < darray_count(dlg->dirs); i++) {
        struct fs_dirent *ent = DA(dlg->dirs, i);
        char label[PATH_MAX + 2];

        snprintf(label, sizeof(label), "%s/", ent->name);

        if (igSelectable_Bool(label, dlg->selection_is_dir && !strcmp(dlg->selection, ent->name),
                              ImGuiSelectableFlags_AllowDoubleClick, (ImVec2){})) {
            set_selection(dlg, ent->name, true);
            update_can_accept(dlg);

            if (igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left)) {
                char next[PATH_MAX];
                cerr err = path_join(next, sizeof(next), dlg->cwd, ent->name);
                if (!IS_CERR(err))
                    (void)load_directory(dlg, next);
            }
        }
    }

    for (size_t i = 0; i < darray_count(dlg->files); i++) {
        struct fs_dirent *ent = DA(dlg->files, i);

        if (igSelectable_Bool(ent->name, !dlg->selection_is_dir && !strcmp(dlg->selection, ent->name),
                              ImGuiSelectableFlags_AllowDoubleClick, (ImVec2){})) {
            set_selection(dlg, ent->name, false);
            update_can_accept(dlg);

            if (igIsMouseDoubleClicked_Nil(ImGuiMouseButton_Left))
                do_accept(dlg);
        }
    }
}

cerr ui_debug_fs_open(ui_debug_fs_dialog *dlg, const struct ui_debug_fs_config *cfg,
                      const struct fs_ops *ops)
{
    if (!dlg || !cfg || !cfg->on_accept)
        return CERR_INVALID_ARGUMENTS;

    err_on(darray_count(dlg->dirs) || darray_count(dlg->files), "FS dialog dir/file arrays not empty\n");
    darray_init(dlg->dirs);
    darray_init(dlg->files);

    dlg->active = true;
    dlg->modal = cfg->modal;
    dlg->has_parent = false;
    dlg->selection_is_dir = false;
    dlg->can_accept = false;
    dlg->ops = ops ? ops : &fs_ops_posix;
    dlg->cfg = *cfg;
    clear_selection(dlg);

    if (!dlg->cfg.action_label)
        dlg->cfg.action_label = "Open";
    if (!dlg->cfg.title)
        dlg->cfg.title = "Filesystem";
    if (!dlg->cfg.select_mode)
        dlg->cfg.select_mode = UI_DEBUG_FS_SELECT_FILE;

    if (dlg->cfg.modal)
        igOpenPopup_Str(dlg->cfg.title, 0);

    if (cfg->start_dir)
        return load_directory(dlg, cfg->start_dir);

    char dir[PATH_MAX];
    CERR_RET_CERR(dlg->ops->get_cwd(dir));

    return load_directory(dlg, dir);
}

void ui_debug_fs_draw(ui_debug_fs_dialog *dlg)
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    bool open = true;
    bool visible;
    ImVec2 avail;
    ImVec2 left;

    if (!dlg || !dlg->active)
        return;

    ImGuiIO *io = igGetIO_Nil();
    ImVec2 init = { 600.0f, 400.0f };
    if (io) {
        init.x = io->DisplaySize.x * 0.25f;
        init.y = io->DisplaySize.y * 0.25f;
    }
    igSetNextWindowSize(init, ImGuiCond_FirstUseEver);

    if (dlg->modal)
        visible = igBeginPopupModal(dlg->cfg.title, &open, flags);
    else
        visible = igBegin(dlg->cfg.title, &open, flags);

    if (!visible) {
        cleanup(dlg);

        if (!dlg->modal)
            igEnd();

        return;
    }

    igTextUnformatted(dlg->cwd, NULL);
    igSeparator();

    avail = igGetContentRegionAvail();
    left = (ImVec2){ dlg->cfg.draw_right_panel ? avail.x * 0.55f : avail.x,
                     avail.y - igGetFrameHeightWithSpacing() };

    if (igBeginChild_Str("fs_entries", left, ImGuiChildFlags_Borders, 0))
        draw_entries(dlg);
    igEndChild();

    if (dlg->cfg.draw_right_panel) {
        igSameLine(0.0f, 8.0f);
        if (igBeginChild_Str("fs_side", (ImVec2){ avail.x - left.x - 8.0f, left.y },
                             ImGuiChildFlags_Borders, 0)) {
            const char *sel = dlg->selection[0] ? dlg->selection : NULL;
            dlg->cfg.draw_right_panel(dlg->cwd, sel, dlg->selection_is_dir, dlg->cfg.data);
        }
        igEndChild();
    }

    igSpacing();

    if (igButton("Cancel", (ImVec2){}))
        cleanup(dlg);

    igSameLine(0.0f, 8.0f);

    update_can_accept(dlg);

    if (!dlg->can_accept)
        igBeginDisabled(true);

    if (igButton(dlg->cfg.action_label, (ImVec2){}))
        do_accept(dlg);

    if (!dlg->can_accept)
        igEndDisabled();

    if (dlg->modal) {
        if (!dlg->active)
            igCloseCurrentPopup();

        igEndPopup();
    } else {
        igEnd();
    }
}
