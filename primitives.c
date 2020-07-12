
static GLushort quad_idx[] = {0, 1, 3, 3, 1, 2};

static GLfloat quad_tx[]  = {
    0.0, 0.0,
    0.0, 1.0,
    1.0, 1.0,
    1.0, 0.0,
};

static GLfloat cube_vx[] = {
    -0.5f, 0.5f, -0.5f,
    -0.5f, -0.5f, -0.5f,
    0.5f, -0.5f, -0.5f,
    0.5f, 0.5f, -0.5f,

    -0.5f, 0.5f, 0.5f,
    -0.5f, -0.5f, 0.5f,
    0.5f, -0.5f, 0.5f,
    0.5f, 0.5f, 0.5f,

    0.5f, 0.5f, -0.5f,
    0.5f, -0.5f, -0.5f,
    0.5f, -0.5f, 0.5f,
    0.5f, 0.5f, 0.5f,

    -0.5f, 0.5f, -0.5f,
    -0.5f, -0.5f, -0.5f,
    -0.5f, -0.5f, 0.5f,
    -0.5f, 0.5f, 0.5f,

    -0.5f, 0.5f, 0.5f,
    -0.5f, 0.5f, -0.5f,
    0.5f, 0.5f, -0.5f,
    0.5f, 0.5f, 0.5f,

    -0.5f, -0.5f, 0.5f,
    -0.5f, -0.5f, -0.5f,
    0.5f, -0.5f, -0.5f,
    0.5f, -0.5f, 0.5f
};

static GLushort cube_idx[] = {
    0, 1, 3,
    3, 1, 2,
    4, 5, 7,
    7, 5, 6,
    8, 9, 11,
    11, 9, 10,
    12, 13, 15,
    15, 13, 14,
    16, 17, 19,
    19, 17, 18,
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

struct model3d *model3d_new_cube(struct shader_prog *p)
{
    return model3d_new_from_vectors("cube", p, cube_vx, sizeof(cube_vx), cube_idx, sizeof(cube_idx),
                                    cube_tx, sizeof(cube_tx), NULL, 0);
}

struct model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float w, float h)
{
    GLfloat quad_vx[] = {
        x, y + h, 0.0, x, y, 0.0, x + w, y, 0.0, x + w, y + h, 0.0,
    };

    return model3d_new_from_vectors("quad", p, quad_vx, sizeof(quad_vx), quad_idx, sizeof(quad_idx),
                                    quad_tx, sizeof(quad_tx), NULL, 0);
}
