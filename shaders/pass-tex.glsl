#ifndef SHADERS_PASS_TEX_GLSL
#define SHADERS_PASS_TEX_GLSL

#include "config.h"

#ifdef CONFIG_ORIGIN_TOP_LEFT
vec2 convert_pass_tex(vec2 uv)  { return vec2(uv.x, 1.0 - uv.y); }
#else
vec2 convert_pass_tex(vec2 uv)  { return uv; }
#endif /* !CONFIG_ORIGIN_TOP_LEFT */

#endif /* SHADERS_PASS_TEX_GLSL */
