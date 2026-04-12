// SPDX-License-Identifier: Apache-2.0
#include <webgpu/webgpu.h>

#undef IMGUI_IMPL_API
#define IMGUI_IMPL_API extern "C"

#include "ui-imgui-wgpu.h"

#include "imgui_impl_wgpu.h"

#include "clap.h"

static renderer_t *renderer;
static unsigned int last_format_gen;

CLAP_API WGPUDevice renderer_device(renderer_t *r);
CLAP_API WGPURenderPassEncoder renderer_pass_encoder(renderer_t *r);
CLAP_API WGPUTextureFormat renderer_swapchain_format(renderer_t *r);
CLAP_API unsigned int renderer_swapchain_format_gen(renderer_t *r);

static void ui_imgui_wgpu_do_init(void)
{
    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = renderer_device(renderer);
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = renderer_swapchain_format(renderer);
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);
    last_format_gen = renderer_swapchain_format_gen(renderer);
}

void ui_imgui_wgpu_init(clap_context *clap_ctx)
{
    renderer = clap_get_renderer(clap_ctx);
    ui_imgui_wgpu_do_init();
}

void ui_imgui_wgpu_new_frame(void)
{
    /*
     * The ImGui WGPU backend bakes the render target format into its pipeline
     * at init time, so a swapchain format flip (HDR toggle) requires a full
     * shutdown/reinit. Detect via the generation counter we expose from the
     * renderer.
     */
    if (renderer_swapchain_format_gen(renderer) != last_format_gen) {
        ImGui_ImplWGPU_Shutdown();
        ui_imgui_wgpu_do_init();
    }
    ImGui_ImplWGPU_NewFrame();
}

void ui_imgui_wgpu_render_draw_data(ImDrawData *draw)
{
    auto encoder = renderer_pass_encoder(renderer);
    ImGui_ImplWGPU_RenderDrawData(draw, encoder);
}
