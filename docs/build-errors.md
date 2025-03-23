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
