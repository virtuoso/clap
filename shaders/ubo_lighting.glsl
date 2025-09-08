#ifndef SHADERS_UBO_LIGHTING_GLSL
#define SHADERS_UBO_LIGHTING_GLSL

layout (std140, binding = UBO_BINDING_lighting) uniform lighting {
    vec3 light_pos[LIGHTS_MAX];
    vec3 light_color[LIGHTS_MAX];
    vec3 light_dir[LIGHTS_MAX];
    vec3 attenuation[LIGHTS_MAX];
    bool light_directional[LIGHTS_MAX];
    float light_cutoff[LIGHTS_MAX];
    int  nr_lights;
    bool use_normals;
    vec3 light_ambient;
};

#endif /* SHADERS_UBO_LIGHTING_GLSL */
