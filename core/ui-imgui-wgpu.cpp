// SPDX-License-Identifier: Apache-2.0
#include <webgpu/webgpu.h>

#undef IMGUI_IMPL_API
#define IMGUI_IMPL_API extern "C"

#include "ui-imgui-wgpu.h"

#include "imgui_impl_wgpu.h"

#include "clap.h"

static renderer_t *renderer;

CLAP_API WGPUDevice renderer_device(renderer_t *r);
CLAP_API WGPURenderPassEncoder renderer_pass_encoder(renderer_t *r);
CLAP_API WGPUTextureFormat renderer_swapchain_format(renderer_t *r);

void ui_imgui_wgpu_init(clap_context *clap_ctx)
{
    renderer = clap_get_renderer(clap_ctx);

    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = renderer_device(renderer);
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = renderer_swapchain_format(renderer);
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);
}

void ui_imgui_wgpu_new_frame(void)
{
    ImGui_ImplWGPU_NewFrame();
}

void ui_imgui_wgpu_render_draw_data(ImDrawData *draw)
{
    auto encoder = renderer_pass_encoder(renderer);
    ImGui_ImplWGPU_RenderDrawData(draw, encoder);
}
