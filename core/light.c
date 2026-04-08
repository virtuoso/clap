#include "light.h"

void light_set_ambient(struct light *light, const float *color)
{
    vec3_dup(light->ambient, color);
}

void light_set_shadow_tint(struct light *light, const float *color)
{
    vec3_dup(light->shadow_tint, color);
}

bool light_is_valid(struct light *light, int idx)
{
    return idx >= 0 && idx < light->nr_lights;
}

bool light_is_directional(struct light *light, int idx)
{
    if (!light_is_valid(light, idx))
        return false;

    return light->is_dir[idx];
}

cres(int) light_get(struct light *light)
{
    if (light->nr_lights == LIGHTS_MAX)
        return cres_error(int, CERR_TOO_LARGE);

    int idx = light->nr_lights++;
    float attenuation[3] = { 1, 0, 0 };

    light->pos[idx * 3]     = 0;
    light->pos[idx * 3 + 1] = 0;
    light->pos[idx * 3 + 2] = 0;
    light->color[idx * 3]     = 0;
    light->color[idx * 3 + 1] = 0;
    light->color[idx * 3 + 2] = 0;
    light->attenuation[idx * 3]     = attenuation[0];
    light->attenuation[idx * 3 + 1] = attenuation[1];
    light->attenuation[idx * 3 + 2] = attenuation[2];
    light->is_dir[idx] = true;

    return cres_val(int, idx);
}

void light_set_pos(struct light *light, int idx, const float pos[3])
{
    if (!light_is_valid(light, idx))
        return;

    for (int i = 0; i < 3; i++)
        light->pos[idx * 3 + i] = pos[i];
}

void light_set_color(struct light *light, int idx, const float color[3])
{
    if (!light_is_valid(light, idx))
        return;

    for (int i = 0; i < 3; i++)
        light->color[idx * 3 + i] = color[i];
}

void light_set_attenuation(struct light *light, int idx, const float attenuation[3])
{
    if (!light_is_valid(light, idx))
        return;

    for (int i = 0; i < 3; i++)
        light->attenuation[idx * 3 + i] = attenuation[i];
}

void light_set_directional(struct light *light, int idx, bool is_directional)
{
    if (!light_is_valid(light, idx))
        return;

    light->is_dir[idx] = is_directional;
}
