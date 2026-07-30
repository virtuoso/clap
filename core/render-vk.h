/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_RENDER_VK_H__
#define __CLAP_RENDER_VK_H__

#include <vulkan/vulkan.h>
#include "render.h"

/*
 * Private interface between render-vk.c and the imgui Vulkan glue, which
 * records into the frame's command buffer and needs the device and
 * swapchain details the backend owns
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * renderer_vk_instance() - the backend's VkInstance
 * @r:  renderer
 *
 * Return: the instance
 */
VkInstance renderer_vk_instance(renderer_t *r);

/**
 * renderer_vk_phys_device() - the physical device the backend picked
 * @r:  renderer
 *
 * Return: the physical device
 */
VkPhysicalDevice renderer_vk_phys_device(renderer_t *r);

/**
 * renderer_vk_device() - the logical device
 * @r:  renderer
 *
 * Return: the device
 */
VkDevice renderer_vk_device(renderer_t *r);

/**
 * renderer_vk_queue_family() - the graphics+present queue family in use
 * @r:  renderer
 *
 * Return: the family index
 */
uint32_t renderer_vk_queue_family(renderer_t *r);

/**
 * renderer_vk_queue() - the one queue the backend submits to
 * @r:  renderer
 *
 * Return: the queue
 */
VkQueue renderer_vk_queue(renderer_t *r);

/**
 * renderer_vk_swapchain_format() - the swapchain's image format
 * @r:  renderer
 *
 * Return: the VkFormat pipelines that render to the swapchain are built for
 */
VkFormat renderer_vk_swapchain_format(renderer_t *r);

/**
 * renderer_vk_swapchain_format_gen() - generation of the swapchain format
 * @r:  renderer
 *
 * Bumped whenever the format changes, so a pipeline baked for the old one
 * knows to rebuild.
 *
 * Return: the generation counter
 */
unsigned int renderer_vk_swapchain_format_gen(renderer_t *r);

/**
 * renderer_vk_min_image_count() - the surface's minimum swapchain image count
 * @r:  renderer
 *
 * Return: the count, never below the 2 the imgui backend requires
 */
uint32_t renderer_vk_min_image_count(renderer_t *r);

/**
 * renderer_vk_image_count() - the swapchain's image count
 * @r:  renderer
 *
 * Return: the number of images, never below renderer_vk_min_image_count()
 */
uint32_t renderer_vk_image_count(renderer_t *r);

/**
 * renderer_vk_cmd_buf() - the command buffer of the frame being recorded
 * @r:  renderer
 *
 * Return: the command buffer
 */
VkCommandBuffer renderer_vk_cmd_buf(renderer_t *r);

/**
 * renderer_vk_pass_open() - whether the swapchain render pass is open
 * @r:  renderer
 *
 * Return: true between renderer_swapchain_begin() and the end of the pass
 */
bool renderer_vk_pass_open(renderer_t *r);

/**
 * renderer_vk_wait_idle() - wait for the device to go idle
 * @r:  renderer
 *
 * A no-op without a device.
 */
void renderer_vk_wait_idle(renderer_t *r);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CLAP_RENDER_VK_H__ */
