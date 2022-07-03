/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CAMERA_H__
#define __CLAP_CAMERA_H__

//#include "common.h"
//#include "character.h"
//#include "matrix.h"
//#include "model.h"
#include "model.h"
//#include "physics.h"

#define NUMBER_OF_DEBUG_LINES 4

struct camera {
    struct character *ch;
    /* GLfloat pitch;  /\* left/right *\/ */
    /* GLfloat yaw;    /\* sideways *\/ */
    /* GLfloat roll;   /\* up/down *\/ */
    GLfloat target_pitch;
    GLfloat target_yaw;
    GLfloat target_roll;
    GLfloat current_pitch;
    GLfloat current_yaw;
    GLfloat current_roll;
    unsigned int zoom;
    float   dist;
    float   yaw_delta;
    float   pitch_delta;
    struct matrix4f     *view_mx;
    struct matrix4f     *inv_view_mx;
    float   tmp_debug_line_start[3 * NUMBER_OF_DEBUG_LINES];
    float   tmp_debug_line_end[3 * NUMBER_OF_DEBUG_LINES];
    float   debug_line_start[3 * NUMBER_OF_DEBUG_LINES];
    float   debug_line_end[3 * NUMBER_OF_DEBUG_LINES];
};

void camera_setup(struct camera *c);
void camera_move(struct camera *c, unsigned long fps);
void camera_position(struct camera *c, float x, float y, float z, GLfloat *pos);
void camera_reset_movement(struct camera *c);
void camera_add_pitch(struct camera *c, float delta);
void camera_add_yaw(struct camera *c, float delta);
void camera_update(struct camera *c, struct scene *s, struct entity3d *entity, vec3 start);
bool camera_has_moved(struct camera *c);
void camera_set_target_to_current(struct camera *c);
void debug_camera_action(struct camera *c);

#endif
