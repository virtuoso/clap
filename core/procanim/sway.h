/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PROCANIM_SWAY_H__
#define __CLAP_PROCANIM_SWAY_H__

#include "error.h"
#include "linmath.h"
#include "model.h"

struct scene;

/**
 * struct sway_config - layered sine chain sway parameters
 * @wind:           wind direction; the chain bends around its horizontal
 *                  perpendicular; zero picks +X, must not be vertical
 * @first:          first swaying joint index; earlier joints stay put
 * @nr:             number of swaying joints; 0 extends to the last joint
 * @amplitude:      bend amplitude at the tip, radians
 * @amp_power:      amplitude profile exponent along the chain; 0: 1.6
 * @frequency:      base sway frequency, rad/s; 0 picks the default
 * @phase_lag:      base sway phase lag per joint, radians; 0: default
 * @gust:           gust amplitude as a fraction of @amplitude; 0: default
 * @gust_frequency: rad/s; 0 picks the default
 * @gust_lag:       phase lag per joint; 0 picks the default
 * @flutter:        flutter amplitude fraction, weighted toward the tip;
 *                  0: default
 * @flutter_frequency: rad/s; 0 picks the default
 * @flutter_lag:    phase lag per joint; 0 picks the default
 * @twist:          twist amplitude around the up axis as a fraction of
 *                  @amplitude; 0: default
 * @twist_frequency: rad/s; 0 picks the default
 * @twist_lag:      phase lag per joint; 0 picks the default
 * @no_gust:        disable the gust layer
 * @no_flutter:     disable the flutter layer
 * @no_twist:       disable the twist layer
 * @phase:          time offset in seconds added to the animation clock,
 *                  to desync otherwise-identical rigs; 0 for none. Unlike
 *                  the tunables, 0 is the real default, not "pick the
 *                  default" - a lone tree wants no offset.
 *
 * Zero fields other than @amplitude, which is required, pick the same
 * defaults that sway_config_init() writes out; start from
 * sway_config_init() to see and tweak them explicitly.
 */
typedef struct sway_config {
    vec3            wind;
    unsigned int    first;
    unsigned int    nr;
    float           amplitude;
    float           amp_power;
    float           frequency;
    float           phase_lag;
    float           gust;
    float           gust_frequency;
    float           gust_lag;
    float           flutter;
    float           flutter_frequency;
    float           flutter_lag;
    float           twist;
    float           twist_frequency;
    float           twist_lag;
    float           phase;
    bool            no_gust;
    bool            no_flutter;
    bool            no_twist;
} sway_config;

/**
 * sway_config_init() - fill a sway configuration with the defaults
 * @cfg:    the configuration; fully overwritten
 */
void sway_config_init(sway_config *cfg);

/**
 * chain_sway() - pose a run of an entity's joints swaying in the wind
 * @e:      entity whose joints to pose
 * @t:      animation time, seconds; the caller anchors it
 * @cfg:    sway parameters
 *
 * A slow base sway, a faster gust harmonic, tip-weighted flutter and a
 * touch of twist, phase-lagged along the chain so it moves like a
 * connected body and not a stiff pole. Writes local rotations only:
 * bracket the frame's pose writers with entity3d_pose_reset() and
 * entity3d_pose_update(), so sway composes with other procedural
 * animation on the same skeleton.
 * Context: anywhere between pose reset and update
 * Return: CERR_OK or CERR_INVALID_ARGUMENTS
 */
cerr_check chain_sway(entity3d *e, double t, const sway_config *cfg);

/**
 * struct sway_rig - a self-updating swaying entity
 * @scene:          the scene, for the animation clock (required)
 * @e:              the entity to sway (required); the rig does not own
 *                  it and claims its update callback and priv pointer
 * @orig_update:    the entity update the rig chains to (internal)
 * @t0:             animation time anchor (internal)
 * @cfg:            sway parameters; zero amplitude picks the defaults
 *
 * The turnkey wrapper over chain_sway(): sway_rig_start() installs an
 * entity update that resets the pose, sways the chain and recomputes
 * the skeleton every frame, then chains to the original update.
 */
typedef struct sway_rig {
    struct scene    *scene;
    entity3d        *e;
    int             (*orig_update)(entity3d *, void *);
    double          t0;
    sway_config     cfg;
} sway_rig;

/**
 * sway_rig_start() - start swaying an entity
 * @rig:    the rig, with sway_rig::scene, sway_rig::e and optionally
 *          sway_rig::cfg filled
 *
 * Context: entity setup time
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS (no skeleton) or
 * CERR_INVALID_OPERATION (the entity's priv pointer is taken)
 */
cerr_check sway_rig_start(sway_rig *rig);

/**
 * sway_rig_stop() - stop swaying an entity
 * @rig:    a started rig; the entity's update and priv are restored
 */
void sway_rig_stop(sway_rig *rig);

#endif /* __CLAP_PROCANIM_SWAY_H__ */
