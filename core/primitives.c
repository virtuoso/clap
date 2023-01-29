// SPDX-License-Identifier: Apache-2.0
#include "mesh.h"

static GLushort quad_idx[] = {0, 1, 3, 3, 1, 2};

static GLfloat quad_tx[]  = {
    0.0, 0.0,
    0.0, 1.0,
    1.0, 1.0,
    1.0, 0.0,
};

static GLfloat cube_vx[] = {
    // back
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    // front
    0.0f, 1.0f, 1.0f,
    0.0f, 0.0f, 1.0f,
    1.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    // left
    1.0f, 1.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    // right
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
    0.0f, 1.0f, 1.0f,
    // top
    0.0f, 1.0f, 1.0f,
    0.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    1.0f, 1.0f, 1.0f,
    // bottom
    0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 1.0f
};

static GLfloat cube_norm[] = {
    // back
    0, 0, -1,
    0, 0, -1,
    0, 0, -1,
    0, 0, -1,
    // front
    0, 0, 1,
    0, 0, 1,
    0, 0, 1,
    0, 0, 1,
    // left
    -1, 0, 0,
    -1, 0, 0,
    -1, 0, 0,
    -1, 0, 0,
    // right
    1, 0, 0,
    1, 0, 0,
    1, 0, 0,
    1, 0, 0,
    // top
    0, 1, 0,
    0, 1, 0,
    0, 1, 0,
    0, 1, 0,
    // bottom
    0, -1, 0,
    0, -1, 0,
    0, -1, 0,
    0, -1, 0
};

static GLushort cube_idx[] = {
    0, 3, 1,
    1, 3, 2,
    4, 5, 7,
    7, 5, 6,
    8, 11, 9,
    9, 11, 10,
    12, 13, 15,
    15, 13, 14,
    16, 19, 17,
    19, 18, 17,
    20, 21, 23,
    23, 21, 22
};

static GLfloat cube_tx[] = {
    1.0f, 1.0f, /* Back. */
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f, /* Front. */
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f, /* Left. */
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    1.0f, 1.0f, /* Right. */
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 1.0f, /* Top. */
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 0.0f, /* Bottom. */
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f
};

extern struct ref_class ref_class_mesh;

struct mesh cube_mesh = {
    .ref    = REF_STATIC(mesh),
    .name   = "cube",
    .attr   = {
        [MESH_VX] = { .data = cube_vx, .nr = array_size(cube_vx) / 3, .stride = sizeof(float) * 3 },
        [MESH_TX] = { .data = cube_tx, .nr = array_size(cube_tx) / 2, .stride = sizeof(float) * 2 },
        [MESH_NORM] = { .data = cube_norm, .nr = array_size(cube_norm) / 3, .stride = sizeof(float) * 3 },
        [MESH_IDX] = { .data = cube_idx, .nr = array_size(cube_idx), .stride = sizeof(unsigned short) },
    }
};

struct model3d *model3d_new_cube(struct shader_prog *p)
{
    return model3d_new_from_vectors("cube", p, cube_vx, sizeof(cube_vx), cube_idx, sizeof(cube_idx),
                                    cube_tx, sizeof(cube_tx), NULL, 0);
}

struct model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float z, float w, float h)
{
    GLfloat quad_vx[] = {
        x, y + h, z, x, y, z, x + w, y, z, x + w, y + h, z,
    };

    return model3d_new_from_vectors("quad", p, quad_vx, sizeof(quad_vx), quad_idx, sizeof(quad_idx),
                                    quad_tx, sizeof(quad_tx), NULL, 0);
}

static GLushort frame_idx[] = {
    4, 0, 5, 0, 1, 5,
    5, 1, 2, 5, 2, 6,
    6, 2, 3, 6, 3, 7,
    7, 3, 0, 7, 0, 4, };

static GLfloat frame_tx[]  = {
    0.0, 0.0,
    0.0, 1.0,
    1.0, 1.0,
    1.0, 0.0,
    0.5, 0.5,
    0.5, 0.5,
    0.5, 0.5,
    0.5, 0.5,
};

struct model3d *model3d_new_frame(struct shader_prog *p, float x, float y, float z, float w, float h, float t)
{
    GLfloat frame_vx[] = {
        x, y + h, z, x, y, z, x + w, y, z, x + w, y + h, z,
        x + t, y + h - t, z, x + t, y + t, z, x + w - t, y + t, z, x + w - t, y + h - t, z,
    };

    return model3d_new_from_vectors("frame", p, frame_vx, sizeof(frame_vx), frame_idx, sizeof(frame_idx),
                                    frame_tx, sizeof(frame_tx), NULL, 0);    
}
