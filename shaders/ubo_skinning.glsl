#ifndef SHADERS_UBO_SKINNING_GLSL
#define SHADERS_UBO_SKINNING_GLSL

layout (std140, binding = UBO_BINDING_skinning) uniform skinning {
    int use_skinning;
    mat4 joint_transforms[JOINTS_MAX];
};

#endif /* SHADERS_UBO_SKINNING_GLSL */
