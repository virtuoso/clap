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

void main()
{
    /* where nothing was rendered the depth reconstructs to the far plane */
    vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, pass_tex);

    float surface_dist = length(view_pos);
    float march_far = min(surface_dist, fog_far);
    float dt = (march_far - near_plane) / float(VOLUMETRIC_STEPS);

    if (dt <= 0.0 || light_volumetric[0] <= 0.0) {
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

    float transmittance = 1.0;
    float scatter = 0.0;

    for (int i = 0; i < VOLUMETRIC_STEPS; i++) {
        float t = near_plane + (float(i) + jitter) * dt;
        vec3 p = origin + world_dir * t;

        float density = volumetric_density
                      + fog_cloud_lod(noise3d, p + vec3(uptime * volumetric_wind), volumetric_amp, volumetric_scale);
        float vis = shadow_sample_visibility(vec4(p, 1.0), -ray_dir.z * t, shadow_vsm);

        scatter += transmittance * vis * density * dt;
        transmittance *= exp(-density * dt);
    }

    /* light_dir[0] is the sun's propagation direction (see scene.c) */
    float mu = dot(world_dir, normalize(-light_dir[0]));
    vec3 inscatter = light_color[0] * light_volumetric[0]
                   * scatter * schlick_phase(mu, volumetric_g) * volumetric_intensity;

    FragColor = vec4(inscatter, transmittance);
}
