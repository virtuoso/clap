#ifndef SHADERS_UBO_PROJVIEW_GLSL
#define SHADERS_UBO_PROJVIEW_GLSL

layout (std140, binding = UBO_BINDING_projview) uniform projview {
    mat4 proj;
    mat4 view;
    mat4 inverse_view;
    mat4 inverse_proj;
};

#endif /* SHADERS_UBO_PROJVIEW_GLSL */
