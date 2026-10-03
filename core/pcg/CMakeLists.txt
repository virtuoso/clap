# The procedural content library: frames, surfaces and cut spans for the
# generators that emit architecture and props. Built as a static library
# and linked into the engine like the other subsystems; nothing in the
# engine calls into it, so a binary only gets the archive members it
# references.

set(PCG_EMIT_SRC span.c)

clap_subsystem_library(clap_pcg ${PCG_EMIT_SRC})
