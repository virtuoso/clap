/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCRIPT_INTERNAL_H__
#define __CLAP_SCRIPT_INTERNAL_H__

#include <stdbool.h>
#include <stdint.h>

#include "umka_api.h"
#include "object.h"     /* struct ref */
#include "util.h"       /* struct list */
#include "script.h"

/* struct script_priv: per-script state each FFI module declares via
 * CLAP_SCRIPT_PRIV(), aggregated so the core need not grow a member per FFI. */
#include "script-priv-gen.h"

/*
 * Layout of the script object, shared across the scripting subsystem. External
 * callers see only the opaque type in script.h; the umka backend lives behind
 * this header and umka.c.
 */

struct clap_context;

struct script {
    struct clap_context *ctx;
    Umka                *vm;
    char                *name;
    char                *source;
    char                *error;
    struct script_priv  priv;
    struct ref          ref;
    bool                compiled;
};

/* Copy the interpreter's current error onto the script and log it (core.c). */
void script_capture_error(script *s);

/**
 * script_read_file() - read a whole script file into memory (loader.c)
 * @path: file to read
 *
 * Return: NUL-terminated, mem_alloc()'d contents, or NULL on any error
 */
char *script_read_file(const char *path);

#endif /* __CLAP_SCRIPT_INTERNAL_H__ */
