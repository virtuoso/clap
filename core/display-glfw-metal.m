// SPDX-License-Identifier: Apache-2.0
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include "clap.h"
#include "error.h"
#include "util.h"

unsigned int metal_refresh_rate(GLFWwindow *window)
{
    NSWindow *nswindow = glfwGetCocoaWindow(window);
    return nswindow.screen.maximumFramesPerSecond;
}

bool metal_supports_edr(GLFWwindow *window)
{
    NSWindow *nswindow = glfwGetCocoaWindow(window);
    return nswindow.screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 2.0;
}

cerr display_metal_init(struct clap_context *ctx, GLFWwindow **pwindow)
{
    const id<MTLDevice> gpu = MTLCreateSystemDefaultDevice();
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = gpu;
    layer.opaque = YES;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    struct clap_config *cfg = clap_get_config(ctx);
    *pwindow = glfwCreateWindow(cfg->width, cfg->height, cfg->title, NULL, NULL);
    if (!pwindow) {
        err("failed to create GLFW window\n");
        return CERR_INITIALIZATION_FAILED;
    }

    NSWindow *nswindow = glfwGetCocoaWindow(*pwindow);
    nswindow.contentView.layer = layer;
    nswindow.contentView.wantsLayer = YES;

    renderer_t *renderer = clap_get_renderer(ctx);
    renderer_init(renderer, .device = gpu, .layer = layer);

    return CERR_OK;
}
