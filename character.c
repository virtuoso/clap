#include "common.h"
#include "model.h"
#include "scene.h"
#include "terrain.h"
#include "character.h"

static void character_move(struct character *ch, struct scene *s)
{
    struct character *cam = s->camera.ch;
    float height;
    vec3 inc;

    if (vec3_len(ch->motion)) {
        vec3_scale(inc, ch->motion, 1.f / (float)s->fps.fps_fine);
        vec3_add(ch->pos, ch->pos, inc);
        ch->entity->dx = ch->pos[0];
        ch->entity->dz = ch->pos[2];

        vec3_norm(inc, inc);
        ch->entity->ry = atan2f(inc[0], inc[2]);
        ch->moved++;
    }

    height = terrain_height(s->terrain, ch->pos[0], ch->pos[2]);
    if (ch != cam && ch->pos[1] != height) {
        ch->pos[1] = height;
        ch->moved++;
    }
    ch->entity->dy = ch->pos[1];

    ch->motion[0] = 0;
    ch->motion[1] = 0;
    ch->motion[2] = 0;
}

static void character_drop(struct ref *ref)
{
    struct character *c = container_of(ref, struct character, ref);

    ref_put_last(&c->entity->ref);
    free(c);
}

/* data is struct scene */
static int character_update(struct entity3d *e, void *data)
{
    struct character *c = e->priv;
    struct scene     *s = data;
    int              ret;

    if (e->phys_body) {
        phys_body_update(e);
        c->pos[0] = e->dx;
        c->pos[1] = e->dy;
        c->pos[2] = e->dz;
    }
    character_move(c, s);
    if (e->phys_body)
        dBodySetPosition(e->phys_body->body, c->pos[0], c->pos[1], c->pos[2]);
    ret = c->orig_update(e, data);
    return ret;
}

struct character *character_new(struct model3dtx *txm, struct scene *s)
{
    struct character *c;

    CHECK(c           = ref_new(struct character, ref, character_drop));
    CHECK(c->entity   = entity3d_new(txm));
    c->entity->priv   = c;
    c->orig_update    = c->entity->update;
    c->entity->update = character_update;
    list_append(&s->characters, &c->entry);

    return c;
}
