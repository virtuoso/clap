#include "light.h"

void light_set_ambient(struct light *light, const float *color)
{
    vec3_dup(light->ambient, color);
}

void light_set_shadow_tint(struct light *light, const float *color)
{
    vec3_dup(light->shadow_tint, color);
}

cres(int) light_get(struct light *light)
{
    if (light->nr_lights == LIGHTS_MAX)
        return cres_error(int, CERR_TOO_LARGE);

    return cres_val(int, light->nr_lights++);
}

void light_set_pos(struct light *light, int idx, const float pos[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->pos[idx * 3 + i] = pos[i];
}

void light_set_color(struct light *light, int idx, const float color[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->color[idx * 3 + i] = color[i];
}

void light_set_attenuation(struct light *light, int idx, const float attenuation[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->attenuation[idx * 3 + i] = attenuation[i];
}

void light_set_directional(struct light *light, int idx, bool is_directional)
{
    light->is_dir[idx] = is_directional;
}
