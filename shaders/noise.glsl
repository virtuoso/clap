#ifndef SHADERS_NOISE_GLSL
#define SHADERS_NOISE_GLSL

layout (binding=SAMPLER_BINDING_noise3d) uniform sampler3D noise3d;

/* Simple hash-based 3D noise using value noise */
float hash(vec3 p)
{
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

/* Generate noise from vec3 using trilinear interpolation */
float noise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);

    /* cube corners */
    float corners[8] = {
        hash(i + vec3(0.0, 0.0, 0.0)),
        hash(i + vec3(0.0, 0.0, 1.0)),
        hash(i + vec3(0.0, 1.0, 0.0)),
        hash(i + vec3(0.0, 1.0, 1.0)),
        hash(i + vec3(1.0, 0.0, 0.0)),
        hash(i + vec3(1.0, 0.0, 1.0)),
        hash(i + vec3(1.0, 1.0, 0.0)),
        hash(i + vec3(1.0, 1.0, 1.0)),
    };

    /* Smoothstep interpolation */
    vec3 u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(mix(corners[0], corners[4], u.x), mix(corners[2], corners[6], u.x), u.y),
        mix(mix(corners[1], corners[5], u.x), mix(corners[3], corners[7], u.x), u.y),
        u.z
    );
}

/* Fractal Brownian Motion */
float fbm(vec3 p, float amplitude, int octaves, float lacunarity)
{
    float sum = 0.0;
    float amp = amplitude;
    for (int i = 0; i < octaves; i++) {
        sum += amp * noise(p);
        p *= lacunarity;
        amp *= amplitude;
    }
    return sum;
}

/* FBM gradient; eps: epsilon */
vec3 fbm_grad(vec3 p, float eps, float amplitude, int octaves, float lacunarity) {
    vec3 ex = vec3(eps, 0, 0);
    vec3 ey = vec3(0, eps, 0);
    vec3 ez = vec3(0, 0, eps);
    float fx1 = fbm(p + ex, amplitude, octaves, lacunarity);
    float fx0 = fbm(p - ex, amplitude, octaves, lacunarity);
    float fy1 = fbm(p + ey, amplitude, octaves, lacunarity);
    float fy0 = fbm(p - ey, amplitude, octaves, lacunarity);
    float fz1 = fbm(p + ez, amplitude, octaves, lacunarity);
    float fz0 = fbm(p - ez, amplitude, octaves, lacunarity);
    return vec3(fx1 - fx0, fy1 - fy0, fz1 - fz0) / (2.0 * eps);
}

vec3 safe_normalize(vec3 v)
{
    float l2 = dot(v, v);
    return v * inversesqrt(max(l2, 1e-12));
}

vec3 noise_normal(vec3 world_pos, vec3 geom_normal, float amp, float freq, float mask)
{
    float flatness = max(dot(geom_normal, vec3(0.0, 1.0, 0.0)), 0.0);
    freq *= 1.3 - flatness;
    /* scale space; eps proportional to frequency for stable gradients */
    vec3 p = world_pos * freq;
    float eps = 0.5 / freq;

    vec3 g = fbm_grad(p, eps, amp, /*octaves*/3, /*lacunarity*/2.0);

    float ndot = dot(safe_normalize(g), geom_normal);

    /* keep only tangent component so we don't shrink/expand along normal */
    vec3 tgrad = safe_normalize(g - geom_normal * abs(ndot));

    /* perturb — amp is small (e.g. 0.2..0.5), mask gates the effect */
    mask *= max(1.0 - flatness, 0.2);
    vec3 n = safe_normalize(geom_normal - (amp * mask) * tgrad);

    return n;
}

vec3 sample_noise3d(vec3 world_pos, float freq)
{
    vec3 uvw = fract(world_pos * freq); /* TEX_FLT_REPEAT */
    return texture(noise3d, uvw).xyz * 2.0 - 1.0;
}

// project to tangent plane and tilt the normal; robust & cheap
vec3 noise_normal(vec3 world_pos, vec3 geom_normal, float amp, float freq)
{
    const float max_tilt = 0.6;
    vec3 grad = sample_noise3d(world_pos * noise(world_pos), freq);
    // vec3 grad = sample_noise3d(world_pos + 0.15 * noise(world_pos * 0.8), freq);
    // n = normalize(n);
    vec3 t = grad - geom_normal * dot(grad, geom_normal);           // tangent component only
    float tl = length(t);
    if (tl < 1e-5) return geom_normal;
    vec3 tdir = t / tl;
    float s = min(amp, max_tilt);               // e.g., amp=0.25..0.35, tilt<=0.6
    return normalize(geom_normal - s * tdir);
}

float saturate(float x) { return clamp(x, 0.0, 1.0); }

float fog_cloud(vec3 pos, float amp, float freq)
{
    float fog = length(sample_noise3d(pos + noise(pos.zxy), freq));
    return mix(0.0, 1.0, mix(0.0, 1.0, fog)) * amp;
}

float cloud_mask(vec2 luv, float amp, float freq)
{
    // luv = luv * 0.5 + 0.5;
    luv = luv * 2.0 - 1.0;
    // float r = dot(luv, luv);//length(luv);
    float r = length(luv);
    float radial = smoothstep(1.0, 0.0, r);
    // return radial;
    float n = fog_cloud(vec3(luv * 2.5, radial), amp, freq);
    return saturate(radial * mix(0.0, 1.0, n));
}

#endif /* SHADERS_NOISE_GLSL */
