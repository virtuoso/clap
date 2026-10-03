// SPDX-License-Identifier: Apache-2.0
/*
 * The aim point the aim family shares (lookat, turret, reach): a point in
 * the entity's model space that moves by a law. The law is data in a pose
 * class's cfg, the point is state in the entity's binding, so one
 * definition serves every entity of the model and one binding serves
 * every definition on the entity.
 */
#include <math.h>
#include <string.h>
#include "interp.h"
#include "model.h"
#include "util.h"
#include "procanim/target.h"

void target_start(target_t *t, const target_law *law)
{
    if (law->kind == TARGET_MOVE || law->kind == TARGET_HOLD)   vec3_dup(t->from, t->cur);
}

void target_hold(target_t *t)
{
    vec3_dup(t->from, t->cur);
    vec3_dup(t->point, t->cur);
}

void target_update(target_t *t, const target_law *law, struct entity3d *e, double tt, double dt,
                   vec3 out)
{
    switch (law->kind) {
        case TARGET_POINT:
        case TARGET_PLANT:
            vec3_dup(t->cur, t->point);
            break;
        case TARGET_FOLLOW: {
            vec3 goal;

            if (t->entity) {
                vec3 center;

                vec3_add(center, t->entity->aabb_center, t->offset);
                entity3d_world_to_model(e, center, goal);
            } else {
                vec3_dup(goal, t->point);
            }

            if (law->rate > 0.0)
                vec3_approach(t->cur, goal, (float)law->rate, (float)dt);
            else
                vec3_dup(t->cur, goal);
            break;
        }
        case TARGET_MOVE: {
            float phase = law->duration > 0.0 ? clampf((float)(tt / law->duration), 0.0f, 1.0f) : 1.0f;

            vec3_interp(t->cur, t->from, t->point, law->ease ? law->ease(phase) : phase);
            break;
        }
        case TARGET_HOLD:
            break;
    }

    vec3_dup(out, t->cur);
}

bool target_done(const target_law *law, double tt)
{
    return (law->kind == TARGET_MOVE || law->kind == TARGET_HOLD) && tt >= law->duration;
}

bool aim_angles(const quat rest, const vec3 forward, const vec3 up, const vec3 dir,
                double *yaw, double *pitch)
{
    vec3 f, u, r;

    *yaw = *pitch = 0.0;
    if (vec3_mul_inner(dir, dir) == 0.0f)   return false;

    quat_mul_vec3(f, (float *)rest, (float *)forward);
    quat_mul_vec3(u, (float *)rest, (float *)up);
    vec3_mul_cross(r, u, f);

    float df = vec3_mul_inner(dir, f), dr = vec3_mul_inner(dir, r), du = vec3_mul_inner(dir, u);

    *yaw = atan2(dr, df);
    *pitch = atan2(du, hypotf(df, dr));

    return true;
}

void aim_rotate(quat out, const quat rest, const vec3 forward, const vec3 up, double yaw,
                double pitch)
{
    vec3 f, u, r;
    quat qy, qp, q, posed;

    /* @out may alias @rest, and quat_mul() reads its operands while it writes */
    memcpy(posed, rest, sizeof(quat));
    quat_mul_vec3(f, posed, (float *)forward);
    quat_mul_vec3(u, posed, (float *)up);
    vec3_mul_cross(r, u, f);

    /* yaw around up turns forward toward right; pitch around the yawed right, up is negative */
    quat_from_axis_angle(qy, u, (float)yaw);
    quat_mul_vec3(r, qy, r);
    quat_from_axis_angle(qp, r, (float)-pitch);
    quat_mul(q, qp, qy);
    quat_mul(out, q, posed);
}
