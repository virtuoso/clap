#ifndef SHADERS_UBO_RENDER_COMMON_GLSL
#define SHADERS_UBO_RENDER_COMMON_GLSL

layout (std140, binding = UBO_BINDING_render_common) uniform render_common {
    bool use_msaa;
    bool use_hdr;
};

#endif /* SHADERS_UBO_RENDER_COMMON_GLSL */
