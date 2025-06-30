#ifndef SHADERS_UBO_TRANSFORM_GLSL
#define SHADERS_UBO_TRANSFORM_GLSL

layout (std140, binding = UBO_BINDING_transform) uniform transform {
    mat4 trans;
};

#endif /* SHADERS_UBO_TRANSFORM_GLSL */
