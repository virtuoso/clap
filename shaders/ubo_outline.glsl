#ifndef SHADERS_UBO_OUTLINE_GLSL
#define SHADERS_UBO_OUTLINE_GLSL

layout (std140, binding = UBO_BINDING_outline) uniform outline {
    bool    outline_exclude;
    bool    sobel_solid;
    float   sobel_solid_id;
};

#endif /* SHADERS_UBO_OUTLINE_GLSL */
