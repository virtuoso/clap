#ifndef CLAP_TEXEL_FETCH_GLSL
#define CLAP_TEXEL_FETCH_GLSL

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

vec4 texel_fetch_2dms_sample(in sampler2DMS map, in vec2 pos, in int idx)
{
    float texel_size_x = textureSize(map).x;
    float texel_size_y = textureSize(map).y;
    return texelFetch(map, ivec2(pos.x * texel_size_x, pos.y * texel_size_y), idx);
}

vec4 texel_fetch_2dms_array_sample(in sampler2DMSArray map, in vec3 pos, in int idx)
{
    float texel_size_x = textureSize(map).x;
    float texel_size_y = textureSize(map).y;
    return texelFetch(map, ivec3(pos.x * texel_size_x, pos.y * texel_size_y, pos.z), idx);
}

/* MSAA resolving algorithms */
vec3 weighted_msaa(in sampler2DMS map, vec2 pos)
{
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;

    float mean = 0.0;
    float variance = 0.0;

    int numSamples = MSAA_SAMPLES;
    vec3 samples[MSAA_SAMPLES];
    for (int i = 0; i < numSamples; i++) {
        samples[i] = texel_fetch_2dms_sample(map, pos, i).rgb;
        mean += samples[i].r;
    }
    mean /= numSamples;

    // Compute variance to detect sharp edges
    for (int i = 0; i < numSamples; i++) {
        variance += pow(samples[i].r - mean, 2);
    }
    variance /= numSamples;

    // Adaptive weight: More variance = less weight (preserves detail)
    float adaptWeight = exp(-variance * 16.0); // Adjust factor based on sharpness needed

    for (int i = 0; i < numSamples; i++) {
        float weight = mix(1.0, adaptWeight, variance);
        sum += samples[i] * weight;
        weightSum += weight;
    }

    return sum / weightSum;
}

vec3 tent_msaa(in sampler2DMS map, vec2 pos)
{
    float weights[4] = float[](0.125, 0.375, 0.375, 0.125); // 4x MSAA tent weights
    vec3 sum = vec3(0.0);

    for (int i = 0; i < MSAA_SAMPLES; i++)
        sum += texel_fetch_2dms_sample(map, pos, i).rgb * weights[i];

    return sum;
}

vec3 clamped_msaa(in sampler2DMS map, vec2 pos)
{
    vec3 center = texel_fetch_2dms_sample(map, pos, 0).rgb;
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        vec3 sample_ = texel_fetch_2dms_sample(map, pos, i).rgb;
        float weight = 1.0 / (1.0 + length(sample_ - center) * 8.0); // Reduce weight for outliers
        sum += sample_ * weight;
        weightSum += weight;
    }

    return sum / weightSum;
}

vec3 box_msaa(in sampler2DMS map, in vec2 pos)
{
    vec3 pixel = vec3(0.0);

    for (int i = 0; i < MSAA_SAMPLES; i++)
        pixel += texel_fetch_2dms_sample(map, pos, i).rgb;
    pixel /= MSAA_SAMPLES;

    return pixel;
}

vec4 texel_fetch_2dms(in sampler2DMS map, in vec2 pos)
{
    /* XXX: parameterize selection of algorithms */
    return vec4(weighted_msaa(map, pos), 1.0);
    //return vec4(box_msaa(map, pos), 1.0);
    //return vec4(clamped_msaa(map, pos), 1.0);
    //return vec4(tent_msaa(map, pos), 1.0);
}

vec4 texel_fetch_2dmsarray(in sampler2DMSArray map, in vec3 pos)
{
    vec4 pixel = vec4(0.0);

    for (int i = 0; i < MSAA_SAMPLES; i++)
        pixel += texel_fetch_2dms_array_sample(map, pos, i);
    pixel /= MSAA_SAMPLES;

    return pixel;
}

#endif

#endif /* CLAP_TEXEL_FETCH_GLSL */
