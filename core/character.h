/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CHARACTER_H__
#define __CLAP_CHARACTER_H__

#include "gpsm.h"
#include "messagebus.h"
#include "model.h"
#include "physics.h"
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
    bool    jump;
    bool    jump_held;
    bool    airborne;
    bool    can_dash;
    bool    can_jump;
    bool    can_levitate;
};

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
 * nothing either; character_anims_playable() says so beforehand.
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
 * Context: a gpsm guard, to refuse a transition with nothing to play it
 */
bool character_anims_playable(struct character *ch, character_state from, character_state to);

void character_set_moved(struct character *c);
void character_handle_input(struct character *ch, struct scene *s, struct message *m);
void character_move(struct character *ch, struct scene *s);
void character_stop(struct character *c, struct scene *s);

#endif /* __CLAP_CHARACTER_H__ */
