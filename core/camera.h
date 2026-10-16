/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CAMERA_H__
#define __CLAP_CAMERA_H__

#include "model.h"
#include "view.h"

/**
 * struct camera_shake_options - camera shake parameters
 * @duration_ms:    how long the shake lasts, in milliseconds
 * @frequency:      oscillations per second
 * @roll:           peak roll around the view direction, in degrees
 *
 * Each motion of the shake has its own amplitude, and a motion left at zero
 * doesn't move the camera. All of them fall off together over @duration_ms,
 * from a hit at the start to rest at the end.
 */
typedef struct camera_shake_options {
    unsigned int    duration_ms;
    float           frequency;
    float           roll;
} camera_shake_options;

/**
 * struct camera_shake - a camera shake under way
 * @opts:       the shake's parameters
 * @elapsed:    time since the shake started, in seconds
 */
struct camera_shake {
    camera_shake_options    opts;
    float                   elapsed;
};

struct camera {
    transform_t         xform;
    struct view         view;
    entity3d            *bv;
    darray(struct camera_shake, shakes);
    float               bv_volume;
    vec3                target;
    unsigned int        zoom;
    float               dist;
    float               dist_override;  /* > 0: manual orbit distance, overrides auto framing */
    float               yaw_delta;
    float               pitch_delta;
    vec4                frustum_corner[4];
#ifndef CONFIG_FINAL
    vec3                debug_corner[4];
    vec3                debug_target;
#endif /* CONFIG_FINAL */
};

void camera_move(struct camera *c, unsigned long fps);
void camera_reset_movement(struct camera *c);
void camera_add_pitch(struct camera *c, float delta);
void camera_add_yaw(struct camera *c, float delta);
void camera_update(struct camera *c, struct scene *s);
bool camera_has_moved(struct camera *c);

/**
 * camera_view_update() - build a camera's view for the frame
 * @c:      camera
 * @ctx:    clap context
 *
 * Build @c's view matrices from its transform, rolled by the shakes under
 * way, and move the shakes on by the frame's time. Called once per frame.
 */
void camera_view_update(struct camera *c, clap_context *ctx);

/**
 * define camera_shake_push - shake a camera
 * @_c:     camera
 * @...:    &struct camera_shake_options fields
 *
 * See _camera_shake_push().
 */
#define camera_shake_push(_c, ...) \
    _camera_shake_push((_c), &(const camera_shake_options) { __VA_ARGS__ })

/**
 * _camera_shake_push() - shake a camera
 * @c:      camera
 * @opts:   shake parameters
 *
 * Start a shake on top of the ones already under way; camera_view_update()
 * applies it from its next call on. Only the view shakes: the camera's
 * transform, which the controls and the orbit go by, holds still.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS for a zero @opts->duration_ms or a
 * non-positive @opts->frequency, or CERR_NOMEM
 */
cerr _camera_shake_push(struct camera *c, const camera_shake_options *opts);
#ifndef CONFIG_FINAL
void debug_camera_action(struct camera *c);
#else
static inline void debug_camera_action(struct camera *c) {}
#endif /* CONFIG_FINAL */

#endif
