# The procedural content library: frames, surfaces, cut spans, the mesh
# sink and the shapes, for the generators that emit architecture and
# props, and the texture generators and material library that finish
# them. Built as a static library and linked into the engine like the
# other subsystems; nothing in the engine calls into it, so a binary
# only gets the archive members it references. The test binaries
# compile the pure sources directly.

set(PCG_EMIT_SRC shape.c sink.c span.c slab.c fascia.c column.c balustrade.c stair.c wall.c glazing.c
    window-wall.c fixture.c sign.c skylight.c flight.c gallery.c facade.c room.c room-row.c atrium.c
    furniture.c)
set(PCG_SCENE_SRC texgen.c material.c presets.c commit.c labels.c)

clap_subsystem_library(clap_pcg ${PCG_EMIT_SRC} ${PCG_SCENE_SRC})

# The emitters append to meshes with the core primitives, which need
# meshoptimizer through mesh.c; only include paths are inherited, so link it
clap_subsystem_test(pcg_tests pcg-test.c ${PCG_EMIT_SRC}
                    ../primitives.c ../mesh.c ../datatypes.c ../triangulate.c ../poly2d.c)
target_link_libraries(pcg_tests PRIVATE meshoptimizer)

# The texture generators and the preset recipes paint with the noise and
# canvas helpers; noise.c also bakes blue noise with kissfft, which has to
# be linked for the same reason. The renderer side is stubbed.
clap_subsystem_test(pcg_texgen_tests texgen-test.c texgen.c presets.c
                    ../noise.c ../draw.c ../render-texture.c ../test-stubs-render.c)
target_link_libraries(pcg_texgen_tests PRIVATE kissfft)
