/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_IMGUI_VK_H__
#define __CLAP_UI_IMGUI_VK_H__

/*
 * C++ consumers get the real imgui.h: imgui_impl_vulkan.h needs ImVector
 * templates that cimgui's C structs mode doesn't provide
 */
#ifdef __cplusplus
#include "imgui.h"
#else /* !__cplusplus */
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#endif /* !__cplusplus */

#include "clap.h"

#ifdef __cplusplus
#define EXTERN extern "C"
#else /* !__cplusplus */
#define EXTERN extern
#endif /* !__cplusplus */

/**
 * ui_imgui_vk_init() - initialize imgui's Vulkan backend for the renderer
 * @clap_ctx:   clap context, the owner of the Vulkan renderer
 */
EXTERN void ui_imgui_vk_init(clap_context *clap_ctx);

/**
 * ui_imgui_vk_new_frame() - start an imgui frame, reinitializing on a swapchain format change
 */
EXTERN void ui_imgui_vk_new_frame(void);

/**
 * ui_imgui_vk_render_draw_data() - record imgui's draw data into the frame
 * @draw:   imgui's draw data for this frame
 *
 * A no-op unless the swapchain pass is open: the demos' init pseudo-frame
 * builds (empty) draw data without one.
 */
EXTERN void ui_imgui_vk_render_draw_data(ImDrawData *draw);

/**
 * ui_imgui_vk_shutdown() - tear imgui's Vulkan backend down, after the device is idle
 */
EXTERN void ui_imgui_vk_shutdown(void);

/**
 * ui_imgui_vk_texture_add() - register an image view as an imgui texture ID
 * @image_view:     the VkImageView, as uint64_t to keep this header C-includable
 * @image_layout:   the VkImageLayout the view is sampled in
 *
 * Wraps ImGui_ImplVulkan_AddTexture(): imgui's Vulkan backend wants a
 * VkDescriptorSet as the texture ID for igImage().
 *
 * Return: the descriptor set as uint64_t, 0 if the backend isn't initialized
 */
EXTERN uint64_t ui_imgui_vk_texture_add(uint64_t image_view, int image_layout);

/**
 * ui_imgui_vk_texture_remove() - release a texture ID from ui_imgui_vk_texture_add()
 * @ds:     the descriptor set
 * @epoch:  ui_imgui_vk_texture_epoch() at the time it was added
 *
 * A backend (re)init destroys imgui's descriptor pool and with it every
 * added binding: the epoch identifies the pool generation a binding was
 * allocated from, so stale ones are dropped instead of double-freed.
 */
EXTERN void ui_imgui_vk_texture_remove(uint64_t ds, unsigned int epoch);

/**
 * ui_imgui_vk_texture_epoch() - the current descriptor pool generation
 *
 * Return: a counter that changes on every backend (re)init; a binding added
 *         under another value is dead
 */
EXTERN unsigned int ui_imgui_vk_texture_epoch(void);

#endif /* __CLAP_UI_IMGUI_VK_H__ */
