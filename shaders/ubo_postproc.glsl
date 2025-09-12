#ifndef SHADERS_UBO_POSTPROC_GLSL
#define SHADERS_UBO_POSTPROC_GLSL

layout (std140, binding = UBO_BINDING_postproc) uniform postproc {
    float   width;
    float   height;
    float   near_plane;
    float   far_plane;
    vec3    ssao_kernel[SSAO_KERNEL_SIZE];
    vec2    ssao_noise_scale;
    float   ssao_radius;
    float   ssao_weight;
    bool    use_ssao;
    int     laplace_kernel;
    float   contrast;
    float   lighting_exposure;
    float   lighting_operator;
    vec3    fog_color;
    float   fog_near;
    float   fog_far;
    bool    film_grain;
    float   film_grain_shift;
    float   film_grain_factor;
    float   film_grain_power;
};

#endif /* SHADERS_UBO_POSTPROC_GLSL */
