# The animation subsystem, built as a static library and linked into the
# engine: lib.c is a model's animation library (what can be played),
# animator.c an entity's playback over it, timeline.c the element clock they
# share.

clap_subsystem_library(clap_animator lib.c animator.c timeline.c)
