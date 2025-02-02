// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include "model.h"
#include "scene.h"
#include "character.h"
#include "messagebus.h"
#include "motion.h"
#include "ui.h"
#include "ui-debug.h"

static void character_dash(struct character *ch, struct scene *s)
{
    if (!ch->can_dash)
        return;

    /* if not already dashing or in dashing cooldown, dash */
    if (!timespec_nonzero(&ch->dash_started)) {
        ch->dash_started = clap_get_current_timespec(s->clap_ctx);
        ch->lin_speed *= 1.5;
        animation_set_speed(ch->entity, 1.5);
    }
}

static float character_lin_speed(struct character *ch)
{
    return entity3d_aabb_Y(ch->entity) * ch->speed;
}

/* Set mctl::lin_speed depending on the dashing phase */
static void character_motion_reset(struct character *ch, struct scene *s)
{
    /* Only applies to the controlled character */
    if (scene_control_character(s) != ch)
        return;

    if (timespec_nonzero(&ch->dash_started)) {
        struct timespec diff, now;

        now = clap_get_current_timespec(s->clap_ctx);
        timespec_diff(&ch->dash_started, &now, &diff);
        /* dashing end, in cooldown */
        if (diff.tv_sec >= 1) {
            ch->lin_speed = scene_character_is_camera(s, ch) ? 0.1 : character_lin_speed(ch);
            animation_set_speed(ch->entity, 1.0);
        }
        /* dashing cooldown end */
        if (diff.tv_sec >= 2)
            ch->dash_started.tv_sec = ch->dash_started.tv_nsec = 0;
    } else {
        ch->lin_speed = scene_character_is_camera(s, ch) ? 0.1 : character_lin_speed(ch);
    }

    /*
     * While we won't perform a second jump while airborne, the ch->jump can
     * still be set from the input handler, so clearing it only in
     * character_jump() (via character_move()) is not enough, it needs to be
     * cleared every frame after character_move() and character_update() are
     * done.
     */
    ch->jump = false;
    ch->rs_height = false;
}

void character_handle_input(struct character *ch, struct scene *s, struct message *m)
{
    struct character *control = scene_control_character(s);

#ifndef CONFIG_FINAL
    if (m->input.trigger_r)
        ch->lin_speed *= (m->input.trigger_r + 1) * 3;
    else if (m->input.pad_rt)
        ch->lin_speed *= 3;
#endif

    if (m->input.dash || m->input.pad_rb)
        character_dash(ch, s);

    if (scene_character_is_camera(s, control) && m->input.trigger_l)
        ch->rs_height = true;

    if (!scene_character_is_camera(s, control) && (m->input.space || m->input.pad_x))
        ch->jump = true;

    s->camera->zoom = !!(m->input.zoom);
}

static void character_idle(struct scene *s, void *priv)
{
    struct character *c = priv;

    c->state = CS_AWAKE;
    anictl_set_state(&c->entity->anictl, 0);
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
        ui_igVecRow(ch->entity->pos, 3, "position");
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
        igCheckbox("moved", (bool *)&ch->moved);
    }

    ui_igEnd(DEBUG_CHARACTER_MOTION);
}
#else
static inline void character_debug(struct character *ch) {}
#endif /* CONFIG_FINAL */

static bool character_jump(struct character *ch, struct scene *s, float dx, float dz)
{
    /*
     * Clearing ch->jump here is pointless, because it can be set again from the
     * input handler for the next frame. We won't act on it here while ch->airborne
     * is set, but we will, as soon as the character touches the ground. Instead,
     * it needs to be cleared every frame at character_motion_reset(). See comment
     * there.
     */
    if (!ch->can_jump || ch->airborne)
        return false;

    struct phys_body *body = ch->entity->phys_body;
    if (!body || !phys_body_has_body(body))
        return false;

    vec3 jump = { dx * ch->jump_forward, ch->jump_upward, dz * ch->jump_forward };

    ch->airborne = true;

    bool was_in_motion = !!vec3_len(ch->motion);

    phys_body_attach_motor(body, false);
    phys_body_set_velocity(body, jump);

    if (anictl_set_state(&ch->entity->anictl, 2)) {
        if (!animation_by_name(ch->entity->txmodel->model, "jump"))
            animation_push_by_name(ch->entity, s, "jump", true, false);
        else
            animation_push_by_name(ch->entity, s, "motion", true, was_in_motion);
        if (!was_in_motion) {
            animation_push_by_name(ch->entity, s, "motion_stop", false, false);
            animation_push_by_name(ch->entity, s, "idle", false, false);
        }
    }

    return true;
}

void character_move(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;
    struct character *control = scene_control_character(s);
    struct character *cam = s->camera->ch;
    vec3 old_motion;
    vec3_dup(old_motion, ch->motion);

    ch->airborne = body ? !phys_body_ground_collide(body, !ch->airborne) : 0;

    if (ch->airborne) {
        phys_body_set_motor_velocity(body, false, (vec3){ 0, 0, 0 });
        goto out;
    }

    if (control == ch) {
        if (!(s->mctl.ls_dx || s->mctl.ls_dy || s->mctl.rs_dx || s->mctl.rs_dy)) {
            // We got no input regarding character position or camera,
            // so we reset the "target" camera position to the "current"
            // (however, we don't allow the pitch to be too extreme).
            camera_set_target_to_current(s->camera);
        }

        if (ch->state == CS_START) {
            if (s->mctl.ls_dx || s->mctl.ls_dy) {
                ch->state = CS_WAKING;
                animation_push_by_name(ch->entity, s, "start_to_idle", true, false);
                animation_set_end_callback(ch->entity, character_idle, ch);
            }
            goto out;
        } else if (ch->state < CS_AWAKE) {
            goto out;
        }

        float delta_x = s->mctl.ls_dx * ch->lin_speed;
        float delta_z = s->mctl.ls_dy * ch->lin_speed;
        float yawcos = cos(to_radians(s->camera->target_yaw));
        float yawsin = sin(to_radians(s->camera->target_yaw));
        float dx = delta_x * yawcos - delta_z * yawsin;
        float dz = delta_x * yawsin + delta_z * yawcos;

        if (ch->jump)
            if (character_jump(ch, s, dx, dz))
                goto out;

        ch->motion[0] = dx;
        if (!scene_character_is_camera(s, ch))
            ch->motion[1] = 0.0;
        ch->motion[2] = dz;
    } else if (ch->state == CS_START) {
        goto out;
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
        vec3_scale(ch->angle, ch->motion, 60. / (float)display_refresh_rate());
        vec3_scale(ch->angle, ch->angle,
                   (float)display_refresh_rate() / (float)clap_get_fps_fine(s->clap_ctx));

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
            vec3 pos;
            vec3_dup(pos, ch->entity->pos);
            vec3_add(pos, pos, ch->angle);
            ch->entity->pos[0] = pos[0];
            ch->entity->pos[2] = pos[2];
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
                entity3d_rotate_Y(ch->entity, atan2f(velocity[0], velocity[2]));
            }
        } else {
            entity3d_rotate_Y(ch->entity, atan2f(ch->angle[0], ch->angle[2]));
        }

        // entity3d_rotate_Z(ch->entity, atan2f(ch->angle[1], ch->velocity[1]));
        ch->moved++;
        if (anictl_set_state(&ch->entity->anictl, 1)) {
            animation_push_by_name(ch->entity, s, "motion_start", true, false);
            animation_push_by_name(ch->entity, s, "motion", false, true);
        }
    } else if (body) {
        ch->angle[0] = 0;
        ch->angle[1] = 0;
        ch->angle[2] = 0;
        phys_body_stop(body);
        if (anictl_set_state(&ch->entity->anictl, 0)) {
            animation_push_by_name(ch->entity, s, "motion_stop", true, false);
            animation_push_by_name(ch->entity, s, "idle", false, true);
        }
    }

    if (body)
        phys_body_enable(body, true);

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

    if (scene_control_character(s) == c) {
        if (s->mctl.rs_dy) {
            float delta = s->mctl.rs_dy * s->ang_speed;

            if (c->rs_height)
                s->camera->ch->motion[1] -= delta / s->ang_speed * 0.1/*s->lin_speed*/;
            else
                camera_add_pitch(s->camera, delta);
            s->camera->ch->moved++;
        }
        if (s->mctl.rs_dx) {
            /* XXX: need a better way to represend horizontal rotational speed */
            camera_add_yaw(s->camera, s->mctl.rs_dx * s->ang_speed * 1.5);
            s->camera->ch->moved++;
        }
    }

    /* XXX "wow out" */
    if (e->pos[1] <= s->limbo_height)
        entity3d_position(e, (vec3){ e->pos[0], -e->pos[1], e->pos[2] });

    if (e->phys_body) {
        if (phys_body_update(e)) {
            c->moved++;
            if (scene_camera_follows(s, c))
                s->camera->ch->moved++;
        }
    }


    if (c->camera) {
        camera_move(c->camera, clap_get_fps_fine(s->clap_ctx));
        camera_update(c->camera, s, e);
    }

    character_motion_reset(c, s);

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
    character_motion_reset(c, s);

    return c;
}
