// SPDX-License-Identifier: Apache-2.0
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "clap.h"
#include "error.h"
#include "render.h"

/**
 * display_vk_init() - create a contextless GLFW window and the Vulkan renderer
 * @ctx:        clap context
 * @pwindow:    filled with the created GLFW window
 *
 * Unlike Metal, the rendering surface can't be created here: VkSurfaceKHR
 * requires a VkInstance, which the renderer owns. The window is passed via
 * renderer_init_options instead and render-vk.c creates the surface itself.
 */
cerr display_vk_init(struct clap_context *ctx, GLFWwindow **pwindow)
{
    if (!glfwVulkanSupported()) {
        err("GLFW reports no Vulkan support\n");
        return CERR_INITIALIZATION_FAILED;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    struct clap_config *cfg = clap_get_config(ctx);
    *pwindow = glfwCreateWindow(cfg->width, cfg->height, cfg->title, NULL, NULL);
    if (!*pwindow) {
        err("failed to create GLFW window\n");
        return CERR_INITIALIZATION_FAILED;
    }

    renderer_t *renderer = clap_get_renderer(ctx);
    CERR_RET(renderer_init(renderer, .backend = RENDER_VULKAN, .window = *pwindow), {
        glfwDestroyWindow(*pwindow);
        *pwindow = NULL;
        return __cerr;
    });

    return CERR_OK;
}
