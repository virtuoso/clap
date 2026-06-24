// SPDX-License-Identifier: Apache-2.0
//
// Live script-directory loader: watch a directory of Umka (.um) scripts and
// keep the running set in sync with what is on disk. Every file present at
// startup is loaded and started; thereafter a file created or modified is
// (re)loaded and a file removed is unloaded, so scripts can be edited and
// hot-reloaded without restarting the engine. A script that fails to compile or
// run is kept around (not ticking) with its error, for the debug UI to show.
//
// Directory watching -- and therefore this whole module's usefulness -- needs a
// filesystem-watch backend, so script_watch_dir() is a no-op on platforms
// without one (see clap_fs_watch_add()).

#include <stdio.h>
#include <string.h>

#include "clap.h"
#include "script.h"

#ifndef CONFIG_FINAL
#include "ui-debug.h"
#endif /* !CONFIG_FINAL */

/* Read granularity of loader_slurp() */
#define LOADER_CHUNK    4096

/* One loaded file: its name (relative to the watched dir) and running script. */
struct loaded_script {
    struct list     entry;
    script          *s;         /* borrowed ref while ticking; owned here */
    bool            failed;     /* last (re)load hit a compile/run error */
    char            name[PATH_MAX];
};

struct script_watch {
    struct clap_context *ctx;
    clap_fs_watch       *watch;
    struct list         scripts;
    char                dir[PATH_MAX];
};

/* Read a whole file into a NUL-terminated, mem_alloc()'d buffer (NULL on error) */
static char *loader_slurp(const char *path)
{
    LOCAL_SET(fs_file, f) = CRES_RET(fs_open(&fs_ops_posix, path, FS_READ, false, false, true),
                                     return NULL);
    darray(char, buf);
    darray_init(buf);

    for (size_t off = 0;;) {
        if (!darray_resize(buf, off + LOADER_CHUNK))    goto err;

        size_t n = CRES_RET(fs_read(f, buf.x + off, LOADER_CHUNK), goto err);
        off += n;
        if (n < LOADER_CHUNK) {
            buf.x[off] = 0;
            return buf.x;
        }
    }

err:
    darray_clearout(buf);
    return NULL;
}

static struct loaded_script *loader_find(struct script_watch *sw, const char *name)
{
    struct loaded_script *ls;

    list_for_each_entry(ls, &sw->scripts, entry)
        if (!strcmp(ls->name, name))    return ls;

    return NULL;
}

/* Drop the script an entry holds: stop its per-frame tick and release it. */
static void loader_drop_script(struct script_watch *sw, struct loaded_script *ls)
{
    if (!ls->s)     return;

    /* A no-op if the script never made it into the tick list (load failed). */
    clap_script_remove(sw->ctx, ls->s);
    ref_put(ls->s);
    ls->s = NULL;
}

static void loader_unload(struct script_watch *sw, struct loaded_script *ls)
{
    list_del(&ls->entry);
    loader_drop_script(sw, ls);
    mem_free(ls);
}

/*
 * (Re)sync a single file with disk. Any previously loaded version is dropped
 * first, so a modified file cleanly replaces the running one; a file that has
 * vanished (deleted or renamed away) leaves nothing behind.
 */
static void loader_reload(struct script_watch *sw, const char *name)
{
    struct loaded_script *ls = loader_find(sw, name);

    char path[PATH_MAX];
    CERR_RET(path_join(path, sizeof(path), sw->dir, name),
             { err("script loader: path too long: %s/%s\n", sw->dir, name); return; });

    LOCAL(char, source);
    source = loader_slurp(path);
    if (!source) {
        /* Gone from disk: forget it entirely. */
        if (ls)     loader_unload(sw, ls);
        return;
    }

    if (!ls) {
        ls = mem_alloc(sizeof(*ls), .zero = 1);
        if (!ls)    return;
        snprintf(ls->name, sizeof(ls->name), "%s", name);
        list_append(&sw->scripts, &ls->entry);
    } else {
        loader_drop_script(sw, ls);
    }

    /*
     * Compile and run in steps (rather than script_load()) so the script object
     * survives a compile/run failure and its error stays reachable via
     * script_error() for the debug UI.
     */
    ls->failed = true;
    ls->s = CRES_RET(script_new(sw->ctx, name, source),
                     { err("script loader: cannot create interpreter for '%s'\n", name); return; });

    CERR_RET(script_compile(ls->s), return);
    CERR_RET(script_run(ls->s), return);
    ls->failed = false;

    CERR_RET(clap_script_add(sw->ctx, ls->s),
             { err("script loader: cannot tick '%s'\n", name); return; });
    dbg("script loader: loaded '%s'\n", name);
}

/* Scan the directory once and load every .um file already present. */
static void loader_scan(struct script_watch *sw)
{
    LOCAL(fs_dir, dir);
    dir = CRES_RET(fs_open_dir(&fs_ops_posix, sw->dir),
                   { err("script loader: cannot scan '%s'\n", sw->dir); return; });

    for (;;) {
        struct fs_dirent ent;

        cerr err = fs_read_dir(dir, &ent);
        if (IS_CERR(err))       /* CERR_EOF at the end */
            break;
        if (ent.is_dir || !str_endswith(ent.name, ".um"))   continue;

        loader_reload(sw, ent.name);
    }
}

/* Directory changed: re-sync the affected .um file with disk. */
static void loader_on_change(const char *path, const struct fs_watch_event *ev, void *data)
{
    struct script_watch *sw = data;

    if (str_endswith(ev->name, ".um"))  loader_reload(sw, ev->name);
}

cresp(script_watch) script_watch_dir(struct clap_context *ctx, const char *path)
{
    if (!ctx || !path)  return cresp_error(script_watch, CERR_INVALID_ARGUMENTS);

    struct script_watch *sw = mem_alloc(sizeof(*sw), .zero = 1);
    if (!sw)            return cresp_error(script_watch, CERR_NOMEM);

    sw->ctx = ctx;
    list_init(&sw->scripts);
    snprintf(sw->dir, sizeof(sw->dir), "%s", path);

    /*
     * A missing directory is created rather than reported, so a program
     * starts out with an empty, watched directory to drop scripts into
     */
    cresp(fs_dir) probe = fs_open_dir(&fs_ops_posix, path);
    if (!IS_CERR(probe)) {
        CERR_RET(fs_close_dir(probe.val),);
    } else if (IS_CERR_CODE(probe, CERR_NOT_FOUND)) {
        CERR_RET(fs_make_dir(&fs_ops_posix, path),
                 { mem_free(sw); return cresp_error_cerr(script_watch, __cerr); });
    }

    /* Watch first, then scan: a change landing during the scan just re-syncs. */
    sw->watch = CRES_RET(
        clap_fs_watch_add(ctx, path,
                          FS_WATCH_CREATED | FS_WATCH_MODIFIED |
                          FS_WATCH_DELETED | FS_WATCH_RENAMED,
                          loader_on_change, sw),
        { mem_free(sw); return cresp_error_cerr(script_watch, __resp); });

    loader_scan(sw);

    return cresp_val(script_watch, sw);
}

void script_watch_done(script_watch *sw)
{
    if (!sw)        return;

    struct loaded_script *ls, *iter;
    list_for_each_entry_iter(ls, iter, &sw->scripts, entry)
        loader_unload(sw, ls);

    if (sw->watch)  clap_fs_watch_remove(sw->ctx, sw->watch);

    mem_free(sw);
}

#ifndef CONFIG_FINAL
void script_watch_show(script_watch *sw)
{
    if (!sw)            return;

    debug_module *m = ui_igBegin_name(DEBUG_SCRIPTS, 0, "umka scripts");
    if (!m->display)    return;

    if (m->unfolded) {
        igText("watching: %s", sw->dir);
        igSeparator();

        if (list_empty(&sw->scripts))   igText("(no scripts loaded)");

        struct loaded_script *ls, *iter;
        list_for_each_entry_iter(ls, iter, &sw->scripts, entry) {
            igPushID_Ptr(ls);

            bool unload = igButton("unload", (ImVec2){});
            igSameLine(0.0, 0.0);
            igText(ls->failed ? "  %s  (error)" : "  %s", ls->name);
            if (ls->failed && script_error(ls->s))  igTextWrapped("%s", script_error(ls->s));

            igPopID();

            /* Deferred: unload frees ls, but list_for_each_entry_iter cached the next. */
            if (unload)                             loader_unload(sw, ls);
        }
    }

    ui_igEnd(DEBUG_SCRIPTS);
}
#endif /* !CONFIG_FINAL */
