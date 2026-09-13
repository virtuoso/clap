# The navigation-mesh subsystem, built as a static library and linked into the
# engine. navmesh.c is pure geometry and roamer.c the path-following agent over
# it; debug.c adds the walkable-surface overlay and the roamer path lines, which
# reach into the render/scene stack, so it is compiled (and CLAP_NAVMESH_DEBUG
# defined) only for non-final builds. The test binary compiles navmesh.c
# directly and never touches debug.c.

set(NAV_SRC navmesh.c roamer.c)
if (NOT CONFIG_FINAL)
    list(APPEND NAV_SRC debug.c)
endif ()

clap_subsystem_library(clap_nav ${NAV_SRC})

if (NOT CONFIG_FINAL)
    # Gate the overlay and the debug_entity slot in struct navmesh, on both the
    # library and the engine translation units that call navmesh_debug_draw().
    target_compile_definitions(clap_nav PRIVATE CLAP_NAVMESH_DEBUG)
    target_compile_definitions(${ENGINE_LIB} PRIVATE CLAP_NAVMESH_DEBUG)
endif ()

clap_subsystem_test(nav_tests navmesh-test.c navmesh.c)
