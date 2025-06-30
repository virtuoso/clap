#ifndef SHADERS_UBO_COLOR_PT_GLSL
#define SHADERS_UBO_COLOR_PT_GLSL

layout (std140, binding = UBO_BINDING_color_pt) uniform color_pt {
    vec4 in_color;
    int color_passthrough;
};

#endif /* SHADERS_UBO_COLOR_PT_GLSL */
