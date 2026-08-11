// SPDX-License-Identifier: Apache-2.0
#include <vulkan/vulkan.h>

#include "ui-imgui-vk.h"

/* C++ linkage, unlike the other backends: its API has overloads */
#include "imgui_impl_vulkan.h"

#include "clap.h"
#include "render-vk.h"

static renderer_t *renderer;
static unsigned int last_format_gen;

/* imgui keeps the pointer to the color format past Init() */
static VkFormat swapchain_format;

/*
 * Texture bindings (ImGui_ImplVulkan_AddTexture()) live in the backend's
 * descriptor pool, which every (re)init recreates: the epoch tells the
 * bindings of dead pool generations apart so they aren't double-freed
 */
static unsigned int texture_epoch;
static bool backend_ready;

static void ui_imgui_vk_do_init(void)
{
    swapchain_format = renderer_vk_swapchain_format(renderer);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_3;
    init_info.Instance = renderer_vk_instance(renderer);
    init_info.PhysicalDevice = renderer_vk_phys_device(renderer);
    init_info.Device = renderer_vk_device(renderer);
    init_info.QueueFamily = renderer_vk_queue_family(renderer);
    init_info.Queue = renderer_vk_queue(renderer);
    /* let the backend create its own descriptor pool */
    init_info.DescriptorPoolSize = 64;
    init_info.MinImageCount = renderer_vk_min_image_count(renderer);
    init_info.ImageCount = renderer_vk_image_count(renderer);
    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    auto &rendering_info = init_info.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &swapchain_format;
    ImGui_ImplVulkan_Init(&init_info);
    last_format_gen = renderer_vk_swapchain_format_gen(renderer);
    texture_epoch++;
    backend_ready = true;
}

static void ui_imgui_vk_do_shutdown(void)
{
    backend_ready = false;
    renderer_vk_wait_idle(renderer);
    ImGui_ImplVulkan_Shutdown();
}

uint64_t ui_imgui_vk_texture_add(uint64_t image_view, int image_layout)
{
    if (!backend_ready) return 0;

    return (uint64_t)(uintptr_t)ImGui_ImplVulkan_AddTexture((VkImageView)(uintptr_t)image_view,
                                                            (VkImageLayout)image_layout);
}

void ui_imgui_vk_texture_remove(uint64_t ds, unsigned int epoch)
{
    /* a stale epoch means the binding died with its descriptor pool */
    if (!backend_ready || !ds || epoch != texture_epoch)    return;

    ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)(uintptr_t)ds);
}

unsigned int ui_imgui_vk_texture_epoch(void)    { return texture_epoch; }

void ui_imgui_vk_init(clap_context *clap_ctx)
{
    renderer = clap_get_renderer(clap_ctx);
    ui_imgui_vk_do_init();
}

void ui_imgui_vk_new_frame(void)
{
    /*
     * The render target format is baked into the imgui pipeline at init
     * time, so a swapchain format flip (HDR toggle) requires a full
     * shutdown/reinit, same as the WGPU backend
     */
    if (renderer_vk_swapchain_format_gen(renderer) != last_format_gen) {
        ui_imgui_vk_do_shutdown();
        ui_imgui_vk_do_init();
    }
    ImGui_ImplVulkan_NewFrame();
}

void ui_imgui_vk_render_draw_data(ImDrawData *draw)
{
    /*
     * The demos' init pseudo-frame runs without a swapchain pass and
     * RenderDrawData() records commands unconditionally: gate it on the
     * pass actually being open
     */
    if (!renderer_vk_pass_open(renderer))   return;

    ImGui_ImplVulkan_RenderDrawData(draw, renderer_vk_cmd_buf(renderer));
}

void ui_imgui_vk_shutdown(void)     { ui_imgui_vk_do_shutdown(); }
