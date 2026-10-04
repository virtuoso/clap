# The procedural content library: frames, surfaces, cut spans, the mesh
# sink and the shapes, for the generators that emit architecture and
# props. Built as a static library and linked into the engine like the
# other subsystems; nothing in the engine calls into it, so a binary only
# gets the archive members it references. The test binary compiles the
# pure sources directly.

set(PCG_EMIT_SRC shape.c sink.c span.c)

clap_subsystem_library(clap_pcg ${PCG_EMIT_SRC})

# The emitters append to meshes with the core primitives, which need
# meshoptimizer through mesh.c; only include paths are inherited, so link it
clap_subsystem_test(pcg_tests pcg-test.c ${PCG_EMIT_SRC}
                    ../primitives.c ../mesh.c ../datatypes.c ../triangulate.c ../poly2d.c)
target_link_libraries(pcg_tests PRIVATE meshoptimizer)
