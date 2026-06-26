// SPDX-License-Identifier: Apache-2.0
//
// ImGui REPL console for the Umka scripting layer. Each submitted line is
// wrapped in a throwaway main() and run against the live clap context, so it
// can drive the same FFI as loaded scripts (move entities, query the scene,
// log, etc.). Results and errors are echoed in a scrolling history; anything a
// command prints via clap::dbg() also goes to the engine log/console.
//
// Debug-only: the console needs cimgui, so it is dropped in final builds.

#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif /* __EMSCRIPTEN__ */

#include "common.h"
#include "clap.h"
#include "memory.h"
#include "script.h"
#include "internal.h"
#include "ui-debug.h"
#ifndef __EMSCRIPTEN__
#include "ui-debug-fs.h"
#endif /* !__EMSCRIPTEN__ */

#define REPL_MAX_LINES  256
#define REPL_LINE_LEN   256
#define REPL_INPUT_LEN  512
#define REPL_WINDOW_W   480
#define REPL_WINDOW_H   240

static struct repl_state {
    char    input[REPL_INPUT_LEN];
    char    lines[REPL_MAX_LINES][REPL_LINE_LEN];
    char    *open_name;         /* file picked via Open, run on the next draw */
    char    *open_source;
#ifndef __EMSCRIPTEN__
    ui_debug_fs_dialog fs_dialog;
#endif /* !__EMSCRIPTEN__ */
    int     head;               /* next slot to write */
    int     count;              /* number of valid lines */
    bool    scroll;             /* scroll to bottom on next draw */
    bool    greeted;
} repl;

static void repl_append(const char *fmt, ...) __printf(1, 2);
static void repl_append(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(repl.lines[repl.head], REPL_LINE_LEN, fmt, ap);
    va_end(ap);

    repl.head = (repl.head + 1) % REPL_MAX_LINES;
    if (repl.count < REPL_MAX_LINES)    repl.count++;
    repl.scroll = true;
}

/* Run @source as a one-shot script, echoing the outcome into the history. */
static void repl_run(struct clap_context *ctx, const char *name, const char *source)
{
    LOCAL_SET(script, s) = CRES_RET(
        script_new(ctx, name, source),
        { repl_append("error: could not create interpreter"); return; }
    );

    CERR_RET(script_compile(s), goto error);
    CERR_RET(script_run(s),     goto error);

    /*
     * A submission that registered an on_frame() callback or created a UI
     * element must outlive this call; hand it to the context so it keeps
     * ticking (and is released at shutdown) instead of being dropped here.
     */
    if (script_persistent(s))
        CERR_RET(clap_script_add(ctx, s),
                 { repl_append("error: cannot keep the script ticking"); return; });

    repl_append("ok");
    return;

error:
    repl_append("%s", script_error(s) ? script_error(s) : "error");
}

/* Wrap the input in a main() and run it. */
static void repl_submit(struct clap_context *ctx)
{
    if (!repl.input[0])     return;

    repl_append("> %s", repl.input);

    LOCAL(char, wrapped);
    (void)CRES_RET(
        mem_asprintf(&wrapped, "import \"clap.um\"\nfn main() {\n%s\n}\n", repl.input),
        { repl_append("error: out of memory"); return; }
    );

    repl_run(ctx, "repl", wrapped);
}

/*
 * Open: the file dialogs hand their pick over asynchronously (the browser's
 * from outside the frame loop altogether), so the file is queued here and run
 * from the next draw, like a submitted line.
 */
static void repl_open_queue(const char *name, const char *source)
{
    mem_free(repl.open_name);
    mem_free(repl.open_source);
    repl.open_name   = memdup(name, strlen(name) + 1);
    repl.open_source = memdup(source, strlen(source) + 1);
    if (!repl.open_name || !repl.open_source)   repl_append("error: out of memory");
}

static void repl_open_run(struct clap_context *ctx)
{
    if (repl.open_name && repl.open_source) {
        repl_append("> open %s", repl.open_name);
        repl_run(ctx, repl.open_name, repl.open_source);
    }

    mem_free(repl.open_name);
    mem_free(repl.open_source);
    repl.open_name   = NULL;
    repl.open_source = NULL;
}

#ifdef __EMSCRIPTEN__
/*
 * The browser's file picker. Browsers only raise one from inside a user
 * gesture's own event handler, Safari strictly so, and a button click that
 * ImGui reports from the frame loop is too late for that. So the button's
 * press arms a one-shot listener for the click event that ends it, and the
 * picker is raised from there. A release outside the window ends the gesture
 * without a click, in which case the next press disarms the listener, so
 * a later click somewhere else does not raise the picker.
 */
EM_JS(void, repl_www_open, (void), {
    function open() {
        window.removeEventListener("mousedown", disarm, true);

        /*
         * In the document, hidden, for the whole round trip: WebKit is free
         * to collect a detached file input once click() returns, and the
         * change event then has nothing to fire on.
         */
        var input = document.createElement("input");
        input.type = "file";
        input.accept = ".um";
        input.style.display = "none";
        document.body.appendChild(input);
        input.addEventListener("cancel", function() { input.remove(); });
        input.addEventListener("change", function() {
            var file = input.files[0];
            input.remove();
            if (!file)  return;

            file.text().then(function(text) {
                var name = stringToNewUTF8(file.name);
                var source = stringToNewUTF8(text);
                ccall("script_repl_open_file", null, ["number", "number"], [name, source]);
                _free(name);
                _free(source);
            });
        });
        input.click();
    }

    function disarm() {
        window.removeEventListener("click", open, true);
    }

    window.addEventListener("click", open, { once: true, capture: true });
    window.addEventListener("mousedown", disarm, { once: true, capture: true });
});

EMSCRIPTEN_KEEPALIVE void script_repl_open_file(const char *name, const char *source)
{
    repl_open_queue(name, source);
}

/* The press, not the release: the click event that follows is what opens the picker */
static bool repl_open_wanted(bool clicked)
{
    return igIsItemActivated();
}

static void repl_open(void)
{
    repl_www_open();
}

static void repl_open_draw(void) {}
#else /* !__EMSCRIPTEN__ */
static bool repl_open_wanted(bool clicked)
{
    return clicked;
}

static const char * const repl_open_exts[] = { ".um", NULL };

static void repl_open_accept(const char *cwd, const char *name, bool is_dir, void *data)
{
    if (!name || !*name || is_dir)  return;

    char path[PATH_MAX];
    CERR_RET(path_join(path, sizeof(path), cwd, name),
             { repl_append("error: path too long: %s/%s", cwd, name); return; });

    LOCAL(char, source);
    source = script_read_file(path);
    if (!source)                    { repl_append("error: cannot read %s", path); return; }

    repl_open_queue(name, source);
}

static void repl_open(void)
{
    struct ui_debug_fs_config cfg = {
        .title          = "Open an Umka script",
        .modal          = true,
        .input_field    = "File name:",
        .action_label   = "Open",
        .select_mode    = UI_DEBUG_FS_SELECT_FILE,
        .extensions     = repl_open_exts,
        .on_accept      = repl_open_accept,
    };

    CERR_RET(ui_debug_fs_open(&repl.fs_dialog, &cfg, NULL),
             repl_append("error: cannot open the file dialog"));
}

static void repl_open_draw(void)
{
    ui_debug_fs_draw(&repl.fs_dialog);
}
#endif /* !__EMSCRIPTEN__ */

void script_repl_show(struct clap_context *ctx)
{
    /* Only ahead of an actual igBegin(): the request would stick to the next window otherwise */
    if (ui_debug_module(DEBUG_REPL)->display)
        igSetNextWindowSize((ImVec2){ REPL_WINDOW_W, REPL_WINDOW_H }, ImGuiCond_FirstUseEver);

    debug_module *m = ui_igBegin_name(DEBUG_REPL, 0, "umka repl");
    if (!m->display)    return;

    /* The modal picker lives in this window's ID scope, where Open raised it */
    repl_open_draw();
    repl_open_run(ctx);

    if (m->unfolded) {
        if (!repl.greeted) {
            repl.greeted = true;
            repl_append("umka REPL. Example:");
            repl_append("  clap::get_entity3d(\"name\").get_xform().rotate_Y(90)");
        }

        /* Output history fills the window above a one-line input footer. */
        float footer = igGetFrameHeightWithSpacing();
        igBeginChild_Str("repl_output", (ImVec2){ 0, -footer }, ImGuiChildFlags_Borders, 0);
        for (int i = 0; i < repl.count; i++) {
            int idx = (repl.head - repl.count + i + REPL_MAX_LINES) % REPL_MAX_LINES;
            igTextUnformatted(repl.lines[idx], NULL);
        }
        if (repl.scroll) {
            igSetScrollHereY(1.0f);
            repl.scroll = false;
        }
        igEndChild();

        bool clicked = igButton("Open", (ImVec2){});
        if (repl_open_wanted(clicked))  repl_open();
        igSameLine(0.0f, -1.0f);

        igPushItemWidth(-FLT_MIN);
        bool reclaim_focus = false;
        if (igInputText("##repl_input", repl.input, sizeof(repl.input),
                        ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL)) {
            repl_submit(ctx);
            repl.input[0] = 0;
            reclaim_focus = true;
        }
        igPopItemWidth();
        if (reclaim_focus)              igSetKeyboardFocusHere(-1);
    }

    ui_igEnd(DEBUG_REPL);
}
