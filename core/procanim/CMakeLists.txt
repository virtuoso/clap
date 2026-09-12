# The procedural-animation subsystem, built as a static library and linked into
# the engine. sway.c reaches into the scene/render stack.

clap_subsystem_library(clap_procanim sway.c)
