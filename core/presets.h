/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PRESETS_H__
#define __CLAP_PRESETS_H__

#include "error.h"
#include "linmath.h"
#include "render.h"

struct scene;
struct entity3d;

/**
 * struct noise_bg_opts - animated noise-emission background preset
 * @pos:                quad center in world space
 * @extent:             quad half-size along X and Z (quad is in the XZ plane)
 * @tex:                diffuse texture, NULL -> white_pixel()
 * @emission:           noise emission color
 * @noise_scale:        noise sampling frequency (smaller -> larger swirls)
 * @noise_amp:          noise-normals tilt amplitude
 * @bloom_threshold:    per-entity bloom threshold
 * @bloom_intensity:    per-entity bloom intensity
 * @add_light:          add a directional light at @light_pos for the quad
 * @light_pos:          directional light source position
 * @light_dir:          directional light direction
 * @light_color:        directional light color
 * @reposition_camera:  place the scene's main camera at @camera_pos with
 *                      @camera_angles (ignored when false)
 * @camera_pos:         camera world position
 * @camera_angles:      camera pitch/yaw/roll in degrees
 */
typedef struct noise_bg_opts {
    vec3       pos;
    float      extent;
    texture_t  *tex;
    vec3       emission;
    float      noise_scale;
    float      noise_amp;
    float      bloom_threshold;
    float      bloom_intensity;

    bool       add_light;
    vec3       light_pos;
    vec3       light_dir;
    vec3       light_color;

    bool       reposition_camera;
    vec3       camera_pos;
    vec3       camera_angles;
} noise_bg_opts;

/**
 * struct noise_bg - noise_bg_new() handle
 * @entity:     background entity, usable for further tweaks
 * @light_idx:  index of the light slot, -1 if no light was added
 */
typedef struct noise_bg {
    struct entity3d *entity;
    int             light_idx;
} noise_bg;

cres_ret(noise_bg);

/**
 * noise_bg_new() - build an animated noise-emission background
 * @scene:  target scene; the quad is added to @scene->mq
 * @opts:   configuration, see &struct noise_bg_opts
 *
 * Creates a quad in the XZ plane at @opts->pos with a noise-emission
 * material that animates when the caller advances
 * render_options.noise_shift. Optionally adds a directional light and
 * repositions the main camera.
 * Return: noise_bg handle on success, cres error on failure.
 */
cres(noise_bg) noise_bg_new(struct scene *scene, const noise_bg_opts *opts);

/**
 * noise_bg_done() - tear down a noise_bg
 * @scene:  scene @bg was added to
 * @bg:     handle returned by noise_bg_new(); fields cleared after this call
 *
 * ref_put()s the entity and releases the light slot. The camera is
 * expected to be reset by scene load; the diffuse texture is not owned
 * by the preset.
 */
void noise_bg_done(struct scene *scene, noise_bg *bg);

#endif /* __CLAP_PRESETS_H__ */
