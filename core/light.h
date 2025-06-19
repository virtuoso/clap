/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LIGHT_H__
#define __CLAP_LIGHT_H__

#include "common.h"
#include "render.h"
#include "render.h"
#include "view.h"

#define LIGHTS_MAX 4

struct light {
    float pos[3 * LIGHTS_MAX];
    float color[3 * LIGHTS_MAX];
    float attenuation[3 * LIGHTS_MAX];
    float dir[3 * LIGHTS_MAX];
    int is_dir[LIGHTS_MAX];
    struct view view[LIGHTS_MAX];
    texture_t *shadow[LIGHTS_MAX][CASCADES_MAX];
    float ambient[3];
    float shadow_tint[3];
    int nr_lights;
};

void light_set_ambient(struct light *light, float *color);
void light_set_shadow_tint(struct light *light, float *color);
cres(int) light_get(struct light *light);
void light_set_pos(struct light *light, int idx, float pos[3]);
void light_set_color(struct light *light, int idx, float color[3]);
void light_set_attenuation(struct light *light, int idx, float attenuation[3]);
void light_set_directional(struct light *light, int idx, bool is_directional);

#endif /* __CLAP_LIGHT_H__ */
