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

/*
 * Jimenez's 13-tap 2x downsample: the average of five overlapping 2x2
 * boxes, the middle one weighing as much as the other four together,
 * which keeps a bright texel from flickering as it crosses output
 * texel boundaries the way one box does. Offsets in input texels: the
 * even ones land on texel corners, where bilinear does the 2x2 boxes,
 * the odd ones on the middle box's own four texels.
 */
vec4 downsample_13tap(sampler2D tex, vec2 uv)
{
    vec2 ts = 1.0 / vec2(textureSize(tex, 0));

    vec4 a = texture(tex, uv + vec2(-2.0,  2.0) * ts);
    vec4 b = texture(tex, uv + vec2( 0.0,  2.0) * ts);
    vec4 c = texture(tex, uv + vec2( 2.0,  2.0) * ts);
    vec4 d = texture(tex, uv + vec2(-2.0,  0.0) * ts);
    vec4 e = texture(tex, uv);
    vec4 f = texture(tex, uv + vec2( 2.0,  0.0) * ts);
    vec4 g = texture(tex, uv + vec2(-2.0, -2.0) * ts);
    vec4 h = texture(tex, uv + vec2( 0.0, -2.0) * ts);
    vec4 i = texture(tex, uv + vec2( 2.0, -2.0) * ts);
    vec4 j = texture(tex, uv + vec2(-1.0,  1.0) * ts);
    vec4 k = texture(tex, uv + vec2( 1.0,  1.0) * ts);
    vec4 l = texture(tex, uv + vec2(-1.0, -1.0) * ts);
    vec4 m = texture(tex, uv + vec2( 1.0, -1.0) * ts);

    return e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
}

#endif /* SHADERS_DOWNSAMPLE_GLSL */
