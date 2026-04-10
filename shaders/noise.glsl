#ifndef SHADERS_NOISE_GLSL
#define SHADERS_NOISE_GLSL

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

/*
 * Sample a baked periodic fBm gradient texture at @world_pos scaled by @freq.
 * The 3D noise is stored as the normalized gradient of periodic fBm packed to
 * RGB8, so the return value is a signed unit-ish vector in [-1, 1]^3 and the
 * texture's REPEAT wrap mode tiles cleanly across the period bound at bake
 * time (see core/noise.c).
 */
vec3 sample_noise3d(sampler3D tex, vec3 world_pos, float freq)
{
    return texture(tex, world_pos * freq).xyz * 2.0 - 1.0;
}

vec3 safe_normalize(vec3 v)
{
    float l2 = dot(v, v);
    return v * inversesqrt(max(l2, 1e-12));
}

/*
 * On-the-fly fBm gradient normal perturbation. Expensive (6 fbm() calls
 * per pixel), but non-periodic — the only mode that produces convincing
 * stone-surface detail without visible tiling, at the cost of aliasing
 * on distant samples. @amp scales the tilt, @freq scales the sampling
 * space (smaller == smoother).
 */
vec3 noise_normal_gpu(vec3 world_pos, vec3 geom_normal, float amp, float freq)
{
    // flatness was once used to reduce the frequency of the floor-ish
    // surfaces, but it has to be parameterized to be generally useful
    // float flatness = max(dot(geom_normal, vec3(0.0, 1.0, 0.0)), 0.0);
    float flatness = 0.0;
    freq *= 1.3 - flatness;
    /* eps proportional to frequency for stable central-difference gradients */
    vec3 p = world_pos * freq;
    float eps = 0.5 / freq;

    vec3 g = fbm_grad(p, eps, amp, /*octaves*/3, /*lacunarity*/2.0);

    /* keep only the tangent component so we don't shrink/expand along normal */
    float ndot = dot(safe_normalize(g), geom_normal);
    vec3 tgrad = safe_normalize(g - geom_normal * abs(ndot));

    /* perturbation is stronger on steep surfaces, clamped at 20% on flat ground */
    float mask = max(1.0 - flatness, 0.2);
    return safe_normalize(geom_normal - (amp * mask) * tgrad);
}

/*
 * Baked periodic fBm gradient normal perturbation. Cheap, tileable
 * alternative to noise_normal_gpu(): good for large-scale diffuse
 * surface variation, but the texture's period can become visible on
 * highly specular materials.
 */
vec3 noise_normal_3d(sampler3D tex, vec3 world_pos, vec3 geom_normal, float amp, float freq)
{
    const float max_tilt = 0.6;
    /* jitter sampling position by a scalar hash to blur the tiling period */
    vec3 grad = sample_noise3d(tex, world_pos * noise(world_pos), freq);
    vec3 t = grad - geom_normal * dot(grad, geom_normal);
    float tl = length(t);
    if (tl < 1e-5) return geom_normal;
    vec3 tdir = t / tl;
    float s = min(amp, max_tilt);
    return safe_normalize(geom_normal - s * tdir);
}

#endif /* SHADERS_NOISE_GLSL */
