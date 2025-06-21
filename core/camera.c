// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "character.h"
#include "ui-debug.h"

void camera_move(struct camera *c, unsigned long fps)
{
    /* XXX: clap_ctx->ts_delta */
    if (!fps)
        return;

    // Add delta and clamp pitch between -90 and 90.
    c->pitch += c->pitch_delta / (float)fps;
    c->pitch = clampf(c->pitch, -90, 90);

    // Add delta and make sure yaw is between -180 and 180.
    c->yaw += c->yaw_delta / (float)fps;
    if (c->yaw > 180)
        c->yaw -= 360;
    else if (c->yaw <= -180)
        c->yaw += 360;
}

static void camera_position(struct camera *c)
{
    // Calculate position of the camera with respect to the character.
    transform_set_pos(
        &c->xform,
        (vec3) {
            c->target[0] + c->dist * sin(to_radians(-c->yaw)) * cos(to_radians(c->pitch)),
            c->target[1] + c->dist * sin(to_radians(c->pitch)),
            c->target[2] + c->dist * cos(to_radians(-c->yaw)) * cos(to_radians(c->pitch))
        }
    );
}

void camera_reset_movement(struct camera *c)
{
    c->pitch_delta = 0;
    c->yaw_delta = 0;
}

void camera_add_pitch(struct camera *c, float delta)
{
    c->pitch_delta = delta;
}

void camera_add_yaw(struct camera *c, float delta)
{
    c->yaw_delta = delta;
}

static bool test_if_ray_intersects_scene(entity3d *entity, vec3 start, vec3 end, double *scale)
{
    vec3 dir;
    double distance, distance_to_hit;

    vec3_sub(dir, end, start);
    distance = vec3_len(dir);
    distance_to_hit = distance;
    entity3d *hit = phys_ray_cast(entity, start, dir, &distance_to_hit);
    if (hit) {
        *scale = distance_to_hit / distance;
        return true;
    }

    return false;
}

static void camera_calc_rays(struct camera *c, float dist)
{
    mat4x4 m;
    mat4x4 m_inverse;
    float c_position[3];
    float w = c->view.main.near_plane;
    float h = c->view.main.near_plane / c->view.aspect;
    vec4 r = { 0.0, 0.0, 0.0, 1.0 };

    mat4x4_identity(m);
    mat4x4_rotate_X(m, m, to_radians(c->pitch));
    mat4x4_rotate_Y(m, m, to_radians(c->yaw));
    c_position[0] = c->target[0] + dist * sin(to_radians(-c->yaw)) * cos(to_radians(c->pitch));
    c_position[1] = c->target[1] + dist * sin(to_radians(c->pitch));
    c_position[2] = c->target[2] + dist * cos(to_radians(-c->yaw)) * cos(to_radians(c->pitch));
    mat4x4_translate_in_place(m, -c_position[0], -c_position[1], -c_position[2]);
    mat4x4_invert(m_inverse, m);

    // test four corners.
    r[0] = w; r[1] = h;
    mat4x4_mul_vec4(c->frustum_corner[0], m_inverse, r);
    r[0] = -w; r[1] = h;
    mat4x4_mul_vec4(c->frustum_corner[1], m_inverse, r);
    r[0] = w; r[1] = -h;
    mat4x4_mul_vec4(c->frustum_corner[2], m_inverse, r);
    r[0] = -w; r[1] = -h;
    mat4x4_mul_vec4(c->frustum_corner[3], m_inverse, r);
}

static bool camera_position_is_good(struct camera *c, entity3d *entity,
                                    float dist, double *next_distance)
{
    double scale = 0;
    double min_scale;

    camera_calc_rays(c, dist);

    min_scale = 1.0;
    if (test_if_ray_intersects_scene(entity, c->target, c->frustum_corner[0], &scale))
        min_scale = fmin(min_scale, scale);
    if (test_if_ray_intersects_scene(entity, c->target, c->frustum_corner[1], &scale))
        min_scale = fmin(min_scale, scale);
    if (test_if_ray_intersects_scene(entity, c->target, c->frustum_corner[2], &scale))
        min_scale = fmin(min_scale, scale);
    if (test_if_ray_intersects_scene(entity, c->target, c->frustum_corner[3], &scale))
        min_scale = fmin(min_scale, scale);

    if (min_scale < 0.99) {
        *next_distance = dist * min_scale;
        return false;
    }

    return true;
}

#ifndef CONFIG_FINAL
static void debug_draw_camera(struct scene *scene, struct camera *c)
{
    if (likely(!scene->render_options.debug_draws_enabled))
        return;

    struct message dm = {
        .type       = MT_DEBUG_DRAW,
        .debug_draw = {
            .shape      = DEBUG_DRAW_LINE,
            .color      = { 1.0, 0.0, 1.0, 1.0 },
            .thickness  = 4.0f,
        }
    };

    vec3_dup(dm.debug_draw.v0, c->debug_target);

    for (int i = 0; i < array_size(c->debug_corner); i++) {
        vec3_dup(dm.debug_draw.v1, c->debug_corner[i]);
        message_send(&dm);
    }
}

void debug_camera_action(struct camera *c)
{
    for (int i = 0; i < array_size(c->debug_corner); i++)
        vec3_dup(c->debug_corner[i], c->frustum_corner[i]);
    vec3_dup(c->debug_target, c->target);
}
#else
static inline void debug_draw_camera(struct scene *scene, struct camera *c) {}
#endif /* CONFIG_FINAL */

static void camera_target(struct camera *c, entity3d *entity)
{
    double height;

    if (entity->priv) {
        /* for characters, assume origin is between their feet */
        transform_pos(&entity->xform, c->target);
        /* look at three quarters their height (XXX: parameterize) */
        height = entity3d_aabb_Y(entity) * 3 / 4;
        c->target[1] += height;
    } else {
        /* otherwise, origin can't be trusted at all; look at dead center */
        vec3_dup(c->target, entity->aabb_center);
        height = entity3d_aabb_Y(entity) / 2;
    }

    float dist_cap = fmaxf(10.0, entity3d_aabb_avg_edge(entity));
    c->dist = fminf(height * 3, fminf(dist_cap, c->view.main.far_plane - 10.0));
}

void camera_update(struct camera *c, struct scene *scene)
{
    double dist, next_distance;

    if (!scene->control)
        return;

    /* circle the entity s->control, unless it's camera */
    if (!camera_has_moved(c))
        goto out;

    camera_target(c, scene->control);
    dist = c->dist;

    camera_reset_movement(c);
    transform_clear_updated(&c->xform);

    // Searching for camera distance that is good enough.
    while (dist > 0.1) {
        if (camera_position_is_good(c, scene->control, dist, &next_distance))
            break;
        dist = next_distance;
    }

    if (c->dist != dist)
        transform_set_updated(&c->xform);

    c->dist = dist;
    camera_position(c);

out:
    debug_draw_camera(scene, c);
}

bool camera_has_moved(struct camera *c)
{
    return (c->yaw_delta || c->pitch_delta || transform_is_updated(&c->xform));
}
