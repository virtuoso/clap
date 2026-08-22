#version 460 core

#include "shader_constants.h"
#include "texel_fetch.glsl"
#include "view_pos.glsl"

layout (location=0) out vec4 FragColor;
layout (location=0) in vec2 pass_tex;

layout (binding=SAMPLER_BINDING_depth_tex) uniform sampler2D depth_tex;
layout (binding=SAMPLER_BINDING_noise3d) uniform sampler3D noise3d;

#include "ubo_projview.glsl"
#include "ubo_postproc.glsl"
#include "ubo_frame.glsl"

/* shadow_factor_calc() references this model.frag input; never used here */
vec4 world_pos;

#include "shadow.glsl"
#include "noise.glsl"

#define PI 3.1415926538

/*
 * Schlick's approximation of the Henyey-Greenstein phase function:
 * cheaper (no pow(x, 1.5)) and close enough for fog. Peaks at mu = 1
 * (looking into the light) for positive anisotropy.
 */
float schlick_phase(float mu, float g)
{
    float k = 1.55 * g - 0.55 * g * g * g;
    float denom = 1.0 - k * mu;
    return (1.0 - k * k) / (4.0 * PI * denom * denom);
}

/*
 * Closed-form line integral of a point light's attenuation along a
 * ray. The ray is x(s) = x_c + u*s, parameterized so that s = 0 is
 * the point of closest approach to the light and gamma is the
 * distance from the light to the ray line, so d^2(s) = s^2 + gamma^2.
 *
 * Attenuation is the engine's 1 / (a + b*d + c*d^2). For b == 0 the
 * integral is elementary:
 *
 *   integral ds / (a + c*(s^2 + gamma^2)) = integral ds / (q + c*s^2)
 *                                         = atan(k*s) / (q*k),  k = sqrt(c/q),
 *
 * with q = a + c*gamma^2 the denominator at closest approach. A
 * nonzero linear term is folded into q = a + b*gamma + c*gamma^2 --
 * exact at s = 0, a slight overestimate of attenuation elsewhere,
 * which fog doesn't notice. Two useful limits: as c -> 0, k -> 0 and
 * atan(k*s)/(q*k) -> s/q (constant attenuation times length); as the
 * segment grows unbounded the integral tops out at pi/(q*k), which is
 * why the camera can sit inside a beam without blowing out.
 */
float att_segment_integral(vec3 att, float gamma, float s0, float s1)
{
    float q = max(att.x + att.y * gamma + att.z * gamma * gamma, 1e-3);
    float k = sqrt(max(att.z, 1e-5) / q);
    return (atan(k * s1) - atan(k * s0)) / (q * k);
}

/*
 * Clip a ray to a solid single-nappe cone (a spotlight beam): apex L,
 * unit axis pointing down the beam, half-angle theta. A point x is
 * inside iff the angle between (x - L) and the axis is at most theta:
 *
 *   dot(axis, x - L) >= |x - L| * cos(theta)                      (*)
 *
 * For theta < 90 degrees both sides of (*) are nonnegative on the real
 * nappe, so squaring is safe; the mirror nappe also satisfies the
 * squared form but with dot(axis, w) < 0, and is rejected by a
 * half-line test. With x(t) = o + u*t, w(t) = m + u*t, m = o - L,
 * squaring (*) gives the quadratic f(t) = c2*t^2 + c1*t + c0 >= 0:
 *
 *   c2 = au^2 - cc            au = dot(axis, u), cc = cos^2(theta)
 *   c1 = 2*(au*am - cc*bu)    am = dot(axis, m), bu = dot(u, m)
 *   c0 = am^2 - cc*mm         mm = dot(m, m)
 *
 * A solid single nappe is convex, so the ray meets the cone in one
 * interval:
 *   c2 < 0: the ray points steeper than the cone surface, f is a
 *           downward parabola, inside lies between its roots;
 *   c2 > 0: f >= 0 outside the roots -- two pieces, each on one nappe
 *           of the double cone; the half-line keeps the real one;
 *   c2 ~ 0: the ray runs parallel to the surface, f degenerates to a
 *           line.
 * Intervals are encoded as vec2(start, end), empty when start > end;
 * the march range is strictly positive, so vec2(1, 0) is safely empty.
 */
vec2 ray_cone_clip(vec3 m, vec3 u, vec3 axis, float cos_theta, vec2 range)
{
    float cc = cos_theta * cos_theta;
    float au = dot(axis, u);
    float am = dot(axis, m);
    float bu = dot(u, m);
    float mm = dot(m, m);

    float c2 = au * au - cc;
    float c1 = 2.0 * (au * am - cc * bu);
    float c0 = am * am - cc * mm;

    const float eps = 1e-6;
    vec2 quad, quad2 = vec2(1.0, 0.0);

    if (abs(c2) < eps) {
        if (abs(c1) < eps)
            quad = c0 >= 0.0 ? range : vec2(1.0, 0.0);
        else if (c1 > 0.0)
            quad = vec2(-c0 / c1, range.y);
        else
            quad = vec2(range.x, -c0 / c1);
    } else {
        float disc = c1 * c1 - 4.0 * c2 * c0;
        if (disc < 0.0) {
            /* no roots: f keeps one sign along the whole line */
            quad = c2 > 0.0 ? range : vec2(1.0, 0.0);
        } else {
            float sq = sqrt(disc);
            vec2 roots = vec2(-c1 - sq, -c1 + sq) / (2.0 * c2);
            vec2 sorted = vec2(min(roots.x, roots.y), max(roots.x, roots.y));
            if (c2 < 0.0) {
                quad = sorted;
            } else {
                quad = vec2(range.x, sorted.x);
                quad2 = vec2(sorted.y, range.y);
            }
        }
    }

    /* single-nappe half-line: dot(axis, w(t)) = am + au*t >= 0 */
    vec2 nappe;
    if (abs(au) < eps)
        nappe = am >= 0.0 ? range : vec2(1.0, 0.0);
    else if (au > 0.0)
        nappe = vec2(-am / au, range.y);
    else
        nappe = vec2(range.x, -am / au);

    vec2 seg = vec2(max(max(quad.x, nappe.x), range.x),
                    min(min(quad.y, nappe.y), range.y));
    if (seg.x > seg.y)
        /* the other piece of the c2 > 0 case, if it's the real nappe */
        seg = vec2(max(max(quad2.x, nappe.x), range.x),
                   min(min(quad2.y, nappe.y), range.y));

    return seg;
}

void main()
{
    /* where nothing was rendered the depth reconstructs to the far plane */
    vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, pass_tex);

    float surface_dist = length(view_pos);
    float march_far = min(surface_dist, fog_far);
    float dt = (march_far - near_plane) / float(VOLUMETRIC_STEPS);

    if (dt <= 0.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 ray_dir = view_pos / surface_dist;
    vec3 origin = inverse_view[3].xyz;
    vec3 world_dir = mat3(inverse_view) * ray_dir;

    /*
     * Per-pixel jitter of the march start hides the step count; the
     * frame counter reseeds it so TAA (or the eye) averages it away.
     * The wrap keeps the seed exact in float.
     */
    float jitter = hash(vec3(gl_FragCoord.xy, float(frame_count & 1023u)));

    bool sun_on = light_volumetric[0] > 0.0;
    float transmittance = 1.0;
    float scatter_dir = 0.0;
    vec3 scatter_loc = vec3(0.0);

    if (sun_on) {
        for (int i = 0; i < VOLUMETRIC_STEPS; i++) {
            float t = near_plane + (float(i) + jitter) * dt;
            vec3 p = origin + world_dir * t;

            float density = volumetric_density
                          + fog_cloud_lod(noise3d, p + vec3(uptime * volumetric_wind), volumetric_amp, volumetric_scale);
            float vis = shadow_sample_visibility(vec4(p, 1.0), -ray_dir.z * t, shadow_vsm);

            scatter_dir += transmittance * vis * density * dt;
            transmittance *= exp(-density * dt);
        }
    } else {
        /* alpha still carries transmittance: homogeneous-medium estimate */
        transmittance = exp(-(volumetric_density + 0.5 * volumetric_amp) * (march_far - near_plane));
    }

    /*
     * Local (point/spot) lights, integrated analytically:
     *
     *   L = color * factor * (1/4pi) * integral T(t) rho(t) att(d(t)) dt
     *
     * over the beam-clipped segment. att integrates in closed form
     * (att_segment_integral()); T and rho vary much more slowly than
     * att across a beam, so both are frozen at the closest approach
     * inside the segment: rho at the full noisy density (dust wisps
     * modulate the beam), T with a homogeneous-medium estimate.
     * Point-sampling this from the march instead dissolves distant
     * cones (a ~3m beam sampled every ~4m) and saturates from inside;
     * the closed form is bounded and step-free. The sun is index 0;
     * other plain directional lights don't scatter.
     */
    for (int l = 1; l <= VOLUMETRIC_LOCAL_LIGHTS_MAX; l++) {
        if (l >= nr_lights || light_volumetric[l] <= 0.0)
            continue;

        bool spot = light_cutoff[l] > 0.0 && light_directional[l];
        if (light_directional[l] && !spot)
            continue;

        vec3 m = origin - light_pos[l];
        vec2 range = vec2(near_plane, march_far);
        float integral;

        /* s = t + beta puts s = 0 at the closest approach */
        float beta = dot(m, world_dir);
        float gamma = sqrt(max(dot(m, m) - beta * beta, 0.0));
        vec2 seg = range;

        if (spot) {
            /* stored light_dir points at the light; the beam axis away from it */
            vec3 axis = normalize(-light_dir[l]);
            /*
             * Soft edge: integrate the hard inner and outer cones and
             * take the midpoint -- a trapezoid estimate of the
             * smoothstep band spotlight_intensity() uses
             */
            const float soft = radians(5.0);
            vec2 inner = ray_cone_clip(m, world_dir, axis, cos(light_cutoff[l]), range);
            vec2 outer = ray_cone_clip(m, world_dir, axis, cos(light_cutoff[l] + soft), range);
            if (outer.x >= outer.y)
                continue;

            float i_inner = inner.x < inner.y ?
                att_segment_integral(attenuation[l], gamma, inner.x + beta, inner.y + beta) : 0.0;
            float i_outer =
                att_segment_integral(attenuation[l], gamma, outer.x + beta, outer.y + beta);
            integral = 0.5 * (i_inner + i_outer);
            seg = outer;
        } else {
            integral = att_segment_integral(attenuation[l], gamma, seg.x + beta, seg.y + beta);
        }

        /* density and transmittance at the closest approach in-segment */
        float t_c = clamp(-beta, seg.x, seg.y);
        vec3 p_c = origin + world_dir * t_c;
        float rho = volumetric_density
                  + fog_cloud_lod(noise3d, p_c + vec3(uptime * volumetric_wind), volumetric_amp, volumetric_scale);
        float T_c = exp(-(volumetric_density + 0.5 * volumetric_amp) * t_c);

        scatter_loc += light_color[l] * light_volumetric[l] * rho * T_c * integral;
    }

    vec3 inscatter = scatter_loc * (1.0 / (4.0 * PI)) * volumetric_local_intensity;
    if (sun_on) {
        /* light_dir[0] is the sun's propagation direction (see scene.c) */
        float mu = dot(world_dir, normalize(-light_dir[0]));
        inscatter += light_color[0] * light_volumetric[0]
                   * scatter_dir * schlick_phase(mu, volumetric_g) * volumetric_intensity;
    }

    FragColor = vec4(inscatter, transmittance);
}
