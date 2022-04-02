// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include "model.h"
#include "scene.h"
#include "terrain.h"
#include "character.h"
#include "messagebus.h"
#include "ui.h"
#include "ui-debug.h"

static void motion_parse_input(struct motionctl *mctl, struct message *m)
{
    if (m->input.trigger_r)
        mctl->lin_speed *= (m->input.trigger_r + 1) * 3;
    else if (m->input.pad_rt)
        mctl->lin_speed *= 3;

    /* left stick right/left/up/down */
    if (m->input.right == 1)
        mctl->ls_right = mctl->lin_speed;
    else if (m->input.right == 2)
        mctl->ls_right = 0;

    if (m->input.left == 1)
        mctl->ls_left = mctl->lin_speed;
    else if (m->input.left == 2)
        mctl->ls_left = 0;

    if (m->input.delta_lx)
        mctl->ls_dx = mctl->lin_speed * m->input.delta_lx;

    if (m->input.up == 1)
        mctl->ls_up = mctl->lin_speed;
    else if (m->input.up == 2)
        mctl->ls_up = 0;

    if (m->input.down == 1)
        mctl->ls_down = mctl->lin_speed;
    else if (m->input.down == 2)
        mctl->ls_down = 0;

    if (m->input.delta_ly)
        mctl->ls_dy = mctl->lin_speed * m->input.delta_ly;

    /* right strick */
    if (m->input.pitch_up == 1)
        mctl->rs_up = mctl->ang_speed;
    else if (m->input.pitch_up == 2)
        mctl->rs_up = 0;

    if (m->input.pitch_down == 1)
        mctl->rs_down = mctl->ang_speed;
    else if (m->input.pitch_down == 2)
        mctl->rs_down = 0;

    if (m->input.delta_ry)
        mctl->rs_dy = mctl->ang_speed * m->input.delta_ry;

    if (m->input.yaw_right == 1)
        mctl->rs_right = mctl->ang_speed;
    else if (m->input.yaw_right == 2)
        mctl->rs_right = 0;

    if (m->input.yaw_left == 1)
        mctl->rs_left = mctl->ang_speed;
    else if (m->input.yaw_left == 2)
        mctl->rs_left = 0;

    if (m->input.delta_rx)
        mctl->rs_dx = mctl->ang_speed * m->input.delta_rx;
}

static void motion_compute_ls(struct motionctl *mctl)
{
    if (mctl->ls_left || mctl->ls_right)
        mctl->ls_dx = mctl->ls_right - mctl->ls_left;
    if (mctl->ls_up || mctl->ls_down)
        mctl->ls_dy = mctl->ls_down - mctl->ls_up;
}

static void motion_compute_rs(struct motionctl *mctl)
{
    if (mctl->rs_left || mctl->rs_right)
        mctl->rs_dx = mctl->rs_right - mctl->rs_left;
    if (mctl->rs_up || mctl->rs_down)
        mctl->rs_dy = mctl->rs_down - mctl->rs_up;
}

static void motion_reset(struct motionctl *mctl, struct scene *s)
{
    mctl->lin_speed = s->lin_speed;
    mctl->ang_speed = s->ang_speed;
    mctl->rs_dx = mctl->rs_dy = mctl->ls_dx = mctl->ls_dy = 0;
}

void character_handle_input(struct character *ch, struct scene *s, struct message *m)
{
    motion_parse_input(&ch->mctl, m);

    /*
     * Only allow motion control when on the ground OR disembodied camera
     */
    if (character_is_grounded(s->control, s) || scene_character_is_camera(s, s->control)) {
        if (s->control->entity && (m->input.space || m->input.pad_x)) {
            struct phys_body *body = s->control->entity->phys_body;
            motion_compute_ls(&ch->mctl);
            float delta_x = ch->mctl.ls_dx;
            float delta_z = ch->mctl.ls_dy;            
            float yawcos = cos(to_radians(s->camera->yaw));
            float yawsin = sin(to_radians(s->camera->yaw));
            float dx = delta_x * yawcos - delta_z * yawsin;
            float dz = delta_x * yawsin + delta_z * yawcos;
            vec3 jump = { dx * 1.3, 5.0, dz * 1.3 };

            if (body && phys_body_has_body(body)) {
                dbg("jump: %f,%f,%f\n", jump[0], jump[1], jump[2]);
                dJointAttach(body->lmotor, NULL, NULL);
                dBodySetLinearVel(body->body, jump[0], jump[1], jump[2]);
            }
        }
    }

    s->camera->zoom = !!(m->input.zoom);
    motion_compute_rs(&ch->mctl);
    if (ch->mctl.rs_dy) {
        float delta = ch->mctl.rs_dy;

        if (s->control == s->camera->ch && m->input.trigger_l)
            s->camera->ch->motion[1] -= delta / s->ang_speed * s->lin_speed;
        else
            s->camera->pitch_turn = delta;
    }
    if (ch->mctl.rs_dx)
        s->camera->yaw_turn = ch->mctl.rs_dx;
    s->camera->ch->moved++;
}

bool character_is_grounded(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;

    if (body)
        return phys_body_is_grounded(body);
    return ch->pos[1] > terrain_height(s->terrain, ch->pos[0], ch->pos[2]);
}

void character_move(struct character *ch, struct scene *s)
{
    struct phys_body *body = ch->entity->phys_body;
    struct character *cam = s->camera->ch;
    float height;

    /*
     * ch->motion: motion resulting from inputs
     */
    motion_compute_ls(&ch->mctl);

    /*
     * 1. Ray cast downwards
     * 2. Find normal with terrain
     * 3. Recalculate ch->motion in that space
     * https://stackoverflow.com/questions/1023948/rotate-normal-vector-onto-axis-plane
     */
    //dRFromZAxis();

    if (s->control == ch) {
        float delta_x = ch->mctl.ls_dx;
        float delta_z = ch->mctl.ls_dy;
        
        float yawcos = cos(to_radians(s->camera->yaw));
        float yawsin = sin(to_radians(s->camera->yaw));
        ch->motion[0] = delta_x * yawcos - delta_z * yawsin;
        if (!scene_character_is_camera(s, ch))
            ch->motion[1] = 0.0;
        ch->motion[2] = delta_x * yawsin + delta_z * yawcos;
    }

    motion_reset(&ch->mctl, s);
    if (vec3_len(ch->motion)) {
        dVector3 newy = { ch->normal[0], ch->normal[1], ch->normal[2] };
        dVector3 oldx = { 1, 0, 0 };
        dVector3 newx, newz, res;
        dMatrix3 R;

        if (!vec3_len(ch->normal) && ch != cam) {
            err("no normal vector: is there collision?\n");
            return;
        }
        if (ch->angle[2] < 0)
            dCalcVectorCross3(newz, newy, oldx);
        else
            dCalcVectorCross3(newz, oldx, newy);

        if ((ch->angle[0] < 0) != (ch->angle[2] < 0))
            dCalcVectorCross3(newx, newz, newy);
        else
            dCalcVectorCross3(newx, newy, newz);

        dSafeNormalize3(newx);
        dSafeNormalize3(newy);
        dSafeNormalize3(newz);

        /* XXX: the numerator has to do with movement speed */
        vec3_scale(ch->angle, ch->motion, /*(float)gl_refresh_rate()*/60. / (float)s->fps.fps_fine);

        /* watch out for Y and Z swapping places */
        dScaleVector3(newx, ch->angle[0]);
        dScaleVector3(newz, ch->angle[2]);
        dAddScaledVectors3(res, newx, newz, ch->angle[0], ch->angle[2]);

        // if (scene_camera_follows(s, ch)) {
        //     ui_debug_printf("[ %f, %f, %f ] (%f) -> [ %f, %f, %f ] (%f)",
        //                     ch->angle[0], ch->angle[1], ch->angle[2], atan2(ch->angle[0], ch->angle[2]),
        //                     res[0], res[1], res[2], atan2(res[0], res[2]));
        // }

        if (body) {
            dJointSetLMotorParam(body->lmotor, dParamVel1, res[0]);
            dJointSetLMotorParam(body->lmotor, dParamVel2, res[1]);
            dJointSetLMotorParam(body->lmotor, dParamVel3, res[2]);
            // dJointSetLMotorParam(body->lmotor, dParamVel1, ch->angle[0]);
            // dJointSetLMotorParam(body->lmotor, dParamVel2, ch->angle[1]);
            // dJointSetLMotorParam(body->lmotor, dParamVel3, ch->angle[2]);
        } else {
            vec3_add(ch->pos, ch->pos, ch->angle);
            ch->entity->dx = ch->pos[0];
            ch->entity->dz = ch->pos[2];
        }

        vec3_norm(ch->angle, ch->angle);

        if (body && body->body) {
            // To determine the orientation of the body,
            // we calculate an average of our motion requested by input and actual velocity.
            const dReal *vel;
            vel = dBodyGetLinearVel(body->body);

            // Only change orientation if the velocity is non-zero
            // (to avoid flickering when the body is stopped).
            if (vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2] > 0.01)
            {
                float velocity_vs_direction_coefficient = 0.2; // direction from input is more important.
                vec3 velocity = { vel[0], vel[1], vel[2] };
                vec3_norm(velocity, velocity);
                vec3_scale(velocity, velocity, velocity_vs_direction_coefficient);

                // ch->angle is already normalized, so we can just add those two.
                vec3_add(velocity, velocity, ch->angle);
                ch->entity->ry = atan2f(velocity[0], velocity[2]);
            }
        } else {
            ch->entity->ry = atan2f(ch->angle[0], ch->angle[2]);
        }

        // ch->entity->rz = atan2f(ch->angle[1], res[1]);
        if (body) {
            dRFromEulerAngles(R, ch->entity->rx, ch->entity->ry, ch->entity->rz);
            // dRFrom2Axes(R, 0, 1, 0, newy[0], newy[1], newy[2]);
            dBodySetRotation(body->body, R);
            // ch->entity->mx->cell[0] = R[0];
            // ch->entity->mx->cell[1] = R[1];
            // ch->entity->mx->cell[2] = R[2];
            // ch->entity->mx->cell[3] = 0;
            // ch->entity->mx->cell[4] = R[4];
            // ch->entity->mx->cell[5] = R[5];
            // ch->entity->mx->cell[6] = R[6];
            // ch->entity->mx->cell[7] = 0;
            // ch->entity->mx->cell[8] = R[8];
            // ch->entity->mx->cell[9] = R[9];
            // ch->entity->mx->cell[10] = R[10];
            // ch->entity->mx->cell[11] = 0;
            // ch->entity->mx->cell[12] = ch->pos[0];
            // ch->entity->mx->cell[13] = ch->pos[1];
            // ch->entity->mx->cell[14] = ch->pos[2];
            // ch->entity->mx->cell[15] = 1;
            // mat4x4_scale_aniso(ch->entity->mx->m, ch->entity->mx->m, ch->entity->scale, ch->entity->scale, ch->entity->scale);

        }
        ch->moved++;
        animation_start_by_name(ch->entity, "boink skate");
    } else if (body) {
        // vec3_scale(ch->angle, ch->angle, 0.5);
        ch->angle[0] = 0;
        ch->angle[1] = 0;
        ch->angle[2] = 0;
        dJointSetLMotorParam(body->lmotor, dParamVel1, ch->angle[0]);
        dJointSetLMotorParam(body->lmotor, dParamVel2, ch->angle[1]);
        dJointSetLMotorParam(body->lmotor, dParamVel3, ch->angle[2]);
        animation_start_by_name(ch->entity, "boink shrugged");
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
}

DECLARE_REFCLASS(character);

static void character_camera_update(struct character *c)
{
    vec3 start = { c->pos[0], c->pos[1], c->pos[2] };
    double dist, maxdist, height;
    struct entity3d *hit;
    vec3 dir;

    if (!c->camera)
        return;

    height = entity3d_aabb_Y(c->entity) * 3 / 4;
    dist = height * 3;
    maxdist = max(c->camera->dist + 1, dist);
    start[1] += height;
retry:
    dir[0] = sin(-to_radians(c->camera->yaw)) * cos(to_radians(c->camera->pitch));
    dir[1] = sin(to_radians(c->camera->pitch));
    dir[2] = cos(-to_radians(c->camera->yaw)) * cos(to_radians(c->camera->pitch));

    hit = phys_ray_cast(c->entity, start, dir, &dist);
    if (!hit) {
        dist = height * 3;
    } else if (dist < 1 && c->camera->pitch < 90) {
        c->camera->pitch = min(c->camera->pitch + 5, 90);
        goto retry;
    }

    ui_debug_printf("hit: '%s' c->dist: %f dist: %f maxdist: %f",
                    entity_name(hit), c->camera->dist, dist, maxdist);
    c->camera->dist = clampf(dist - 0.1, 1, maxdist);
}

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

    character_camera_update(c);

    if (e->phys_body) {
        if (!phys_body_is_grounded(e->phys_body)) {
            // dReal *rot = phys_body_rotation(e->phys_body);
            dMatrix3 rot;

            dRFromEulerAngles(rot, e->rx, e->ry, e->rz);
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
            // dMatrix3 rot;
            // vec3 n;

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

    // return 0;
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
    motion_reset(&c->mctl, s);

    return c;
}
