#ifndef SHADERS_UBO_SHADOW_GLSL
#define SHADERS_UBO_SHADOW_GLSL

layout (std140, binding = UBO_BINDING_shadow) uniform shadow {
    mat4 shadow_mvp[CASCADES_MAX];
    float cascade_distances[CASCADES_MAX];
    vec3 shadow_tint;
    bool shadow_vsm;
    bool shadow_outline;
    float shadow_outline_threshold;
    int nr_cascades;
};

#endif /* SHADERS_UBO_SHADOW_GLSL */
