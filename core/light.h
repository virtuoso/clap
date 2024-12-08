/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LIGHT_H__
#define __CLAP_LIGHT_H__

#include "common.h"
#include "render.h"
#include "display.h"
#include "view.h"

#define LIGHTS_MAX 4
#define CASCADES_MAX 3

struct light {
    GLfloat pos[3 * LIGHTS_MAX];
    GLfloat color[3 * LIGHTS_MAX];
    GLfloat attenuation[3 * LIGHTS_MAX];
    GLfloat dir[3 * LIGHTS_MAX];
    struct view view[LIGHTS_MAX];
    texture_t *shadow[LIGHTS_MAX];
    bool shadow_outline;
    bool shadow_msaa;
};

void light_set_pos(struct light *light, int idx, float pos[3]);
void light_set_color(struct light *light, int idx, float color[3]);
void light_set_attenuation(struct light *light, int idx, float attenuation[3]);

#endif /* __CLAP_LIGHT_H__ */
