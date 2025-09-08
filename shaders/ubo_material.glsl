#ifndef SHADERS_UBO_MATERIAL_GLSL
#define SHADERS_UBO_MATERIAL_GLSL

layout (std140, binding = UBO_BINDING_material) uniform material {
    float reflectivity;
    float shine_damper;
    float roughness;
    float metallic;
    /* procedural roughness/metallic parameters, see material type */
    float roughness_ceil;
    float roughness_amp;
    int   roughness_oct;
    float roughness_scale;
    float metallic_ceil;
    float metallic_amp;
    int   metallic_oct;
    float metallic_scale;
    int   metallic_mode;
    bool  shared_scale;
    int   use_noise_normals;
    float noise_normals_amp;
    float noise_normals_scale;
    bool  use_noise_emission;
    bool  use_3d_fog;
    float fog_3d_amp;
    float fog_3d_scale;
};

#endif /* SHADERS_UBO_MATERIAL_GLSL */
