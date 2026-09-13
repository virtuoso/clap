You contributions are welcome and appreciated! This document is here to help with that.

# Index

- [Where do I start?](#where-do-i-start)
- [How do I start?](#how-do-i-start)
  - [But C?](#but-c)
  - [Code layout](#code-layout)
  - [More specifically](#more-specifically)
- [I get build errors](#i-get-build-errors)
- [Coding style](#coding-style)
  - [Object Lifetime Management](#object-lifetime-management)
- [Pull requests](#pull-requests)

## Where do I start?

The easiest place is to take one of the open [issues](https://github.com/virtuoso/clap/issues) and implement/fix/improve what it says. If an issue is unclear or reads as if I'm talking to myself there, please leave a comment and ask for clarification. (Also, feel free to [file new issues](https://github.com/virtuoso/clap/issues/new), having checked that one doesn't already exist for the behavior that you're seeing.) All the usual bug tracking rules apply:
- if you're working on an issue, assign it to yourself
- make sure that the labels match (bug, enchancement, documentation, etc)
- add Fixes: #XYZ tag under your signed-off-by line.

## How do I start?

Unless you've done so already, first [fork](https://github.com/virtuoso/clap/fork) the repository. Follow the build instructions in [README](https://github.com/virtuoso/clap/blob/main/README.md). All supported platforms have a debug build configuration, you'll want to use that except for WASM builds, where a debug build will render at 5 FPS (because all OpenGL calls are followed by glGetError(), which brings most browsers to their knees). Testing WASM builds is best on a "test" configuration (cmake --preset=emtest). The "debug" configuration is still useful, though, as it contains all the debug information and can be debugged in chrome or vscode+chrome as described in [this post by floooh](https://floooh.github.io/2023/11/11/emscripten-ide.html).

Debug builds also have address sanitizer instrumentation, which is useful more often than not, but obviously slows the CPU side of things down considerably.

The "test" and "rel" builds have LTO (link time optimizations) enabled, so take slightly longer to link.

Running ./build.sh will build all 3 (or 6, if you have EMSDK configured), which is good for testing that your code works in all configurations. Otherwise, install a cmake extension for vscode and use F7 to build or F5 to debug just one particular (probably debug) configuration.

If you fix a bug that can concievably be tested for, or an API, please add (a) test(s) to core/test.c.

### But C?

Well, one can't go faster and more cross-platform than C. (That is, unless you're working with windows, in which case just bring your own C library. But hey, the compiler works there, too.)
Of course, this doesn't mean that you can't write horrible monstrosities in C, just like in any other language, you can. But if you ever programmed in anything other than Haskell, you should be able to understand the syntax more or less instantly. Once you understand it, you can start changing it. Searching youtube for "introduction to C" turns up a few playlists, so if you're completely lost, try one or several of those (and then send a pull request to link the best one here; I've checked out a couple and they seem to start really slow, so I'm not going to recommend anything myself).

### Code layout

All the engine C code is in core/, all GLSL shaders are in shaders/, runnable executables are under demo/. Plus, there's a server in tools/server/ and a test binary in core/ that runs on cmake --build. Documentation will eventually be in docs/. Public API headers will be in include/, once there is a public API.

Most dependencies are fetched at cmake configuration stage in deps/ by bootstrap.py, except ODE, which is a git submodule under ode/.

By now, most code should be sufficiently modular so many changes should be localized, unless they are API changes. There aren't many comments in the code, feel free to add them as you go. If you can't figure out what it does or why, check the file's git history and, if that doesn't help, [file a documentation issue](https://github.com/virtuoso/clap/issues/new).

### More specifically

At the moment, the main executable is at demo/ldjam56/, so the main compilation unit and the main() function are at [demo/ldjam56/onehandclap.c](https://github.com/virtuoso/clap/blob/main/demo/ldjam56/onehandclap.c). Each frame a render_frame() function is called to construct a frame. Most of the interesting stuff is called from there (and main()).

A short tour of core/:
- model.c: all of the 3D model, textured model, entities of these models, their animations and rendering
- gltf.c: the only source of 3D models at the moment are .gltf or .glb files, this is where they are loaded
- scene.c: loads a [scene file](https://github.com/virtuoso/ldjam56-asset/blob/main/scene.json), manages light sources etc
- render.h: renderer abstraction that eventually will support renderers other than OpenGL
- render-gl.c: OpenGL renderer implementation over render.h abstraction; most of the GL code should have been moved here by now
- shader.c: shader loading, setting uniforms, loading buffers/textures etc; models_render() from model.c is the main user of this; contains GL code that will soon move to render-gl.c as well
- pipeline.c: builds a pipeline with render passes and things; onehandclap.c/build_main_pl() uses it at the moment
- sound.c: plays sounds (OGG files) using OGG, VorbisFile ond OpenAL
- test.c: tests for non-interactive APIs
- object.h/object.c: source of ref_new()/ref_put(), which are used for refcounted object management all over the place; look for DECLARE_REFCLASS()/DECLARE_REFCLASS2() for examples of such objects
- util.c/util.h: various utility functions, macros, etc etc to compensate for the total lack of container types in the C library; can easily be broken into like 10 different compilation units
- ui.c/ui.h: homemade UI, widgets like menu
- ui-imgui*: the ImGui bindings into the engine, for debug purposes
- ui-debug.c: debug UI using the homemade UI in ui.c, slowly morphing into ImGui widgets
- settings.c: loads and stores various settings of the engine (window dimentions, position, sound volume, all the debug widget positions)
- memory.c/memory.h: memory management
... to be continued (send pull requests).

Another useful tool is git grep. Also, build configurations generate compile_commands.json that can be plugged into your favorite IDE. Having done that, you'll be able to inspect the code much more efficiently.

### Adding uniforms

To add a new uniform, the bare minimum checklist is:
- add it to the appropriate UBO in `core/render-bindings.json`
- anywhere: have a property or somesuch that this uniform will be uploading to the shader (e.g. `render_options::contrast`)
- model.c: carefully add shader_set_var_*() of this uniform to `_models_render()`, where it gets set to the above
- (optional, but often useful): add a debug UI checkbox/slider/color picker/etc so you can set this uniform manually if you need to
- shaders/*: use the new uniform in the shader code (making sure to include corresponding `ubo_*.glsl`).
That's it.

## I get build errors

If you're suddenly getting build errors, [check here](https://github.com/virtuoso/clap/tree/main/docs/build-errors.md). Otherwise, [report an issue](https://github.com/virtuoso/clap/issues/new) and/or send a pull request.

## Coding style

There's a .clang-format file at the top, unfortunately, as of this writing clang-format still isn't quite able to align pointers on the right the way that it's done in the code, so either fix up after it or just format the code by hand.

The guidelines below take precedence over the surrounding code: when a file predates a rule, new code in that file follows the rule anyway, even if the neighboring lines don't. Bringing the rest of the file in line is a separate commit, typically one bulk reflow at the end of a series, never folded into a functional change.

- Indentation is 4 spaces
- There's a space between a control statement and whatever follows
- Curly brackets go on the same line as the control statement it belongs to
- Curly bracket at the start of a function goes on a new line, though, unless it's a short inline function that can entirely fit in one line, then it's a judgement call
- A short guard like `if (condition) action();` reads better on a single line than split across two, so prefer that when the whole thing fits comfortably (roughly 100 columns or less); once it needs braces or spills past that, give it its own line
- Blank lines separate logically contiguous statements, although this can be subjective
```C
void endless_loop(void)
{
    if (global_state.should_abort) {
        err("aborting before spinning forever\n");
        abort();
    }

    for (;;)
        ;
}
```
- Comments are good: both /* C */ and // C++ style
- Multi-line comments are either a series of `//` lines, or `/*` and `*/` on their own lines with ` * `-prefixed lines in between; no text on the opening `/*` or closing `*/` lines
- Comments describe the code as it is, not how it got there: "it's FOO and not BAR because BAR breaks BAZ" is the useful form, "this used to abort()" is a changelog, and the changelog is one `git blame` away. The same goes for tense: code is always in the present
- Punctuation in source files is plain ASCII: no typographic dashes, quotes, ellipses or other non-ASCII punctuation in code or comments; an em dash is spelled `--`, which is still ASCII. Non-ASCII letters and symbols are fine (a contributor's name in a comment, emoji in test data), and so is Markdown documentation; `scripts/check-ascii.py` checks staged changes for this when plugged in as a pre-commit hook
- SPDX license headers: multiline comments in header files (/* SPDX... */) and single-line in C/C++/ObjC files (// SPDX...)
- Declare variables at assignment site instead of the top of the function
- Self-evident code is better
- Unused function parameters are just that, unused: no `__unused` annotation, no `(void)param;` ritual. A callback that ignores an argument of its prototype is the normal case, not something to flag. `__unused` on a function is occasionally justified, and then the reason goes in a comment next to it; the annotation earns its keep while prototyping or debugging, on transient code that doesn't get committed
- In conditional expressions:
    - constants go on the right: no "if (5 == number)" and suchlike; your compiler should warn you if you accidentally write an assignment in a conditional expression
    - no need for spelling out the obvious: "if (x == nullptr)" => "if (!x)", "if (b == true)" => "if (b)" etc
- Prefer `nullptr` over `NULL`: we build as C23, and next to it `NULL` reads like a caveman's scream
- Short control flow statements with a single-statement body (`if`, `else`, loops) go on one line, and the body starts at a column that is a multiple of 4:
    - a one-liner on its own uses the nearest multiple of 4 past its condition
    - when several similar one-liners are in close visual proximity — commonly within the same function, though exceptions are made for very short functions — the longest of them picks the column and they all align to it
    - what counts as "close visual proximity" is a judgement call
```C
    if (item->type != TYPE_STRING)  return false;

    if (value_new == value)     return;
    if (value_new > value_max)  return;
```
- Vertical alignment of things is encouraged, but not always necessary; it's good if it improves readability, which is, of course, subjective
- Structure members should be right-aligned for improved readability
- In vertically aligned declarations, a pointer's name is aligned by its (first) asterisk, not by the name's first character; that is, the `*` goes in the column where non-pointer names start:
```C
char    *s;
double  n;
bool    b;
```
- Adding a member to a structure must not open a padding hole: put it where its alignment fits, next to members of the same size if need be. `pahole` shows the holes, but they are just as visible from the member sizes, so look before you add. The one conceivable exception is a hole forced by grouping members into a cacheline, which shouldn't happen; if it ever does, the hole is documented in a comment right next to it
- Line length soft limit: 120 columns (statements can span multiple lines when needed)
- Naming conventions:
    - Use snake_case throughout (C and ObjC code)
    - Avoid Hungarian notation prefixes like `p` for pointers or `pp` for pointer-to-pointer
    - Choose clear, descriptive names over terse abbreviations
    - Boolean predicates read as the question they answer: `thing_is_active()`, not `thing_active()`
- Resolve configuration (environment variables, settings) once on the init path and store the result; don't re-query it on hot paths
- Statistics or counters that can be written from multiple threads are either atomic or come with an explicit concurrency plan; the atomics stay internal, user-facing snapshot APIs expose plain types

### There is no such thing as a "sensible default"

Every time a "sensible default" is introduced,
- a problem is postponed instead of solved, likely to the worst possible moment
- a problem is quietly relocated to somebody else's shoulders.

### Conditional compilation

- A large chunk of code behind `#ifdef CONFIG_*` is a sign that it belongs in a separate compilation unit, gated at the cmake level instead
- Such a unit's public-facing API goes in its own header, and that is where `#ifdef` is appropriate: the real declarations on one side, no-op/passthrough `static inline` stubs on the other
- Call sites into gated code use those stubs; peppering unrelated code with `#ifdef`s is bad form
- The trailing comment on `#endif` names the condition of the branch it closes: after an `#else`, that's the inverted condition (e.g. `#ifdef CONFIG_FOO ... #else ... #endif /* !CONFIG_FOO */`)

### Object Lifetime Management

Clap uses a ref-counted object system (see object.h/object.c) for managing object lifetimes. When implementing new types that need reference counting:

- Use `DEFINE_REFCLASS_INIT_OPTIONS()` to define constructor options structures
- Use `DECLARE_REFCLASS()` in headers to expose the refclass
- Use `DEFINE_REFCLASS2()` in implementation to define both make and drop functions
- Constructor functions (`_make`) should:
    - Return `cerr` for error handling
    - Validate parameters at the beginning
    - Initialize any embedded structures
    - Use `container_of(ref, struct_name, ref)` to get the object pointer
- Destructor functions (`_drop`) should:
    - Clean up any resources (uninit nodes, free buffers, etc.)
    - Release any held references with `ref_put()`
    - Not free the object itself (handled by refclass infrastructure)
- Effect-specific code should use callbacks (init/done/process) stored in descriptor structures
- Use static const descriptor arrays indexed by type enums for polymorphic behavior
- APIs that add objects to containers should `ref_get()` them
- APIs that remove objects from containers should `ref_put()` them
- Create objects with `ref_new_checked()` which returns a `cresp()` for proper error handling

This approach keeps layering clean, decouples implementations, and ensures proper memory management without manual malloc/free tracking.

### Error handling

Fallible functions report errors through a single return channel, never out-parameters or errno-style globals (see error.h):

- `cerr` — a function that either succeeds or fails but has no value to return.
- `cresp(T)` / `cres(T)` — a function that returns a `T *` / scalar `T` *or* an error. These are declared for refcounted types by `DECLARE_REFCLASS()`, and otherwise with the `cresp_ret()` / `cres_ret()` declarators. `cerr` and every `cres*` share a layout (the error code sits at offset 0), so `IS_CERR()` works on all of them.

You can test a result with `IS_CERR()`, but for the common "unwrap the value or bail out" case prefer the `CRES_RET` / `CERR_RET` family over an explicit `if (IS_CERR(...))` branch — they unwrap and escape in a single expression:

```C
/* value, or return cresp(thing) on error */
struct thing *t = CRES_RET_T(thing_get(id), thing);
/* value, or return the bare cerr */
struct thing *t = CRES_RET_CERR(thing_get(id));
/* value, or run any statement (goto/return/...); the cres is available as __resp */
struct thing *t = CRES_RET(thing_get(id), { err_cerr(cerr_error_cres(__resp), "no thing\n"); goto err; });
```

Functions returning a bare `cerr` use the `CERR_RET*` variants (the cerr is `__cerr` inside the statement):

```C
CERR_RET(clap_set_lighting_lut(ctx, "scifi bluegreen"),
         err_cerr(__cerr, "failed to set LUT\n"));    /* log, then fall through */
CERR_RET_CERR(some_step());                           /* propagate the cerr */
CERR_RET_T(some_step(), thing);                       /* propagate as cresp(thing) */
```

Rule of thumb: if you catch yourself writing `if (IS_CERR(x)) return ...;` right after a fallible call, you almost always want `CRES_RET(x, return ...)` (or its `_T` / `_CERR` variants) instead. Reserve a bare `IS_CERR()` for the genuinely conditional cases, where an error is an expected outcome handled in place (e.g. a lookup miss that yields a default), not propagated.

The single-channel rule extends to in-band sentinels: a lookup or accessor that can fail returns `cres(int)` / `cresp(T)` with a distinguishing error — `CERR_NOT_FOUND` for a miss, `CERR_INVALID_ARGUMENTS` for a bad index — not `-1` or `NULL`. Internal validation helpers return `cerr` too, with a formatted reason so the failure carries context: `CERR_INVALID_ARGUMENTS_REASON(.fmt = "invalid node %d", .arg0 = (void *)(intptr_t)node)`. When converting a sentinel-returning function, watch for sentinels *stored in the data* — a `cres` getter happily returns a stored `-1` as a success value unless the check survives as a `CERR_NOT_FOUND`.

In the other direction, don't cascade `cres` where there's nothing to report: a predicate that answers a yes/no question returns plain `bool` — `cres(bool)` on a "does X exist" query is a hat on a hat.

### Scope-based cleanup

Prefer scope-based (RAII-style) cleanup over manual `mem_free()` / `ref_put()` / `fclose()` on each exit path. Freeing a *scope-local* resource by hand is almost always a latent bug — the next early `return` (or a newly added `CRES_RET`) leaks it. The `LOCAL*` macros (util.h) attach a compiler cleanup that runs at scope exit:

```C
LOCAL(char, uri);      /* char *uri, mem_free()'d at scope exit, NULL-initialised */
LOCAL(FILE, f);        /* FILE *f, fclose()'d at scope exit */
```

`LOCAL(t, x)` declares an auto-released `t *x` (NULL-initialised); `LOCAL_SET(t, x)` is the same but lets you assign on the declaration line. Built-in cleanups exist for `char`, `uchar`, `void`, `FILE` and file descriptors (`cleanup__fd`).

For a refcounted type, give it a cleanup that drops the reference — `DECLARE_CLEANUP(T)` in the header and `DEFINE_CLEANUP(T, if (*p) ref_put(*p))` in the .c — so `LOCAL(T, x)` / `LOCAL_SET(T, x)` release it automatically. When the function hands the resource out on the success path, cancel the cleanup with `NOCU()`:

```C
cresp(script) script_load(struct clap_context *ctx, const char *name, const char *src)
{
    LOCAL_SET(script, s) = CRES_RET_T(script_new(ctx, name, src), script);
    CERR_RET_T(script_compile(s), script);   /* s is ref_put() on early return */
    CERR_RET_T(script_run(s), script);
    return cresp_val(script, NOCU(s));        /* success: hand off, skip cleanup */
}
```

Together with the error-handling helpers above, error paths carry no cleanup boilerplate at all.

Also, check out existing commits in the tree to get a good idea of code/commit style etc.

## Pull requests

Some things to check:
- Sign off your commits (git commit -s)
- Make commits self-contained: each commit should be able to compile and produce a working binary without the commits that follow it
- At the same time, make commits as small as possible, that is, split large commits into smaller ones that are still self-contained
- One logical change per commit: a bug fix and the test that covers it, a cleanup and the feature it enables, are separate commits. An "and" in the subject line, or in your head ("fix X and add a test for it"), is a red flag that two commits are lumped into one
- Commit messages don't reference the future: "this will be useful for the upcoming mesh rework" is permissible, "two commits from now this function is called by xyz" is not. The reader may not have that commit, a rebase may reorder it, and whatever is worth saying about a later commit belongs in that commit's message
- Referring back to earlier commits is done kernel-style, always as `<abbreviated sha1> ("<commit subject line>")`: either in a `Fixes:` tag line at the bottom of the message, or in the body prefixed with "commit". Such references only make sense for commits that are already in upstream `main`, which is never rebased; if the commit you're fixing is still on your branch, the fix is not a new commit, it's a fixup of that commit (`git commit --fixup=<sha1>`), because "fix a bug added earlier in this branch" is never ok
- Write good commit messages (not including a link to a howto page, because googling for "writing commit messages" already comes up with a lot of good articles/blog posts). In short:
  - Use imperative mood ("change XXX" vs "XXX is changed") — in the entire message, not just the subject line
  - Describe the problem in the present tense ("XYZ does a stupid thing. Fix it."), not the past ("XYZ did a stupid thing"): the message is read on top of the parent commit, where the problem still exists, and the past tense reads as if it were already fixed
  - State the reason for the commit, what is being done, why it is done and why it is done in this particular way
  - First explain what the commit is and why it exists, then how; don't open with implementation minutiae
  - Implementation details belong in the message only when defending a particular choice over the alternatives; everything else is visible in the diff and belongs in code comments
  - Subject line most often starts with a module name or comma separated names, then colon, then the summary of the change ("xyarray: Reimplement as an xyzarray")
  - One line commit messages are acceptable if the summary covers it all, if it's a one-line patch
  - Check the existing commit history
- No *new* compiler warnings (the build will fail, because -Wall and -Werror are enabled, unless you're on windows)
- Rebase your code onto the "main" branch, so that everything is fast-forward and no merge commits are necessary
- `main` means the upstream branch published in the github repository, `origin/main` once you've run `git remote update origin`; your local `main` is not an indicator of anything. Integration goes through `testing` on github, which is rebased and force-pushed freely: once all actions are green and smoke testing passes, the same commits are pushed to `main`, which is strictly fast-forward only
- Make sure that all github actions pass (with a possible exception of www deployment action).
