# The inverse-kinematics subsystem, built as a static library and linked into
# the engine. fabrik.c is the positional solver.

clap_subsystem_library(clap_ik fabrik.c)
