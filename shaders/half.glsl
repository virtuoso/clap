#ifndef SHADERS_HALF_GLSL
#define SHADERS_HALF_GLSL

#if defined(SHADER_RENDERER_OPENGL) || defined(SHADER_RENDERER_WGPU)
# ifdef SHADER_BROWSER
// At a superficial glance, WebGL doesn't handle mediump float well at all
// Needs proper profiling
#  define float16_t float
#  define f16vec2 vec2
#  define f16vec3 vec3
#  define f16vec4 vec4
#  define H(x)    float(x)
#  define HVEC3(x) vec3(x)
#  define HVEC4(x) vec4(x)
# else /* !SHADER_BROWSER */
#  define float16_t mediump float
#  define f16vec2 mediump vec2
#  define f16vec3 mediump vec3
#  define f16vec4 mediump vec4
#  define H(x)    float(x)
#  define HVEC3(x) vec3(x)
#  define HVEC4(x) vec4(x)
# endif /* !SHADER_BROWSER */
#else /* !SHADER_RENDERER_OPENGL */
#extension GL_EXT_shader_explicit_arithmetic_types: require
#define H(x)        float16_t(x)
#define HVEC3(x)    f16vec3(x)
#define HVEC4(x)    f16vec4(x)
#endif /* !SHADER_RENDERER_OPENGL */
precision highp float;
precision highp int;

#endif /* SHADERS_HALF_GLSL */
