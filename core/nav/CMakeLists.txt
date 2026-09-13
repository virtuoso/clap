# The navigation-mesh subsystem, built as a static library and linked into the
# engine. navmesh.c is pure geometry, so the test binary compiles it directly.

clap_subsystem_library(clap_nav navmesh.c)

clap_subsystem_test(nav_tests navmesh-test.c navmesh.c)
