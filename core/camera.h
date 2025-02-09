/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CAMERA_H__
#define __CLAP_CAMERA_H__

#include "model.h"
#include "view.h"

#define NUMBER_OF_DEBUG_LINES 4

struct camera {
    struct character *ch;
    struct view view;
    float  target_pitch;
    float  target_yaw;
    float  target_roll;
    float  current_pitch;
    float  current_yaw;
    float  current_roll;
    unsigned int zoom;
    float   dist;
    float   yaw_delta;
    float   pitch_delta;
    float   tmp_debug_line_start[3];
    float   tmp_debug_line_end[3 * NUMBER_OF_DEBUG_LINES];
    float   debug_line_start[3];
    float   debug_line_end[3 * NUMBER_OF_DEBUG_LINES];
};

void camera_setup(struct camera *c);
void camera_move(struct camera *c, unsigned long fps);
void camera_position(struct camera *c, float x, float y, float z);
void camera_reset_movement(struct camera *c);
void camera_add_pitch(struct camera *c, float delta);
void camera_add_yaw(struct camera *c, float delta);
void camera_update(struct camera *c, struct scene *s, entity3d *entity);
bool camera_has_moved(struct camera *c);
void camera_set_target_to_current(struct camera *c);
void debug_camera_action(struct camera *c);

#endif
