vec4 texel_fetch_2d(in sampler2D map, in vec2 pos, in ivec2 tex_off)
{
    float texel_size_x = 1.0 / float(textureSize(map, 0).x);
    float texel_size_y = 1.0 / float(textureSize(map, 0).y);
    vec2 off = vec2(float(tex_off.x) * texel_size_x, float(tex_off.y) * texel_size_y);
    return texture(map, vec2(pos.xy + off), 0.0);
}

vec4 texel_fetch_2darray(in sampler2DArray map, in vec3 pos, in ivec2 tex_off)
{
    float texel_size_x = 1.0 / float(textureSize(map, 0).x);
    float texel_size_y = 1.0 / float(textureSize(map, 0).y);
    vec2 off = vec2(float(tex_off.x) * texel_size_x, float(tex_off.y) * texel_size_y);
    return texture(map, vec3(pos.xy + off, pos.z), 0.0);
}

#ifndef CONFIG_GLES

vec4 texel_fetch_2dms(in sampler2DMS map, in vec2 pos)
{
    float texel_size_x = textureSize(map).x;
    float texel_size_y = textureSize(map).y;
    vec4 pixel = vec4(0.0);

    for (int i = 0; i < 4; i++)
        pixel += texelFetch(map, ivec2(pos.x * texel_size_x, pos.y * texel_size_y), i);
    pixel /= 4;

    return pixel;
}

vec4 texel_fetch_2dmsarray(in sampler2DMSArray map, in vec3 pos)
{
    float texel_size_x = textureSize(map).x;
    float texel_size_y = textureSize(map).y;
    vec4 pixel = vec4(0.0);

    for (int i = 0; i < 4; i++)
        pixel += texelFetch(map, ivec3(pos.x * texel_size_x, pos.y * texel_size_y, pos.z), i);
    pixel /= 4;

    return pixel;
}

#endif
