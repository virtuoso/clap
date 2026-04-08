#ifndef CLAP_SHADER_CONSTANTS_H
#define CLAP_SHADER_CONSTANTS_H

#include "config.h"

#define JOINTS_MAX 200
#define PARTICLES_MAX 1024
#define LIGHTS_MAX 32
#define CASCADES_MAX 4
#define MSAA_SAMPLES 4
#define SSAO_KERNEL_SIZE 16
#define SSAO_NOISE_DIM 16

#define HDR_LUT_K   1.0f
#define HDR_LUT_MAX 16.0f

#define COLOR_PT_NONE 0
#define COLOR_PT_SET_RGB        0x01
#define COLOR_PT_REPLACE_RGB    0x02
#define COLOR_PT_SET_ALPHA      0x04
#define COLOR_PT_REPLACE_ALPHA  0x08
#define COLOR_PT_BLEND_ALPHA    0x10
#define COLOR_PT_ALL            (COLOR_PT_SET_RGB | COLOR_PT_SET_ALPHA)

#define MAT_METALLIC_INDEPENDENT            0
#define MAT_METALLIC_ROUGHNESS              1
#define MAT_METALLIC_ONE_MINUS_ROUGHNESS    2

/* Attribute locations */
#define ATTR_LOC_POSITION   0
#define ATTR_LOC_TEX        1
#define ATTR_LOC_NORMAL     2
#define ATTR_LOC_TANGENT    3
#define ATTR_LOC_JOINTS     4
#define ATTR_LOC_WEIGHTS    5

/* Renderer-specific binding locations */
#if defined(SHADER_RENDERER_OPENGL)
#include "bindings/render-bindings-gl.h"
#elif defined(SHADER_RENDERER_METAL)
#include "bindings/render-bindings-metal.h"
#elif defined(SHADER_RENDERER_WGPU)
#include "bindings/render-bindings-wgpu.h"
#elif !defined(__STDC__) && !defined(__OBJC__) && !defined(__cplusplus)
#error "Unsupported renderer"
#endif

/* Edge filter controls bits */
#define EDGE_EXCLUDE            (1u << 7)
#define EDGE_SOLID_LUMA_OFFSET  (4u)
#define EDGE_LUMA_WIDTH         (3u)
#define EDGE_LUMA_MAX           ((1u << EDGE_LUMA_WIDTH) - 1u)
#define EDGE_SOLID_MASK         ((1u << EDGE_SOLID_LUMA_OFFSET) - 1u)
#define EDGE_SOLID_OFFSET       (0u)

/* Render targets */
#define RT_MODEL_LIGHTING       0
#define RT_MODEL_EMISSION       1
#define RT_MODEL_VIEW_NORMALS   2
#define RT_MODEL_LAST           RT_MODEL_VIEW_NORMALS

#endif /* CLAP_SHADER_CONSTANTS_H */

