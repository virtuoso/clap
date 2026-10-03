// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include "mesh.h"
#include "test.h"
#include "util.h"

/* a resize to zero empties an attribute on every C library, and it grows again from there */
static int mesh_attr_resize_zero_test(void)
{
    LOCAL_SET(mesh_t, m) = CRES_RET(ref_new_checked(mesh, .name = "resize"), return EXIT_FAILURE);
    struct mesh_attr *ma = mesh_attr(m, MESH_VX);

    CERR_RET(mesh_attr_alloc(m, MESH_VX, sizeof(float) * 3, 4), return EXIT_FAILURE);
    CERR_RET(mesh_attr_resize(m, MESH_VX, 0), return EXIT_FAILURE);
    if (ma->data || ma->nr)                             return EXIT_FAILURE;

    CERR_RET(mesh_attr_resize(m, MESH_VX, 0), return EXIT_FAILURE);
    CERR_RET(mesh_attr_resize(m, MESH_VX, 4), return EXIT_FAILURE);
    if (!ma->data || ma->stride != sizeof(float) * 3)   return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "mesh attr resize to zero", .test = mesh_attr_resize_zero_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
