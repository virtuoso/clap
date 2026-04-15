// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
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
        animation_set_speed(ch->entity, s, 1.5);
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

    if (ch->state == CS_IDLE) {
        ch->dash_started.tv_sec = 0;
        ch->dash_started.tv_nsec = 0;
    }

    if (timespec_nonzero(&ch->dash_started)) {
        struct timespec diff, now;

        now = clap_get_current_timespec(s->clap_ctx);
        timespec_diff(&ch->dash_started, &now, &diff);
        /* dashing end, in cooldown */
        if (diff.tv_sec >= 1) {
            ch->lin_speed = character_lin_speed(ch);
            animation_set_speed(ch->entity, s, 1.0);
        }
        /* dashing cooldown end */
        if (diff.tv_sec >= 2)
            ch->dash_started.tv_sec = ch->dash_started.tv_nsec = 0;
    } else {
        ch->lin_speed = character_lin_speed(ch);
    }

    /*
     * While we won't perform a second jump while airborne, the ch->jump can
     * still be set from the input handler, so clearing it only in
     * character_jump() (via character_move()) is not enough, it needs to be
     * cleared every frame after character_move() and character_update() are
     * done.
     */
    ch->jump = false;
}

void character_handle_input(struct character *ch, struct scene *s, struct message *m)
{
#ifndef CONFIG_FINAL
    if (m->input.trigger_r)
        ch->lin_speed *= (m->input.trigger_r + 1) * 3;
    else if (m->input.pad_rt)
        ch->lin_speed *= 3;
#endif

    if (m->input.dash || m->input.pad_rb)
        character_dash(ch, s);

    if ((m->input.space || m->input.pad_y) &&
        ch->state != CS_JUMPING && ch->state != CS_JUMP_START)
        ch->jump = true;
}

static void character_idle(struct scene *s, void *priv)
{
    struct character *c = priv;

    c->state = CS_AWAKE;
    animation_push_by_name(c->entity, s, "idle", true, true);
}

static void character_start_motion(struct scene *s, void *priv)
{
    struct character *c = priv;

    c->state = CS_MOVING;
}

static void character_set_state(struct character *ch, struct scene *s, character_state state);

static void character_any_to_jump(struct scene *s, void *priv)
{
    struct character *c = priv;

    c->airborne = true;
    phys_body_set_velocity(c->entity->phys_body, c->velocity);
    character_set_state(c, s, CS_JUMPING);
}

static void character_jump_frame_callback(struct queued_animation *qa, entity3d *e, struct scene *s, double time)
{
    struct character *ch = CRES_RET(entity3d_character(e), return);

    if (time >= 0.5) {
        phys_body_set_velocity(ch->entity->phys_body, ch->velocity);
        qa->frame_cb = NULL;
    }
}

void character_set_moved(struct character *c)
{
    if (c->camera)
        transform_set_updated(&c->camera->xform);
}

#ifndef CONFIG_FINAL
static const char *character_state_string[] = {
    [CS_START]      = "start",
    [CS_WAKING]     = "waking",
    [CS_IDLE]       = "idle",
    [CS_MOVING]     = "moving",
    [CS_JUMP_START] = "jump start",
    [CS_JUMPING]    = "jumping",
    [CS_FALLING]    = "falling"
};

static void character_debug(struct character *ch)
{
    const char *name = entity_name(ch->entity);
    debug_module *dbgm = ui_igBegin_name(DEBUG_CHARACTER_MOTION, ImGuiWindowFlags_AlwaysAutoResize,
                                         "character %s", name);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        const float *pos = transform_pos(&ch->entity->xform, NULL);
        ui_igVecTableHeader("vectors", 3);
        ui_igVecRow(pos, 3, "position");
        ui_igVecRow(ch->motion, 3, "motion");
        ui_igVecRow(ch->velocity, 3, "velocity");
        ui_igVecRow(ch->normal, 3, "normal");
        igEndTable();
        vec3 up = { 0, 1, 0 };
        float dot = vec3_mul_inner(ch->normal, up);
        igText("upness %f", dot);

        igText("collision %s", entity_name(ch->collision));
        igText("state %s", character_state_string[ch->state]);
        igCheckbox("airborne", &ch->airborne);
        bool moved = transform_is_updated(&ch->entity->xform);
        igBeginDisabled(true);
        igCheckbox("moved", &moved);
        igEndDisabled();
        if (igButton("disable body", (ImVec2){}))
            phys_body_enable(ch->entity->phys_body, false);
    }

    ui_igEnd(DEBUG_CHARACTER_MOTION);
}
#else
static inline void character_debug(struct character *ch) {}
#endif /* CONFIG_FINAL */

/*
 * character_sweep_delta() - move body by delta via sweep-and-slide
 * @body:           character's phys_body
 * @delta:          desired movement vector (modified during slide iterations)
 * @min_normal_y:   ignore contacts whose Y normal is below this (e.g. 0.5
 *                  for vertical sweeps to skip edge/wall contacts)
 * @stop_on_block:  if true, stop immediately when frac <= 0 (character is
 *                  pressed against geometry).  Set for grounded/horizontal
 *                  sweeps to prevent slide-creep through meshes.  Clear for
 *                  vertical sweeps so the character slides off corners
 *                  instead of hanging.
 *
 * Returns the fraction from the first sweep iteration (< 1.0 means
 * something was hit on the primary movement).
 */
static float character_sweep_delta(struct phys_body *body, vec3 delta,
                                   float min_normal_y, bool stop_on_block,
                                   const vec3 push_velocity, float push_mass)
{
    float first_frac = 1.0f;

    for (int iter = 0; iter < 3; iter++) {
        if (vec3_len(delta) < 1e-6f)
            break;

        vec3 normal;
        entity3d *hit = NULL;
        float frac = phys_body_sweep_capsule(body, delta, normal, &hit);

        /*
         * Filter by normal direction: for vertical sweeps, only
         * floor-like contacts (normal[1] >= min_normal_y) should
         * block; edges and walls are ignored so the character
         * slides off corners instead of catching on them.
         */
        if (frac < 1.0f && normal[1] < min_normal_y)
            frac = 1.0f;

        if (iter == 0)
            first_frac = frac;

        /* Push dynamic bodies on contact */
        if (frac < 1.0f && hit)
            phys_body_push(hit, push_velocity, push_mass);

        if (frac > 0) {
            vec3 step;
            vec3_scale(step, delta, frac);
            phys_body_move(body, step);
        }

        if (frac >= 1.0f)
            break;
        if (frac <= 0.0f && stop_on_block)
            break;

        /* Project remaining delta onto the contact plane */
        vec3 remaining;
        vec3_scale(remaining, delta, 1.0f - frac);
        float dot = vec3_mul_inner(remaining, normal);
        vec3_scale(delta, normal, dot);
        vec3_sub(delta, remaining, delta);
    }

    return first_frac;
}

static void character_apply_velocity(struct character *ch, struct scene *s)
{
    entity3d *e = ch->entity;

    if (!entity3d_matches(e, ENTITY3D_HAS_PHYSICS)) {
        transform_move(&e->xform, ch->velocity);
        goto rotate;
    }

    /*
     * Kinematic sweep-and-slide: compute per-frame delta from velocity,
     * then sweep the capsule and slide along contact surfaces.
     */
    double dt_sec = clap_get_fps_delta(s->clap_ctx).tv_nsec / (double)NSEC_PER_SEC;
    if (dt_sec < 1e-6)
        goto rotate;
    /* Clamp to prevent oversized deltas on frame spikes */
    if (dt_sec > 1.0 / 30.0)
        dt_sec = 1.0 / 30.0;

    float char_mass = phys_body_get_mass(e->phys_body);

    if (ch->airborne) {
        /*
         * Airborne: sweep vertical and horizontal independently.
         * This prevents the "fall of shame" -- blocking horizontal
         * movement against a wall doesn't stop gravity from pulling
         * the character down.
         */
        vec3 v_delta = { 0, ch->velocity[1] * dt_sec, 0 };
        vec3 h_delta = { ch->velocity[0] * dt_sec, 0, ch->velocity[2] * dt_sec };
        float v_moved = character_sweep_delta(e->phys_body, v_delta, 0.5f, false,
                                              ch->velocity, char_mass);
        character_sweep_delta(e->phys_body, h_delta, -1.0f, true,
                              ch->velocity, char_mass);
        /*
         * If the vertical sweep was blocked, the character hit
         * something below (or above).  Zero vertical velocity so
         * gravity doesn't accumulate while the character is stuck
         * at the surface waiting for ground_collide to ground it.
         */
        if (v_moved < 1.0f)
            ch->velocity[1] = 0;
    } else {
        vec3 delta;
        vec3_scale(delta, ch->velocity, dt_sec);
        character_sweep_delta(e->phys_body, delta, -1.0f, true,
                              ch->velocity, char_mass);
    }

    /* Zero body velocity so ODE dynamics don't fight the sweep */
    phys_body_set_velocity(e->phys_body, (vec3){ 0, 0, 0 });

rotate:
    entity3d_rotate(ch->entity, 0, atan2f(ch->motion[0], ch->motion[2]), 0);
}

static void character_set_state(struct character *ch, struct scene *s, character_state state)
{
    /* if character is in START state and motion is triggered */
    if (unlikely(state != CS_IDLE && ch->state < CS_IDLE)) {
        if (ch->state == CS_START) {
            ch->state = CS_WAKING;
            animation_push_by_name(ch->entity, s, "start_to_idle", true, false);
            animation_set_end_callback(ch->entity, character_idle, ch);
        }
        return;
    }

    struct phys_body *body = ch->entity->phys_body;

fail_fallback:
    switch (state) {
        case CS_IDLE:
            if (ch->airborne)
                return;

            if (ch->state == CS_MOVING) {
                animation_push_by_name(ch->entity, s, "motion_stop", true, false);
            } else if (ch->state == CS_JUMPING) {
                animation_push_by_name(ch->entity, s, "jump_to_idle", true, false);
            } else if (ch->state == CS_FALLING) {
                animation_push_by_name(ch->entity, s, "fall_to_idle", true, false);
            } else if (ch->state <= CS_IDLE || ch->state == CS_JUMP_START) {
                return;
            }
            animation_push_by_name(ch->entity, s, "idle", false, true);

            if (body) {
                phys_body_set_velocity(body, (vec3){ 0, 0, 0 });
                phys_body_enable(body, false);
            }

            ch->state = state;
            break;

        case CS_MOVING:
            /* velocity vector may have changed, always apply it */
            character_apply_velocity(ch, s);
            character_set_moved(ch);

            if (ch->state == CS_IDLE) {
                if (animation_push_by_name(ch->entity, s, "motion_start", true, false)) {
                    animation_set_end_callback(ch->entity, character_start_motion, ch);
                } else {
                    state = CS_IDLE;
                    goto fail_fallback;
                }
            } else if ((ch->state == CS_FALLING || ch->state == CS_JUMPING) && !ch->airborne) {
                if (!animation_push_by_name(ch->entity, s, "jump_to_motion", true, false)) {
                    state = CS_IDLE;
                    goto fail_fallback;
                }
            } else if (ch->state == CS_JUMP_START) {
                state = CS_IDLE;
                goto fail_fallback;
            } else if (ch->state == CS_MOVING) {
                return;
            }

            if (body)
                phys_body_enable(body, true);

            if (!animation_push_by_name(ch->entity, s, "motion", false, true)) {
                state = CS_IDLE;
                goto fail_fallback;
            }
            ch->state = state;
            break;

        case CS_JUMP_START:
            if (ch->state == CS_IDLE) {
                if (body)
                    phys_body_enable(body, true);

                if (animation_push_by_name(ch->entity, s, "idle_to_jump", true, false)) {
                    animation_set_frame_callback(ch->entity, character_jump_frame_callback);
                    animation_set_end_callback(ch->entity, character_any_to_jump, ch);
                } else {
                    state = CS_IDLE;
                    goto fail_fallback;
                }
            } else if (ch->state == CS_MOVING) {
                phys_body_set_velocity(body, ch->velocity);
                ch->airborne = true;

                if (animation_push_by_name(ch->entity, s, "motion_to_jump", true, false)) {
                    animation_set_end_callback(ch->entity, character_any_to_jump, ch);
                } else {
                    state = CS_IDLE;
                    goto fail_fallback;
                }
            } else if (ch->state == CS_JUMP_START || ch->state == CS_JUMPING) {
                state = CS_IDLE;
                goto fail_fallback;
            }

            ch->state = state;
            break;

        case CS_JUMPING:
            if (ch->state == CS_JUMP_START) {
                if (animation_push_by_name(ch->entity, s, "jump", true, true)) {
                    ch->state = state;
                    break;
                }
            }
            state = CS_IDLE;
            goto fail_fallback;

        case CS_FALLING:
            if (ch->state == CS_MOVING) {
                phys_body_set_velocity(body, (vec3){ 0, 0, 0 });
            } else if (ch->state == CS_IDLE) {
                /* ground disappeared */
                phys_body_enable(body, true);
            } else if (ch->state == CS_JUMP_START || ch->state == CS_JUMPING) {
                return;
            }
            animation_push_by_name(ch->entity, s, "fall", true, true);
            ch->state = state;
            break;

        default:
            ch->state = state;
            break;
    }
}

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

    if (!entity3d_matches(ch->entity, ENTITY3D_PHYS_IS_BODY))
        return false;

    vec3_dup(ch->velocity, (vec3){ dx * ch->jump_forward, ch->jump_upward, dz * ch->jump_forward });

    character_set_state(ch, s, CS_JUMP_START);

    return true;
}

void character_move(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;

    ch->airborne = body ? !phys_body_ground_collide(body, !ch->airborne) : 0;

    if (ch->airborne) {
        /*
         * Apply gravity and move via sweep-and-slide.  The sweep's
         * slide projection naturally preserves the vertical component
         * on near-vertical surfaces while redirecting the horizontal
         * one, so horizontal and vertical don't need separate handling.
         *
         * Disable rollback while airborne so the character can land on
         * geometry.  The grounded path keeps rollback for wall collision.
         */
        double dt_sec = clap_get_fps_delta(s->clap_ctx).tv_nsec / (double)NSEC_PER_SEC;
        if (dt_sec > 1e-6) {
            vec3 gravity;
            phys_body_get_gravity(body, gravity);
            ch->velocity[1] += gravity[1] * dt_sec;
            character_apply_velocity(ch, s);
            character_set_moved(ch);
        }

        character_set_state(ch, s, CS_FALLING);
        goto out;
    }

    if (ch->old_collision != ch->collision) {
        if (ch->old_collision && ch->old_collision->disconnect)
            ch->old_collision->disconnect(ch->old_collision, ch->entity, ch->old_collision->connect_priv);
        if (ch->collision && ch->collision->connect)
            ch->collision->connect(ch->collision, ch->entity, ch->collision->connect_priv);
        ch->old_collision = ch->collision;
    }

    struct motionctl *mctl = &s->mctl;
    vec3_dup(ch->motion, (vec3){ mctl->dx, 0.0, mctl->dz });

    if (ch->jump && character_jump(ch, s, mctl->dx, mctl->dz))
        goto out;

    if (vec3_len(ch->motion)) {
        vec3 newx, newy, newz;
        vec3 oldx = { 1, 0, 0 };

        vec3_dup(newy, ch->normal);
        if (likely(vec3_len(newy) > 0.0)) {
            float motion_coefficient;

            vec3_mul_cross(newz, oldx, newy);
            vec3_mul_cross(newx, newy, newz);

            vec3_norm(newx, newx);
            vec3_norm(newy, newy);
            vec3_norm(newz, newz);

            /* XXX: apply this in character_apply_velocity() */
            if (ch->state == CS_MOVING)
                motion_coefficient = 1.0f;
            else
                motion_coefficient = 0.3f;

            /* watch out for Y and Z swapping places */
            vec3_add_scaled(ch->velocity, newx, newz, ch->motion[0] * motion_coefficient, ch->motion[2] * motion_coefficient);
        }

        character_set_state(ch, s, CS_MOVING);
    } else if (!ch->airborne) {
        character_set_state(ch, s, CS_IDLE);
    }

out:
    if (scene_control_character(s) == ch)
        character_debug(ch);
}

void character_stop(struct character *c, struct scene *s)
{
    vec3_dup(c->motion, (vec3){});
    vec3_dup(c->velocity, (vec3){});
    character_set_state(c, s, CS_IDLE);
}

static void history_push(struct character *c)
{
    if (c->airborne)
        return;

    transform_pos(&c->entity->xform, c->history.pos[c->history.head++]);
    c->history.head %= POS_HISTORY_MAX;

    if (!c->history.wrapped && !c->history.head)
        c->history.wrapped = true;
}

static void history_fetch(struct character *c, vec3 pos)
{
    if (c->history.wrapped) {
        vec3_dup(pos, c->history.pos[c->history.head]);
        c->history.wrapped = false;
    } else {
        /* if history is completely empty, (0,0,0) is as good a place as any */
        vec3_dup(pos, c->history.pos[0]);
    }

    c->history.head = 0;
}

static void history_newest(struct character *c, vec3 pos)
{
    if (c->history.head) {
        vec3_dup(pos, c->history.pos[c->history.head - 1]);
    } else if (c->history.wrapped) {
        vec3_dup(pos, c->history.pos[POS_HISTORY_MAX - 1]);
    } else {
        vec3_dup(pos, (vec3){});
    }
}

/* data is struct scene */
static int character_update(entity3d *e, void *data)
{
    struct character *c = CRES_RET(entity3d_character(e), return -1);
    struct scene     *s = data;

    /*
     * If character falls too far down from their last know grounded Y coordinate,
     * teleport them back to a farthest grounded location in history buffer
     */
    vec3 last;
    history_newest(c, last);
    const float *pos = transform_pos(&e->xform, NULL);
    if (vec3_mul_inner(last, last) > 0.0 && fabsf(pos[1] - last[1]) >= s->limbo_height) {
        vec3 pos;
        history_fetch(c, pos);
        entity3d_position(e, pos);
    }

    if (entity3d_matches(e, ENTITY3D_HAS_PHYSICS)) {
        if (phys_body_update(e)) {
            history_push(c);
            character_set_moved(c);
        }
    }

    character_motion_reset(c, s);

    return c->orig_update(e, data);
}

static cerr character_make(struct ref *ref, void *_opts)
{
    rc_init_opts(character) *opts = _opts;

    if (!opts->txmodel || !opts->scene)
        return CERR_INVALID_ARGUMENTS;

    struct character *c = container_of(ref, struct character, ref);

    c->entity = CRES_RET_CERR(ref_new_checked(entity3d, .txmodel = opts->txmodel));
    entity3d_set(c->entity, ENTITY3D_IS_CHARACTER, c);
    c->orig_update = c->entity->update;
    c->entity->update = character_update;
    c->state = CS_AWAKE;
    c->jump_forward = 0.5;
    c->jump_upward = 3.5;
    c->mq = &opts->scene->mq;
    list_append(&opts->scene->characters, &c->entry);
    c->mq->nr_characters++;
    character_motion_reset(c, opts->scene);

    return CERR_OK;
}

static void character_drop(struct ref *ref)
{
    struct character *c = container_of(ref, struct character, ref);

    c->mq->nr_characters--;
    ref_put_last_ref(&c->entity->ref);
}

DEFINE_REFCLASS2(character);
