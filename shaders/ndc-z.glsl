#ifndef SHADERS_NDC_Z_GLSL
#define SHADERS_NDC_Z_GLSL

#include "config.h"

#ifdef CONFIG_NDC_ZERO_ONE
float convert_to_ndc_z(float z)     { return clamp(z, 0.0, 1.0); }
#else
float convert_to_ndc_z(float z)     { return clamp(z * 2.0 - 1.0, -1.0, 1.0); }
#endif /* !CONFIG_NDC_ZERO_ONE */

#endif /* SHADERS_NDC_Z_GLSL */
