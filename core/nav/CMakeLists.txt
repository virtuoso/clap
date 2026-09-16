# The navigation-mesh subsystem, built as a static library and linked into the
# engine. navmesh.c is pure geometry and roamer.c the path-following agent over
# it; debug.c adds the walkable-surface overlay, the roamer debug window and the
# path/radius lines, which reach into the render/UI stack, so it is compiled
# only for non-final builds, matching the #ifndef CONFIG_FINAL gates around
# everything it implements. The test binary compiles navmesh.c directly and
# never touches debug.c.

set(NAV_SRC navmesh.c roamer.c)
if (NOT CONFIG_FINAL)
    list(APPEND NAV_SRC debug.c)
endif ()

clap_subsystem_library(clap_nav ${NAV_SRC})

clap_subsystem_test(nav_tests navmesh-test.c navmesh.c)
