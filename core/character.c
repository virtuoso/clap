// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include "model.h"
#include "scene.h"
#include "terrain.h"
#include "character.h"
#include "messagebus.h"
#include "ui.h"
#include "ui-debug.h"

static void motion_parse_input(struct motionctl *mctl, struct message *m)
{
    struct character *ch = container_of(mctl, struct character, mctl);

#ifndef CONFIG_FINAL
    if (m->input.trigger_r)
        mctl->lin_speed *= (m->input.trigger_r + 1) * 3;
    else if (m->input.pad_rt)
        mctl->lin_speed *= 3;
#endif

    if (ch->dashing && (m->input.dash || m->input.pad_rb)) {
        /* if not already dashing or in dashing cooldown, dash */
        if (!timespec_nonzero(&mctl->dash_started)) {
            memcpy(&mctl->dash_started, &mctl->ts, sizeof(mctl->dash_started));
            mctl->lin_speed *= 1.5;
        }
    }

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
        mctl->ls_dx = mctl->lin_speed * cos(angle);
        mctl->ls_dy = mctl->lin_speed * sin(angle);
    }

    /* right stick */
    if (m->input.pitch_up == 1)
        mctl->rs_up = mctl->ang_speed;
    else if (m->input.pitch_up == 2)
        mctl->rs_up = 0;

    if (m->input.pitch_down == 1)
        mctl->rs_down = mctl->ang_speed;
    else if (m->input.pitch_down == 2)
        mctl->rs_down = 0;

    if (m->input.delta_ry)
        mctl->rs_dy = mctl->ang_speed * m->input.delta_ry;

    if (m->input.yaw_right == 1)
        mctl->rs_right = mctl->h_ang_speed;
    else if (m->input.yaw_right == 2)
        mctl->rs_right = 0;

    if (m->input.yaw_left == 1)
        mctl->rs_left = mctl->h_ang_speed;
    else if (m->input.yaw_left == 2)
        mctl->rs_left = 0;

    if (m->input.delta_rx)
        mctl->rs_dx = mctl->h_ang_speed * m->input.delta_rx;
}

static void motion_compute_ls(struct motionctl *mctl)
{
    int dir = 0;

    if (mctl->ls_left || mctl->ls_right) {
        mctl->ls_dx = (mctl->ls_right - mctl->ls_left) * mctl->lin_speed;
        dir++;
    }
    if (mctl->ls_up || mctl->ls_down) {
        mctl->ls_dy = (mctl->ls_down - mctl->ls_up) * mctl->lin_speed;
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

static float character_lin_speed(struct character *ch)
{
    return entity3d_aabb_Y(ch->entity) * ch->speed;
}

static void motion_reset(struct motionctl *mctl, struct scene *s)
{
    struct character *ch = container_of(mctl, struct character, mctl);

    if (timespec_nonzero(&mctl->dash_started)) {
        struct timespec diff;

        timespec_diff(&mctl->dash_started, &s->ts, &diff);
        /* dashing end, in cooldown */
        if (diff.tv_sec >= 1)
            mctl->lin_speed = scene_character_is_camera(s, ch) ? 0.1 : character_lin_speed(ch);
        /* dashing cooldown end */
        if (diff.tv_sec >= 2)
            mctl->dash_started.tv_sec = mctl->dash_started.tv_nsec = 0;
    } else {
        mctl->lin_speed = scene_character_is_camera(s, ch) ? 0.1 : character_lin_speed(ch);
    }
    mctl->ang_speed = s->ang_speed;
    mctl->h_ang_speed = s->ang_speed * 1.5;
    mctl->rs_dx = mctl->rs_dy = mctl->ls_dx = mctl->ls_dy = 0;
    mctl->jump = mctl->rs_height = false;
}

void character_handle_input(struct character *ch, struct scene *s, struct message *m)
{
    memcpy(&ch->mctl.ts, &s->ts, sizeof(ch->mctl.ts));
    motion_parse_input(&ch->mctl, m);

    if (scene_character_is_camera(s, s->control) && m->input.trigger_l)
        ch->mctl.rs_height = true;

    if (!scene_character_is_camera(s, s->control) && (m->input.space || m->input.pad_x))
        ch->mctl.jump = true;

    s->camera->zoom = !!(m->input.zoom);
}

bool character_is_grounded(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;

    if (body)
        return phys_body_is_grounded(body);
    return false;
}

void character_move(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;
    struct character *cam = s->camera->ch;
    float height;

    /*
     * ch->motion: motion resulting from inputs
     */
    motion_compute_ls(&ch->mctl);
    motion_compute_rs(&ch->mctl); // We need to compute this to determine if the user moved the camera.

    float delta_x = ch->mctl.ls_dx;
    float delta_z = ch->mctl.ls_dy;
    float yawcos = cos(to_radians(s->camera->target_yaw));
    float yawsin = sin(to_radians(s->camera->target_yaw));

    if (s->control == ch && !(ch->mctl.ls_dx || ch->mctl.ls_dy || ch->mctl.rs_dx || ch->mctl.rs_dy))
    {
        // We got no input regarding character position or camera,
        // so we reset the "target" camera position to the "current"
        // (however, we don't allow the pitch to be too extreme).
        camera_set_target_to_current(s->camera);
    }

    ch->ragdoll = body ? !phys_body_ground_collide(body) : 0;
    if (ch->ragdoll) {
        dJointSetLMotorParam(body->lmotor, dParamVel1, 0);
        dJointSetLMotorParam(body->lmotor, dParamVel2, 0);
        dJointSetLMotorParam(body->lmotor, dParamVel3, 0);
        return;
    }

    if (s->control == ch) {
        if (ch->jumping && !ch->ragdoll && ch->mctl.jump) {
            float dx = delta_x * yawcos - delta_z * yawsin;
            float dz = delta_x * yawsin + delta_z * yawcos;
            vec3 jump = { dx * 0.8, 3.0, dz * 0.8 };

            if (body && phys_body_has_body(body)) {
                bool was_in_motion = !!vec3_len(ch->motion);

                dJointAttach(body->lmotor, NULL, NULL);
                dBodyEnable(body->body);
                dBodySetLinearVel(body->body, jump[0], jump[1], jump[2]);

                if (anictl_set_state(&ch->anictl, 2)) {
                    if (!animation_by_name(ch->entity->txmodel->model, "jump"))
                        animation_push_by_name(ch->entity, s, "jump", true, false);
                    else
                        animation_push_by_name(ch->entity, s, "motion", true, was_in_motion);
                    if (!was_in_motion) {
                        animation_push_by_name(ch->entity, s, "motion_stop", false, false);
                        animation_push_by_name(ch->entity, s, "idle", false, false);
                    }
                }
                return; /* XXX */
            }
        } else {
            /*
             * XXX: this allows motion controls while in the air,
             * but without it (phys_body_ground_collide()==true), motion
             * becomes much more restricted and less fun. The alternative
             * is to set a higher epsilon in phys_body_ground_collide(),
             * but that will have other side effects. For future cleanup.
             */
            ch->motion[0] = delta_x * yawcos - delta_z * yawsin;
            if (!scene_character_is_camera(s, ch))
                ch->motion[1] = 0.0;
            ch->motion[2] = delta_x * yawsin + delta_z * yawcos;
        }
    }

    if (vec3_len(ch->motion)) {
        dVector3 newy = { ch->normal[0], ch->normal[1], ch->normal[2] };
        dVector3 oldx = { 1, 0, 0 };
        dVector3 newx, newz, res;
        dMatrix3 R;

        if (!vec3_len(ch->normal) && ch != cam) {
            err("no normal vector: is there collision?\n");
            return;
        }

        dCalcVectorCross3(newz, oldx, newy);
        dCalcVectorCross3(newx, newy, newz);

        dSafeNormalize3(newx);
        dSafeNormalize3(newy);
        dSafeNormalize3(newz);

        /* XXX: the numerator has to do with movement speed */
        vec3_scale(ch->angle, ch->motion, 60. / (float)gl_refresh_rate());
        vec3_scale(ch->angle, ch->angle, (float)gl_refresh_rate() / (float)s->fps.fps_fine);

        /* watch out for Y and Z swapping places */
        dAddScaledVectors3(res, newx, newz, ch->angle[0], ch->angle[2]);

        // if (scene_camera_follows(s, ch)) {
        //     ui_debug_printf("[ %f, %f, %f ] (%f) -> [ %f, %f, %f ] (%f)",
        //                     ch->angle[0], ch->angle[1], ch->angle[2], atan2(ch->angle[0], ch->angle[2]),
        //                     res[0], res[1], res[2], atan2(res[0], res[2]));
        // }

        if (body) {
            dJointSetLMotorParam(body->lmotor, dParamVel1, res[0]);
            dJointSetLMotorParam(body->lmotor, dParamVel2, res[1]);
            dJointSetLMotorParam(body->lmotor, dParamVel3, res[2]);
        } else {
            vec3_add(ch->pos, ch->pos, ch->angle);
            ch->entity->dx = ch->pos[0];
            ch->entity->dz = ch->pos[2];
        }

        vec3_norm(ch->angle, ch->angle);

        if (body && body->body) {
            // To determine the orientation of the body,
            // we calculate an average of our motion requested by input and actual velocity.
            const dReal *vel;
            vel = dBodyGetLinearVel(body->body);

            // Only change orientation if the velocity is non-zero
            // (to avoid flickering when the body is stopped).
            if (vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2] > 0.01)
            {
                float velocity_vs_direction_coefficient = 0.2; // direction from input is more important.
                vec3 velocity = { vel[0], vel[1], vel[2] };
                vec3_norm(velocity, velocity);
                vec3_scale(velocity, velocity, velocity_vs_direction_coefficient);

                // ch->angle is already normalized, so we can just add those two.
                vec3_add(velocity, velocity, ch->angle);
                ch->entity->ry = atan2f(velocity[0], velocity[2]);
            }
        } else {
            ch->entity->ry = atan2f(ch->angle[0], ch->angle[2]);
        }

        // ch->entity->rz = atan2f(ch->angle[1], res[1]);
        if (body) {
            dRFromEulerAngles(R, ch->entity->rx, ch->entity->ry, ch->entity->rz);
            // dRFrom2Axes(R, 0, 1, 0, newy[0], newy[1], newy[2]);
            dBodySetRotation(body->body, R);
            // ch->entity->mx->cell[0] = R[0];
            // ch->entity->mx->cell[1] = R[1];
            // ch->entity->mx->cell[2] = R[2];
            // ch->entity->mx->cell[3] = 0;
            // ch->entity->mx->cell[4] = R[4];
            // ch->entity->mx->cell[5] = R[5];
            // ch->entity->mx->cell[6] = R[6];
            // ch->entity->mx->cell[7] = 0;
            // ch->entity->mx->cell[8] = R[8];
            // ch->entity->mx->cell[9] = R[9];
            // ch->entity->mx->cell[10] = R[10];
            // ch->entity->mx->cell[11] = 0;
            // ch->entity->mx->cell[12] = ch->pos[0];
            // ch->entity->mx->cell[13] = ch->pos[1];
            // ch->entity->mx->cell[14] = ch->pos[2];
            // ch->entity->mx->cell[15] = 1;
            // mat4x4_scale_aniso(ch->entity->mx->m, ch->entity->mx->m, ch->entity->scale, ch->entity->scale, ch->entity->scale);

        }
        ch->moved++;
        if (anictl_set_state(&ch->anictl, 1)) {
            animation_push_by_name(ch->entity, s, "motion_start", true, false);
            animation_push_by_name(ch->entity, s, "motion", false, true);
        }
    } else if (body) {
        // vec3_scale(ch->angle, ch->angle, 0.5);
        ch->angle[0] = 0;
        ch->angle[1] = 0;
        ch->angle[2] = 0;
        dJointSetLMotorParam(body->lmotor, dParamVel1, ch->angle[0]);
        dJointSetLMotorParam(body->lmotor, dParamVel2, ch->angle[1]);
        dJointSetLMotorParam(body->lmotor, dParamVel3, ch->angle[2]);
        dBodySetLinearVel(body->body, 0, 0, 0);
        if (anictl_set_state(&ch->anictl, 0)) {
            animation_push_by_name(ch->entity, s, "motion_stop", true, false);
            animation_push_by_name(ch->entity, s, "idle", false, true);
        }
    }

    if (body)
        dBodyEnable(body->body);

    ch->entity->dy = ch->pos[1];
}

static void character_drop(struct ref *ref)
{
    struct character *c = container_of(ref, struct character, ref);

    ref_put_last_ref(&c->entity->ref);
}

DECLARE_REFCLASS(character);

/* data is struct scene */
static int character_update(struct entity3d *e, void *data)
{
    struct character *c = e->priv;
    struct scene     *s = data;
    int              ret;

    motion_compute_rs(&c->mctl);
    if (c->mctl.rs_dy) {
        float delta = c->mctl.rs_dy;

        if (c->mctl.rs_height)
            s->camera->ch->motion[1] -= delta / s->ang_speed * 0.1/*s->lin_speed*/;
        else
            camera_add_pitch(s->camera, delta);
        s->camera->ch->moved++;
    }
    if (c->mctl.rs_dx) {
        camera_add_yaw(s->camera, c->mctl.rs_dx);
        s->camera->ch->moved++;
    }

    /* XXX "wow out" */
    if (e->dy <= s->limbo_height) {
        entity3d_position(e, e->dx, -e->dy, e->dz);
        c->pos[1] = -c->pos[1];
    }

    if (e->phys_body) {
        if (phys_body_update(e)) {
            c->moved++;
            if (scene_camera_follows(s, c))
                s->camera->ch->moved++;
        }
        c->pos[0] = e->dx;
        c->pos[1] = e->dy;
        c->pos[2] = e->dz;
    }


    if (c->camera) {
        camera_move(c->camera, s->fps.fps_fine);
        vec3 start = { c->pos[0], c->pos[1], c->pos[2] };
        camera_update(c->camera, s, c->entity, start);
    }

    if (e->phys_body) {
        if (c->ragdoll) {
            // dReal *rot = phys_body_rotation(e->phys_body);
            dMatrix3 rot;

            dRFromEulerAngles(rot, e->rx, e->ry, e->rz);
            e->mx->cell[0] = rot[0];
            e->mx->cell[1] = rot[1];
            e->mx->cell[2] = rot[2];
            e->mx->cell[3] = 0;
            e->mx->cell[4] = rot[4];
            e->mx->cell[5] = rot[5];
            e->mx->cell[6] = rot[6];
            e->mx->cell[7] = 0;
            e->mx->cell[8] = rot[8];
            e->mx->cell[9] = rot[9];
            e->mx->cell[10] = rot[10];
            e->mx->cell[11] = 0;
            e->mx->cell[12] = c->pos[0];
            e->mx->cell[13] = c->pos[1];
            e->mx->cell[14] = c->pos[2];
            e->mx->cell[15] = 1;
            mat4x4_scale_aniso(e->mx->m, e->mx->m, e->scale, e->scale, e->scale);
        } else {
            entity3d_position(e, c->pos[0], c->pos[1], c->pos[2]);
        }
    }

    if (e->phys_body)
        c->stuck = dJointGetBody(e->phys_body->lmotor, 0) ? 1 : 0;
    if (c->moved) {
        /* update body position */
        //entity3d_position(e, e->dx, e->dy, e->dz);
    }

    motion_reset(&c->mctl, s);

    // return 0;
    return c->orig_update(e, data);
}

struct character *character_new(struct model3dtx *txm, struct scene *s)
{
    struct character *c;

    CHECK(c = ref_new(character));
    CHECK(c->entity = entity3d_new(txm));
    c->entity->priv = c;
    c->orig_update = c->entity->update;
    c->entity->update = character_update;
    list_append(&s->characters, &c->entry);
    motion_reset(&c->mctl, s);

    return c;
}
