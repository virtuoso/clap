#ifndef CLAP_SHADER_CONSTANTS_H
#define CLAP_SHADER_CONSTANTS_H

#define JOINTS_MAX 200
#define PARTICLES_MAX 1024
#define LIGHTS_MAX 4
#define CASCADES_MAX 4
#define MSAA_SAMPLES 4
#define SSAO_KERNEL_SIZE 64
#define SSAO_NOISE_DIM 16

#define COLOR_PT_NONE 0
#define COLOR_PT_ALPHA 1
#define COLOR_PT_ALL 2

/* UBO binding locations */
#define UBO_BINDING_color_pt    0
#define UBO_BINDING_lighting    1
#define UBO_BINDING_shadow      2
#define UBO_BINDING_transform   3
#define UBO_BINDING_projview    4
#define UBO_BINDING_skinning    5
#define UBO_BINDING_particles   6

#endif /* CLAP_SHADER_CONSTANTS_H */

