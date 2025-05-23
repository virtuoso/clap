// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "character.h"
#include "ui-debug.h"

void camera_move(struct camera *c, unsigned long fps)
{
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

void camera_position(struct camera *c, float x, float y, float z)
{
    // Calculate position of the camera with respect to the character.
    entity3d_position(c->ch->entity, (vec3){
                      x + c->dist * sin(to_radians(-c->yaw)) * cos(to_radians(c->pitch)),
                      y + c->dist * sin(to_radians(c->pitch)),
                      z + c->dist * cos(to_radians(-c->yaw)) * cos(to_radians(c->pitch))});
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

void camera_set_target_to_current(struct camera *c)
{
    vec3_dup(c->ch->motion, (vec3){});
    vec3_dup(c->ch->angle, (vec3){});
    vec3_dup(c->ch->velocity, (vec3){});
}

bool test_if_ray_intersects_scene(entity3d *entity, vec3 start, vec3 end, double *scale, entity3d **hit)
{
    vec3 dir;
    double distance, distance_to_hit;

    vec3_sub(dir, end, start);
    distance = vec3_len(dir);
    distance_to_hit = distance;
    *hit = phys_ray_cast(entity, start, dir, &distance_to_hit);
    if (*hit) {
        *scale = distance_to_hit / distance;
        return true;
    }

    return false;
}

static void camera_calc_rays(struct camera *c, struct scene *s, vec3 start, float dist,
                             vec4 nw, vec4 ne, vec4 sw, vec4 se)
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
    c_position[0] = start[0] + dist * sin(to_radians(-c->yaw)) * cos(to_radians(c->pitch));
    c_position[1] = start[1] + dist * sin(to_radians(c->pitch));
    c_position[2] = start[2] + dist * cos(to_radians(-c->yaw)) * cos(to_radians(c->pitch));
    mat4x4_translate_in_place(m, -c_position[0], -c_position[1], -c_position[2]);
    mat4x4_invert(m_inverse, m);

    // test four corners.
    r[0] = w; r[1] = h;
    mat4x4_mul_vec4(nw, m_inverse, r);
    r[0] = -w; r[1] = h;
    mat4x4_mul_vec4(ne, m_inverse, r);
    r[0] = w; r[1] = -h;
    mat4x4_mul_vec4(sw, m_inverse, r);
    r[0] = -w; r[1] = -h;
    mat4x4_mul_vec4(se, m_inverse, r);
}

bool camera_position_is_good(struct camera *c, entity3d *entity,
                             vec3 start, float dist, struct scene *s, double *next_distance)
{
    double scale_nw = 0;
    double scale_ne = 0;
    double scale_sw = 0;
    double scale_se = 0;
    double min_scale;
    entity3d *e1, *e2, *e3, *e4;
    vec4 nw, ne, sw, se;

    camera_calc_rays(c, s, start, dist, nw, ne, sw, se);

    min_scale = 1.0;
    if (test_if_ray_intersects_scene(entity, start, nw, &scale_nw, &e1))
        min_scale = fmin(min_scale, scale_nw);
    if (test_if_ray_intersects_scene(entity, start, ne, &scale_ne, &e2))
        min_scale = fmin(min_scale, scale_ne);
    if (test_if_ray_intersects_scene(entity, start, sw, &scale_sw, &e3))
        min_scale = fmin(min_scale, scale_sw);
    if (test_if_ray_intersects_scene(entity, start, se, &scale_se, &e4))
        min_scale = fmin(min_scale, scale_se);

    if (min_scale < 0.99) {
        // ui_debug_printf("entity: %s\nstart %f,%f,%f\nmin_scale: %f\ndist: %f\nnw: %f %s\nne: %f %s\nsw: %f %s\nse: %f %s",
        //                 entity_name(entity),
        //                 start[0], start[1], start[2], min_scale, dist, scale_nw, entity_name(e1),
        //                 scale_ne, entity_name(e2), scale_sw, entity_name(e3), scale_se, entity_name(e4));
        *next_distance = dist * min_scale;
        return false;
    }

    return true;
}


bool debug_draw_camera(struct scene *scene, struct camera *c, vec3 start, float dist, struct scene *s)
{
    vec4 nw, ne, sw, se;

    camera_calc_rays(c, s, start, dist, nw, ne, sw, se);

    /* use common start vertex + 4 corner vertices */
    memcpy(c->tmp_debug_line_start, start, sizeof(float) * 3);
    memcpy(c->tmp_debug_line_end, nw, sizeof(float) * 3);
    memcpy(3 + c->tmp_debug_line_end, ne, sizeof(float) * 3);
    memcpy(6 + c->tmp_debug_line_end, sw, sizeof(float) * 3);
    memcpy(9 + c->tmp_debug_line_end, se, sizeof(float) * 3);

    for (int i = 0; i < NUMBER_OF_DEBUG_LINES; i++)
        debug_draw_line(scene, c->debug_line_start, c->debug_line_end + 3 * i, NULL);

    return true;
}

void camera_update(struct camera *c, struct scene *scene, entity3d *entity)
{
    double dist, height, next_distance;
    vec3 start;

    if (entity->priv)
        vec3_dup(start, entity->pos);
    else
        vec3_dup(start, entity->aabb_center);

    if (entity->priv)
        height = entity3d_aabb_Y(entity) * 3 / 4;
    else
        height = entity3d_aabb_Y(entity) / 2;

    float dist_cap = fmaxf(10.0, entity3d_aabb_avg_edge(entity));
    dist = fminf(height * 3, fminf(dist_cap, c->view.main.far_plane - 10.0));
    if (entity->priv)
        start[1] += height;

    // Searching for camera distance that is good enough.
    while (dist > 0.1) {
        if (camera_position_is_good(c, entity, start, dist, scene, &next_distance))
            break;
        dist = next_distance;
    }

    if (c->dist != dist)
        c->ch->moved++;

    c->dist = dist;
    debug_draw_camera(scene, c, start, dist, scene);
}

void debug_camera_action(struct camera *c) {
    memcpy(c->debug_line_start, c->tmp_debug_line_start, sizeof(float) * 3);
    memcpy(c->debug_line_end, c->tmp_debug_line_end, sizeof(float) * 3 * NUMBER_OF_DEBUG_LINES);
}

bool camera_has_moved(struct camera *c)
{
    return (c->yaw_delta || c->pitch_delta || c->ch->moved);
}
