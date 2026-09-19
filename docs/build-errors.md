# Build Errors

If after updating from the `main` branch you suddenly start getting build errors
and Github runners built the same code successfully (which should always be the
case), they are probably coming from ImGui API mismatch between your local copy
and ImGui and/or cimgui upstream. They usually look something like this:
```
core/pipeline-debug.c:222:49: error: too few arguments to function call, expected 6, have 4
  221 |             igImage((ImTextureID)texture_id(pass_tex), (ImVec2){512, 512 * aspect},
      |             ~~~~~~~
  222 |                     (ImVec2){1,1}, (ImVec2){0,0});
      |                                                 ^
```
or
```
core/ui-imgui.c:23:26: error: use of undeclared identifier 'igGetIO_ContextPtr'
   23 |     struct ImGuiIO *io = igGetIO_ContextPtr(ctx);
      |                          ^
```

You may think that removing the local build directory is the solution, but actually
you need to remove `deps/.bootstrap.json`:
```sh
rm deps/.bootstrap.json
```

and configure again with `./configure.sh` or `cmake --preset=emtest` or suchlike. The
build directory can stay.

## The OpenGL build fails in `fs-ops-fsevents.c` after a macOS SDK update

The OpenGL build on macOS links `${OPENGL_LIBRARIES}` by framework path, and
CMake turns that into `-F<SDK>/System/Library/Frameworks` on every compile
line. That makes the SDK's frameworks non-system headers, so a deprecation
the SDK carries inside its own headers reaches `-Werror`. The macOS 27 SDK
has one in `CarbonCore/Script.h`, which `<CoreServices/CoreServices.h>`
pulls in:
```
.../CarbonCore.framework/Headers/Script.h:837:3: error: 'ScriptTokenType' is deprecated: first deprecated in macOS 13.0 - Deprecated [-Werror,-Wdeprecated-declarations]
```

The Metal build has no such `-F` and compiles the same file. Dropping the
`-F`, or passing the directory as `-iframework`, compiles too. Until the GL
build links `OpenGL::GL` (an imported target's include directories are
system directories) or `fs-ops-fsevents.c` gets `-Wno-deprecated-declarations`
the way `render-mtl.m` has, build the Metal renderer on macOS.
