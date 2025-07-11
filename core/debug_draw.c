// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "messagebus.h"
#include "ui-debug.h"
#include "view.h"

static int debug_draw(struct message *m, void *data)
{
    struct message_debug_draw *dd = &m->debug_draw;
    struct camera *cam = data;

    if (!cam)
        return MSG_HANDLED;

    ImDrawList *draw = igGetBackgroundDrawList_Nil();
    ImGuiIO *io = igGetIO_Nil();

    struct subview *sv = &cam->view.main;
    mat4x4 mvp;
    mat4x4_mul(mvp, sv->proj_mx, sv->view_mx);

    ImU32 color = IM_COL32(
        (int)(dd->color[0] * 255),
        (int)(dd->color[1] * 255),
        (int)(dd->color[2] * 255),
        (int)(dd->color[3] * 255)
    );

    switch (dd->shape) {
        case DEBUG_DRAW_TEXT:
            {
                vec4 v0;
                vec3_dup(v0, dd->v0);
                v0[3] = 1.0;

                mat4x4_mul_vec4_post(v0, mvp, v0);

                if (v0[3] > 1e-3) {
                    vec3_scale(v0, v0, 1.0 / v0[3]);

                    ImVec2 p0 = {
                        .x = ((v0[0] + 1.0) / 2.0) * io->DisplaySize.x,
                        .y = ((1.0 - v0[1]) / 2.0) * io->DisplaySize.y,
                    };

                    ImDrawList_AddText_Vec2(draw, p0, color, dd->text, NULL);
                }
                mem_free(dd->text);
            }
            break;

        case DEBUG_DRAW_CIRCLE:
            {
                vec4 v0;
                vec3_dup(v0, dd->v0);
                v0[3] = 1.0;

                mat4x4_mul_vec4_post(v0, mvp, v0);
                vec3_scale(v0, v0, 1.0 / v0[3]);

                if (v0[3] < 1e-3)
                    break;

                ImVec2 p0 = {
                    .x = ((v0[0] + 1.0) / 2.0) * io->DisplaySize.x,
                    .y = ((1.0 - v0[1]) / 2.0) * io->DisplaySize.y,
                };

                ImDrawList_AddCircleFilled(draw, p0, dd->radius, color, 16);
            }
            break;

        case DEBUG_DRAW_LINE:
            {
                vec4 v0, v1;
                vec3_dup(v0, dd->v0);
                vec3_dup(v1, dd->v1);
                v0[3] = v1[3] = 1.0;

                mat4x4_mul_vec4_post(v0, mvp, v0);
                vec3_scale(v0, v0, 1.0 / v0[3]);
                mat4x4_mul_vec4_post(v1, mvp, v1);
                vec3_scale(v1, v1, 1.0 / v1[3]);

                if (v0[3] < 1e-3 || v1[3] < 1e-3)
                    break;

                ImVec2 p0 = {
                    .x = ((v0[0] + 1.0) / 2.0) * io->DisplaySize.x,
                    .y = ((1.0 - v0[1]) / 2.0) * io->DisplaySize.y,
                };

                ImVec2 p1 = {
                    .x = ((v1[0] + 1.0) / 2.0) * io->DisplaySize.x,
                    .y = ((1.0 - v1[1]) / 2.0) * io->DisplaySize.y,
                };

                ImDrawList_AddLine(draw, p0, p1, color, dd->thickness);
            }
            break;

        case DEBUG_DRAW_AABB:
            {
                vec3 min, max;
                vec3_dup(min, dd->v0);
                vec3_dup(max, dd->v1);

                vec3 corners[8] = {
                    { min[0], min[1], min[2] },
                    { max[0], min[1], min[2] },
                    { max[0], max[1], min[2] },
                    { min[0], max[1], min[2] },
                    { min[0], min[1], max[2] },
                    { max[0], min[1], max[2] },
                    { max[0], max[1], max[2] },
                    { min[0], max[1], max[2] },
                };

                int edges[12][2] = {
                    {0,1},{1,2},{2,3},{3,0}, // bottom face
                    {4,5},{5,6},{6,7},{7,4}, // top face
                    {0,4},{1,5},{2,6},{3,7}  // verticals
                };

                for (int i = 0; i < 12; i++) {
                    vec4 a = { corners[edges[i][0]][0], corners[edges[i][0]][1], corners[edges[i][0]][2], 1.0 };
                    vec4 b = { corners[edges[i][1]][0], corners[edges[i][1]][1], corners[edges[i][1]][2], 1.0 };

                    mat4x4 mvp;
                    mat4x4_mul(mvp, sv->proj_mx, sv->view_mx);
                    mat4x4_mul_vec4_post(a, mvp, a);
                    mat4x4_mul_vec4_post(b, mvp, b);

                    if (a[3] < 1e-3 || b[3] < 1e-3)
                        continue;

                    vec3_scale(a, a, 1.0 / a[3]);
                    vec3_scale(b, b, 1.0 / b[3]);

                    ImVec2 p0 = {
                        .x = ((a[0] + 1.0f) / 2.0f) * io->DisplaySize.x,
                        .y = ((1.0f - a[1]) / 2.0f) * io->DisplaySize.y
                    };

                    ImVec2 p1 = {
                        .x = ((b[0] + 1.0f) / 2.0f) * io->DisplaySize.x,
                        .y = ((1.0f - b[1]) / 2.0f) * io->DisplaySize.y
                    };

                    ImDrawList_AddLine(draw, p0, p1, color, dd->thickness);
                }
            }
            break;

        default:
            break;
    }

    return MSG_HANDLED;
}

cerr debug_draw_install(struct camera *cam)
{
    return subscribe(MT_DEBUG_DRAW, debug_draw, cam);
}
