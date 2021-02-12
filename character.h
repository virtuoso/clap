#ifndef __CLAP_CHARACTER_H__
#define __CLAP_CHARACTER_H__

#include "matrix.h"
#include "model.h"
#include "physics.h"

struct character {
    struct ref  ref;
    struct entity3d *entity;
    int (*orig_update)(struct entity3d *, void *);
    GLfloat pos[3];
    GLfloat pitch;  /* left/right */
    GLfloat yaw;    /* sideways */
    GLfloat roll;   /* up/down */
    vec3    motion;
    vec3    angle;
    float   yaw_turn;
    float   pitch_turn;
    struct list entry;
    int     moved;
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

#endif /* __CLAP_CHARACTER_H__ */
