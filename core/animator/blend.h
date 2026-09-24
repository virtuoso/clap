/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_ANIMATOR_BLEND_H__
#define __CLAP_ANIMATOR_BLEND_H__

/*
 * Blending local joint transforms: what an overlay does to the joints it
 * masks and a cross-fade does to all of them. Pure arithmetic over
 * struct joint, no entity in sight.
 */

struct joint;

/**
 * joint_blend() - @dst = @a blended toward @b by @w
 * @dst:    the result; may be @a or @b
 * @a:      the transform at w = 0
 * @b:      the transform at w = 1
 * @w:      [0, 1]
 *
 * Translation and scale lerp; the rotation is a normalized lerp along the
 * shorter arc, which is what a blend of two poses a fade apart wants and
 * cheaper than slerp. The keyframe cursors are @dst's own and untouched.
 */
void joint_blend(struct joint *dst, const struct joint *a, const struct joint *b, float w);

/**
 * joints_copy_trs() - copy @nr joints' local transforms, not their cursors
 * @dst:    the buffer an overlay evaluates into
 * @src:    the pose it starts from
 * @nr:     how many joints
 */
void joints_copy_trs(struct joint *dst, const struct joint *src, unsigned int nr);

#endif /* __CLAP_ANIMATOR_BLEND_H__ */
