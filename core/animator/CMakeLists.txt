# The animation subsystem, built as a static library and linked into the
# engine: lib.c is a model's animation library (what can be played),
# animator.c an entity's playback over it, timeline.c the element clock they
# share. timeline.c is pure arithmetic; the test binary compiles it directly
# and never touches the rest.

clap_subsystem_library(clap_animator lib.c animator.c timeline.c)

clap_subsystem_test(animator_tests timeline-test.c timeline.c)
