// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "messagebus.h"
#include "motion.h"
#include "scene.h"

void motion_parse_input(struct motionctl *mctl, struct message *m)
{
    /* left stick right/left/up/down */
    if (m->input.right == 1)
        mctl->ls_right = 1;
    else if (m->input.right == 2)
        mctl->ls_right = 0;

    if (m->input.left == 1)
        mctl->ls_left = 1;
    else if (m->input.left == 2)
        mctl->ls_left = 0;

    if (m->input.up == 1)
        mctl->ls_up = 1;
    else if (m->input.up == 2)
        mctl->ls_up = 0;

    if (m->input.down == 1)
        mctl->ls_down = 1;
    else if (m->input.down == 2)
        mctl->ls_down = 0;

    if (m->input.delta_lx || m->input.delta_ly) {
        float angle = atan2f(m->input.delta_ly, m->input.delta_lx);
        mctl->ls_dx = cos(angle);
        mctl->ls_dy = sin(angle);
    }

    /* right stick */
    if (m->input.pitch_up == 1)
        mctl->rs_up = 1;
    else if (m->input.pitch_up == 2)
        mctl->rs_up = 0;

    if (m->input.pitch_down == 1)
        mctl->rs_down = 1;
    else if (m->input.pitch_down == 2)
        mctl->rs_down = 0;

    if (m->input.delta_ry)
        mctl->rs_dy = m->input.delta_ry;

    if (m->input.yaw_right == 1)
        mctl->rs_right = 1;
    else if (m->input.yaw_right == 2)
        mctl->rs_right = 0;

    if (m->input.yaw_left == 1)
        mctl->rs_left = 1;
    else if (m->input.yaw_left == 2)
        mctl->rs_left = 0;

    if (m->input.delta_rx)
        mctl->rs_dx = m->input.delta_rx;
}

static void motion_compute_ls(struct motionctl *mctl)
{
    int dir = 0;

    if (mctl->ls_left || mctl->ls_right) {
        mctl->ls_dx = mctl->ls_right - mctl->ls_left;
        dir++;
    }
    if (mctl->ls_up || mctl->ls_down) {
        mctl->ls_dy = mctl->ls_down - mctl->ls_up;
        dir++;
    }
    if (dir == 2) {
        mctl->ls_dx *= cos(M_PI_4);
        mctl->ls_dy *= sin(M_PI_4);
    }
}

static void motion_compute_rs(struct motionctl *mctl)
{
    if (mctl->rs_left || mctl->rs_right)
        mctl->rs_dx = mctl->rs_right - mctl->rs_left;
    if (mctl->rs_up || mctl->rs_down)
        mctl->rs_dy = mctl->rs_down - mctl->rs_up;
}

static void motion_get(struct motionctl *mctl, struct camera *cam, float lin_speed)
{
    float delta_x = mctl->ls_dx * lin_speed;
    float delta_z = mctl->ls_dy * lin_speed;

    if (cam) {
        float yawcos = cos(to_radians(cam->yaw));
        float yawsin = sin(to_radians(cam->yaw));
        mctl->dx = delta_x * yawcos - delta_z * yawsin;
        mctl->dz = delta_x * yawsin + delta_z * yawcos;
    } else {
        mctl->dx = delta_x;
        mctl->dz = delta_z;
    }
}

void motion_compute(struct motionctl *mctl, struct camera *cam, float lin_speed)
{
    motion_compute_ls(mctl);
    motion_compute_rs(mctl);
    motion_get(mctl, cam, lin_speed);
}

void motion_reset(struct motionctl *mctl, struct scene *s)
{
    mctl->rs_dx = mctl->rs_dy = mctl->ls_dx = mctl->ls_dy = 0;
}
