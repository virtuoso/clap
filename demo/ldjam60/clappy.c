// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <string.h>
#include "character.h"
#include "clap.h"
#include "interp.h"
#include "physics.h"
#include "scene.h"
#include "ldjam60.h"

/* clappy's model entry in the scene file */
#define CLAPPY_NAME         "clappy"

/*
 * The wheels' electric motors. Their hum's pitch follows the wheels' speed,
 * like a motor's whine follows its revolutions: from MOTOR_HZ_REST at a
 * standstill to MOTOR_HZ_TOP at MOTOR_TOP_SPEED (units per second), past
 * which the speed doesn't count (a teleport out of limbo isn't the wheels).
 * They sound louder as they go faster, up to MOTOR_GAIN from
 * MOTOR_LOUD_SPEED on, and brighter, with the cutoff from MOTOR_CUTOFF_REST
 * to MOTOR_CUTOFF_LOUD. The speed they hear follows the wheels' at
 * MOTOR_RESPONSE (1/s): a motor takes a moment to spin up and down.
 */
#define MOTOR_HZ_REST       30.0f       // starting hum frequency
#define MOTOR_HZ_TOP        55.0f       // top speed hum frequency
// #define MOTOR_TOP_SPEED     10.0f
#define MOTOR_LOUD_SPEED    0.2f
#define MOTOR_GAIN          0.1f
#define MOTOR_CUTOFF_REST   300.0f
#define MOTOR_CUTOFF_LOUD   3000.0f
#define MOTOR_RESPONSE      8.0f

/*
 * The death fall: landing from more than CLAPPY_DEATH_FALL below the highest
 * point clappy got to in the air breaks him into his parts, whether he rode
 * off an edge, jumped, or levitated and let go. Each part goes loose at his
 * pose as a body of its own, its convex hull, thrown out from his middle and
 * up at CLAPPY_BURST_SPEED, while he's hidden, out of the collisions and
 * holding still. The rotatable he fell off of, if he did, goes back the way
 * the scene had it, or it would stay where it threw him off, and the puzzle
 * with it. CLAPPY_RESPAWN_TIME later the pieces are gone and he's whole
 * again where he stood on plain ground CLAPPY_SAFE_TIME to twice that before
 * he left it, back from the edge he went over and off any switch, or where he
 * started if nothing is left under that spot to land on within
 * CLAPPY_DEATH_FALL (a rotatable that turned away).
 */
#define CLAPPY_DEATH_FALL   5.0f        // meters
#define CLAPPY_BURST_SPEED  2.5f        // meters per second
#define CLAPPY_RESPAWN_TIME 3.0f        // seconds
#define CLAPPY_SAFE_TIME    0.5f        // seconds

/*
 * Clappy claps: once he's stood idle on the ground for CLAPPY_CLAP_DELAY with
 * the camera on his front side, within the angle whose cosine is
 * CLAPPY_CLAP_FACING of straight ahead of him, he claps once, and not again
 * until he's gone somewhere
 */
#define CLAPPY_CLAP_DELAY   1.5f        // seconds
#define CLAPPY_CLAP_FACING  0.5f        // cosine: 60 degrees

/*
 * The antennae: the base they stand on (ANTENNAE_BASE) looks around, a slow
 * look with a quicker glance out of step on top; the spinner (ANTENNAE_SPIN)
 * spins about its length; the aimer (ANTENNAE_AIM) sways side to side. They
 * are lively when he stands and calm down as he goes, to ANTENNAE_GOING of
 * it at full speed, on an overlay over the character's own (character.c
 * takes 1 to 6).
 */
#define ANTENNAE_BASE           "antenna.base"
#define ANTENNAE_SPIN           "antenna.spin.bottom"
#define ANTENNAE_AIM            "antenna.aim.bottom"
#define ANTENNAE_LAYER          7
#define ANTENNAE_GOING          0.3f    // of their liveliness, at full speed
#define ANTENNAE_RESPONSE       3.0f    // 1/s: how fast they calm down and liven up
#define ANTENNAE_LOOK           0.6f    // radians either way the base looks...
#define ANTENNAE_LOOK_HZ        0.21f   // ...this often...
#define ANTENNAE_GLANCE         0.4f    // ...with a glance this much of the look...
#define ANTENNAE_GLANCE_HZ      0.53f   // ...this often
#define ANTENNAE_SPIN_RATE      9.0f    // radians per second the spinner turns
#define ANTENNAE_SWAY           0.25f   // radians either way the aimer sways...
#define ANTENNAE_SWAY_HZ        0.8f    // ...this often

/* The crash of a death fall's landing: the camera takes the blow */
static const camera_shake_options clappy_crash_shake = { .duration_ms = 700, .frequency = 7.0f, .roll = 3.0f };

/**
 * enum motor_osc - the oscillators of the motors' patch
 * @MOTOR_HUM:      the motor's hum, at the wheels' pitch
 * @MOTOR_WHINE:    the gears' whine an octave up, detuned into a beat
 * @MOTOR_RUMBLE:   the wheels rolling on the floor
 * @MOTOR_NR_OSCS:  number of oscillators
 */
enum motor_osc {
    MOTOR_HUM = 0,
    MOTOR_WHINE,
    MOTOR_RUMBLE,
    MOTOR_NR_OSCS,
};

/* A drone: it plays the oscillators' base frequencies, which clappy_update() keeps moving */
static const synth_config motor_patch = {
    .oscillators    = {
        [MOTOR_HUM]     = { .waveform = SYNTH_WAVE_SAWTOOTH, .frequency = MOTOR_HZ_REST, .gain = 0.4f },
        [MOTOR_WHINE]   = { .waveform = SYNTH_WAVE_SQUARE, .frequency = 2.0f * MOTOR_HZ_REST, .detune = 8.0f,
                            .gain = 0.15f },
        [MOTOR_RUMBLE]  = { .waveform = SYNTH_WAVE_PINK_NOISE, .gain = 0.5f },
    },
    .nr_oscillators = MOTOR_NR_OSCS,
    .category       = SOUND_CAT_SFX,
    .filter         = { .cutoff = MOTOR_CUTOFF_REST, .resonance = 1.2f },
    .amp_env        = { .attack_ms = 20.0f, .sustain = 1.0f, .release_ms = 50.0f },
    .gain           = 0.0f,
};

/* @joint turns by @angle about @axis, model space as its parent is posed so far: on top of its posed rotation */
static void antennae_turn(entity3d *e, struct joint *out, int joint, const vec3 axis, float angle)
{
    model3d *m = e->txmodel->model;
    vec4 model = { axis[0], axis[1], axis[2], 0.0f }, local;
    mat4x4 parent, inv;
    quat turn, posed;

    entity3d_joint_global(e, m->joints[joint].parent, parent);
    mat4x4_invert(inv, parent);
    mat4x4_mul_vec4(local, inv, model);

    /* out[joint] is read while quat_mul() writes it */
    memcpy(posed, out[joint].rotation, sizeof(quat));
    quat_from_axis_angle(turn, local, angle);
    quat_mul(out[joint].rotation, turn, posed);
}

static void antennae_start(const void *cfg, void *priv, entity3d *e)
{
    struct clappy_antennae *an = priv;

    an->lively = an->activity;
}

static cerr antennae_eval(const void *cfg, void *priv, entity3d *e, double t, double dt, struct joint *out)
{
    struct clappy_antennae *an = priv;
    float time = (float)t, turn = 2.0f * (float)M_PI;
    quat spin, posed;

    an->lively = linf_interp(an->lively, an->activity, approachf(ANTENNAE_RESPONSE, (float)dt));
    an->spun = fmodf(an->spun + ANTENNAE_SPIN_RATE * an->lively * (float)dt, turn);

    float look = sinf(turn * ANTENNAE_LOOK_HZ * time) + ANTENNAE_GLANCE * sinf(turn * ANTENNAE_GLANCE_HZ * time + 1.0f);
    antennae_turn(e, out, an->base, (vec3){ 0.0f, 1.0f, 0.0f }, ANTENNAE_LOOK * an->lively * look);

    /* about its own length, its bone's: after its posed rotation; out[] is read while quat_mul() writes it */
    memcpy(posed, out[an->spin].rotation, sizeof(quat));
    quat_from_axis_angle(spin, (vec3){ 0.0f, 1.0f, 0.0f }, an->spun);
    quat_mul(out[an->spin].rotation, posed, spin);

    antennae_turn(e, out, an->aim, (vec3){ 0.0f, 0.0f, 1.0f },
                  ANTENNAE_SWAY * an->lively * sinf(turn * ANTENNAE_SWAY_HZ * time));

    return CERR_OK;
}

static const anim_pose_ops antennae_ops = {
    .start  = antennae_start,
    .eval   = antennae_eval,
};

/* His antennae's own motions go on, over whatever the rest of him does */
static cerr clappy_antennae_start(clappy *cl)
{
    entity3d *e = cl->c->entity;
    model3d *m = e->txmodel->model;
    animator_t *a = entity3d_animator(e);
    struct clappy_antennae *an = &cl->antennae;

    an->base = CRES_RET_CERR(model3d_joint_by_name(m, ANTENNAE_BASE));
    an->spin = CRES_RET_CERR(model3d_joint_by_name(m, ANTENNAE_SPIN));
    an->aim = CRES_RET_CERR(model3d_joint_by_name(m, ANTENNAE_AIM));
    an->activity = 1.0f;

    anim_t ani = CRES_RET_CERR(anim_lib_pose(model3d_anim_lib(m), &antennae_ops, nullptr));

    CERR_RET_CERR(animator_bind(a, ani, an));
    CERR_RET_CERR(animator_mask_chain(a, ANTENNAE_LAYER, an->base, 1));
    CERR_RET_CERR(animator_mask_chain(a, ANTENNAE_LAYER, an->spin, 1));
    CERR_RET_CERR(animator_mask_chain(a, ANTENNAE_LAYER, an->aim, 1));

    return animator_play(a, ani, .layer = ANTENNAE_LAYER);
}

/* clappy_init()'s search for the rest of what he's made of */
struct clappy_parts {
    clappy  *cl;
    cerr    err;
};

/* One of clappy's visible child entities is one of his parts */
static void clappy_part_find(entity3d *e, void *data)
{
    struct clappy_parts *p = data;

    if (e->parent != p->cl->c->entity || !entity3d_matches(e, ENTITY3D_VISIBLE))   return;

    entity3d **part = darray_add(p->cl->parts);
    if (!part)  p->err = CERR_NOMEM;
    else        *part = e;
}

cerr clappy_init(clappy *cl, struct scene *scene)
{
    entity3d *e = CRES_RET_CERR(mq_find_entity(&scene->mq, CLAPPY_NAME));
    struct character *c = CRES_RET_CERR(entity3d_character(e));

    *cl = (clappy){ .c = c, .sound = clap_get_sound(scene->clap_ctx), .dead = -1.0f, .airborne = c->airborne };
    darray_init(cl->parts);
    darray_init(cl->pieces);
    transform_pos(&e->xform, cl->last_pos);
    vec3_dup(cl->start, cl->last_pos);
    vec3_dup(cl->safe, cl->last_pos);
    vec3_dup(cl->recent, cl->last_pos);
    cl->fall_top = cl->last_pos[1];

    /* his own entity first, then its children */
    if (entity3d_matches(e, ENTITY3D_VISIBLE)) {
        entity3d **part = darray_add(cl->parts);
        if (!part)  return CERR_NOMEM;
        *part = e;
    }

    struct clappy_parts parts = { .cl = cl, .err = CERR_OK };
    mq_for_each(&scene->mq, clappy_part_find, &parts);

    /* a model without the joints for them holds its antennae still */
    CERR_RET(clappy_antennae_start(cl), warn("clappy's antennae hold still: %s\n", cerr_str(__cerr.err)));

    return parts.err;
}

/* A piece of clappy where @part of him was, flying out from his middle and up */
static cerr clappy_piece_new(clappy *cl, struct scene *scene, entity3d *part, float mass)
{
    entity3d *e = cl->c->entity;
    entity3d *piece = CRES_RET_CERR(ref_new_checked(entity3d, .txmodel = part->txmodel));
    entity3d **slot = darray_add(cl->pieces);

    if (!slot) {
        ref_put(piece);
        return CERR_NOMEM;
    }
    *slot = piece;

    /* named for what it is: a lookup by his name finds him, not one of his pieces */
    if (!(piece->name = strdup(CLAPPY_NAME " piece")))  return CERR_NOMEM;

    /* in his pose, the bind pose, which a new entity's joints start out in */
    vec3 pos;
    transform_pos(&e->xform, pos);
    entity3d_position(piece, pos);
    transform_set_quat(&piece->xform, transform_rotation_quat(&e->xform));
    entity3d_scale(piece, e->scale);

    entity3d_add_physics(piece, clap_get_phys(scene->clap_ctx), mass, GEOM_TRIMESH, PHYS_BODY, 0.0, 0.0, 0.0);
    if (!entity3d_matches(piece, ENTITY3D_PHYS_IS_BODY))  return CERR_NOMEM;
    phys_body_rotate_xform(piece->phys_body, &piece->xform);

    /* out along the floor from his middle to the part's, and as fast up */
    model3d *m = part->txmodel->model;
    vec4 middle = {
        (m->aabb[0][0] + m->aabb[1][0]) / 2.0f,
        (m->aabb[0][1] + m->aabb[1][1]) / 2.0f,
        (m->aabb[0][2] + m->aabb[1][2]) / 2.0f,
        1.0f,
    }, at;
    mat4x4_mul_vec4(at, e->mx, middle);

    vec3 out = { at[0] - e->aabb_center[0], 0.0f, at[2] - e->aabb_center[2] };
    float len = vec3_len(out);
    vec3 vel = { 0.0f, CLAPPY_BURST_SPEED, 0.0f };

    if (len > 0.0f) {
        vel[0] = out[0] * CLAPPY_BURST_SPEED / len;
        vel[2] = out[2] * CLAPPY_BURST_SPEED / len;
    }
    phys_body_set_velocity(piece->phys_body, vel);

    return CERR_OK;
}

/* What he fell off of goes back the way the scene had it, if it's a rotatable or glued to one */
static void clappy_fell_off(clappy *cl)
{
    rotatable *r = CRES_RET(rotatable_of(cl->left), return);

    rotatable_reset(r);
}

/* Clappy breaks into his parts: loose pieces in his place, him hidden and out of the world, which holds him still */
static cerr clappy_break(clappy *cl, struct scene *scene)
{
    struct character *c = cl->c;
    entity3d *e = c->entity, **part;
    struct phys_body **limb;
    cerr err = CERR_OK;

    /* his mass, shared out among the pieces */
    float mass = phys_body_get_mass(e->phys_body) / darray_count(cl->parts);

    darray_for_each(part, cl->parts) {
        cerr piece_err = clappy_piece_new(cl, scene, *part, mass);

        if (IS_CERR(piece_err)) err = piece_err;
        entity3d_clear(*part, ENTITY3D_VISIBLE);
    }

    /* out of his pieces' way, and still, while the camera stays on where he fell */
    phys_body_set_collides(e->phys_body, false);
    darray_for_each(limb, e->limbs) phys_body_set_collides(*limb, false);
    character_stop(c, scene);

    if (scene->camera)
        CERR_RET(_camera_shake_push(scene->camera, &clappy_crash_shake),
                 err_cerr(__cerr, "couldn't shake the camera\n"));

    clappy_fell_off(cl);

    cl->dead = 0.0f;

    return err;
}

/* Whether there's something under @pos to land on, no further down than a death fall */
static bool clappy_has_ground(clappy *cl, const vec3 pos)
{
    entity3d *e = cl->c->entity;
    float height = entity3d_aabb_Y(e);
    double dist = height + CLAPPY_DEATH_FALL;

    /* from as high as he stands, so the floor he'd stand on is within reach */
    return phys_ray_cast(e, (vec3){ pos[0], pos[1] + height, pos[2] }, (vec3){ 0.0f, -1.0f, 0.0f }, &dist);
}

/* Clappy whole again: his pieces gone, if he's in pieces, and him back at @at */
static void clappy_respawn_at(clappy *cl, struct scene *scene, const vec3 at)
{
    struct character *c = cl->c;
    entity3d *e = c->entity, **part, **piece;
    struct phys_body **limb;
    vec3 to;

    darray_for_each(piece, cl->pieces)  ref_put(*piece);
    darray_clearout(cl->pieces);
    darray_for_each(part, cl->parts)    entity3d_set(*part, ENTITY3D_VISIBLE, nullptr);

    vec3_dup(to, at);
    entity3d_position(e, to);
    phys_body_set_collides(e->phys_body, true);
    darray_for_each(limb, e->limbs) phys_body_set_collides(*limb, true);

    character_stop(c, scene);

    /* the motors don't hear the jump back */
    vec3_dup(cl->last_pos, to);
    vec3_dup(cl->safe, to);
    vec3_dup(cl->recent, to);
    cl->stood = 0.0f;
    cl->fall_top = to[1];
    cl->dead = -1.0f;
}

/* After a death fall: back at @safe, or where he started if that has gone */
static void clappy_respawn(clappy *cl, struct scene *scene)
{
    clappy_respawn_at(cl, scene, clappy_has_ground(cl, cl->safe) ? cl->safe : cl->start);
}

/* Whether @cam is on clappy's front side, seen from above */
static bool clappy_faces(clappy *cl, struct camera *cam)
{
    entity3d *e = cl->c->entity;
    vec3 front = { 0.0f, 0.0f, 1.0f }, to;

    transform_rotate_vec3(&e->xform, front);
    vec3_sub(to, transform_pos(&cam->xform, nullptr), transform_pos(&e->xform, nullptr));
    front[1] = to[1] = 0.0f;

    float len = vec3_len(front) * vec3_len(to);
    return len > 0.0f && vec3_mul_inner(front, to) / len > CLAPPY_CLAP_FACING;
}

/* Standing idle, the camera on his front side, a while: he claps, once */
static void clappy_clap_update(clappy *cl, struct scene *scene, float dt)
{
    struct character *c = cl->c;
    bool idle = cl->dead < 0.0f && !c->airborne && gpsm_get(&c->sm) == CS_IDLE;

    if (!idle) {
        cl->idle = 0.0f;
        cl->clapped = false;
        return;
    }
    if (cl->clapped || !scene->camera || !clappy_faces(cl, scene->camera))  return;

    cl->idle += dt;
    if (cl->idle < CLAPPY_CLAP_DELAY)   return;

    cl->clapped = true;
    CERR_RET(character_handclap(c), warn_once("clappy can't clap: %s\n", cerr_str(__cerr.err)));
}

static inline float clappy_top_speed(clappy *cl)    { return cl->c->lin_speed; }

/* Set the motors' pitch, brightness and loudness to the speed they hear */
static cerr clappy_motor_set(clappy *cl)
{
    float pitch = clampf(cl->speed / clappy_top_speed(cl), 0.0f, 1.0f);
    float level = clampf(cl->speed / MOTOR_LOUD_SPEED, 0.0f, 1.0f);
    float hz = linf_interp(MOTOR_HZ_REST, MOTOR_HZ_TOP, pitch);

    synth_oscillator_config osc = motor_patch.oscillators[MOTOR_HUM];
    osc.frequency = hz;
    CERR_RET_CERR(synth_set_oscillator(cl->motor, MOTOR_HUM, &osc));

    osc = motor_patch.oscillators[MOTOR_WHINE];
    osc.frequency = 2.0f * hz;
    CERR_RET_CERR(synth_set_oscillator(cl->motor, MOTOR_WHINE, &osc));

    synth_filter_config filter = motor_patch.filter;
    filter.cutoff = linf_interp(MOTOR_CUTOFF_REST, MOTOR_CUTOFF_LOUD, level);
    CERR_RET_CERR(synth_set_filter(cl->motor, &filter));

    return synth_set_gain(cl->motor, MOTOR_GAIN * level);
}

cerr clappy_update(clappy *cl, struct scene *scene)
{
    if (!cl->c)     return CERR_OK;

    float dt = clap_get_fps_delta(scene->clap_ctx).tv_nsec / (double)NSEC_PER_SEC;
    if (dt <= 0.0f) return CERR_OK;

    const float *pos = transform_pos(&cl->c->entity->xform, nullptr);
    vec3 delta;

    vec3_sub(delta, pos, cl->last_pos);
    vec3_dup(cl->last_pos, pos);

    /* in the air, nothing turns the wheels: the motors spin down */
    float wheels = cl->c->airborne ? 0.0f : fminf(vec3_len(delta) / dt, clappy_top_speed(cl));
    cl->speed = linf_interp(cl->speed, wheels, approachf(MOTOR_RESPONSE, dt));

    /* the antennae calm down as he goes */
    struct character *c = cl->c;
    float going = c->lin_speed > 0.0f ? clampf(vec3_len(c->motion) / c->lin_speed, 0.0f, 1.0f) : 0.0f;
    cl->antennae.activity = linf_interp(1.0f, ANTENNAE_GOING, going);

    /* in pieces, he's whole again in time; whole, a landing from too high breaks him */
    bool landed = cl->airborne && !cl->c->airborne;

    cl->airborne = cl->c->airborne;
    if (cl->dead >= 0.0f) {
        cl->dead += dt;
        if (cl->dead >= CLAPPY_RESPAWN_TIME)    clappy_respawn(cl, scene);
    } else if (cl->c->airborne) {
        cl->fall_top = fmaxf(cl->fall_top, pos[1]);
    } else if (landed && cl->fall_top - pos[1] > CLAPPY_DEATH_FALL) {
        CERR_RET_CERR(clappy_break(cl, scene));
    } else {
        entity3d *ground = cl->c->old_collision;

        cl->left = ground;

        /*
         * Where he comes back to is where he stood a moment ago on plain
         * ground: not the edge he's about to go over, and not a switch,
         * which would go off
         */
        if (ground && !ground->connect) cl->stood += dt;
        if (cl->stood >= CLAPPY_SAFE_TIME) {
            vec3_dup(cl->safe, cl->recent);
            vec3_dup(cl->recent, pos);
            cl->stood = 0.0f;
        }
        cl->fall_top = pos[1];
    }

    clappy_clap_update(cl, scene, dt);

    /* the sound engine isn't running yet: the next frame tries again (as the menu's click does, see ui_click_play()) */
    if (!cl->motor) {
        cl->motor = CRES_RET(ref_new_checked(synth, .ctx = cl->sound, .config = &motor_patch), return CERR_OK);
        synth_start(cl->motor);
    }

    return clappy_motor_set(cl);
}

void clappy_restart(clappy *cl, struct scene *scene)
{
    if (!cl->c)     return;

    clappy_respawn_at(cl, scene, cl->start);
}

void clappy_done(clappy *cl)
{
    entity3d **piece;

    if (cl->motor)  ref_put(cl->motor);
    darray_for_each(piece, cl->pieces)  ref_put(*piece);
    darray_clearout(cl->pieces);
    darray_clearout(cl->parts);

    *cl = (clappy){};
}
