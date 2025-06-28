// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "character.h"
#include "ui-debug.h"

/*
 * Apply pitch_delta, while keeping the total pitch within [-90, 90].
 * The price of dealing in quaternions is that one can't simply clamp
 * the pitch to a range, some trickery is required.
 */
static void camera_apply_pitch(struct camera *c, float delta)
{
    if (delta == 0.0)
        return;

    transform_t xform;
    transform_clone(&xform, &c->xform);
    transform_rotate_axis(&xform, (vec3){ 1.0, 0.0, 0.0 }, delta, true);

    vec3 up = { 0.0, 1.0, 0.0 };
    transform_rotate_vec3(&xform, up);
    if (up[1] < 0.0)
        return;

    transform_clone(&c->xform, &xform);
}

void camera_move(struct camera *c, unsigned long fps)
{
    /* XXX: clap_ctx->ts_delta */
    if (!fps)
        return;

    camera_apply_pitch(c, -c->pitch_delta / (float)fps);

    transform_rotate_axis(&c->xform, (vec3){ 0.0, 1.0, 0.0 }, -c->yaw_delta / (float)fps, true);
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
    float w = c->view.main.near_plane;
    float h = c->view.main.near_plane / c->view.aspect;
    vec4 r = { 0.0, 0.0, 0.0, 1.0 };

    transform_t xform;
    transform_clone(&xform, &c->xform);
    transform_orbit(&xform, c->target, dist);
    transform_view_mat4x4(&xform, m);
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
    if (likely(!clap_get_render_options(scene->clap_ctx)->debug_draws_enabled))
        goto debug_ui;

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
        message_send(scene->clap_ctx, &dm);
    }

debug_ui:
    debug_module *dbgm = ui_igBegin_name(DEBUG_CAMERA, ImGuiWindowFlags_AlwaysAutoResize, "camera");

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        const float *pos = transform_pos(&c->xform, NULL);
        vec3 angles;
        transform_rotation(&c->xform, angles, true);
        ui_igVecTableHeader("camera", 3);
        ui_igVecRow(pos, 3, "pos");
        ui_igVecRow(angles, 3, "angles");
        igEndTable();

        ui_igVecTableHeader("rotation", 4);
        ui_igVecRow(transform_rotation_quat(&c->xform), 4, "quat");
        igEndTable();
    }

    ui_igEnd(DEBUG_CAMERA);
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

    if (!scene->control) {
        /*
         * camera not bound to a target, no further calculation are necessary,
         * reset the motion controller and drop out early
         */
        camera_reset_movement(c);
        return;
    }

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

    // const float *rot = transform_rotation_quat(&c->xform);
    // igText("camera quat: %f %f %f %f", rot[0], rot[1], rot[2], rot[3]);

    c->dist = dist;
    transform_orbit(&c->xform, c->target, c->dist);

out:
    debug_draw_camera(scene, c);
}

bool camera_has_moved(struct camera *c)
{
    return (c->yaw_delta || c->pitch_delta || transform_is_updated(&c->xform));
}
