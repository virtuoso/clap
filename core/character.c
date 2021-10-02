#include "common.h"
#include "model.h"
#include "scene.h"
#include "terrain.h"
#include "character.h"
#include "ui.h"

void character_handle_input(struct character *ch, struct scene *s)
{
}

bool character_is_grounded(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;

    if (body)
        return phys_body_is_grounded(body);
    return ch->pos[1] > terrain_height(s->terrain, ch->pos[0], ch->pos[2]);
}

void ui_debug_printf(const char *fmt, ...);

void character_move(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;
    struct character *cam = s->camera->ch;
    float height;

    /*
     * ch->motion: motion resulting from inputs
     */

    /*
     * 1. Ray cast downwards
     * 2. Find normal with terrain
     * 3. Recalculate ch->motion in that space
     * https://stackoverflow.com/questions/1023948/rotate-normal-vector-onto-axis-plane
     */
    //dRFromZAxis();
    if (vec3_len(ch->motion)) {
        // dVector3 newz = { ch->normal[0], ch->normal[1], ch->normal[2] };
        // dVector3 oldx = { 1, 0, 0 };
        // dVector3 newx, newy, res;
        dMatrix3 R;

        // if ((ch->angle[0] < 0) != (ch->angle[2] < 0)) {
        //     dCalcVectorCross3(newy, newz, oldx);
        //     dCalcVectorCross3(newx, newy, newz);
        // } else {
        //     dCalcVectorCross3(newy, oldx, newz);
        //     dCalcVectorCross3(newx, newz, newy);
        // }
        // dNormalize3(newx);
        // dNormalize3(newy);

        /* XXX: the numerator has to do with movement speed */
        vec3_scale(ch->angle, ch->motion, 60.f / (float)s->fps.fps_fine);

        /* watch out for Y and Z swapping places */
        // dScaleVector3(newx, ch->angle[0]);
        // dScaleVector3(newy, ch->angle[2]);
        // dAddScaledVectors3(res, newx, newy, ch->angle[0], ch->angle[2]);
        // if (scene_camera_follows(s, ch)) {
        //     ui_debug_printf("[ %f, %f, %f ] -> [ %f, %f, %f ]", ch->angle[0], ch->angle[1], ch->angle[2], res[0], res[1], res[2]);
        // }

        if (body) {
            //dBodySetLinearVel(body->body, ch->angle[0], ch->angle[1], ch->angle[2]);
            //dBodyAddForce(body->body, ch->angle[0], ch->angle[1], ch->angle[2]);
            // dJointSetLMotorParam(body->lmotor, dParamVel1, res[0]);
            // dJointSetLMotorParam(body->lmotor, dParamVel2, res[1]);
            // dJointSetLMotorParam(body->lmotor, dParamVel3, res[2]);
            dJointSetLMotorParam(body->lmotor, dParamVel1, ch->angle[0]);
            dJointSetLMotorParam(body->lmotor, dParamVel2, ch->angle[1]);
            dJointSetLMotorParam(body->lmotor, dParamVel3, ch->angle[2]);
        } else {
            vec3_add(ch->pos, ch->pos, ch->angle);
            ch->entity->dx = ch->pos[0];
            ch->entity->dz = ch->pos[2];
        }

        vec3_norm(ch->angle, ch->angle);
        ch->entity->ry = atan2f(ch->angle[0], ch->angle[2]);
        if (body) {
            dRFromEulerAngles(R, ch->entity->rx, ch->entity->ry, ch->entity->rz);
            dBodySetRotation(body->body, R);
        }
        ch->moved++;
    } else if (body) {
        // vec3_scale(ch->angle, ch->angle, 0.5);
        ch->angle[0] = 0;
        ch->angle[1] = 0;
        ch->angle[2] = 0;
        dJointSetLMotorParam(body->lmotor, dParamVel1, ch->angle[0]);
        dJointSetLMotorParam(body->lmotor, dParamVel2, ch->angle[1]);
        dJointSetLMotorParam(body->lmotor, dParamVel3, ch->angle[2]);
    }

    if (body)
        dBodyEnable(body->body);

    height = terrain_height(s->terrain, ch->pos[0], ch->pos[2]);
    if (fabs(height - ch->pos[1]) > 0.001) {
        if (ch->pos[1] < height) {
            if (ch->entity->phys_body) {
                dbg_once("character '%s' NOT correcting height by %f\n", character_name(ch), height - ch->pos[1]);
            } else {
                ch->pos[1] = height;
                ch->moved++;
            }
        } else {
            if (!ch->entity->phys_body && ch != cam) {
                ch->pos[1] = height;
                ch->moved++;
            }
        }

    }

    ch->ragdoll = body ? !phys_body_ground_collide(body) : 0;
    ch->entity->dy = ch->pos[1];

    ch->motion[0] = 0;
    ch->motion[1] = 0;
    ch->motion[2] = 0;
}

static void character_drop(struct ref *ref)
{
    struct character *c = container_of(ref, struct character, ref);

    ref_put_last_ref(&c->entity->ref);
    free(c);
}

DECLARE_REFCLASS(character);

/* data is struct scene */
static int character_update(struct entity3d *e, void *data)
{
    struct character *c = e->priv;
    struct scene     *s = data;
    int              ret;

    /* XXX "wow out" */
    if (e->dy <= s->limbo_height) {
        entity3d_position(e, e->dx, -e->dy, e->dz);
        c->pos[1] = -c->pos[1];
    }

    if (e->phys_body) {
        if (phys_body_update(e)) {
            c->moved++;
            if (scene_camera_follows(s, c))
                s->camera->ch->moved++;
        }
        c->pos[0] = e->dx;
        c->pos[1] = e->dy;
        c->pos[2] = e->dz;
    }

    if (e->phys_body) {
        if (!phys_body_is_grounded(e->phys_body)) {
            dReal *rot = phys_body_rotation(e->phys_body);
            e->mx->cell[0] = rot[0];
            e->mx->cell[1] = rot[1];
            e->mx->cell[2] = rot[2];
            e->mx->cell[3] = 0;
            e->mx->cell[4] = rot[4];
            e->mx->cell[5] = rot[5];
            e->mx->cell[6] = rot[6];
            e->mx->cell[7] = 0;
            e->mx->cell[8] = rot[8];
            e->mx->cell[9] = rot[9];
            e->mx->cell[10] = rot[10];
            e->mx->cell[11] = 0;
            e->mx->cell[12] = c->pos[0];
            e->mx->cell[13] = c->pos[1];
            e->mx->cell[14] = c->pos[2];
            e->mx->cell[15] = 1;
            mat4x4_scale_aniso(e->mx->m, e->mx->m, e->scale, e->scale, e->scale);
            c->ragdoll = 1;
        } else {
            dMatrix3 rot;
            vec3 n;

            c->ragdoll = 0;
            entity3d_position(e, c->pos[0], c->pos[1], c->pos[2]);
            // terrain_normal(s->terrain, c->pos[0], c->pos[2], n);
        }
    }

    if (e->phys_body)
        c->stuck = dJointGetBody(e->phys_body->lmotor, 0) ? 1 : 0;
    if (c->moved) {
        /* update body position */
        //entity3d_position(e, e->dx, e->dy, e->dz);
    }

    return c->ragdoll ? 0 : c->orig_update(e, data);
}

struct character *character_new(struct model3dtx *txm, struct scene *s)
{
    struct character *c;

    CHECK(c = ref_new(character));
    CHECK(c->entity = entity3d_new(txm));
    c->entity->priv = c;
    c->orig_update = c->entity->update;
    c->entity->update = character_update;
    list_append(&s->characters, &c->entry);

    return c;
}
