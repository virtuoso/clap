// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "character.h"
#include "ui-debug.h"

void camera_setup(struct camera *c)
{
    c->target_yaw = 180;
    c->current_yaw = 180;
}

void camera_move_target(struct camera *c, unsigned long fps)
{
    c->target_pitch += c->pitch_delta / (float)fps;
    c->target_pitch = clampf(c->target_pitch, -90, 90);
    c->target_yaw += c->yaw_delta / (float)fps;
    if (c->target_yaw > 180)
        c->target_yaw -= 360;
    else if (c->target_yaw <= -180)
        c->target_yaw += 360;
}

void camera_move_current(struct camera *c, unsigned long fps)
{
    c->current_pitch += c->pitch_delta / (float)fps;
    c->current_pitch = clampf(c->current_pitch, -90, 90);
    c->current_yaw += c->yaw_delta / (float)fps;
    if (c->current_yaw > 180)
        c->current_yaw -= 360;
    else if (c->current_yaw <= -180)
        c->current_yaw += 360;
}

void camera_move(struct camera *c, unsigned long fps)
{
    camera_move_target(c, fps);
}

void camera_position(struct camera *c, float x, float y, float z, GLfloat *pos)
{
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
    c->target_pitch = clampf(c->current_pitch, -60, 60);
    c->target_yaw = c->current_yaw;
}

void camera_update(struct camera *c, struct entity3d *entity, vec3 start)
{
    double dist, maxdist, height;
    struct entity3d *hit;
    vec3 dir;

    c->current_pitch = c->target_pitch;
    c->current_yaw = c->target_yaw;
    
    height = entity3d_aabb_Y(entity) * 3 / 4;
    dist = height * 3;
    maxdist = max(c->dist + 1, dist);
    start[1] += height;
retry:
    dir[0] = sin(-to_radians(c->current_yaw)) * cos(to_radians(c->current_pitch));
    dir[1] = sin(to_radians(c->current_pitch));
    dir[2] = cos(-to_radians(c->current_yaw)) * cos(to_radians(c->current_pitch));

    dist = height * 3;
    hit = phys_ray_cast(entity, start, dir, &dist);
    if (!hit) {
        dist = height * 3;
    } else if (dist < 1 && c->current_pitch < 90) {
        c->current_pitch = min(c->current_pitch + 5, 90);
        goto retry;
    }

    ui_debug_printf("hit: '%s' c->dist: %f dist: %f maxdist: %f",
                    entity_name(hit), c->dist, dist, maxdist);
    c->dist = clampf(dist - 0.1, 1, maxdist);
}

bool camera_has_moved(struct camera *c)
{
    return (c->yaw_delta || c->pitch_delta || c->ch->moved);
}
