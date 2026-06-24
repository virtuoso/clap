/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCRIPT_H__
#define __CLAP_SCRIPT_H__

#include <stdbool.h>

#include "config.h"
#include "error.h"
#include "object.h"

struct clap_context;
typedef struct script script;

/*
 * Live script-directory loader: watch a directory of Umka (.um) files and keep
 * the running set in sync with what's on disk, so scripts can be edited and
 * hot-reloaded without restarting.
 */
typedef struct script_watch script_watch;
cresp_ret(script_watch);

#ifdef CONFIG_SCRIPTING
DEFINE_REFCLASS_INIT_OPTIONS(script,
    struct clap_context *ctx;
    const char          *name;
    const char          *source;
);
DECLARE_REFCLASS(script);
DECLARE_CLEANUP(script);

/** script_context() - the clap context a script's FFI operates on */
struct clap_context *script_context(script *s);

/**
 * script_new() - create an Umka script from source, without compiling it
 * @ctx:    clap context the script's FFI operates on
 * @name:   script name for diagnostics, or NULL for a default
 * @source: Umka program source (borrowed; copied internally)
 *
 * The returned script is not compiled yet: call script_compile() then
 * script_run(). This split lets a caller (e.g. the REPL) retrieve the error
 * text via script_error() before dropping the object.
 */
cresp(script) script_new(struct clap_context *ctx, const char *name, const char *source);

/**
 * script_load() - create, compile and run a script in one step
 *
 * Convenience wrapper around script_new()/script_compile()/script_run() that
 * logs and propagates any error. On success the script's main() has run (so any
 * clap::on_frame() callbacks are registered) and the object is returned for
 * per-frame ticking via clap_script_add().
 */
cresp(script) script_load(struct clap_context *ctx, const char *name, const char *source);

/** script_compile() - compile the script; error retrievable via script_error() */
cerr script_compile(struct script *s);

/** script_run() - run the script's main() function */
cerr script_run(struct script *s);

/** script_run_frame() - invoke every clap::on_frame() callback once */
void script_run_frame(struct script *s);

/** script_error() - last compile/runtime error message, or NULL */
const char *script_error(struct script *s);

/**
 * script_persistent() - does the script own live per-frame callbacks or widgets?
 *
 * True once a script has registered a clap::on_frame() callback or created a UI
 * element, i.e. it must outlive the call that ran it (e.g. a REPL submission) so
 * it keeps ticking rather than being dropped immediately.
 */
bool script_persistent(struct script *s);

/**
 * script_watch_dir() - load and live-watch every .um script in a directory
 * @ctx:  clap context the scripts' FFI operates on
 * @path: directory to scan and watch, created when missing
 *
 * Loads and starts (script_load() + clap_script_add()) every *.um file in
 * @path, then watches it: a file created or modified is (re)loaded, a file
 * removed is unloaded. Reloading drops the previous version's per-frame
 * callbacks and widgets before running the new one. Native only: returns
 * CERR_NOT_SUPPORTED where directory watching is unavailable (e.g. wasm).
 *
 * Must be torn down with script_watch_done() before clap_done().
 *
 * Return: the watch, or CERR_NOT_SUPPORTED / CERR_NOMEM / an fs error.
 */
cresp(script_watch) script_watch_dir(struct clap_context *ctx, const char *path);

/** script_watch_done() - stop watching and unload every script it loaded */
void script_watch_done(script_watch *sw);

#ifndef CONFIG_FINAL
/** script_repl_show() - draw the ImGui REPL console (debug builds only) */
void script_repl_show(struct clap_context *ctx);
/** script_watch_show() - draw the loaded-scripts debug window (debug builds only) */
void script_watch_show(script_watch *sw);
#else /* CONFIG_FINAL */
static inline void script_repl_show(struct clap_context *ctx) {}
static inline void script_watch_show(script_watch *sw) {}
#endif /* CONFIG_FINAL */
#else /* !CONFIG_SCRIPTING */
static inline void script_run_frame(struct script *s) {}
static inline void script_repl_show(struct clap_context *ctx) {}

static inline cresp(script_watch) script_watch_dir(struct clap_context *ctx, const char *path)
{
    return cresp_error(script_watch, CERR_NOT_SUPPORTED);
}

static inline void script_watch_done(script_watch *sw) {}
static inline void script_watch_show(script_watch *sw) {}
#endif /* !CONFIG_SCRIPTING */

#endif /* __CLAP_SCRIPT_H__ */
