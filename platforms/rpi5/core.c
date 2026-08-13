// SPDX-License-Identifier: Apache-2.0
/*
 * Raspberry Pi 5 (BCM2712 / V3D 7.1) platform constraints.
 *
 * The GPU is fragment-bound on this engine's scenes and the V3D compiler
 * drops from 4- to 2-thread mode under register pressure (a ~2x
 * throughput divisor the uber model shader always hits), so the defaults
 * trade eye candy for staying interactive; each knob below is backed by
 * measurements on ldjam56. Pair with the "rpi5" cmake preset, which
 * covers the compile-time half: CLAP_BUILD_TAA=OFF (drops the RG16F
 * velocity RT) and CLAP_SHADER_NO_FP16 (v3dv has no shaderFloat16).
 */
#include "pipeline.h"
#include "platform.h"

static void rpi5_init_render_options(render_options *ropts)
{
    /*
     * The VSM moment math is a register/instruction hog in the model
     * pass; plain CSM/PCF sampling measures a dead heat on the total
     * frame with far less pressure, and scales per-screen-pixel rather
     * than per-map-texel
     */
    ropts->shadow_vsm = false;
    ropts->ssao = false;
    /* the edge pass alone is ~11 ms of a ~90 ms full-size frame */
    ropts->edges = false;
    ropts->edge_antialiasing = false;
    /* fp16 RTs buy nothing here and tonemapping math isn't free */
    ropts->hdr = false;
    ropts->film_grain = false;
    /* fragment-bound: model pass cost scales ~linearly with area */
    ropts->render_scale = 0.7f;
}

static const clap_platform rpi5_platform = {
    .name                   = "rpi5",
    .init_render_options    = rpi5_init_render_options,
    /* cascades 2+ cost full shadow passes with little visible return */
    .shadow_passes_max      = 2,
};

void platform_init(void)
{
    platform_register(&rpi5_platform);
}
