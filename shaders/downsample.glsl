#ifndef SHADERS_DOWNSAMPLE_GLSL
#define SHADERS_DOWNSAMPLE_GLSL

vec4 downsample_5tap(sampler2D tex, vec2 uv)
{
    vec2 texel_size = 1.0 / textureSize(tex, 0);

    vec4 color = texture(tex, uv) * 4 +
                 texture(tex, uv + vec2(-texel_size.x, -texel_size.y)) +
                 texture(tex, uv + vec2( texel_size.x, -texel_size.y)) +
                 texture(tex, uv + vec2(-texel_size.x,  texel_size.y)) +
                 texture(tex, uv + vec2( texel_size.x,  texel_size.y));

    return color * 0.125; // Gaussian filter average
}

#endif /* SHADERS_DOWNSAMPLE_GLSL */
