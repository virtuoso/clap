# The procedural-animation subsystem (gait, sway, humanoid, arachnid), built as
# a static library and linked into the engine. gait.c is pure math; the others
# reach into the scene/render stack. The test binary compiles gait.c directly
# and never touches the rest.

clap_subsystem_library(clap_procanim gait.c sway.c humanoid.c arachnid.c)

clap_subsystem_test(procanim_tests gait-test.c gait.c)
