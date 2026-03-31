# Infer host compilers for building build-time tools

# C
find_program(HOST_C_COMPILER clang)
if (NOT HOST_C_COMPILER)
    find_program(HOST_C_COMPILER gcc)
endif ()
if (NOT HOST_C_COMPILER)
    find_program(HOST_C_COMPILER cc)
endif ()

# C++
find_program(HOST_CXX_COMPILER clang++)
if (NOT HOST_C_COMPILER)
    find_program(HOST_C_COMPILER g++)
endif ()
