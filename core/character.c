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

/*
 * The head follows a point of interest: the look-at pose class on an
 * overlay masked to the head's subtree, over whatever the body plays.
 * One configuration for every model with a head joint: the joint is
 * resolved by type, the axes come from the bind pose.
 */
/** CHARACTER_LOOK_LAYER - the overlay the head looks around on */
#define CHARACTER_LOOK_LAYER    1
/** CHARACTER_LOOK_RANGE - meters; further than this, the head rests */
#define CHARACTER_LOOK_RANGE    12.0f
/** CHARACTER_LOOK_HEAD - of the other's height above its center: the face */
#define CHARACTER_LOOK_HEAD     0.35f

static const lookat_config character_look_cfg = {
    .joint      = -1,
    .joint_type = JOINT_HEAD,
    .yaw_max    = 1.2,
    .pitch_max  = 0.6,
    .slew       = 3.0,
    .law        = { .kind = TARGET_FOLLOW, .rate = 8.0 },
};

static void character_look_start(struct character *ch)
{
    entity3d *e = ch->entity;
    model3d *m = e->txmodel->model;
    cres(joint_type) head = model3d_get_joint(m, JOINT_HEAD);

    if (IS_CERR(head))  return;

    animator_t *a = entity3d_animator(e);
    anim_t ani = CRES_RET(anim_lib_pose(model3d_anim_lib(m), &lookat_pose_ops, &character_look_cfg),
                          return);

    ch->look.target.none = true;
    CERR_RET(animator_bind(a, ani, &ch->look), return);
    CERR_RET(animator_mask_subtree(a, CHARACTER_LOOK_LAYER, head.val), return);
    CERR_RET(animator_play(a, ani, .layer = CHARACTER_LOOK_LAYER), return);
}

/* the brain's part: the controlled entity if it is someone else, else the nearest other character */
static void character_look_pick(struct character *ch, struct scene *s)
{
    entity3d *e = ch->entity, *poi = nullptr;
    vec3 pos;
    float best = CHARACTER_LOOK_RANGE * CHARACTER_LOOK_RANGE;

    transform_pos(&e->xform, pos);

    if (s->control && s->control != e && entity3d_matches(s->control, ENTITY3D_IS_CHARACTER)) {
        vec3 d;

        vec3_sub(d, s->control->aabb_center, pos);
        if (vec3_mul_inner(d, d) < best)    poi = s->control;
    } else {
        struct character *other;

        list_for_each_entry(other, &s->characters, entry) {
            vec3 d;

            if (other == ch)    continue;

            vec3_sub(d, other->entity->aabb_center, pos);
            float d2 = vec3_mul_inner(d, d);
            if (d2 < best) {
                best = d2;
                poi = other->entity;
            }
        }
    }

    ch->look.target.entity = poi;
    ch->look.target.none = !poi;
    if (poi)    vec3_dup(ch->look.target.offset, (vec3){ 0.0f, entity3d_aabb_Y(poi) * CHARACTER_LOOK_HEAD, 0.0f });
}

static void character_dash(struct character *ch, struct scene *s)
{
    if (!ch->can_dash)
        return;

    /* if not already dashing or in dashing cooldown, dash */
    if (!timespec_nonzero(&ch->dash_started)) {
        ch->dash_started = clap_get_current_timespec(s->clap_ctx);
        ch->lin_speed *= 1.5;
        animator_set_speed(entity3d_animator(ch->entity), 0, 1.5);
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

    if (gpsm_get(&ch->sm) == CS_IDLE) {
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
            animator_set_speed(entity3d_animator(ch->entity), 0, 1.0);
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

    /*
     * The jump buttons (space on the keyboard, Y on the gamepad) carry
     * to_press_release() values: 1 on press, and on the keyboard's auto-repeat,
     * 2 on release. jump_held mirrors the button, and only the release clears
     * it, so auto-repeat can't retrigger a jump.
     */
    bool press   = m->input.space == 1 || m->input.pad_y == 1;
    bool release = m->input.space == 2 || m->input.pad_y == 2;

    if (release) {
        ch->jump_held = false;
    } else if (press) {
        /*
         * ch->jump is the edge: only on the rising edge (was-released ->
         * pressed). Hold-repeat events keep arriving while held, but
         * jump_held latches them so we don't stack spurious jumps.
         */
        character_state state = gpsm_get(&ch->sm);
        if (!ch->jump_held &&
            state != CS_JUMPING && state != CS_JUMP_START && state != CS_ASCENDING)
            ch->jump = true;
        ch->jump_held = true;
    }
}

static void character_idle(struct scene *s, void *priv)
{
    struct character *c = priv;

    /* end of the wake-up animation: the character is awake and idle */
    gpsm_set(&c->sm, CS_IDLE);
}

static void character_any_to_jump(struct scene *s, void *priv)
{
    struct character *c = priv;

    c->airborne = true;

    /*
     * At the end of the pre-jump windup we branch on hold state: if the
     * character can levitate and the button is still held, it transitions
     * into a sustained ascent; otherwise we commit to a regular ballistic
     * jump.
     */
    if (c->jump_held && c->can_levitate) {
        gpsm_set(&c->sm, CS_ASCENDING);
        return;
    }

    /*
     * Re-establish jump velocity at the moment of actual liftoff.
     * During the "motion_to_jump" animation, the grounded motion code
     * in character_move() overwrites ch->velocity with horizontal-only
     * running velocity (via vec3_add_scaled), losing the jump_upward
     * component that character_jump() originally set.
     */
    vec3_dup(c->velocity, (vec3){
        c->motion[0] * c->jump_forward,
        c->jump_upward,
        c->motion[2] * c->jump_forward,
    });
    gpsm_set(&c->sm, CS_JUMPING);
}

/*
 * The one end callback for every transition the table plays: only the
 * lead-in states have anything to do when their clip runs out.
 */
static void character_transition_done(animator_t *a, struct scene *s, void *priv)
{
    struct character *ch = priv;

    switch (gpsm_get(&ch->sm)) {
        case CS_WAKING:     character_idle(s, ch);          break;
        case CS_JUMP_START: character_any_to_jump(s, ch);   break;
        default:            break;
    }
}

/* the states the character leaves when what they play ends */
static bool character_state_waits(character_state s)
{
    return s == CS_WAKING || s == CS_JUMP_START;
}

character_state character_anims_enter(struct character *ch, character_state from, character_state to)
{
    animator_t *a = entity3d_animator(ch->entity);

    if (from == to || (int)from == GPSM_NO_STATE)   return to;

    /*
     * What the new state plays may be what plays already: a rig that
     * serves every state (character_anims_pose()). Restarting it would
     * re-anchor it for nothing, so it is left alone.
     */
    cres(anim_t) same = anim_lib_transition_find(animator_lib(a), to, from, nullptr);
    if (!IS_CERR(same) && animator_is_playing(a, 0, same.val))  return to;

    cerr err = animator_play_transition(a, from, to, .end = character_transition_done, .end_priv = ch);

    /*
     * A state the character leaves on the end callback can't wait for a
     * clip the model doesn't have: a zero hold ends at the next update
     * instead. A body with no clips at all wakes and jumps this way.
     */
    if (IS_CERR_CODE(err, CERR_NOT_FOUND) && character_state_waits(to)) {
        anim_t hold = CRES_RET(anim_lib_hold(animator_lib(a), 0.0), return to);

        animator_play(a, hold, .end = character_transition_done, .end_priv = ch);
    }

    return to;
}

bool character_anims_playable(struct character *ch, character_state from, character_state to)
{
    anim_lib_t *lib = model3d_anim_lib(ch->entity->txmodel->model);

    /* nothing to play, nothing to refuse with */
    if (!anim_lib_nr_transitions(lib))  return true;

    cres(anim_t) ani = anim_lib_transition_find(lib, to, from, nullptr);

    return !IS_CERR(ani);
}

void character_set_moved(struct character *c)
{
    if (c->camera)
        transform_set_updated(&c->camera->xform);
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
        gpsm_debug(&ch->sm, false);
        igCheckbox("airborne", &ch->airborne);
        bool moved = transform_is_updated(&ch->entity->xform);
        igBeginDisabled(true);
        igCheckbox("moved", &moved);
        igCheckbox("jump_held", &ch->jump_held);
        igCheckbox("jump", &ch->jump);
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
        if (ch->velocity[1] > 0) {
            /*
             * Rising (jumping): combined sweep with all contacts
             * blocking. The split vertical/horizontal path used
             * while falling lets walls pass the vertical filter
             * (so the character doesn't catch on edges), but
             * during an upward sweep that same filter lets the
             * character tunnel through wall top edges into the
             * interior of a mesh. Combined sweep blocks any
             * contact, which matches grounded motion behavior.
             */
            vec3 delta;
            vec3_scale(delta, ch->velocity, dt_sec);
            float moved = character_sweep_delta(e->phys_body, delta, -1.0f, true,
                                                ch->velocity, char_mass);
            if (moved < 1.0f)
                ch->velocity[1] = 0;
        } else {
            /*
             * Falling: split vertical and horizontal sweeps. The
             * vertical sweep ignores wall/edge contacts so the
             * character doesn't catch on edges ("fall of shame").
             * Horizontal sweep blocks any contact, including
             * walls, so the character slides along them.
             */
            vec3 v_delta = { 0, ch->velocity[1] * dt_sec, 0 };
            vec3 h_delta = { ch->velocity[0] * dt_sec, 0, ch->velocity[2] * dt_sec };
            float v_moved = character_sweep_delta(e->phys_body, v_delta, 0.5f, false,
                                                  ch->velocity, char_mass);
            character_sweep_delta(e->phys_body, h_delta, -1.0f, true,
                                  ch->velocity, char_mass);
            if (v_moved < 1.0f)
                ch->velocity[1] = 0;
        }
    } else {
        vec3 delta;
        vec3_scale(delta, ch->velocity, dt_sec);
        character_sweep_delta(e->phys_body, delta, -1.0f, true,
                              ch->velocity, char_mass);
    }

    if (gpsm_get(&ch->sm) == CS_LEVITATING)     vec3_dup(ch->velocity, (vec3){});

    /* Zero body velocity so ODE dynamics don't fight the sweep */
    phys_body_set_velocity(e->phys_body, (vec3){ 0, 0, 0 });

rotate:
    entity3d_rotate(ch->entity, 0, atan2f(ch->motion[0], ch->motion[2]), 0);
}

/*
 * State machine (gpsm.h). Every callback gets the character as @priv. Guards
 * refuse or redirect a transition before anything is committed; enters push
 * the animations, and can land on a different state by returning it.
 */

static cres(gpsm_state_t) cs_moving_guard(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    /* asked of a sleeping character, MOVING (and FALLING) wakes it up instead */
    return cres_val(gpsm_state_t, from == CS_START ? CS_WAKING : CS_MOVING);
}

static cres(gpsm_state_t) cs_falling_guard(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    /* see cs_moving_guard() */
    return cres_val(gpsm_state_t, from == CS_START ? CS_WAKING : CS_FALLING);
}

static cres(gpsm_state_t) cs_idle_guard(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    struct character *ch = priv;

    /* The end of the wake-up animation always lands; nothing else idles mid-air */
    if (from != CS_WAKING && ch->airborne)  return cres_error(gpsm_state_t, CERR_GPSM_ABORT);

    return cres_val(gpsm_state_t, CS_IDLE);
}

static cres(gpsm_state_t) cs_jump_start_guard(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    struct character *ch = priv;

    /* No windup clip, no jump. Refusing here keeps what plays as it is */
    if (!character_anims_playable(ch, from, CS_JUMP_START))
        return cres_error(gpsm_state_t, CERR_GPSM_ABORT);

    return cres_val(gpsm_state_t, CS_JUMP_START);
}

static gpsm_state_t cs_start_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    return character_anims_enter(priv, from, CS_START);
}

static gpsm_state_t cs_waking_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    return character_anims_enter(priv, from, CS_WAKING);
}

static gpsm_state_t cs_idle_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    return character_anims_enter(priv, from, CS_IDLE);
}

static gpsm_state_t cs_moving_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    struct character *ch = priv;

    /* Missing a clip, land idle instead: from the air, that plays the landing */
    if (!character_anims_playable(ch, from, CS_MOVING)) return CS_IDLE;

    return character_anims_enter(ch, from, CS_MOVING);
}

static gpsm_state_t cs_jump_start_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    struct character *ch = priv;

    character_anims_enter(ch, from, CS_JUMP_START);
    /* a running jump is airborne from the windup on */
    if (from == CS_MOVING)  ch->airborne = true;

    return CS_JUMP_START;
}

static gpsm_state_t cs_jumping_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    return character_anims_enter(priv, from, CS_JUMPING);
}

static gpsm_state_t cs_ascending_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    /*
     * Entered from the end of the pre-jump windup when the button is still
     * held (character_any_to_jump()). character_move() drives the per-frame
     * ascent and the ASCENDING -> LEVITATING transition on release.
     */
    return character_anims_enter(priv, from, CS_ASCENDING);
}

static gpsm_state_t cs_levitating_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    struct character *ch = priv;

    /*
     * The jump button was released mid-ascent, so the character latches into
     * a zero-velocity hover. Zero out the ascent velocity here,
     * character_apply_velocity() keeps it zeroed on steered frames;
     * character_move() re-applies horizontal motion from input each frame.
     * The fall animation has been playing since the ascent started.
     */
    vec3_dup(ch->velocity, (vec3){});

    return character_anims_enter(ch, from, CS_LEVITATING);
}

static gpsm_state_t cs_falling_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    return character_anims_enter(priv, from, CS_FALLING);
}

/*
 * Missing edges are deliberate: JUMP_START and JUMPING stay put while airborne
 * (JUMPING covers the whole arc down to the landing), JUMP_START and
 * LEVITATING never idle (the windup ends in a jump, a hover ends in a fall),
 * and IDLE lists itself only so that cs_moving_enter() can fall back to it.
 */
const struct gpsm_state character_states[CS_NR] = {
    [CS_START]      = { .name = "start",      .enter = cs_start_enter,
                        .valid_from = GPSM_FROM(CS_IDLE) },
    [CS_WAKING]     = { .name = "waking",     .enter = cs_waking_enter,
                        .valid_from = GPSM_FROM(CS_START) },
    [CS_IDLE]       = { .name = "idle",       .enter = cs_idle_enter,       .guard = cs_idle_guard,
                        .valid_from = GPSM_FROM(CS_WAKING, CS_IDLE, CS_MOVING, CS_JUMPING, CS_FALLING) },
    [CS_MOVING]     = { .name = "moving",     .enter = cs_moving_enter,     .guard = cs_moving_guard,
                        .valid_from = GPSM_FROM(CS_START, CS_IDLE, CS_JUMPING, CS_FALLING) },
    [CS_JUMP_START] = { .name = "jump start", .enter = cs_jump_start_enter, .guard = cs_jump_start_guard,
                        .valid_from = GPSM_FROM(CS_IDLE, CS_MOVING) },
    [CS_JUMPING]    = { .name = "jumping",    .enter = cs_jumping_enter,
                        .valid_from = GPSM_FROM(CS_JUMP_START) },
    [CS_ASCENDING]  = { .name = "ascending",  .enter = cs_ascending_enter,
                        .valid_from = GPSM_FROM(CS_JUMP_START) },
    [CS_LEVITATING] = { .name = "levitating", .enter = cs_levitating_enter,
                        .valid_from = GPSM_FROM(CS_ASCENDING) },
    [CS_FALLING]    = { .name = "falling",    .enter = cs_falling_enter,    .guard = cs_falling_guard,
                        .valid_from = GPSM_FROM(CS_START, CS_IDLE, CS_MOVING, CS_LEVITATING) },
};

static bool character_is_awake(struct character *ch)
{
    /* START and WAKING are not: input wakes the character up, but doesn't move it */
    return gpsm_get(&ch->sm) >= CS_IDLE;
}

static bool character_jump(struct character *ch, struct scene *s, float dx, float dz)
{
    /*
     * Jumping while levitating drops the character. Return true so the
     * caller skips this frame's motion block: asking for MOVING from FALLING
     * while airborne would push the landing animation mid-air.
     */
    if (gpsm_get(&ch->sm) == CS_LEVITATING) {
        gpsm_set(&ch->sm, CS_FALLING);
        return true;
    }

    /*
     * Clearing ch->jump here is pointless, because it can be set again from the
     * input handler for the next frame. We won't act on it here while ch->airborne
     * is set, but we will, as soon as the character touches the ground. Instead,
     * it needs to be cleared every frame at character_motion_reset(). See comment
     * there.
     */
    if (!ch->can_jump || ch->airborne)
        return false;

    /*
     * On the landing frame the state is still JUMPING or FALLING: the
     * grounded transition hasn't run yet, so there is no animation to
     * chain the jump onto and the state machine would refuse it.
     * Setting the velocity first would then leak it into that frame.
     */
    character_state state = gpsm_get(&ch->sm);
    if (state != CS_IDLE && state != CS_MOVING)     return false;

    if (!entity3d_matches(ch->entity, ENTITY3D_PHYS_IS_BODY))
        return false;

    vec3_dup(ch->velocity, (vec3){ dx * ch->jump_forward, ch->jump_upward, dz * ch->jump_forward });

    gpsm_set(&ch->sm, CS_JUMP_START);

    return true;
}

void character_move(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;

    ch->airborne = body ? !phys_body_ground_collide(body, !ch->airborne) : 0;

    /*
     * Protect airborne state during the rising phase of a jump. The
     * character just set velocity[1] = jump_upward and hasn't moved
     * up yet, so ground_collide sees ground directly below and would
     * wrongly mark us grounded; the state machine is the authority
     * here. Once gravity brings velocity[1] to zero, ground_collide
     * takes over for landing detection.
     */
    if (gpsm_get(&ch->sm) == CS_JUMPING && ch->velocity[1] > 0)
        ch->airborne = true;

    /*
     * ASCENDING: jump button still held at the end of the pre-jump windup;
     * sustain an upward velocity and fly the character ourselves rather
     * than falling through to the motion block, which would clobber
     * velocity[1] via vec3_add_scaled() and leave us hovering at the
     * windup height. Once the button is released (jump_held flips off
     * via the release event from to_press_release) latch into LEVITATING.
     */
    if (gpsm_get(&ch->sm) == CS_ASCENDING) {
        if (!ch->jump_held) {
            gpsm_set(&ch->sm, CS_LEVITATING);
        } else {
            ch->airborne = true;
            vec3_dup(ch->normal, (vec3){ 0.0f, 1.0f, 0.0f });

            struct motionctl *mctl = &s->mctl;
            vec3_dup(ch->motion, (vec3){ mctl->dx, 0.0f, mctl->dz });

            /*
             * Horizontal control while ascending is a quarter of grounded
             * speed so the player can steer but not zoom around;
             * velocity[1] is the actual rise rate.
             */
            ch->velocity[0] = ch->motion[0] * 0.25f;
            ch->velocity[1] = ch->jump_upward;
            ch->velocity[2] = ch->motion[2] * 0.25f;

            character_apply_velocity(ch, s);
            character_set_moved(ch);
            goto out;
        }
    }

    if (gpsm_get(&ch->sm) == CS_LEVITATING) {
        ch->airborne = true;
        vec3_dup(ch->normal, (vec3){ 0.0f, 1.0f, 0.0f });
        goto motion;
    }

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
            /*
             * A fall from walking/running bleeds off horizontal velocity
             * (exponential decay at fall_drag 1/s), unlike a jump, whose
             * ballistic arc is tuned via jump_forward and stays intact.
             * The state tells the two apart: CS_FALLING's valid_from
             * omits CS_JUMPING, so jump states own the whole arc.
             */
            character_state state = gpsm_get(&ch->sm);

            if (state != CS_JUMP_START && state != CS_JUMPING) {
                float keep = expf(-ch->fall_drag * (float)dt_sec);

                ch->velocity[0] *= keep;
                ch->velocity[2] *= keep;
            }
            character_apply_velocity(ch, s);
            character_set_moved(ch);
        }

        /* JUMP_START and JUMPING turn this down: they are already airborne */
        if (gpsm_get(&ch->sm) != CS_FALLING)
            gpsm_set(&ch->sm, CS_FALLING);
        goto out;
    }

    if (ch->old_collision != ch->collision) {
        if (ch->old_collision && ch->old_collision->disconnect)
            ch->old_collision->disconnect(ch->old_collision, ch->entity, ch->old_collision->connect_priv);
        if (ch->collision && ch->collision->connect)
            ch->collision->connect(ch->collision, ch->entity, ch->collision->connect_priv);
        ch->old_collision = ch->collision;
    }

motion:
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
            character_state state = gpsm_get(&ch->sm);
            if (state == CS_MOVING || state == CS_LEVITATING)
                motion_coefficient = 1.0f;
            else
                motion_coefficient = 0.3f;

            /* watch out for Y and Z swapping places */
            vec3_add_scaled(ch->velocity, newx, newz, ch->motion[0] * motion_coefficient, ch->motion[2] * motion_coefficient);
        }

        /*
         * The velocity is input-driven, so it is applied here and not by the
         * state machine: it moves the character whether or not MOVING is
         * granted (JUMP_START windup, LEVITATING), as long as it is awake.
         */
        if (character_is_awake(ch)) {
            character_apply_velocity(ch, s);
            character_set_moved(ch);
        }

        if (gpsm_get(&ch->sm) != CS_MOVING)
            gpsm_set(&ch->sm, CS_MOVING);
    } else if (!ch->airborne && character_is_awake(ch) && gpsm_get(&ch->sm) != CS_IDLE) {
        /* WAKING -> IDLE is the wake-up animation's to take, see character_idle() */
        gpsm_set(&ch->sm, CS_IDLE);
    }

out:
    if (scene_control_character(s) == ch)
        character_debug(ch);
}

void character_stop(struct character *c, struct scene *s)
{
    vec3_dup(c->motion, (vec3){});
    vec3_dup(c->velocity, (vec3){});
    c->jump_held = false;
    /* a waking character finishes waking up on its own */
    if (character_is_awake(c))
        gpsm_set(&c->sm, CS_IDLE);
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
        phys_body_update(e);
        /*
         * phys_body_update()'s velocity-based motion signal doesn't
         * work for characters since kinematic sweep zeros ODE body
         * velocity every frame. Detect motion by position delta
         * against the newest history entry instead.
         */
        if (!c->airborne) {
            vec3 newest, diff;
            history_newest(c, newest);
            vec3_sub(diff, transform_pos(&e->xform, NULL), newest);
            if (vec3_mul_inner(diff, diff) > 1e-4f) {
                history_push(c);
                character_set_moved(c);
            }
        }
    }

    character_motion_reset(c, s);
    character_look_pick(c, s);

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
    c->scene = opts->scene;
    c->sm.name = txmodel_name(opts->txmodel);
    CERR_RET_CERR(gpsm_init(&c->sm, character_states, CS_NR, c, CS_IDLE));
    c->jump_forward = 0.5;
    c->jump_upward = 3.5;
    /* sharp, near-realistic drop; lower values carry more run momentum */
    c->fall_drag = 10.0;
    c->mq = &opts->scene->mq;
    list_append(&opts->scene->characters, &c->entry);
    c->mq->nr_characters++;
    character_motion_reset(c, opts->scene);
    character_look_start(c);

    return CERR_OK;
}

static void character_drop(struct ref *ref)
{
    struct character *c = container_of(ref, struct character, ref);

    c->mq->nr_characters--;
    ref_put_last_ref(&c->entity->ref);
}

DEFINE_REFCLASS2(character);
