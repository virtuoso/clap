#ifndef SHADERS_UBO_RENDER_COMMON_GLSL
#define SHADERS_UBO_RENDER_COMMON_GLSL

layout (std140, binding = UBO_BINDING_render_common) uniform render_common {
    bool use_msaa;
    bool use_edge_aa;
    bool use_hdr;
    bool hdr_output;
    float   hdr_white_nits;
    float   hdr_peak_nits;
    float   hdr_compress_knee;
    float   hdr_knee_softness;
};

#endif /* SHADERS_UBO_RENDER_COMMON_GLSL */
