/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_HANDCLAP_H__
#define __CLAP_PROCANIM_HANDCLAP_H__

#include "animator.h"
#include "linmath.h"
#include "model.h"

/**
 * struct handclap_config - the hands clapped together once: the definition's cfg
 * @duration:   seconds a clap takes, together and back apart; 0: 0.45
 * @reach:      how far toward each other the hands swing, of the way to meet
 *              midway; 0: 0.85, short of the hands' own thickness
 *
 * The joints the hands the model maps (JOINT_HAND_LEFT and JOINT_HAND_RIGHT)
 * hang from swing about the model's up, toward the point midway between the
 * hands as posed so far, faster and faster until the hands meet, then ease
 * back apart: a delta over their posed rotations.
 */
typedef struct handclap_config {
    float           duration;
    float           reach;
} handclap_config;

/**
 * struct handclap - the entity's binding
 * @arm:    the joints the hands hang from, left and right; handclap_mask()
 *          finds them
 * @hand:   the hands, left and right; likewise
 * @start:  when the clap under way started, in the class's time; negative
 *          while none is
 * @go:     a clap is asked for, which the next eval starts; the brain sets it
 */
typedef struct handclap {
    int             arm[2];
    int             hand[2];
    double          start;
    bool            go;
} handclap_t;

/**
 * handclap_pose_ops - the clap as a pose class
 *
 * anim_lib_pose(lib, &handclap_pose_ops, &cfg) on the model, animator_bind()
 * with a handclap_t on the entity, played on an overlay masked to the arms
 * (handclap_mask()). eval writes the arms' rotations and nothing else, and
 * only while a clap is under way.
 */
extern const anim_pose_ops handclap_pose_ops;

/**
 * handclap_mask() - find the hands and the arms, and mask an overlay to the arms
 * @hc:     the binding: its arms and hands are filled in, no clap under way
 * @a:      the animator
 * @m:      the model the class is defined on
 * @layer:  the overlay the class plays on
 *
 * Return: CERR_OK, CERR_NOT_FOUND for a model that maps no two hands hanging
 *         from something, or animator_mask_chain()'s errors
 */
cerr_check handclap_mask(handclap_t *hc, animator_t *a, model3d *m, unsigned int layer);

#endif /* __CLAP_PROCANIM_HANDCLAP_H__ */
