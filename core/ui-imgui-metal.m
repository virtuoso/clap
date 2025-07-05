// SPDX-License-Identifier: Apache-2.0
#import <Metal/Metal.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "imgui_impl_metal.h"

#include "clap.h"

static renderer_t *renderer;
static struct ImGuiIO *io;

/* These are private between here and render-mtl.m */
extern void *renderer_device(renderer_t *renderer);
extern void *renderer_cmd_buffer(renderer_t *r);
extern void *renderer_cmd_encoder(renderer_t *r);
extern void *renderer_screen_desc(renderer_t *r);

void ui_imgui_metal_init(clap_context *clap_ctx)
{
    renderer = clap_get_renderer(clap_ctx);
    ImGui_ImplMetal_Init(renderer_device(renderer));
}

void ui_imgui_metal_new_frame()
{
    MTLRenderPassDescriptor *render_pass_desc = renderer_screen_desc(renderer);
    ImGui_ImplMetal_NewFrame(render_pass_desc);
}

void ui_imgui_metal_render_draw_data(ImDrawData *draw)
{
    ImGui_ImplMetal_RenderDrawData(
        draw,
        renderer_cmd_buffer(renderer),
        renderer_cmd_encoder(renderer)
    );
}

void ui_imgui_metal_shutdown()
{
    ImGui_ImplMetal_Shutdown();
}
