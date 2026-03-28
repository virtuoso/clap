#ifndef SHADERS_UBO_OUTLINE_GLSL
#define SHADERS_UBO_OUTLINE_GLSL

layout (std140, binding = UBO_BINDING_outline) uniform outline {
    uint    edge_mode;
};

#endif /* SHADERS_UBO_OUTLINE_GLSL */
