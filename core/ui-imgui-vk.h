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

#endif /* __CLAP_UI_IMGUI_VK_H__ */
