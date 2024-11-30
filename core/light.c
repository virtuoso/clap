#include "light.h"

void light_set_pos(struct light *light, int idx, float pos[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->pos[idx * 3 + i] = pos[i];
}

void light_set_color(struct light *light, int idx, float color[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->color[idx * 3 + i] = color[i];
}

void light_set_attenuation(struct light *light, int idx, float attenuation[3])
{
    int i;

    for (i = 0; i < 3; i++)
        light->attenuation[idx * 3 + i] = attenuation[i];
}
