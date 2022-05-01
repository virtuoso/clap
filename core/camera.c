// SPDX-License-Identifier: Apache-2.0
#include "camera.h"
#include "character.h"
#include "ui-debug.h"

void camera_setup(struct camera *c)
{
    c->target_yaw = 180;
    c->current_yaw = 180;
}

void camera_move(struct camera *c, unsigned long fps)
{
    c->target_pitch += c->pitch_delta / (float)fps;
    c->target_pitch = clampf(c->target_pitch, -90, 90);
    c->target_yaw += c->yaw_delta / (float)fps;
    if (c->target_yaw > 180)
        c->target_yaw -= 360;
    else if (c->target_yaw <= -180)
        c->target_yaw += 360;
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

    float pitch = c->target_pitch;
    float yaw = c->target_yaw;
    bool done = false;
    while (pitch < 90) {
        for (float yaw_delta = 0.0f; yaw_delta < 30.0f; yaw_delta += 2.0f) {
            for (int yaw_direction = -1; yaw_direction <= 1; yaw_direction += 2) {
                yaw = c->target_yaw + yaw_direction * yaw_delta;
                dir[0] = sin(-to_radians(yaw)) * cos(to_radians(pitch));
                dir[1] = sin(to_radians(pitch));
                dir[2] = cos(-to_radians(yaw)) * cos(to_radians(pitch));

                dist = height * 3;
                hit = phys_ray_cast(entity, start, dir, &dist);
                if (!hit) {
                    dist = height * 3;
                    done = true;
                } else if (dist < 1) {
                    continue;
                } else {
                    done = true;
                }

                if (done)
                    break;
            }

            if (done)
                break;
        }
        
        if (done)
            break;
        pitch += 5;
    }

    c->current_pitch = pitch;
    c->current_yaw = yaw;

    ui_debug_printf("hit: '%s' c->dist: %f dist: %f maxdist: %f",
                    entity_name(hit), c->dist, dist, maxdist);
    c->dist = clampf(dist - 0.1, 1, maxdist);
}

bool camera_has_moved(struct camera *c)
{
    return (c->yaw_delta || c->pitch_delta || c->ch->moved);
}
