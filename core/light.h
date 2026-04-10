/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LIGHT_H__
#define __CLAP_LIGHT_H__

#include "render.h"
#include "render.h"
#include "shader_constants.h"
#include "view.h"

typedef struct {
    uint32_t    v[4];
} ui32vec4;

struct light {
    float pos[3 * LIGHTS_MAX];
    float color[3 * LIGHTS_MAX];
    float attenuation[3 * LIGHTS_MAX];
    float dir[3 * LIGHTS_MAX];
    float cutoff[LIGHTS_MAX];
    int is_dir[LIGHTS_MAX];
    struct view view[LIGHTS_MAX];
    texture_t *shadow[LIGHTS_MAX][CASCADES_MAX];
    float ambient[3];
    float shadow_tint[3];
    /*
     * Active slots bitmap. nr_lights is the highest allocated index + 1,
     * kept for shader upload range; individual slots within [0, nr_lights)
     * may be unset if previously released via light_put().
     */
    struct bitmap active;
    int nr_lights;
    struct light_grid {
        ui32vec4        *tiles;
        texture_t       tex;
        unsigned int    cell;
        unsigned int    twidth;
        unsigned int    theight;
        unsigned int    width;
        unsigned int    height;
    } grid;
};

void light_grid_compute(struct light *light, struct view *view);
void light_set_ambient(struct light *light, const float *color);
void light_set_shadow_tint(struct light *light, const float *color);
bool light_is_valid(struct light *light, int idx);
bool light_is_directional(struct light *light, int idx);
float light_get_radius(struct light *light, unsigned int idx);
cres(int) light_get(struct light *light);
void light_put(struct light *light, int idx);
void light_set_pos(struct light *light, int idx, const float pos[3]);
void light_set_color(struct light *light, int idx, const float color[3]);
void light_set_attenuation(struct light *light, int idx, const float attenuation[3]);
void light_set_directional(struct light *light, int idx, bool is_directional);
void light_set_direction(struct light *light, int idx, vec3 dir);
bool light_is_spotlight(struct light *light, int idx);
void light_set_cutoff(struct light *light, int idx, float cutoff);
void light_init(struct clap_context *ctx, struct light *light);
void light_done(struct clap_context *ctx, struct light *light);

#ifdef CONFIG_FINAL
static inline void light_draw(struct clap_context *ctx, struct light *light) {}
#else
void light_draw(struct clap_context *ctx, struct light *light);
#endif /* CONFIG_FINAL */

#endif /* __CLAP_LIGHT_H__ */
