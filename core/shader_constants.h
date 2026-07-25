#ifndef CLAP_SHADER_CONSTANTS_H
#define CLAP_SHADER_CONSTANTS_H

#include "config.h"

#define JOINTS_MAX 200
#define PARTICLES_MAX 1024
#define LIGHTS_MAX 128
#define CASCADES_MAX 4
#define MSAA_SAMPLES 4
#define SSAO_KERNEL_SIZE 16
#define SSAO_NOISE_DIM 16
#define FILM_GRAIN_SIZE 64

#define LIGHT_CUTOFF    (1.0f / 256.0f)
#define TILE_WIDTH      64

#define HDR_LUT_K   1.0f
#define HDR_LUT_MAX 16.0f

/*
 * HDR output encodings present.frag can produce, one per surface the
 * backends know how to ask a compositor for:
 *   PQ                 -- BT.2020 primaries, SMPTE ST2084 (vulkan
 *                         HDR10_ST2084, CoreGraphics ITUR_2100_PQ). Absolute:
 *                         1.0 out of hdr_display_map() lands at
 *                         hdr_white_nits exactly.
 *   EXT_SRGB_LINEAR    -- BT.709 primaries, linear light (scRGB; vulkan
 *                         EXTENDED_SRGB_LINEAR, metal's extended linear sRGB)
 *   EXT_P3_LINEAR      -- Display P3 primaries, linear light (vulkan
 *                         DISPLAY_P3_LINEAR)
 *   EXT_SRGB_NONLINEAR -- BT.709 primaries, the sRGB curve continued past
 *                         1.0 (vulkan EXTENDED_SRGB_NONLINEAR, and what a
 *                         browser canvas is: wgpu srgb + extended tone
 *                         mapping)
 *   EXT_P3_NONLINEAR   -- Display P3 primaries, the same curve (vulkan
 *                         DISPLAY_P3_NONLINEAR, wgpu display-p3 + extended)
 *   SCRGB              -- the same fp16 EXTENDED_SRGB_LINEAR surface as
 *                         EXT_SRGB_LINEAR, on a windows desktop in HDR mode:
 *                         DWM reads 1.0 as 80 nits there, not as its SDR
 *                         white, so the encode scales by hdr_white_nits / 80
 *                         to put diffuse white where PQ puts it
 * The four extended pairs put 1.0 at the compositor's SDR reference white,
 * so hdr_white_nits is advisory there and absolute under PQ and SCRGB. Only
 * PQ needs a primary conversion out of the scene's BT.709 working space; the
 * sRGB pairs need none, which makes them the ones that isolate gamut bugs.
 */
#define HDR_TRANSFER_PQ                 0
#define HDR_TRANSFER_EXT_SRGB_LINEAR    1
#define HDR_TRANSFER_EXT_P3_LINEAR      2
#define HDR_TRANSFER_EXT_SRGB_NONLINEAR 3
#define HDR_TRANSFER_EXT_P3_NONLINEAR   4
#define HDR_TRANSFER_SCRGB              5
#define HDR_TRANSFER_MAX                6

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

#define NOISE_NORMALS_NONE                  0
#define NOISE_NORMALS_GPU                   1
#define NOISE_NORMALS_3D                    2

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
#elif defined(SHADER_RENDERER_VULKAN)
#include "bindings/render-bindings-vulkan.h"
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
#define RT_MODEL_VELOCITY       3
#define RT_MODEL_LAST           RT_MODEL_VELOCITY

#endif /* CLAP_SHADER_CONSTANTS_H */

