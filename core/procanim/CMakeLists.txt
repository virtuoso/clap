# The procedural-animation subsystem (gait, sway, humanoid, arachnid, the aim
# family), built as a static library and linked into the engine. gait.c and
# target.c are pure math; the others reach into the scene/render stack. The
# test binaries compile those two directly and never touch the rest.

clap_subsystem_library(clap_procanim gait.c sway.c humanoid.c arachnid.c
                       target.c lookat.c turret.c reach.c)

clap_subsystem_test(procanim_tests gait-test.c gait.c)
clap_subsystem_test(aim_tests aim-test.c target.c)
