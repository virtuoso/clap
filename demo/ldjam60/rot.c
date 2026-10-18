// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <string.h>
#include "clap.h"
#include "gpsm.h"
#include "interp.h"
#include "memory.h"
#include "physics.h"
#include "scene.h"
#include "ldjam60.h"

/** ROTATABLE_TURN_TIME - seconds a quarter turn takes */
#define ROTATABLE_TURN_TIME 1.5f

/**
 * ROTATABLE_BLOCK_DEPTH - how far a turning rotatable goes into another static
 * mesh before it turns back
 *
 * The rotatables sit flush against their neighbors, and some go a centimeter
 * or so into them where the meshes meet: that's a touch, not a collision.
 */
#define ROTATABLE_BLOCK_DEPTH 0.02f

/*
 * The camera feels the turns: a platform locking into place at the end of a
 * turn, or of a turn back, gives it a bump; one running into something, a
 * clunk
 */
static const camera_shake_options rotatable_settle_shake = { .duration_ms = 250, .frequency = 10.0f, .roll = 0.5f };
static const camera_shake_options rotatable_clunk_shake = { .duration_ms = 450, .frequency = 14.0f, .roll = 1.5f };

static const struct {
    const char  *name;
    vec3        axis;
    float       angle;
} quarter_turns[QUARTER_TURN_NR] = {
    [QUARTER_TURN_X_POS] = { .name = "+x", .axis = { 1.0f, 0.0f, 0.0f }, .angle =  M_PI_2 },
    [QUARTER_TURN_X_NEG] = { .name = "-x", .axis = { 1.0f, 0.0f, 0.0f }, .angle = -M_PI_2 },
    [QUARTER_TURN_Y_POS] = { .name = "+y", .axis = { 0.0f, 1.0f, 0.0f }, .angle =  M_PI_2 },
    [QUARTER_TURN_Y_NEG] = { .name = "-y", .axis = { 0.0f, 1.0f, 0.0f }, .angle = -M_PI_2 },
    [QUARTER_TURN_Z_POS] = { .name = "+z", .axis = { 0.0f, 0.0f, 1.0f }, .angle =  M_PI_2 },
    [QUARTER_TURN_Z_NEG] = { .name = "-z", .axis = { 0.0f, 0.0f, 1.0f }, .angle = -M_PI_2 },
};

cres(quarter_turn) quarter_turn_parse(const char *str)
{
    if (!str)   return cres_error(quarter_turn, CERR_INVALID_ARGUMENTS);

    /* an axis without a sign turns the positive way */
    const char positive[] = { '+', str[0], '\0' };
    if (str[0] && !str[1])  str = positive;

    for (quarter_turn t = 0; t < QUARTER_TURN_NR; t++)
        if (!strcmp(quarter_turns[t].name, str))    return cres_val(quarter_turn, t);

    return cres_error(quarter_turn, CERR_NOT_FOUND);
}

const char *quarter_turn_name(quarter_turn turn)
{
    if ((unsigned int)turn >= QUARTER_TURN_NR)  return "<invalid>";

    return quarter_turns[turn].name;
}

/* The turns come in pairs around each axis, the positive one first */
quarter_turn quarter_turn_reversed(quarter_turn turn)
{
    return turn ^ 1;
}

/**
 * enum rotatable_state - what a rotatable is doing
 * @ROTATABLE_REST:         standing still, ready to turn
 * @ROTATABLE_TURNING:      turning a quarter, see rotatable_turn()
 * @ROTATABLE_TURNING_BACK: going back to where the turn started, after it ran
 *                          into something
 * @ROTATABLE_NR:           number of states
 */
typedef enum rotatable_state {
    ROTATABLE_REST = 0,
    ROTATABLE_TURNING,
    ROTATABLE_TURNING_BACK,
    ROTATABLE_NR,
} rotatable_state;

/**
 * struct rotatable - a suspended platform that turns a quarter at a time
 * @sm:             what it is doing, &enum rotatable_state
 * @e:              its entity, a static collision mesh
 * @orig_update:    @e's update before rotatable_new() took it over
 * @camera:         the camera the turns shake, the scene's as of the frame,
 *                  like @dt; nullptr until the first
 * @from:           @e's rotation where the turn under way started
 * @initial:        @e's rotation as the scene had it: rotatable_reset()'s
 * @turn:           the turn under way
 * @next_turn:      the turn rotatable_turn() asks for, which a turn that
 *                  starts takes on
 * @elapsed:        seconds into the turn under way; turning back, the seconds
 *                  of it left to undo
 * @dt:             seconds the frame takes, for the turn's tick
 */
struct rotatable {
    struct gpsm     sm;
    entity3d        *e;
    int             (*orig_update)(entity3d *e, void *data);
    struct camera   *camera;
    quat            from;
    quat            initial;
    quarter_turn    turn;
    quarter_turn    next_turn;
    float           elapsed;
    float           dt;
};

DEFINE_CLEANUP(rotatable, if (*p) rotatable_done(*p))

static void rotatable_shake(rotatable *r, const camera_shake_options *opts)
{
    if (!r->camera) return;

    CERR_RET(_camera_shake_push(r->camera, opts),
             err_cerr(__cerr, "%s: couldn't shake the camera\n", entity_name(r->e)));
}

/* A turn or a turn back comes to rest: the platform locks into place */
static gpsm_state_t rotatable_rest_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    if (from != GPSM_NO_STATE)  rotatable_shake(priv, &rotatable_settle_shake);

    return ROTATABLE_REST;
}

static gpsm_state_t rotatable_turning_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    rotatable *r = priv;

    r->turn = r->next_turn;
    r->elapsed = 0.0f;
    /* every turn starts from a unit quaternion, so the turns' rounding errors don't add up */
    quat_norm(r->from, transform_rotation_quat(&r->e->xform));

    return ROTATABLE_TURNING;
}

/* Turn the entity to where the turn under way is @r->elapsed seconds in */
static void rotatable_pose(rotatable *r)
{
    quat step, rot;
    vec3 axis;

    /* the turn's angle eases in and out: the motors spin up and wind down */
    vec3_dup(axis, quarter_turns[r->turn].axis);
    quat_from_axis_angle(step, axis, quarter_turns[r->turn].angle * smoothf(r->elapsed / ROTATABLE_TURN_TIME));

    /* around the world axis: the step goes on top of the rotation it started from */
    quat_mul(rot, step, r->from);
    transform_set_quat(&r->e->xform, rot);
}

/*
 * What's glued to the rotatable turns with it, so it's never in the way: the
 * first thing that isn't glued to it turns it back
 */
static bool rotatable_pen(entity3d *e, void *data)
{
    rotatable *r = data;

    if (e->parent == r->e)  return true;

    dbg("%s: runs into %s, turns back\n", entity_name(r->e), entity_name(e));
    gpsm_set(&r->sm, ROTATABLE_TURNING_BACK);

    return false;
}

static void rotatable_turning_tick(struct gpsm *sm, void *priv)
{
    rotatable *r = priv;
    struct phys_body *body = r->e->phys_body;

    /* the collider is where the last tick left it: if that's into something, go back from there */
    if (body)
        phys_body_for_each_pen(body, .fn = rotatable_pen, .data = r, .depth = ROTATABLE_BLOCK_DEPTH);

    if (gpsm_get(&r->sm) != ROTATABLE_TURNING)  return;

    /* the collider has taken the turn's last pose, and that's clear: the turn is done */
    if (r->elapsed >= ROTATABLE_TURN_TIME) {
        gpsm_set(&r->sm, ROTATABLE_REST);
        return;
    }

    r->elapsed = fminf(r->elapsed + r->dt, ROTATABLE_TURN_TIME);
    rotatable_pose(r);
}

/* The turn has run into something */
static gpsm_state_t rotatable_turning_back_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    rotatable_shake(priv, &rotatable_clunk_shake);

    return ROTATABLE_TURNING_BACK;
}

static void rotatable_turning_back_tick(struct gpsm *sm, void *priv)
{
    rotatable *r = priv;

    /* back the way it came, at the pace it came */
    r->elapsed = fmaxf(r->elapsed - r->dt, 0.0f);
    rotatable_pose(r);

    if (r->elapsed <= 0.0f) gpsm_set(&r->sm, ROTATABLE_REST);
}

static const struct gpsm_state rotatable_states[ROTATABLE_NR] = {
    [ROTATABLE_REST] = {
        .name       = "rest",
        .valid_from = GPSM_FROM(ROTATABLE_TURNING, ROTATABLE_TURNING_BACK),
        .enter      = rotatable_rest_enter,
    },
    [ROTATABLE_TURNING] = {
        .name       = "turning",
        .valid_from = GPSM_FROM(ROTATABLE_REST),
        .enter      = rotatable_turning_enter,
        .tick       = rotatable_turning_tick,
    },
    [ROTATABLE_TURNING_BACK] = {
        .name       = "turning back",
        .valid_from = GPSM_FROM(ROTATABLE_TURNING),
        .enter      = rotatable_turning_back_enter,
        .tick       = rotatable_turning_back_tick,
    },
};

/* The rotatable's turn goes before its entity's own update, which takes the new rotation to the collider */
static int rotatable_update(entity3d *e, void *data)
{
    rotatable *r = e->connect_priv;
    struct scene *scene = data;

    r->dt = clap_get_fps_delta(scene->clap_ctx).tv_nsec / (double)NSEC_PER_SEC;
    r->camera = scene->camera;
    gpsm_tick(&r->sm);

    return r->orig_update(e, data);
}

cresp(rotatable) rotatable_new(entity3d *e)
{
    if (!e)     return cresp_error(rotatable, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(rotatable, r) = mem_alloc(sizeof(*r), .zero = 1);
    if (!r)     return cresp_error(rotatable, CERR_NOMEM);

    /* the entity is the rotatable's from here on: rotatable_done() gives it back, on an error too */
    r->e = e;
    r->orig_update = e->update;
    e->update = rotatable_update;
    e->connect_priv = r;

    quat_norm(r->initial, transform_rotation_quat(&e->xform));
    r->sm.name = entity_name(e);
    CERR_RET_T(gpsm_init(&r->sm, rotatable_states, ROTATABLE_NR, r, ROTATABLE_REST), rotatable);

    return cresp_val(rotatable, NOCU(r));
}

void rotatable_done(rotatable *r)
{
    r->e->update = r->orig_update;
    r->e->connect_priv = nullptr;
    mem_free(r);
}

entity3d *rotatable_entity(rotatable *r)
{
    return r->e;
}

/* A rotatable's entity runs rotatable_update(), which its connect_priv leads back to the rotatable */
cresp(rotatable) rotatable_of(entity3d *e)
{
    for (; e; e = e->parent)
        if (e->update == rotatable_update)  return cresp_val(rotatable, e->connect_priv);

    return cresp_error(rotatable, CERR_NOT_FOUND);
}

void rotatable_reset(rotatable *r)
{
    /* rest is where a turn and a turn back both end: it takes either */
    if (gpsm_get(&r->sm) != ROTATABLE_REST)
        CERR_RET(gpsm_set(&r->sm, ROTATABLE_REST), err_cerr(__cerr, "%s: won't come to rest\n", entity_name(r->e)));

    transform_set_quat(&r->e->xform, r->initial);
    dbg("%s: back the way the scene had it\n", entity_name(r->e));
}

cerr rotatable_turn(rotatable *r, quarter_turn turn)
{
    if (turn >= QUARTER_TURN_NR)    return CERR_INVALID_ARGUMENTS;

    r->next_turn = turn;

    return gpsm_set(&r->sm, ROTATABLE_TURNING);
}
