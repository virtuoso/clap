/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCRIPT_H__
#define __CLAP_SCRIPT_H__

#include <stdbool.h>

#include "config.h"
#include "error.h"
#include "object.h"

struct clap_context;
typedef struct script script;

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
#else /* !CONFIG_SCRIPTING */
static inline void script_run_frame(struct script *s) {}
#endif /* !CONFIG_SCRIPTING */

#endif /* __CLAP_SCRIPT_H__ */
