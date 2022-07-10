// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "character.h"
#include "ui-debug.h"

void camera_setup(struct camera *c)
{
    // starting yaw values
    c->target_yaw = 180;
    c->current_yaw = 180;
}

void camera_move(struct camera *c, unsigned long fps)
{
    // Add delta and clamp pitch between -90 and 90.
    c->target_pitch += c->pitch_delta / (float)fps;
    c->target_pitch = clampf(c->target_pitch, -90, 90);

    // Add delta and make sure yaw is between -180 and 180.
    c->target_yaw += c->yaw_delta / (float)fps;
    if (c->target_yaw > 180)
        c->target_yaw -= 360;
    else if (c->target_yaw <= -180)
        c->target_yaw += 360;
}

void camera_position(struct camera *c, float x, float y, float z, GLfloat *pos)
{
    // Calculate position of the camera with respect to the character.
    pos[0] = x + c->dist * sin(to_radians(-c->current_yaw)) * cos(to_radians(c->current_pitch));
    pos[1] = y + c->dist * sin(to_radians(c->current_pitch));
    pos[2] = z + c->dist * cos(to_radians(-c->current_yaw)) * cos(to_radians(c->current_pitch));
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
    // Target pitch should never be too extreme.
    c->target_pitch = clampf(c->current_pitch, -60, 60);
    c->target_yaw = c->current_yaw;
}

bool test_if_ray_intersects_scene(struct entity3d *entity, vec3 start, vec3 end, double * scale) {
    struct entity3d *hit;
    vec3 dir;
    double distance, distance_to_hit;

    vec3_sub(dir, end, start);
    distance = vec3_len(dir);
    distance_to_hit = distance;
    hit = phys_ray_cast(entity, start, dir, &distance_to_hit);
    if (hit) {
        *scale = distance_to_hit / distance;
        return true;
    }

    return false;
}

bool camera_position_is_good(struct camera *c, struct entity3d *entity, vec3 start, float pitch, float yaw, float dist, struct scene *s, double *next_distance) {
    mat4x4 m;
    mat4x4 m_inverse;
    GLfloat c_position[3];
    double scale_nw;
    double scale_ne;
    double scale_sw;
    double scale_se;
    double min_scale;
    float w = s->near_plane;
    float h = s->near_plane / s->aspect;
    vec4 r = { 0.0, 0.0, 0.0, 1.0 };
    vec4 nw, ne, sw, se;

    mat4x4_identity(m);
    mat4x4_rotate_X(m, m, to_radians(pitch));
    mat4x4_rotate_Y(m, m, to_radians(yaw));
    c_position[0] = start[0] + dist * sin(to_radians(-yaw)) * cos(to_radians(pitch));
    c_position[1] = start[1] + dist * sin(to_radians(pitch));
    c_position[2] = start[2] + dist * cos(to_radians(-yaw)) * cos(to_radians(pitch));
    mat4x4_translate_in_place(m, -c_position[0], -c_position[1], -c_position[2]);
    mat4x4_invert(m_inverse, m);

    // test four corners.
    r[0] = w; r[1] = h;
    mat4x4_mul_vec4(nw, m_inverse, r);
    r[0] = w; r[1] = -h;
    mat4x4_mul_vec4(ne, m_inverse, r);
    r[0] = -w; r[1] = h;
    mat4x4_mul_vec4(sw, m_inverse, r);
    r[0] = -w; r[1] = -h;
    mat4x4_mul_vec4(se, m_inverse, r);

    min_scale = 1.0;
    if (test_if_ray_intersects_scene(entity, start, nw, &scale_nw))
        min_scale = fmin(min_scale, scale_nw);
    if (test_if_ray_intersects_scene(entity, start, ne, &scale_ne))
        min_scale = fmin(min_scale, scale_ne);
    if (test_if_ray_intersects_scene(entity, start, sw, &scale_sw))
        min_scale = fmin(min_scale, scale_sw);
    if (test_if_ray_intersects_scene(entity, start, se, &scale_se))
        min_scale = fmin(min_scale, scale_se);

    if (min_scale < 0.99) {
        *next_distance = dist * min_scale;
        return false;
    }

    return true;
}


bool debug_draw_camera(struct camera *c, vec3 start, float pitch, float yaw, float dist, struct scene *s) {
    mat4x4 m;
    GLfloat c_position[3];
    mat4x4 m_inverse;
    float w = s->near_plane;
    float h = s->near_plane / s->aspect;
    vec4 r = { 0.0, 0.0, 0.0, 1.0 };
    vec4 nw, ne, sw, se;

    mat4x4_identity(m);
    mat4x4_rotate_X(m, m, to_radians(pitch));
    mat4x4_rotate_Y(m, m, to_radians(yaw));
    c_position[0] = start[0] + dist * sin(to_radians(-yaw)) * cos(to_radians(pitch));
    c_position[1] = start[1] + dist * sin(to_radians(pitch));
    c_position[2] = start[2] + dist * cos(to_radians(-yaw)) * cos(to_radians(pitch));
    mat4x4_translate_in_place(m, -c_position[0], -c_position[1], -c_position[2]);
    mat4x4_invert(m_inverse, m);

    r[0] = w;
    r[1] = h;
    mat4x4_mul_vec4(nw, m_inverse, r);

    r[0] = w;
    r[1] = -h;
    mat4x4_mul_vec4(ne, m_inverse, r);

    r[0] = -w;
    r[1] = h;
    mat4x4_mul_vec4(sw, m_inverse, r);

    r[0] = -w;
    r[1] = -h;
    mat4x4_mul_vec4(se, m_inverse, r);

    memcpy(c->tmp_debug_line_start, start, sizeof(float) * 3);
    memcpy(c->tmp_debug_line_end, nw, sizeof(float) * 3);
    memcpy(3 + c->tmp_debug_line_start, start, sizeof(float) * 3);
    memcpy(3 + c->tmp_debug_line_end, ne, sizeof(float) * 3);
    memcpy(6 + c->tmp_debug_line_start, start, sizeof(float) * 3);
    memcpy(6 + c->tmp_debug_line_end, sw, sizeof(float) * 3);
    memcpy(9 + c->tmp_debug_line_start, start, sizeof(float) * 3);
    memcpy(9 + c->tmp_debug_line_end, se, sizeof(float) * 3);

    for (int i = 0; i < NUMBER_OF_DEBUG_LINES; i++) {
        debug_draw_line(c->debug_line_start + 3 * i, c->debug_line_end + 3 * i, NULL);
    }

    return true;
}

void camera_update(struct camera *c, struct scene *scene, struct entity3d *entity, vec3 start) {
    double dist, height, next_distance;
    struct entity3d *hit;
    vec3 dir;

    // We start with target pitch.
    c->current_pitch = c->target_pitch;
    c->current_yaw = c->target_yaw;

    height = entity3d_aabb_Y(entity) * 3 / 4;
    dist = height * 3;
    start[1] += height;

    // Searching for camera distance that is good enough.
    while (dist > 0.1) {
        if (camera_position_is_good(c, entity, start, c->current_pitch, c->current_yaw, dist, scene, &next_distance))
            break;
        dist = next_distance;
    }

    c->dist = dist;
    debug_draw_camera(c, start, c->current_pitch, c->current_yaw, dist, scene);
}

void debug_camera_action(struct camera *c) {
    memcpy(c->debug_line_start, c->tmp_debug_line_start, sizeof(float) * 3 * NUMBER_OF_DEBUG_LINES);
    memcpy(c->debug_line_end, c->tmp_debug_line_end, sizeof(float) * 3 * NUMBER_OF_DEBUG_LINES);
}

bool camera_has_moved(struct camera *c)
{
    return (c->yaw_delta || c->pitch_delta || c->ch->moved);
}
