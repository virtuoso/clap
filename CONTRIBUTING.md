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

## I get build errors

If you're suddenly getting build errors, [check here](https://github.com/virtuoso/clap/tree/main/docs/build-errors.md). Otherwise, [report an issue](https://github.com/virtuoso/clap/issues/new) and/or send a pull request.

## Coding style

There's a .clang-format file at the top, unfortunately, as of this writing clang-format still isn't quite able to align pointers on the right the way that it's done in the code, so either fix up after it or just format the code by hand.

- Indentation is 4 spaces
- There's a space between a control statement and whatever follows
- Curly brackets go on the same line as the control statement it belongs to
- Curly bracket at the start of a function goes on a new line, though, unless it's a short inline function that can entirely fit in one line, then it's a judgement call
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
- SPDX license headers: multiline comments in header files (/* SPDX... */) and single-line in C/C++/ObjC files (// SPDX...)
- Declare variables at assignment site instead of the top of the function
- Self-evident code is better
- In conditional expressions:
    - constants go on the right: no "if (5 == number)" and suchlike; your compiler should warn you if you accidentally write an assignment in a conditional expression
    - no need for spelling out the obvious: "if (x == NULL)" => "if (!x)", "if (b == true)" => "if (b)" etc
- Vertical alignment of things is encouraged, but not always necessary; it's good if it improves readability, which is, of course, subjective
- Structure members should be right-aligned for improved readability
- Line length soft limit: 120 columns (statements can span multiple lines when needed)
- Naming conventions:
    - Use snake_case throughout (C and ObjC code)
    - Avoid Hungarian notation prefixes like `p` for pointers or `pp` for pointer-to-pointer
    - Choose clear, descriptive names over terse abbreviations

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

Also, check out existing commits in the tree to get a good idea of code/commit style etc.

## Pull requests

Some things to check:
- Sign off your commits (git commit -s)
- Make commits self-contained: each commit should be able to compile and produce a working binary without the commits that follow it
- At the same time, make commits as small as possible, that is, split large commits into smaller ones that are still self-contained
- Write good commit messages (not including a link to a howto page, because googling for "writing commit messages" already comes up with a lot of good articles/blog posts). In short:
  - Use imperative mood ("change XXX" vs "XXX is changed")
  - State the reason for the commit, what is being done, why it is done and why it is done in this particular way
  - Subject line most often starts with a module name or comma separated names, then colon, then the summary of the change ("xyarray: Reimplement as an xyzarray")
  - One line commit messages are acceptable if the summary covers it all, if it's a one-line patch
  - Check the existing commit history
- No *new* compiler warnings (the build will fail, because -Wall and -Werror are enabled, unless you're on windows)
- Rebase your code onto the "main" branch, so that everything is fast-forward and no merge commits are necessary
- Make sure that all github actions pass (with a possible exception of www deployment action).
