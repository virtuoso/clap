#ifndef SHADERS_UBO_PROJVIEW_GLSL
#define SHADERS_UBO_PROJVIEW_GLSL

layout (std140, binding = UBO_BINDING_projview) uniform projview {
    mat4 proj;
    mat4 view;
    mat4 inverse_view;
};

#endif /* SHADERS_UBO_PROJVIEW_GLSL */
