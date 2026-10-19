/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CHARACTER_H__
#define __CLAP_CHARACTER_H__

#include "gpsm.h"
#include "messagebus.h"
#include "model.h"
#include "ik.h"
#include "physics.h"
#include "procanim/handclap.h"
#include "procanim/incline.h"
#include "procanim/lookat.h"
#include "procanim/nod.h"
#include "procanim/plant.h"
#include "procanim/trail.h"
#include "scene.h"

typedef enum character_state {
    CS_START = 0,
    CS_WAKING,
    CS_AWAKE,
    CS_IDLE = CS_AWAKE,
    CS_MOVING,
    CS_JUMP_START,
    CS_JUMPING,
    CS_ASCENDING,
    CS_LEVITATING,
    CS_FALLING,
    CS_NR,
} character_state;

/* the state machine's table, for its names (anim_lib_transitions_from_json()) */
extern const struct gpsm_state character_states[CS_NR];

#define POS_HISTORY_MAX 8

struct character {
    struct ref  ref;
    entity3d *entity;
    int (*orig_update)(entity3d *, void *);
    struct camera *camera;
    struct timespec dash_started;
    struct gpsm sm;
    struct list entry;
    entity3d *collision;
    entity3d *old_collision;
    struct mq *mq;
    struct scene *scene;
    vec3    motion;
    vec3    velocity;
    vec3    normal;
    float   speed;
    float   jump_forward;
    float   jump_upward;
    /* exponential decay rate (1/s) of horizontal velocity in a non-jump fall */
    float   fall_drag;
    float   lin_speed;
    struct {
        vec3            pos[POS_HISTORY_MAX];
        unsigned int    head;
        bool            wrapped;
    } history;
    /* the head's look-at binding, for a model with a head joint */
    lookat_t look;
    /* the skeleton's tilt with the ground, for a character that tilts_with_ground */
    incline_t incline;
    /* the hands clapped, once character_handclap() asks */
    handclap_t handclap;
    /* the feet planted on the ground under them, for a model with foot joints: the leg chains and the binding */
    ik_chain foot_chain[2];
    plant_t  plant;
    /* the tail dragged behind, for a model with a tail joint */
    trail_t  tail;
    /* the head bobbing as it goes, for a character that bobs_head */
    nod_t    nod;
    /* where the character stands and which way it faces in old_collision's model space, when its seq was ground_seq */
    vec3    ground_pos;
    vec3    ground_dir;
    uint16_t ground_seq;
    bool    jump;
    bool    jump_held;
    bool    airborne;
    bool    can_dash;
    bool    can_jump;
    bool    can_levitate;
    /* the ground turns the character with it, not just carries it: see character_ride() */
    bool    turns_with_ground;
    /* the ground tilts the skeleton with it: see character_incline_update() */
    bool    tilts_with_ground;
    /* the head bobs as the character goes: see character_nod_update() */
    bool    bobs_head;
    /* the planting is faded out: the character is in the air */
    bool     plant_off;
};

/*
 * The head's look-at for a model whose data doesn't define one; the
 * scene's "look" overrides fields of it
 */
extern const lookat_config character_look_cfg;

DEFINE_REFCLASS_INIT_OPTIONS(character,
    model3dtx       *txmodel;
    struct scene    *scene;
);
DECLARE_REFCLASS(character);

static inline entity3d *character_entity(struct character *c)
{
    return c->entity;
}

static inline const char *character_name(struct character *c)
{
    return entity_name(character_entity(c));
}

/**
 * entity3d_character() - get character from entity3d
 * @e:  entity3d object
 *
 * Context: anywhere
 * Return: character pointer or CERR_INVALID_OPERATION
 */
cresp(character) entity3d_character(entity3d *e);

/**
 * character_anims_enter() - play what a state transition calls for
 * @ch:     the character
 * @from:   the state being left
 * @to:     the state being entered
 *
 * Plays the model library's transition for @from -> @to on the character's
 * animator, with the end callback that drives the lead-in states. A
 * self-transition and the initial enter play nothing: the first because
 * restarting a looping idle is a visible pop, the second so the idle
 * fallback can phase the clip. A transition the library doesn't have plays
 * nothing either, character_anims_playable() says so beforehand, unless @to
 * is a state the character leaves on the end callback: that plays a zero
 * hold, so a body with no clips (the golem's) wakes and jumps too.
 * Context: a gpsm enter callback
 * Return: @to
 */
character_state character_anims_enter(struct character *ch, character_state from, character_state to);

/**
 * character_anims_playable() - does the model's library have this transition
 * @ch:     the character
 * @from:   the state being left
 * @to:     the state being entered
 *
 * A model with no table at all has nothing to refuse with: everything is
 * playable for it.
 * Context: a gpsm guard, to refuse a transition with nothing to play it
 */
bool character_anims_playable(struct character *ch, character_state from, character_state to);

void character_set_moved(struct character *c);
void character_handle_input(struct character *ch, struct scene *s, struct message *m);
void character_move(struct character *ch, struct scene *s);
void character_stop(struct character *c, struct scene *s);

/**
 * character_handclap() - clap the character's hands, once
 * @ch:     the character
 *
 * The hands the model maps ("armature": "hand_left", "hand_right") swing
 * together off what they hang from and back apart, over whatever the arms
 * are doing; it starts at the next update.
 * Return: CERR_OK; CERR_NOT_FOUND for a model without both hands,
 *         CERR_INVALID_OPERATION while a clap is under way, or the animator's
 *         errors
 */
cerr character_handclap(struct character *ch);

#endif /* __CLAP_CHARACTER_H__ */
