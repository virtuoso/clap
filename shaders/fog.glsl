#ifndef SHADERS_FOG_GLSL
#define SHADERS_FOG_GLSL

#include "shader_constants.h"

/*
 * The radial fog's density curve: how much of a surface @dist away the
 * haze hides, from the postproc UBO's use_fog, fog_near, fog_far,
 * fog_curve and fog_max, which the including shader declares. Both fog
 * modes take their haze from it, so switching between them keeps a
 * scene's fog settings and its overall density.
 */
float fog_factor_at(float dist)
{
    if (!use_fog)   return 0.0;

    /* the range dragged shut is a step at fog_near, not a division by zero */
    float t = (dist - fog_near) / max(fog_far - fog_near, FOG_RANGE_MIN);
    float factor;

    if (fog_curve == FOG_CURVE_EXP)         factor = 1.0 - exp(-3.0 * max(t, 0.0));
    else if (fog_curve == FOG_CURVE_SMOOTH) factor = smoothstep(0.0, 1.0, t);
    else                                    factor = clamp(t, 0.0, 1.0);

    return factor * fog_max;
}

#endif /* SHADERS_FOG_GLSL */
