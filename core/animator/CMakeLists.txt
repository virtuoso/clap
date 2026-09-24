# The animation subsystem, built as a static library and linked into the
# engine: lib.c is a model's animation library (what can be played), json.c
# its transition table from the scene's JSON, character-anims.c the
# character's default table, animator.c an entity's playback over it,
# timeline.c the element clock they share, blend.c the joint blending the
# layers do. The test binaries compile what they test directly: timeline.c
# and blend.c are pure arithmetic, lib.c and json.c need only a stand-in for
# the model's clip lookup and the engine's JSON parser.

clap_subsystem_library(clap_animator lib.c json.c character-anims.c animator.c timeline.c blend.c)

clap_subsystem_test(animator_tests timeline-test.c timeline.c)
clap_subsystem_test(anim_lib_tests lib-test.c lib.c json.c ../json.c)
clap_subsystem_test(anim_blend_tests blend-test.c blend.c)
