/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CHARACTER_H__
#define __CLAP_CHARACTER_H__

#include "anictl.h"
#include "matrix.h"
#include "messagebus.h"
#include "model.h"
#include "physics.h"
#include "scene.h"

enum character_state {
    CS_START = 0,
    CS_WAKING,
    CS_AWAKE
};

struct character {
    struct ref  ref;
    struct entity3d *entity;
    int (*orig_update)(struct entity3d *, void *);
    struct camera *camera;
    struct timespec dash_started;
    vec3    motion;
    vec3    angle;
    vec3    velocity;
    vec3    normal;
    float   speed;
    float   jump_forward;
    float   jump_upward;
    float   lin_speed;
    struct list entry;
    struct anictl anictl;
    struct entity3d *collision;
    int     moved;
    bool    jump;
    bool    airborne;
    bool    can_dash;
    bool    can_jump;
    /*
     * Right stick moves the camera on the Y axis if character is
     * camera if additional input is present, setting this to true
     */
    bool    rs_height;
    enum character_state state;
};

static inline struct entity3d *character_entity(struct character *c)
{
    return c->entity;
}

static inline const char *character_name(struct character *c)
{
    return entity_name(character_entity(c));
}

struct character *character_new(struct model3dtx *txm, struct scene *s);
void character_handle_input(struct character *ch, struct scene *s, struct message *m);
void character_move(struct character *ch, struct scene *s);

#endif /* __CLAP_CHARACTER_H__ */
