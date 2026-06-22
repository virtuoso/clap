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

#include "common.h"
#include "clap.h"
#include "memory.h"
#include "script.h"
#include "ui-debug.h"

#define REPL_MAX_LINES  256
#define REPL_LINE_LEN   256
#define REPL_INPUT_LEN  512

static struct repl_state {
    char    input[REPL_INPUT_LEN];
    char    lines[REPL_MAX_LINES][REPL_LINE_LEN];
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

/* Wrap the input in a main() and run it as a one-shot script. */
static void repl_submit(struct clap_context *ctx)
{
    if (!repl.input[0])     return;

    repl_append("> %s", repl.input);

    LOCAL(char, wrapped);
    (void)CRES_RET(
        mem_asprintf(&wrapped, "import \"clap.um\"\nfn main() {\n%s\n}\n", repl.input),
        { repl_append("error: out of memory"); return; }
    );

    LOCAL_SET(script, s) = CRES_RET(
        script_new(ctx, "repl", wrapped),
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

void script_repl_show(struct clap_context *ctx)
{
    debug_module *m = ui_igBegin_name(DEBUG_REPL, 0, "umka repl");
    if (!m->display)    return;

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

        igPushItemWidth(-FLT_MIN);
        bool reclaim_focus = false;
        if (igInputText("##repl_input", repl.input, sizeof(repl.input),
                        ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL)) {
            repl_submit(ctx);
            repl.input[0] = 0;
            reclaim_focus = true;
        }
        igPopItemWidth();
        if (reclaim_focus)  igSetKeyboardFocusHere(-1);
    }

    ui_igEnd(DEBUG_REPL);
}
