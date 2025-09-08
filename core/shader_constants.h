#ifndef CLAP_SHADER_CONSTANTS_H
#define CLAP_SHADER_CONSTANTS_H

#define JOINTS_MAX 200
#define PARTICLES_MAX 1024
#define LIGHTS_MAX 128
#define CASCADES_MAX 4
#define MSAA_SAMPLES 4
#define SSAO_KERNEL_SIZE 16
#define SSAO_NOISE_DIM 16

#define TILE_WIDTH      64

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

#define NOISE_NORMALS_NONE  0
#define NOISE_NORMALS_GPU   1
#define NOISE_NORMALS_3D    2

/* Attribute locations */
#define ATTR_LOC_POSITION   0
#define ATTR_LOC_TEX        1
#define ATTR_LOC_NORMAL     2
#define ATTR_LOC_TANGENT    3
#define ATTR_LOC_JOINTS     4
#define ATTR_LOC_WEIGHTS    5

/* UBO binding locations */
#define UBO_BINDING_color_pt    0
#define UBO_BINDING_lighting    1
#define UBO_BINDING_shadow      2
#define UBO_BINDING_transform   3
#define UBO_BINDING_projview    4
#define UBO_BINDING_skinning    5
#define UBO_BINDING_particles   6
#define UBO_BINDING_material    7
#define UBO_BINDING_render_common 8
#define UBO_BINDING_outline     9
#define UBO_BINDING_bloom       10
#define UBO_BINDING_postproc    11

#define SAMPLER_BINDING_model_tex       0
#define SAMPLER_BINDING_normal_map      1
#define SAMPLER_BINDING_emission_map    2
#define SAMPLER_BINDING_sobel_tex       3
#define SAMPLER_BINDING_shadow_map      4
#define SAMPLER_BINDING_shadow_map_ms   5
#define SAMPLER_BINDING_shadow_map1     5
#define SAMPLER_BINDING_shadow_map2     6
#define SAMPLER_BINDING_shadow_map3     7
#define SAMPLER_BINDING_lut_tex         5
#define SAMPLER_BINDING_noise3d         8
#define SAMPLER_BINDING_light_map       9

#endif /* CLAP_SHADER_CONSTANTS_H */

