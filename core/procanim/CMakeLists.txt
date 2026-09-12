# The procedural-animation subsystem (gait, sway), built as a static library and
# linked into the engine. gait.c is pure math; sway.c reaches into the
# scene/render stack.

clap_subsystem_library(clap_procanim gait.c sway.c)
