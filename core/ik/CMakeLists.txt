# The inverse-kinematics subsystem: ik.c and the FABRIK solver it builds on,
# built as a static library and linked into the engine. fabrik.c is pure
# math, so the test binary compiles it directly.

clap_subsystem_library(clap_ik ik.c fabrik.c)

clap_subsystem_test(ik_tests fabrik-test.c fabrik.c)
