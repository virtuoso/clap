#ifndef SHADERS_UBO_MATERIAL_GLSL
#define SHADERS_UBO_MATERIAL_GLSL

layout (std140, binding = UBO_BINDING_material) uniform material {
    float reflectivity;
    float shine_damper;
};

#endif /* SHADERS_UBO_MATERIAL_GLSL */
