#ifndef SHADERS_UBO_BLOOM_GLSL
#define SHADERS_UBO_BLOOM_GLSL

layout (std140, binding = UBO_BINDING_bloom) uniform bloom {
    float   bloom_exposure;
    float   bloom_intensity;
    float   bloom_threshold;
    float   bloom_operator;
};

#endif /* SHADERS_UBO_BLOOM_GLSL */
