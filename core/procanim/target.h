/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_TARGET_H__
#define __CLAP_PROCANIM_TARGET_H__

#include "linmath.h"

struct entity3d;

/**
 * enum target_kind - how an aim point moves
 * @TARGET_POINT:   a fixed point
 * @TARGET_FOLLOW:  an entity's center (plus an offset), low-passed at a
 *                  rate; without an entity, the point, low-passed the same
 * @TARGET_MOVE:    from wherever the aim point is when the law starts to
 *                  the point, over a duration, eased
 * @TARGET_HOLD:    wherever it is when the law starts, for a duration
 */
typedef enum target_kind {
    TARGET_POINT,
    TARGET_FOLLOW,
    TARGET_MOVE,
    TARGET_HOLD,
} target_kind;

/**
 * struct target_law - the data half of an aim point: in a pose class's cfg
 * @kind:       how the point moves
 * @rate:       FOLLOW: low-pass rate, 1/s; 0 snaps
 * @duration:   MOVE, HOLD: seconds
 * @ease:       MOVE: the easing over the move's phase; nullptr is linear
 */
typedef struct target_law {
    double          rate;
    double          duration;
    float           (*ease)(float);
    target_kind     kind;
} target_law;

/**
 * struct target - the state half of an aim point: in the entity's binding
 * @point:  POINT: the point; MOVE: the destination; FOLLOW without an
 *          entity: what to follow. The brain writes it, model space
 * @entity: FOLLOW: what to follow; nullptr follows @point
 * @offset: FOLLOW: added to the entity's center, world space
 * @none:   nothing to aim at: an effector rests where it points with no
 *          target; the brain sets and clears it
 * @from:   MOVE's start, captured by target_start()
 * @cur:    the aim point as of the last target_update(); what MOVE and
 *          HOLD anchor on, so a fresh binding wants it seeded
 */
typedef struct target {
    struct entity3d *entity;
    vec3            point;
    vec3            offset;
    vec3            from;
    vec3            cur;
    bool            none;
} target_t;

/**
 * target_start() - a law starts on an aim point
 * @t:      the aim point
 * @law:    the law starting
 *
 * MOVE and HOLD anchor on where the point is now (target::cur).
 */
void target_start(target_t *t, const target_law *law);

/**
 * target_update() - move the aim point by its law
 * @t:      the aim point
 * @law:    its law
 * @e:      the entity whose model space the point lives in; FOLLOW
 *          converts the followed entity's center into it
 * @tt:     seconds since the law started
 * @dt:     seconds since the previous update
 * @out:    receiver of the aim point, model space
 */
void target_update(target_t *t, const target_law *law, struct entity3d *e, double tt, double dt,
                   vec3 out);

/**
 * target_done() - has a timed law run out
 * @law:    the law
 * @tt:     seconds since it started
 *
 * Return: true for MOVE and HOLD at their duration; POINT and FOLLOW never
 * end
 */
bool target_done(const target_law *law, double tt);

/**
 * aim_angles() - yaw and pitch of a direction in a joint's posed frame
 * @rest:       the joint's local rotation as posed before aiming
 * @forward:    the joint's forward axis in its own frame, unit
 * @up:         the joint's up axis in its own frame, unit
 * @dir:        where to point, in the parent's frame; any length
 * @yaw:        receiver: radians around @up, positive toward the right
 *              (cross(up, forward))
 * @pitch:      receiver: radians toward @up, positive up
 *
 * Return: false for a zero @dir, in which case the angles are 0
 */
bool aim_angles(const quat rest, const vec3 forward, const vec3 up, const vec3 dir,
                double *yaw, double *pitch);

/**
 * aim_rotate() - turn a joint's posed rotation by yaw and pitch
 * @out:        receiver of the aimed local rotation; may be @rest itself
 * @rest:       the joint's local rotation as posed before aiming
 * @forward:    as for aim_angles()
 * @up:         as for aim_angles()
 * @yaw:        radians around the posed up axis
 * @pitch:      radians up around the yawed right axis
 *
 * The rotation aim_angles() measured, applied: the forward axis then
 * points along the direction it was measured from.
 */
void aim_rotate(quat out, const quat rest, const vec3 forward, const vec3 up, double yaw,
                double pitch);

#endif /* __CLAP_PROCANIM_TARGET_H__ */
