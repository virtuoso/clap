/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CHARACTER_H__
#define __CLAP_CHARACTER_H__

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
    CS_FALLING,
} character_state;

#define POS_HISTORY_MAX 8

struct character {
    struct ref  ref;
    entity3d *entity;
    int (*orig_update)(entity3d *, void *);
    struct camera *camera;
    struct timespec dash_started;
    vec3    motion;
    vec3    velocity;
    vec3    normal;
    float   speed;
    float   jump_forward;
    float   jump_upward;
    float   lin_speed;
    struct list entry;
    entity3d *collision;
    entity3d *old_collision;
    struct mq *mq;
    struct {
        vec3            pos[POS_HISTORY_MAX];
        unsigned int    head;
        bool            wrapped;
    } history;
    bool    jump;
    bool    airborne;
    bool    can_dash;
    bool    can_jump;
    enum character_state state;
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

void character_set_moved(struct character *c);
void character_handle_input(struct character *ch, struct scene *s, struct message *m);
void character_move(struct character *ch, struct scene *s);
void character_stop(struct character *c, struct scene *s);

#endif /* __CLAP_CHARACTER_H__ */
