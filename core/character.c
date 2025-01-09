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

    if (ch->can_sprint && (m->input.dash || m->input.pad_rb)) {
        /* if not already dashing or in dashing cooldown, dash */
        if (!timespec_nonzero(&mctl->dash_started)) {
            memcpy(&mctl->dash_started, &mctl->ts, sizeof(mctl->dash_started));
            mctl->lin_speed *= 1.5;
            animation_set_speed(ch->entity, 1.5);
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
        if (diff.tv_sec >= 1) {
            mctl->lin_speed = scene_character_is_camera(s, ch) ? 0.1 : character_lin_speed(ch);
            animation_set_speed(ch->entity, 1.0);
        }
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

static void character_idle(struct scene *s, void *priv)
{
    struct character *c = priv;

    c->state = CS_AWAKE;
    anictl_set_state(&c->anictl, 0);
    animation_push_by_name(c->entity, s, "idle", true, true);
}

#ifndef CONFIG_FINAL
static void character_debug(struct character *ch)
{
    const char *name = entity_name(ch->entity);
    debug_module *dbgm = ui_igBegin_name(DEBUG_CHARACTER_MOTION, ImGuiWindowFlags_AlwaysAutoResize,
                                         "character %s", name);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        ui_igVecTableHeader("vectors", 3);
        ui_igVecRow(ch->pos, 3, "position");
        ui_igVecRow(ch->angle, 3, "angle");
        ui_igVecRow(ch->motion, 3, "motion");
        ui_igVecRow(ch->velocity, 3, "velocity");
        ui_igVecRow(ch->normal, 3, "normal");
        igEndTable();
        vec3 up = { 0, 1, 0 };
        float dot = vec3_mul_inner(ch->normal, up);
        igText("upness %f", dot);

        igText("collision %s", entity_name(ch->collision));
        igCheckbox("airborne", &ch->airborne);
        igCheckbox("stuck", &ch->stuck);
        igCheckbox("stopped", &ch->stopped);
        igCheckbox("moved", (bool *)&ch->moved);
    }

    ui_igEnd(DEBUG_CHARACTER_MOTION);
}
#else
static inline void character_debug(struct character *ch) {}
#endif /* CONFIG_FINAL */

void character_move(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;
    struct character *cam = s->camera->ch;
    vec3 old_motion;
    vec3_dup(old_motion, ch->motion);

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

    ch->airborne = body ? !phys_body_ground_collide(body, !ch->airborne) : 0;

    if (ch->state == CS_START) {
        if (ch->mctl.ls_dx || ch->mctl.ls_dy) {
            ch->state = CS_WAKING;
            animation_push_by_name(ch->entity, s, "start_to_idle", true, false);
            animation_set_end_callback(ch->entity, character_idle, ch);
        }
        goto out;
    } else if (ch->state < CS_AWAKE) {
        goto out;
    }

    if (ch->airborne) {
        phys_body_set_motor_velocity(body, false, (vec3){ 0, 0, 0 });
        goto out;
    }

    if (s->control == ch) {
        if (ch->jumping && !ch->airborne && ch->mctl.jump) {
            float dx = delta_x * yawcos - delta_z * yawsin;
            float dz = delta_x * yawsin + delta_z * yawcos;
            vec3 jump = { dx * ch->jump_forward, ch->jump_upward, dz * ch->jump_forward };

            ch->airborne = true;

            if (body && phys_body_has_body(body)) {
                bool was_in_motion = !!vec3_len(ch->motion);

                phys_body_attach_motor(body, false);
                phys_body_set_velocity(body, jump);

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
                goto out;
            }
        } else {
            ch->motion[0] = delta_x * yawcos - delta_z * yawsin;
            if (!scene_character_is_camera(s, ch))
                ch->motion[1] = 0.0;
            ch->motion[2] = delta_x * yawsin + delta_z * yawcos;
        }
    }

    if (vec3_len(ch->motion)) {
        vec3 newx, newy, newz;
        vec3 oldx = { 1, 0, 0 };

        vec3_dup(newy, ch->normal);

        if (!vec3_len(ch->normal) && ch != cam) {
            err("no normal vector: is there collision?\n");
            goto out;
        }

        vec3_mul_cross(newz, oldx, newy);
        vec3_mul_cross(newx, newy, newz);

        vec3_norm(newx, newx);
        vec3_norm(newy, newy);
        vec3_norm(newz, newz);

        /* XXX: the numerator has to do with movement speed */
        vec3_scale(ch->angle, ch->motion, 60. / (float)gl_refresh_rate());
        vec3_scale(ch->angle, ch->angle, (float)gl_refresh_rate() / (float)s->fps.fps_fine);

        /* watch out for Y and Z swapping places */
        vec3_add_scaled(ch->velocity, newx, newz, ch->angle[0], ch->angle[2]);

        if (body) {
            vec3 res_norm;
            vec3_norm(res_norm, ch->velocity);
            vec3_norm_safe(old_motion, old_motion);

            /* Get rid of the drift */
            bool body_also = false;
            if (fabsf(vec3_mul_inner(res_norm, old_motion) - 1) >= 1e-3)
                body_also = true;

            phys_body_set_motor_velocity(body, body_also, ch->velocity);
        } else {
            vec3_add(ch->pos, ch->pos, ch->angle);
            ch->entity->dx = ch->pos[0];
            ch->entity->dz = ch->pos[2];
        }

        vec3_norm(ch->angle, ch->angle);

        if (body && phys_body_has_body(body)) {
            // To determine the orientation of the body,
            // we calculate an average of our motion requested by input and actual velocity.
            vec3 vel;
            phys_body_get_velocity(body, vel);

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
        phys_body_stop(body);
        if (anictl_set_state(&ch->anictl, 0)) {
            animation_push_by_name(ch->entity, s, "motion_stop", true, false);
            animation_push_by_name(ch->entity, s, "idle", false, true);
        }
    }

    if (body)
        dBodyEnable(body->body);

    ch->entity->dy = ch->pos[1];

out:
    if (scene_camera_follows(s, ch))
        character_debug(ch);
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
        if (c->airborne) {
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
    c->state = CS_AWAKE;
    c->jump_forward = 2.0;
    c->jump_upward = 3.0;
    list_append(&s->characters, &c->entry);
    motion_reset(&c->mctl, s);

    return c;
}
