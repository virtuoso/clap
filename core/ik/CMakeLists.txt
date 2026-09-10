# The inverse-kinematics subsystem, built as a static library and linked into
# the engine. fabrik.c is the positional solver and pure math, so the test
# binary compiles it directly.

clap_subsystem_library(clap_ik fabrik.c)

clap_subsystem_test(ik_tests fabrik-test.c fabrik.c)
