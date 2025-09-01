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

#endif /* SHADERS_NOISE_GLSL */
