// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "display.h"
#include "render.h"
#undef IMPLEMENTOR

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string.h>

#include "common.h"
#include "logger.h"
#include "memory.h"
#include "render-vk.h"
#include "util.h"

/*
 * Vulkan renderer backend
 *
 * Modeled on render-wgpu.c (pass/pipeline/binding flow) and render-mtl.m
 * (frames in flight, swapchain pacing).
 *
 * Frame-state machine: cb_open (command buffer recording), image_acquired
 * (swapchain image owned by this frame), pass_open (inside the swapchain
 * render pass). renderer_frame_begin() is idempotent and
 * renderer_frame_end() auto-ends an open pass and submits with or without
 * present, because the engine's edge paths demand it: the demos' init
 * pseudo-frame wraps loading-screen mini-frames that never call
 * renderer_swapchain_end().
 */

static const renderer_caps vk_renderer_caps = {
    .renderer           = RENDER_VULKAN,
    .ndc_y_down         = true,
    .ndc_z_zero_one     = true,
    .origin_top_left    = true,
};

static const renderer_caps *vk_renderer_get_caps(void)    { return &vk_renderer_caps; }

static void vk_mat4x4_ortho(mat4x4 M, float l, float r, float b, float t, float n, float f)
{
    mat4x4_ortho_ndc_z_1(M, l, r, b, t, n, f);
}

static void vk_mat4x4_perspective(mat4x4 m, float y_fov, float aspect, float n, float f)
{
    mat4x4_perspective_ndc_z_1(m, y_fov, aspect, n, f);
}

/****************************************************************************
 * Instance / device bring-up
 ****************************************************************************/

/** VK_API_FLOOR - the lowest Vulkan version the backend runs on */
#define VK_API_FLOOR            VK_API_VERSION_1_3
/** VK_VALIDATION_LAYER - the layer enabled when the loader has it */
#define VK_VALIDATION_LAYER     "VK_LAYER_KHRONOS_validation"

/** VK_INIT_FAILED - an initialization failure carrying the VkResult of the call that failed */
#define VK_INIT_FAILED(_what, _res) \
    CERR_INITIALIZATION_FAILED_REASON(.fmt = _what " failed: %d", .arg0 = (void *)(intptr_t)(_res))

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                  VkDebugUtilsMessageTypeFlagsEXT type,
                                                  const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                  void *user)
{
    /* err_on() honors -E / abort_on_error */
    err_on(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "vulkan: %s\n", data->pMessage);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) warn("vulkan: %s\n", data->pMessage);

    return VK_FALSE;
}

static bool vk_validation_available(void)
{
    uint32_t count = 0;

    if (vkEnumerateInstanceLayerProperties(&count, NULL) != VK_SUCCESS || !count)   return false;

    LOCAL_SET(void, props_buf) = mem_alloc(sizeof(VkLayerProperties), .nr = count, .zero = true);
    VkLayerProperties *props = props_buf;
    if (!props) return false;

    vkEnumerateInstanceLayerProperties(&count, props);

    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(props[i].layerName, VK_VALIDATION_LAYER))   return true;

    return false;
}

static const VkDebugUtilsMessengerCreateInfoEXT vk_messenger_info = {
    .sType              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity    = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
    .messageType        = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback    = vk_debug_cb,
};

static cerr vk_instance_create(renderer_t *r)
{
    uint32_t nr_glfw_exts = 0;
    const char **glfw_exts = glfwGetRequiredInstanceExtensions(&nr_glfw_exts);

    if (!glfw_exts || !nr_glfw_exts)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "GLFW reports no Vulkan surface extensions");

    r->vk.validation = vk_validation_available();

    /* GLFW's surface extensions, plus maybe debug utils */
    LOCAL_SET(void, exts_buf) = mem_alloc(sizeof(const char *), .nr = nr_glfw_exts + 1);
    const char **exts = exts_buf;
    if (!exts)  return CERR_NOMEM;

    uint32_t nr_exts = 0;
    for (uint32_t i = 0; i < nr_glfw_exts; i++) exts[nr_exts++] = glfw_exts[i];
    if (r->vk.validation)   exts[nr_exts++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    const char *layers[] = { VK_VALIDATION_LAYER };
    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "clap",
        .pEngineName        = "clap",
        .apiVersion         = VK_API_FLOOR,
    };
    VkInstanceCreateInfo info = {
        .sType                      = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        /* covers vkCreateInstance/vkDestroyInstance themselves */
        .pNext                      = r->vk.validation ? &vk_messenger_info : NULL,
        .pApplicationInfo           = &app_info,
        .enabledExtensionCount      = nr_exts,
        .ppEnabledExtensionNames    = exts,
        .enabledLayerCount          = r->vk.validation ? 1 : 0,
        .ppEnabledLayerNames        = r->vk.validation ? layers : NULL,
    };

    VkResult res = vkCreateInstance(&info, NULL, &r->vk.instance);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("vkCreateInstance", res);

    if (r->vk.validation) {
        PFN_vkCreateDebugUtilsMessengerEXT create_messenger =
            (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(r->vk.instance, "vkCreateDebugUtilsMessengerEXT");

        if (create_messenger)   create_messenger(r->vk.instance, &vk_messenger_info, NULL, &r->vk.messenger);
        msg("vulkan: validation layer enabled\n");
    }

    return CERR_OK;
}

/*
 * Queue family that supports both graphics and presenting to the surface;
 * a shared one always exists on the hardware clap targets
 */
static bool vk_queue_family_pick(renderer_t *r, VkPhysicalDevice dev, uint32_t *out)
{
    uint32_t count = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, NULL);
    if (!count) return false;

    LOCAL_SET(void, families_buf) = mem_alloc(sizeof(VkQueueFamilyProperties), .nr = count);
    VkQueueFamilyProperties *families = families_buf;
    if (!families)  return false;

    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families);

    for (uint32_t i = 0; i < count; i++) {
        VkBool32 present = VK_FALSE;

        if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))                                      continue;

        if (vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, r->vk.surface, &present) != VK_SUCCESS)    continue;

        if (present) {
            *out = i;
            return true;
        }
    }

    return false;
}

static void vk_limits_fill(renderer_t *r, const VkPhysicalDeviceLimits *l)
{
    int *lim = r->vk.limits;

    lim[RENDER_LIMIT_MAX_TEXTURE_SIZE]              = l->maxImageDimension2D;
    lim[RENDER_LIMIT_MAX_TEXTURE_UNITS]             = l->maxPerStageDescriptorSampledImages;
    lim[RENDER_LIMIT_MAX_TEXTURE_ARRAY_LAYERS]      = l->maxImageArrayLayers;
    lim[RENDER_LIMIT_MAX_COLOR_ATTACHMENTS]         = min(l->maxColorAttachments, FBO_COLOR_ATTACHMENTS_MAX);
    lim[RENDER_LIMIT_MAX_COLOR_TEXTURE_SAMPLES]     = 1 << (fls(l->framebufferColorSampleCounts) - 1);
    lim[RENDER_LIMIT_MAX_DEPTH_TEXTURE_SAMPLES]     = 1 << (fls(l->framebufferDepthSampleCounts) - 1);
    lim[RENDER_LIMIT_MAX_SAMPLES]                   = min(lim[RENDER_LIMIT_MAX_COLOR_TEXTURE_SAMPLES],
                                                          lim[RENDER_LIMIT_MAX_DEPTH_TEXTURE_SAMPLES]);
    lim[RENDER_LIMIT_MAX_DRAW_BUFFERS]              = lim[RENDER_LIMIT_MAX_COLOR_ATTACHMENTS];
    lim[RENDER_LIMIT_MAX_ANISOTROPY]                = (int)l->maxSamplerAnisotropy;
    lim[RENDER_LIMIT_MAX_UBO_SIZE]                  = l->maxUniformBufferRange;
    lim[RENDER_LIMIT_MAX_UBO_BINDINGS]              = l->maxPerStageDescriptorUniformBuffers;
    lim[RENDER_LIMIT_MAX_VERTEX_UNIFORM_BLOCKS]     = l->maxPerStageDescriptorUniformBuffers;
    lim[RENDER_LIMIT_MAX_GEOMETRY_UNIFORM_BLOCKS]   = l->maxPerStageDescriptorUniformBuffers;
    lim[RENDER_LIMIT_MAX_FRAGMENT_UNIFORM_BLOCKS]   = l->maxPerStageDescriptorUniformBuffers;
}

static cerr vk_physical_device_pick(renderer_t *r)
{
    uint32_t count = 0;
    VkResult res = vkEnumeratePhysicalDevices(r->vk.instance, &count, NULL);

    if (res != VK_SUCCESS || !count)    return CERR_NOT_SUPPORTED_REASON(.fmt = "no Vulkan devices found");

    LOCAL_SET(void, devs_buf) = mem_alloc(sizeof(VkPhysicalDevice), .nr = count);
    VkPhysicalDevice *devs = devs_buf;
    if (!devs)                                      return CERR_NOMEM;

    res = vkEnumeratePhysicalDevices(r->vk.instance, &count, devs);
    if (res != VK_SUCCESS && res != VK_INCOMPLETE)  return VK_INIT_FAILED("vkEnumeratePhysicalDevices", res);

    int best_score = 0;
    VkPhysicalDeviceProperties best_props = {};

    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        uint32_t queue_family;

        vkGetPhysicalDeviceProperties(devs[i], &props);

        if (props.apiVersion < VK_API_FLOOR)    continue;

        VkPhysicalDeviceVulkan13Features f13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        };
        VkPhysicalDeviceFeatures2 f2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &f13,
        };
        vkGetPhysicalDeviceFeatures2(devs[i], &f2);
        if (!f13.dynamicRendering)                                              continue;

        if (!vk_queue_family_pick(r, devs[i], &queue_family))                   continue;

        int score = 1;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)           score = 3;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)    score = 2;

        if (score > best_score) {
            best_score = score;
            best_props = props;
            r->vk.phys_device = devs[i];
            r->vk.queue_family = queue_family;
        }
    }

    if (!best_score)
        return CERR_NOT_SUPPORTED_REASON(
            .fmt = "no Vulkan 1.3 device with dynamic rendering and a graphics+present queue"
        );

    vk_limits_fill(r, &best_props.limits);
    msg("vulkan: using '%s', API %d.%d.%d\n", best_props.deviceName,
        VK_API_VERSION_MAJOR(best_props.apiVersion),
        VK_API_VERSION_MINOR(best_props.apiVersion),
        VK_API_VERSION_PATCH(best_props.apiVersion));

    return CERR_OK;
}

static cerr vk_device_create(renderer_t *r)
{
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qinfo = {
        .sType              = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex   = r->vk.queue_family,
        .queueCount         = 1,
        .pQueuePriorities   = &prio,
    };
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering   = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 f2 = {
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext              = &f13,
    };
    const char *exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo info = {
        .sType                      = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                      = &f2,
        .queueCreateInfoCount       = 1,
        .pQueueCreateInfos          = &qinfo,
        .enabledExtensionCount      = array_size(exts),
        .ppEnabledExtensionNames    = exts,
    };

    VkResult res = vkCreateDevice(r->vk.phys_device, &info, NULL, &r->vk.device);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("vkCreateDevice", res);

    vkGetDeviceQueue(r->vk.device, r->vk.queue_family, 0, &r->vk.queue);

    return CERR_OK;
}

/* Transient pools: every command buffer is recorded once and reset wholesale */
static cerr vk_command_pool_create(renderer_t *r, VkCommandPool *pool)
{
    VkCommandPoolCreateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags              = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex   = r->vk.queue_family,
    };

    if (vkCreateCommandPool(r->vk.device, &info, NULL, pool) != VK_SUCCESS)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreateCommandPool failed");

    return CERR_OK;
}

static cerr vk_command_buffer_alloc(renderer_t *r, VkCommandPool pool, VkCommandBuffer *cb)
{
    VkCommandBufferAllocateInfo info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(r->vk.device, &info, cb) != VK_SUCCESS)
        return CERR_NOMEM_REASON(.fmt = "vkAllocateCommandBuffers failed");

    return CERR_OK;
}

/* Start recording a one-time submit */
static VkResult vk_command_buffer_begin(VkCommandBuffer cb)
{
    VkCommandBufferBeginInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    return vkBeginCommandBuffer(cb, &info);
}

static cerr vk_semaphore_create(renderer_t *r, VkSemaphore *sem)
{
    VkSemaphoreCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    if (vkCreateSemaphore(r->vk.device, &info, NULL, sem) != VK_SUCCESS)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreateSemaphore failed");

    return CERR_OK;
}

static cerr vk_frames_init(renderer_t *r)
{
    for (unsigned int i = 0; i < VK_INFLIGHT_FRAMES; i++) {
        auto f = &r->vk.frames[i];

        CERR_RET_CERR(vk_command_pool_create(r, &f->cmd_pool));
        CERR_RET_CERR(vk_command_buffer_alloc(r, f->cmd_pool, &f->cmd_buf));

        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        if (vkCreateFence(r->vk.device, &fence_info, NULL, &f->fence) != VK_SUCCESS)
            return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreateFence failed");

        CERR_RET_CERR(vk_semaphore_create(r, &f->acquire_sem));
    }

    return vk_command_pool_create(r, &r->vk.upload_pool);
}

/****************************************************************************
 * Memory and synchronous uploads
 ****************************************************************************/

static bool vk_memory_type_find(renderer_t *r, uint32_t type_bits, VkMemoryPropertyFlags props,
                                uint32_t *out)
{
    VkPhysicalDeviceMemoryProperties mem_props;

    vkGetPhysicalDeviceMemoryProperties(r->vk.phys_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
        if ((type_bits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            *out = i;
            return true;
        }

    return false;
}

/* Dedicated allocation per resource: fine at the engine's allocation counts */
static cerr vk_memory_alloc(renderer_t *r, const VkMemoryRequirements *reqs,
                            VkMemoryPropertyFlags props, VkDeviceMemory *mem)
{
    uint32_t type;

    if (!vk_memory_type_find(r, reqs->memoryTypeBits, props, &type))
        return CERR_NOT_SUPPORTED_REASON(.fmt = "no suitable memory type");

    VkMemoryAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize     = reqs->size,
        .memoryTypeIndex    = type,
    };

    VkResult res = vkAllocateMemory(r->vk.device, &alloc_info, NULL, mem);
    if (res != VK_SUCCESS)  return CERR_NOMEM;

    return CERR_OK;
}

static cerr vk_buffer_create(renderer_t *r, VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem)
{
    VkBufferCreateInfo info = {
        .sType          = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size           = size,
        .usage          = usage,
        .sharingMode    = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkResult res = vkCreateBuffer(r->vk.device, &info, NULL, buf);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("vkCreateBuffer", res);

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(r->vk.device, *buf, &reqs);

    CERR_RET(vk_memory_alloc(r, &reqs, props, mem), {
        vkDestroyBuffer(r->vk.device, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return __cerr;
    });

    res = vkBindBufferMemory(r->vk.device, *buf, *mem, 0);
    if (res != VK_SUCCESS) {
        vkDestroyBuffer(r->vk.device, *buf, NULL);
        vkFreeMemory(r->vk.device, *mem, NULL);
        *buf = VK_NULL_HANDLE;
        *mem = VK_NULL_HANDLE;
        return VK_INIT_FAILED("vkBindBufferMemory", res);
    }

    return CERR_OK;
}

/* Host-visible transfer source holding a copy of @data */
static cerr vk_staging_create(renderer_t *r, const void *data, size_t size, VkBuffer *buf, VkDeviceMemory *mem)
{
    CERR_RET_CERR(vk_buffer_create(r, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   buf, mem));

    void *mapped;
    if (vkMapMemory(r->vk.device, *mem, 0, size, 0, &mapped) != VK_SUCCESS) {
        vkDestroyBuffer(r->vk.device, *buf, NULL);
        vkFreeMemory(r->vk.device, *mem, NULL);
        *buf = VK_NULL_HANDLE;
        *mem = VK_NULL_HANDLE;
        return CERR_INVALID_OPERATION_REASON(.fmt = "vkMapMemory failed");
    }
    memcpy(mapped, data, size);
    vkUnmapMemory(r->vk.device, *mem);

    return CERR_OK;
}

/* Single-level optimally tiled image with a dedicated device-local allocation */
static cerr vk_image_create(renderer_t *r, VkImageType type, VkFormat format, VkExtent3D extent,
                            uint32_t nr_layers, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                            VkImage *image, VkDeviceMemory *mem)
{
    VkImageCreateInfo info = {
        .sType          = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType      = type,
        .format         = format,
        .extent         = extent,
        .mipLevels      = 1,
        .arrayLayers    = nr_layers,
        .samples        = samples,
        .tiling         = VK_IMAGE_TILING_OPTIMAL,
        .usage          = usage,
        .sharingMode    = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult res = vkCreateImage(r->vk.device, &info, NULL, image);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("vkCreateImage", res);

    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(r->vk.device, *image, &reqs);

    CERR_RET(vk_memory_alloc(r, &reqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem), {
        vkDestroyImage(r->vk.device, *image, NULL);
        *image = VK_NULL_HANDLE;
        return __cerr;
    });

    res = vkBindImageMemory(r->vk.device, *image, *mem, 0);
    if (res != VK_SUCCESS) {
        vkDestroyImage(r->vk.device, *image, NULL);
        vkFreeMemory(r->vk.device, *mem, NULL);
        *image = VK_NULL_HANDLE;
        *mem = VK_NULL_HANDLE;
        return VK_INIT_FAILED("vkBindImageMemory", res);
    }

    return CERR_OK;
}

static cerr vk_image_view_create(renderer_t *r, VkImage image, VkImageViewType type, VkFormat format,
                                 VkImageAspectFlags aspect, uint32_t nr_layers, VkImageView *view)
{
    VkImageViewCreateInfo info = {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image      = image,
        .viewType   = type,
        .format     = format,
        .subresourceRange = {
            .aspectMask = aspect,
            .levelCount = 1,
            .layerCount = nr_layers,
        },
    };

    if (vkCreateImageView(r->vk.device, &info, NULL, view) != VK_SUCCESS)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreateImageView failed");

    return CERR_OK;
}

static void vk_image_layout_transition(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect,
                                       uint32_t nr_levels, uint32_t nr_layers,
                                       VkImageLayout from, VkImageLayout to,
                                       VkAccessFlags src_access, VkAccessFlags dst_access,
                                       VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask          = src_access,
        .dstAccessMask          = dst_access,
        .oldLayout              = from,
        .newLayout              = to,
        .srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED,
        .image                  = image,
        .subresourceRange       = {
            .aspectMask = aspect,
            .levelCount = nr_levels,
            .layerCount = nr_layers,
        },
    };

    vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static VkRenderingAttachmentInfo vk_attachment_info(VkImageView view, VkImageLayout layout,
                                                    VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op,
                                                    VkClearValue clear)
{
    return (VkRenderingAttachmentInfo){
        .sType          = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView      = view,
        .imageLayout    = layout,
        .loadOp         = load_op,
        .storeOp        = store_op,
        .clearValue     = clear,
    };
}

static cerr vk_upload_begin(renderer_t *r, VkCommandBuffer *cb)
{
    CERR_RET_CERR(vk_command_buffer_alloc(r, r->vk.upload_pool, cb));

    VkResult res = vk_command_buffer_begin(*cb);
    if (res != VK_SUCCESS) {
        vkFreeCommandBuffers(r->vk.device, r->vk.upload_pool, 1, cb);
        *cb = VK_NULL_HANDLE;
        return VK_INIT_FAILED("vkBeginCommandBuffer", res);
    }

    return CERR_OK;
}

static cerr vk_upload_end(renderer_t *r, VkCommandBuffer cb)
{
    VkResult res = vkEndCommandBuffer(cb);
    if (res != VK_SUCCESS) {
        vkFreeCommandBuffers(r->vk.device, r->vk.upload_pool, 1, &cb);
        return VK_INIT_FAILED("vkEndCommandBuffer", res);
    }

    VkSubmitInfo submit_info = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cb,
    };
    res = vkQueueSubmit(r->vk.queue, 1, &submit_info, VK_NULL_HANDLE);
    if (res == VK_SUCCESS)  vkQueueWaitIdle(r->vk.queue);

    vkFreeCommandBuffers(r->vk.device, r->vk.upload_pool, 1, &cb);

    if (res != VK_SUCCESS)
        return CERR_INVALID_OPERATION_REASON(
            .fmt    = "upload vkQueueSubmit failed: %d",
            .arg0   = (void *)(intptr_t)res
        );

    return CERR_OK;
}

/****************************************************************************
 * Swapchain
 ****************************************************************************/

static void vk_swapchain_destroy_resources(renderer_t *r)
{
    for (unsigned int i = 0; i < r->vk.nr_images; i++) {
        if (r->vk.views[i])         vkDestroyImageView(r->vk.device, r->vk.views[i], NULL);
        if (r->vk.render_sems[i])   vkDestroySemaphore(r->vk.device, r->vk.render_sems[i], NULL);
        r->vk.views[i] = VK_NULL_HANDLE;
        r->vk.render_sems[i] = VK_NULL_HANDLE;
        r->vk.images[i] = VK_NULL_HANDLE;
    }
    r->vk.nr_images = 0;
}

/* BGRA8 sRGB, RGBA8 sRGB or whatever comes first; VK_FORMAT_UNDEFINED if the surface has nothing */
static VkSurfaceFormatKHR vk_surface_format_pick(renderer_t *r)
{
    VkSurfaceFormatKHR none = { .format = VK_FORMAT_UNDEFINED };
    uint32_t count = 0;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(r->vk.phys_device, r->vk.surface, &count, NULL) != VK_SUCCESS || !count)
        return none;

    LOCAL_SET(void, formats_buf) = mem_alloc(sizeof(VkSurfaceFormatKHR), .nr = count);
    VkSurfaceFormatKHR *formats = formats_buf;
    if (!formats)   return none;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(r->vk.phys_device, r->vk.surface, &count, formats) != VK_SUCCESS)
        return none;

    for (uint32_t i = 0; i < count; i++)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];

    for (uint32_t i = 0; i < count; i++)
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return formats[i];

    return formats[0];
}

static cerr vk_swapchain_create(renderer_t *r)
{
    VkSurfaceCapabilitiesKHR caps;
    VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->vk.phys_device, r->vk.surface, &caps);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", res);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xffffffff) {
        extent.width = clamp((unsigned int)r->width, caps.minImageExtent.width,
                             caps.maxImageExtent.width);
        extent.height = clamp((unsigned int)r->height, caps.minImageExtent.height,
                              caps.maxImageExtent.height);
    }

    /* Zero-sized surface (minimized window): nothing to create, stay dirty and retry later */
    if (!extent.width || !extent.height)        return CERR_OK;

    VkSurfaceFormatKHR format = vk_surface_format_pick(r);
    if (format.format == VK_FORMAT_UNDEFINED)   return CERR_NOT_SUPPORTED_REASON(.fmt = "no surface formats");

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    if (image_count > VK_SWAPCHAIN_IMAGES_MAX)                  image_count = VK_SWAPCHAIN_IMAGES_MAX;

    VkCompositeAlphaFlagsKHR calpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & calpha))
        calpha = caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha;

    VkSwapchainKHR old = r->vk.swapchain;
    VkSwapchainCreateInfoKHR info = {
        .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface            = r->vk.surface,
        .minImageCount      = image_count,
        .imageFormat        = format.format,
        .imageColorSpace    = format.colorSpace,
        .imageExtent        = extent,
        .imageArrayLayers   = 1,
        .imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform       = caps.currentTransform,
        .compositeAlpha     = calpha,
        .presentMode        = VK_PRESENT_MODE_FIFO_KHR,
        .clipped            = VK_TRUE,
        .oldSwapchain       = old,
    };

    res = vkCreateSwapchainKHR(r->vk.device, &info, NULL, &r->vk.swapchain);
    if (old)    vkDestroySwapchainKHR(r->vk.device, old, NULL);
    if (res != VK_SUCCESS) {
        r->vk.swapchain = VK_NULL_HANDLE;
        return VK_INIT_FAILED("vkCreateSwapchainKHR", res);
    }

    uint32_t nr_images = 0;
    vkGetSwapchainImagesKHR(r->vk.device, r->vk.swapchain, &nr_images, NULL);
    if (nr_images > VK_SWAPCHAIN_IMAGES_MAX)
        return CERR_NOT_SUPPORTED_REASON(
            .fmt    = "%d swapchain images, max %d",
            .arg0   = (void *)(uintptr_t)nr_images,
            .arg1   = (void *)(uintptr_t)VK_SWAPCHAIN_IMAGES_MAX
        );
    vkGetSwapchainImagesKHR(r->vk.device, r->vk.swapchain, &nr_images, r->vk.images);
    r->vk.nr_images = nr_images;

    for (unsigned int i = 0; i < nr_images; i++) {
        CERR_RET_CERR(vk_image_view_create(r, r->vk.images[i], VK_IMAGE_VIEW_TYPE_2D, format.format,
                                           VK_IMAGE_ASPECT_COLOR_BIT, 1, &r->vk.views[i]));
        CERR_RET_CERR(vk_semaphore_create(r, &r->vk.render_sems[i]));
    }

    if (r->vk.swapchain_format != format.format) {
        r->vk.swapchain_format = format.format;
        r->swapchain_gen++;
        draw_control_cache_drop(r, NULL);
    }
    r->vk.min_image_count = caps.minImageCount;
    r->vk.sc_width = extent.width;
    r->vk.sc_height = extent.height;
    r->vk.swapchain_dirty = false;

    dbg("vulkan: swapchain %ux%u, %u images, format %d\n",
        extent.width, extent.height, nr_images, format.format);

    return CERR_OK;
}

static cerr vk_swapchain_recreate(renderer_t *r)
{
    vkDeviceWaitIdle(r->vk.device);
    vk_swapchain_destroy_resources(r);

    return vk_swapchain_create(r);
}

static int vk_renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    if ((unsigned int)limit >= RENDER_LIMIT_MAX)    return 0;

    return renderer->vk.limits[limit];
}

static cerr vk_renderer_init(renderer_t *renderer, const renderer_init_options *opts)   { return CERR_OK; }

static void vk_renderer_done(renderer_t *r)
{
    if (r->vk.device) {
        vkDeviceWaitIdle(r->vk.device);

        vk_swapchain_destroy_resources(r);
        if (r->vk.swapchain)    vkDestroySwapchainKHR(r->vk.device, r->vk.swapchain, NULL);

        for (unsigned int i = 0; i < VK_INFLIGHT_FRAMES; i++) {
            auto f = &r->vk.frames[i];

            if (f->fence)       vkDestroyFence(r->vk.device, f->fence, NULL);
            if (f->acquire_sem) vkDestroySemaphore(r->vk.device, f->acquire_sem, NULL);
            if (f->cmd_pool)    vkDestroyCommandPool(r->vk.device, f->cmd_pool, NULL);
        }

        if (r->vk.upload_pool)  vkDestroyCommandPool(r->vk.device, r->vk.upload_pool, NULL);

        vkDestroyDevice(r->vk.device, NULL);
    }

    if (!r->vk.instance)    return;

    if (r->vk.surface)      vkDestroySurfaceKHR(r->vk.instance, r->vk.surface, NULL);

    if (r->vk.messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger =
            (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(r->vk.instance, "vkDestroyDebugUtilsMessengerEXT");

        if (destroy_messenger)  destroy_messenger(r->vk.instance, r->vk.messenger, NULL);
    }

    vkDestroyInstance(r->vk.instance, NULL);
}

static void vk_renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    if ((unsigned int)width != r->vk.sc_width || (unsigned int)height != r->vk.sc_height)
        r->vk.swapchain_dirty = true;

    r->x = x;
    r->y = y;
    r->width = width;
    r->height = height;
}

static void vk_renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight)
{
    if (px)         *px = r->x;
    if (py)         *py = r->y;
    if (pwidth)     *pwidth = r->width;
    if (pheight)    *pheight = r->height;
}

static void vk_renderer_hdr_enable(renderer_t *r, bool enable)
{
}

static void vk_renderer_frame_begin(renderer_t *r)
{
    /* Idempotent: loading-screen mini-frames re-enter an open pseudo-frame */
    if (r->vk.cb_open)  return;

    auto f = &r->vk.frames[r->vk.frame_idx];

    /* a frame whose submit failed never signals its fence: don't wait for it */
    if (f->submitted) {
        vkWaitForFences(r->vk.device, 1, &f->fence, VK_TRUE, UINT64_MAX);
        f->submitted = false;
    }
    vkResetCommandPool(r->vk.device, f->cmd_pool, 0);

    if (vk_command_buffer_begin(f->cmd_buf) != VK_SUCCESS) {
        err("vulkan: vkBeginCommandBuffer failed\n");
        return;
    }

    r->vk.cb_open = true;
}

static void vk_renderer_swapchain_begin(renderer_t *r)
{
    if (!r->vk.cb_open) vk_renderer_frame_begin(r);
    if (!r->vk.cb_open || r->vk.pass_open)  return;

    auto f = &r->vk.frames[r->vk.frame_idx];

    if (r->vk.swapchain_dirty) {
        CERR_RET(vk_swapchain_recreate(r), { err_cerr(__cerr, "vulkan: swapchain recreation failed\n"); return; });
        /* still dirty: a zero-sized (minimized) surface, nothing to present to */
        if (r->vk.swapchain_dirty)  return;
    }

    uint32_t idx;
    VkResult res = vkAcquireNextImageKHR(r->vk.device, r->vk.swapchain, UINT64_MAX,
                                         f->acquire_sem, VK_NULL_HANDLE, &idx);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        CERR_RET(vk_swapchain_recreate(r), { err_cerr(__cerr, "vulkan: swapchain recreation failed\n"); return; });
        if (r->vk.swapchain_dirty)  return;
        res = vkAcquireNextImageKHR(r->vk.device, r->vk.swapchain, UINT64_MAX,
                                    f->acquire_sem, VK_NULL_HANDLE, &idx);
    }
    if (res == VK_SUBOPTIMAL_KHR) {
        /* Still presentable; recreate at the next opportunity */
        r->vk.swapchain_dirty = true;
    } else if (res != VK_SUCCESS) {
        warn("vulkan: vkAcquireNextImageKHR failed: %d\n", res);
        return;
    }

    r->vk.image_idx = idx;
    r->vk.image_acquired = true;

    /*
     * UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL; chains after the acquire
     * semaphore wait (COLOR_ATTACHMENT_OUTPUT) at submit time
     */
    vk_image_layout_transition(f->cmd_buf, r->vk.images[idx], VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo color = vk_attachment_info(
        r->vk.views[idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        (VkClearValue){ .color = { .float32 = { 0.0, 0.0, 0.0, 1.0 } } }
    );
    VkRenderingInfo rendering_info = {
        .sType                  = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea             = { .extent = { r->vk.sc_width, r->vk.sc_height } },
        .layerCount             = 1,
        .colorAttachmentCount   = 1,
        .pColorAttachments      = &color,
    };
    vkCmdBeginRendering(f->cmd_buf, &rendering_info);
    r->vk.pass_open = true;

    /* Negative height: y-up clip space, same convention as WGPU/Metal */
    VkViewport viewport = {
        .y          = (float)r->vk.sc_height,
        .width      = (float)r->vk.sc_width,
        .height     = -(float)r->vk.sc_height,
        .maxDepth   = 1.0,
    };
    vkCmdSetViewport(f->cmd_buf, 0, 1, &viewport);

    VkRect2D scissor = { .extent = { r->vk.sc_width, r->vk.sc_height } };
    vkCmdSetScissor(f->cmd_buf, 0, 1, &scissor);
}

static void vk_renderer_swapchain_end(renderer_t *r)
{
    if (!r->vk.pass_open)   return;

    vkCmdEndRendering(r->vk.frames[r->vk.frame_idx].cmd_buf);
    r->vk.pass_open = false;
}

static void vk_renderer_frame_end(renderer_t *r)
{
    if (!r->vk.cb_open) return;

    auto f = &r->vk.frames[r->vk.frame_idx];

    /* The loading screen never calls renderer_swapchain_end() */
    vk_renderer_swapchain_end(r);

    if (r->vk.image_acquired)
        vk_image_layout_transition(f->cmd_buf, r->vk.images[r->vk.image_idx], VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    if (vkEndCommandBuffer(f->cmd_buf) != VK_SUCCESS)   err("vulkan: vkEndCommandBuffer failed\n");

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount     = r->vk.image_acquired ? 1 : 0,
        .pWaitSemaphores        = &f->acquire_sem,
        .pWaitDstStageMask      = &wait_stage,
        .commandBufferCount     = 1,
        .pCommandBuffers        = &f->cmd_buf,
        .signalSemaphoreCount   = r->vk.image_acquired ? 1 : 0,
        .pSignalSemaphores      = &r->vk.render_sems[r->vk.image_idx],
    };
    vkResetFences(r->vk.device, 1, &f->fence);
    VkResult res = vkQueueSubmit(r->vk.queue, 1, &submit_info, f->fence);
    warn_on(res != VK_SUCCESS, "vulkan: vkQueueSubmit failed: %d\n", res);
    f->submitted = res == VK_SUCCESS;

    if (r->vk.image_acquired && res == VK_SUCCESS) {
        VkPresentInfoKHR present_info = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &r->vk.render_sems[r->vk.image_idx],
            .swapchainCount     = 1,
            .pSwapchains        = &r->vk.swapchain,
            .pImageIndices      = &r->vk.image_idx,
        };
        res = vkQueuePresentKHR(r->vk.queue, &present_info);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)    r->vk.swapchain_dirty = true;
        else if (res != VK_SUCCESS)    warn("vulkan: vkQueuePresentKHR failed: %d\n", res);

        dbg_once("vulkan: first present\n");
    }

    r->vk.frame_idx = (r->vk.frame_idx + 1) % VK_INFLIGHT_FRAMES;
    r->vk.cb_open = false;
    r->vk.image_acquired = false;
}

static void vk_renderer_debug(renderer_t *r)
{
}

static void vk_renderer_cull_face(renderer_t *r, cull_face cull)                    {}
static void vk_renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor) { r->blend = _blend; }

static cerr vk_renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                             data_type idx_type, unsigned int nr_instances)
{
    return CERR_NOT_SUPPORTED_REASON(.fmt = "vulkan: draw not implemented yet");
}

/****************************************************************************
 * Buffers
 ****************************************************************************/

static cerr vk_buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    return ref_embed(buffer, buf);
}

static void vk_buffer_deinit(buffer_t *buf)
{
}

static void vk_buffer_bind(buffer_t *buf, uniform_t loc)
{
}

static void vk_buffer_unbind(buffer_t *buf, uniform_t loc)      {}

static void vk_buffer_set_name(buffer_t *buf, const char *name) {}

/****************************************************************************
 * Vertex arrays
 ****************************************************************************/

static cerr vk_vertex_array_init(vertex_array_t *va, renderer_t *r)
{
    return ref_embed(vertex_array, va, .renderer = r);
}
static void vk_vertex_array_done(vertex_array_t *va)                {}
static void vk_vertex_array_bind(vertex_array_t *va)                {}
static void vk_vertex_array_unbind(vertex_array_t *va)              {}

/****************************************************************************
 * Draw controls
 ****************************************************************************/

static cerr vk_draw_control_init(draw_control_t *dc, const draw_control_init_options *opts)
{
    return CERR_OK;
}

static void vk_draw_control_done(draw_control_t *dc)
{
}

static void vk_draw_control_bind(draw_control_t *dc)     {}
static void vk_draw_control_unbind(draw_control_t *dc)   {}

/****************************************************************************
 * Textures
 ****************************************************************************/

static VkFormat vk_texture_format(texture_format format)
{
    switch (format) {
        case TEX_FMT_R32F:      return VK_FORMAT_R32_SFLOAT;
        case TEX_FMT_R16F:      return VK_FORMAT_R16_SFLOAT;
        case TEX_FMT_R8:        return VK_FORMAT_R8_UNORM;

        case TEX_FMT_RG32F:     return VK_FORMAT_R32G32_SFLOAT;
        case TEX_FMT_RG16F:     return VK_FORMAT_R16G16_SFLOAT;
        case TEX_FMT_RG16:      return VK_FORMAT_R16G16_UNORM;
        case TEX_FMT_RG8:       return VK_FORMAT_R8G8_UNORM;

        /* no 3-component sampled formats on desktop GPUs: alpha gets added */
        case TEX_FMT_RGB32F:    return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TEX_FMT_RGB16F:    return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TEX_FMT_RGB8:      return VK_FORMAT_R8G8B8A8_UNORM;
        case TEX_FMT_RGB8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;

        case TEX_FMT_RGBA32F:   return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TEX_FMT_RGBA16:    return VK_FORMAT_R16G16B16A16_UNORM;
        case TEX_FMT_RGBA16F:   return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TEX_FMT_RGBA8:     return VK_FORMAT_R8G8B8A8_UNORM;
        case TEX_FMT_RGBA8_SRGB:return VK_FORMAT_R8G8B8A8_SRGB;

        /* BGRA10XR is a Metal EDR format with no Vulkan equivalent */
        case TEX_FMT_BGR10A2:   return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case TEX_FMT_BGRA8:     return VK_FORMAT_B8G8R8A8_UNORM;

        case TEX_FMT_DEPTH16F:  return VK_FORMAT_D16_UNORM;
        /* D24 isn't universal (RADV); D32 is a strict superset */
        case TEX_FMT_DEPTH24F:  return VK_FORMAT_D32_SFLOAT;
        case TEX_FMT_DEPTH32F:  return VK_FORMAT_D32_SFLOAT;

        case TEX_FMT_R32UI:     return VK_FORMAT_R32_UINT;
        case TEX_FMT_RG32UI:    return VK_FORMAT_R32G32_UINT;
        case TEX_FMT_RGBA32UI:  return VK_FORMAT_R32G32B32A32_UINT;

        default:                break;
    }

    return VK_FORMAT_UNDEFINED;
}

static bool vk_format_is_depth(texture_format format)
{
    return format == TEX_FMT_DEPTH16F || format == TEX_FMT_DEPTH24F || format == TEX_FMT_DEPTH32F;
}

static VkImageAspectFlags vk_texture_aspect(texture_t *tex)
{
    return vk_format_is_depth(tex->vk.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

static VkSamplerAddressMode vk_sampler_address_mode(texture_wrap wrap)
{
    switch (wrap) {
        case TEX_CLAMP_TO_EDGE:         return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case TEX_CLAMP_TO_BORDER:       return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case TEX_WRAP_REPEAT:           return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case TEX_WRAP_MIRRORED_REPEAT:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default:                        break;
    }

    clap_unreachable();
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

static VkFilter vk_filter(texture_filter filter)
{
    switch (filter) {
        case TEX_FLT_LINEAR:    return VK_FILTER_LINEAR;
        case TEX_FLT_NEAREST:   return VK_FILTER_NEAREST;
        default:                break;
    }

    clap_unreachable();
    return VK_FILTER_NEAREST;
}

static VkImageType vk_image_type(texture_type type)
{
    switch (type) {
        case TEX_1D:        return VK_IMAGE_TYPE_1D;
        case TEX_2D:
        case TEX_2D_ARRAY:  return VK_IMAGE_TYPE_2D;
        case TEX_3D:        return VK_IMAGE_TYPE_3D;
        default:            break;
    }

    clap_unreachable();
    return VK_IMAGE_TYPE_2D;
}

static VkImageViewType vk_image_view_type(texture_type type)
{
    switch (type) {
        case TEX_1D:        return VK_IMAGE_VIEW_TYPE_1D;
        case TEX_2D:        return VK_IMAGE_VIEW_TYPE_2D;
        case TEX_2D_ARRAY:  return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TEX_3D:        return VK_IMAGE_VIEW_TYPE_3D;
        default:            break;
    }

    clap_unreachable();
    return VK_IMAGE_VIEW_TYPE_2D;
}

static bool vk_texture_format_supported(renderer_t *r, texture_format format)
{
    return vk_texture_format(format) != VK_FORMAT_UNDEFINED;
}

static cerr vk_texture_init(texture_t *tex, const texture_init_options *opts)
{
    if (!vk_texture_format_supported(opts->renderer, opts->format))
        return CERR_NOT_SUPPORTED_REASON(
            .fmt    = "format %s (%ld) not supported",
            .arg0   = texture_format_string(opts->format),
            .arg1   = (const void *)(uintptr_t)opts->format
        );

    CERR_RET_CERR(ref_embed(texture, tex));

    tex->renderer = opts->renderer;
    tex->vk.type = opts->type;
    tex->vk.format = opts->format;
    tex->vk.render_target = opts->render_target;
    tex->width = 0;
    tex->height = 0;
    tex->layers = opts->layers;
    tex->multisampled = opts->multisampled;

    /* sampler state is immutable, create it up front */
    VkSamplerCreateInfo sampler_info = {
        .sType          = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter      = vk_filter(opts->mag_filter),
        .minFilter      = vk_filter(opts->min_filter),
        .mipmapMode     = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU   = vk_sampler_address_mode(opts->wrap),
        .addressModeV   = vk_sampler_address_mode(opts->wrap),
        .addressModeW   = vk_sampler_address_mode(opts->wrap),
        /* TODO: custom border colors for the shadow path */
        .borderColor    = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    if (vkCreateSampler(tex->renderer->vk.device, &sampler_info, NULL, &tex->vk.sampler) != VK_SUCCESS)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreateSampler failed");

    return CERR_OK;
}

static void vk_texture_release(texture_t *tex)
{
    auto r = tex->renderer;

    if (tex->vk.view) {
        vkDestroyImageView(r->vk.device, tex->vk.view, NULL);
        tex->vk.view = VK_NULL_HANDLE;
    }
    if (tex->vk.image) {
        vkDestroyImage(r->vk.device, tex->vk.image, NULL);
        tex->vk.image = VK_NULL_HANDLE;
    }
    if (tex->vk.mem) {
        vkFreeMemory(r->vk.device, tex->vk.mem, NULL);
        tex->vk.mem = VK_NULL_HANDLE;
    }
}

static void vk_texture_deinit(texture_t *tex)
{
    if (!tex || !tex->renderer) return;

    /*
     * Immediate destruction: nothing samples textures yet, so no in-flight
     * frame can still reference one
     */
    vk_texture_release(tex);
    if (tex->vk.sampler) {
        vkDestroySampler(tex->renderer->vk.device, tex->vk.sampler, NULL);
        tex->vk.sampler = VK_NULL_HANDLE;
    }

    tex->width = 0;
    tex->height = 0;
    tex->layers = 0;
    tex->multisampled = false;
    tex->loaded = false;
}

static cerr vk_texture_create(texture_t *tex, unsigned int width, unsigned int height)
{
    auto r = tex->renderer;
    VkFormat format = vk_texture_format(tex->vk.format);
    bool is_3d = tex->vk.type == TEX_3D;
    /* array layers or 3D depth, whichever the type calls for */
    uint32_t nr_layers = is_3d ? 1 : max(tex->layers, 1u);
    VkExtent3D extent = { width, height, is_3d ? max(tex->layers, 1u) : 1 };

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (tex->vk.render_target)
        usage |= vk_format_is_depth(tex->vk.format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                    : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    /* release previous, if resizing */
    vk_texture_release(tex);

    CERR_RET_CERR(vk_image_create(r, vk_image_type(tex->vk.type), format, extent, nr_layers,
                                  tex->multisampled ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT, usage,
                                  &tex->vk.image, &tex->vk.mem));

    return vk_image_view_create(r, tex->vk.image, vk_image_view_type(tex->vk.type), format,
                                vk_texture_aspect(tex), nr_layers, &tex->vk.view);
}

static cerr vk_texture_load(texture_t *tex, texture_format format,
                            unsigned int width, unsigned int height, void *buf)
{
    auto r = tex->renderer;
    LOCAL_SET(void, converted) = NULL;

    if (buf && texture_format_needs_alpha(format)) {
        size_t rgba_size;

        converted = texture_rgb_to_rgba(format, buf,
                                        (size_t)width * height * max(tex->layers, 1u),
                                        &rgba_size);
        if (!converted) return CERR_NOMEM;
        buf = converted;
        format = texture_format_add_alpha(format);
    }

    tex->vk.format = format;
    tex->width = width;
    tex->height = height;

    CERR_RET_CERR(vk_texture_create(tex, width, height));

    VkImageAspectFlags aspect = vk_texture_aspect(tex);
    unsigned int depth = tex->vk.type == TEX_3D ? max(tex->layers, 1u) : 1;
    unsigned int layers = tex->vk.type != TEX_3D ? max(tex->layers, 1u) : 1;

    VkCommandBuffer cb;
    CERR_RET_CERR(vk_upload_begin(r, &cb));

    if (buf) {
        size_t bpp = texture_format_comp_size(format) * texture_format_nr_comps(format);
        size_t size = (size_t)width * height * depth * layers * bpp;
        VkBuffer staging;
        VkDeviceMemory staging_mem;

        CERR_RET(vk_staging_create(r, buf, size, &staging, &staging_mem),
                 { vk_upload_end(r, cb); return __cerr; });

        vk_image_layout_transition(cb, tex->vk.image, aspect,
                                   VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS,
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy copy = {
            .imageSubresource   = {
                .aspectMask     = aspect,
                .layerCount     = layers,
            },
            .imageExtent        = { width, height, depth },
        };
        vkCmdCopyBufferToImage(cb, staging, tex->vk.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        vk_image_layout_transition(cb, tex->vk.image, aspect,
                                   VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        cerr end_err = vk_upload_end(r, cb);

        vkDestroyBuffer(r->vk.device, staging, NULL);
        vkFreeMemory(r->vk.device, staging_mem, NULL);
        CERR_RET_CERR(end_err);
    } else {
        /* no data: still leave the image in a sampleable layout */
        vk_image_layout_transition(cb, tex->vk.image, aspect,
                                   VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS,
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   0, VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        CERR_RET_CERR(vk_upload_end(r, cb));
    }

    tex->loaded = true;

    return CERR_OK;
}

static cerr vk_texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!texture_loaded(tex))   return CERR_TEXTURE_NOT_LOADED;

    tex->width = width;
    tex->height = height;

    return vk_texture_create(tex, width, height);
}

static void vk_texture_bind(texture_t *tex, unsigned int target, uniform_t uniform)
{
}

static void vk_texture_unbind(texture_t *tex, unsigned int target)
{
}

static bool vk_texture_is_array(texture_t *tex)                     { return tex && tex->vk.type == TEX_2D_ARRAY; }
static void vk_texture_set_name(texture_t *tex, const char *name)   {}

static texture_t *vk_texture_clone(texture_t *tex)
{
    if (!tex)   return NULL;

    texture_t *clone = ref_new(texture);
    if (!clone) return NULL;

    clone->renderer = tex->renderer;
    clone->vk.type = tex->vk.type;
    clone->vk.format = tex->vk.format;
    clone->width = tex->width;
    clone->height = tex->height;
    clone->layers = tex->layers;
    clone->multisampled = tex->multisampled;
    clone->vk.render_target = false;
    clone->loaded = tex->loaded;
    /*
     * Take over the GPU objects: the cloned original is an embedded ref
     * that texture_done() deliberately skips deiniting, which makes the
     * clone the sole owner from here on
     */
    clone->vk.image = tex->vk.image;
    clone->vk.mem = tex->vk.mem;
    clone->vk.view = tex->vk.view;
    clone->vk.sampler = tex->vk.sampler;

    return clone;
}

static texid_t vk_texture_id(texture_t *tex)
{
    return 0;
}

/****************************************************************************
 * Framebuffers
 ****************************************************************************/

static void vk_fbo_prepare(fbo_t *fbo)
{
}

static void vk_fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
}

static cerr vk_fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    return CERR_OK;
}

static bool vk_fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment)    { return false; }

static texture_format vk_fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment)
{
    return TEX_FMT_DEFAULT;
}

static texture_t *vk_fbo_texture(fbo_t *fbo, fbo_attachment attachment)
{
    return NULL;
}

static texture_format vk_fbo_texture_format(fbo_t *fbo, fbo_attachment attachment)
{
    return TEX_FMT_DEFAULT;
}

static bool vk_fbo_texture_supported(renderer_t *r, texture_format format)
{
    return format < TEX_FMT_MAX;
}

static cresp(fbo_t) vk_fbo_create(const fbo_init_options *opts)
{
    return cresp_error(fbo_t, CERR_NOT_SUPPORTED);
}

static void vk_fbo_destroy(fbo_t *fbo)
{
}

/****************************************************************************
 * Uniform buffers
 ****************************************************************************/

cerr_check _uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                               unsigned int count, const void *value);

static cerr vk_uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name,
                                   int binding)
{
    CERR_RET_CERR(ref_embed(uniform_buffer, ubo, .binding = binding));

    ubo->vk.binding = binding;

    return CERR_OK;
}

static void vk_uniform_buffer_free(uniform_buffer_t *ubo)
{
    if (!ubo->data) return;

    mem_free(ubo->data);
    ubo->data = NULL;
}

static void vk_uniform_buffer_done(uniform_buffer_t *ubo)       { vk_uniform_buffer_free(ubo); }
static void vk_uniform_buffer_destroy(uniform_buffer_t *ubo)    { vk_uniform_buffer_free(ubo); }

static cerr vk_uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size)
{
    ubo->data = mem_alloc(size, .zero = true);
    if (!ubo->data) return CERR_NOMEM;

    ubo->size = size;

    return CERR_OK;
}

static cerr vk_uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points)   { return CERR_OK; }

static void vk_uniform_buffer_update(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
}

static cerr vk_uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset,
                                  size_t *size, unsigned int count, const void *value)
{
    return _uniform_buffer_set(ubo, type, offset, size, count, value);
}

/****************************************************************************
 * Shaders
 ****************************************************************************/

static cerr vk_shader_module_create(renderer_t *r, const char *code, size_t size,
                                    VkShaderModule *mod)
{
    if (!code || !size || size % 4)
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "bad SPIR-V blob: %zu bytes",
            .arg0   = (void *)(uintptr_t)size
        );

    /*
     * Built-in shaders are string literals with no alignment guarantees;
     * vkCreateShaderModule() wants uint32-aligned code
     */
    LOCAL_SET(void, aligned) = mem_alloc(size);
    if (!aligned)           return CERR_NOMEM;
    memcpy(aligned, code, size);

    VkShaderModuleCreateInfo info = {
        .sType      = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize   = size,
        .pCode      = aligned,
    };
    VkResult res = vkCreateShaderModule(r->vk.device, &info, NULL, mod);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("vkCreateShaderModule", res);

    return CERR_OK;
}

static void vk_shader_done(shader_t *shader)
{
    auto r = shader->vk.renderer;
    if (!r)                 return;

    free(shader->vk.name);
    shader->vk.name = NULL;

    if (shader->vk.vert)    vkDestroyShaderModule(r->vk.device, shader->vk.vert, NULL);
    if (shader->vk.geom)    vkDestroyShaderModule(r->vk.device, shader->vk.geom, NULL);
    if (shader->vk.frag)    vkDestroyShaderModule(r->vk.device, shader->vk.frag, NULL);
    shader->vk.vert = VK_NULL_HANDLE;
    shader->vk.geom = VK_NULL_HANDLE;
    shader->vk.frag = VK_NULL_HANDLE;
}

static cerr vk_shader_init(renderer_t *r, shader_t *shader,
                           const char *vertex, size_t vert_size,
                           const char *geometry, size_t geom_size,
                           const char *fragment, size_t frag_size)
{
    shader->vk.renderer = r;
    shader->vk.binding_mask = 0;

    cerr err = vk_shader_module_create(r, vertex, vert_size, &shader->vk.vert);
    if (!IS_CERR(err) && geometry)  err = vk_shader_module_create(r, geometry, geom_size, &shader->vk.geom);
    if (!IS_CERR(err))              err = vk_shader_module_create(r, fragment, frag_size, &shader->vk.frag);

    if (IS_CERR(err))               vk_shader_done(shader);

    return err;
}

static_assert(6 >= ATTR_MAX, "shader_t::vertex_attrs array too small for ATTR_MAX");

static VkFormat vk_vertex_format(data_type type)
{
    switch (type) {
        case DT_FLOAT:  return VK_FORMAT_R32_SFLOAT;
        case DT_VEC2:   return VK_FORMAT_R32G32_SFLOAT;
        case DT_VEC3:   return VK_FORMAT_R32G32B32_SFLOAT;
        case DT_VEC4:   return VK_FORMAT_R32G32B32A32_SFLOAT;
        case DT_INT:    return VK_FORMAT_R32_SINT;
        case DT_IVEC2:  return VK_FORMAT_R32G32_SINT;
        case DT_IVEC3:  return VK_FORMAT_R32G32B32_SINT;
        case DT_IVEC4:  return VK_FORMAT_R32G32B32A32_SINT;
        case DT_UINT:   return VK_FORMAT_R32_UINT;
        case DT_UVEC2:  return VK_FORMAT_R32G32_UINT;
        case DT_UVEC3:  return VK_FORMAT_R32G32B32_UINT;
        case DT_UVEC4:  return VK_FORMAT_R32G32B32A32_UINT;
        default:        break;
    }

    clap_unreachable();
    return VK_FORMAT_UNDEFINED;
}

static void vk_shader_set_vertex_attrs(shader_t *shader, size_t stride,
                                       size_t *offs, data_type *types,
                                       size_t *comp_counts, unsigned int *locs,
                                       unsigned int nr_attrs)
{
    shader->vk.vertex_stride = stride;
    shader->vk.nr_vertex_attrs = nr_attrs;

    for (unsigned int i = 0; i < nr_attrs; i++)
        shader->vk.vertex_attrs[i] = (vk_vertex_attr_t){
            .format     = vk_vertex_format(types[i]),
            .offset     = offs[i],
            .location   = locs[i],
        };
}

static int vk_shader_id(shader_t *shader)   { return (int)(uintptr_t)shader; }

static cerr vk_shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name)
{
    shader->vk.binding_mask |= 1ull << bpt->binding;
    return CERR_OK;
}

static attr_t vk_shader_attribute(shader_t *shader, const char *name, attr_t attr)
{
    /* force reading vertex attributes from reflection */
    return UA_UNKNOWN;
}

static uniform_t vk_shader_uniform(shader_t *shader, const char *name)
{
    /*
     * No runtime uniform introspection; reflection JSON populates
     * p->vars[] via shader_reflection_apply(), same as WGPU
     */
    return UA_NOT_PRESENT;
}

static cerr vk_shader_use(shader_t *shader, bool draw)
{
    /* no pipelines to bind: only the resource-loading path can succeed */
    if (!draw)  return CERR_OK;

    return CERR_NOT_SUPPORTED;
}

static void vk_shader_unuse(shader_t *shader, bool draw)    {}

static cres(size_t) vk_shader_uniform_offset_query(shader_t *shader, const char *ubo_name,
                                                   const char *var_name)
{
    return cres_error(size_t, CERR_NOT_FOUND);
}

static void vk_shader_set_name(shader_t *shader, const char *name)
{
    free(shader->vk.name);
    shader->vk.name = strdup(name);
}

static const renderer_ops vk_renderer_ops = {
    .renderer_size          = sizeof(renderer_t),
    .get_caps               = vk_renderer_get_caps,
    .mat4x4_ortho           = vk_mat4x4_ortho,
    .mat4x4_perspective     = vk_mat4x4_perspective,
    .query_limits           = vk_renderer_query_limits,
    .init                   = vk_renderer_init,
    .done                   = vk_renderer_done,
    .viewport               = vk_renderer_viewport,
    .get_viewport           = vk_renderer_get_viewport,
    .hdr_enable             = vk_renderer_hdr_enable,
    .swapchain_begin        = vk_renderer_swapchain_begin,
    .frame_begin            = vk_renderer_frame_begin,
    .frame_end              = vk_renderer_frame_end,
    .swapchain_end          = vk_renderer_swapchain_end,
    .debug                  = vk_renderer_debug,
    .cull_face              = vk_renderer_cull_face,
    .blend                  = vk_renderer_blend,
    .draw                   = vk_renderer_draw,
    .buf_init               = vk_buffer_init,
    .buf_deinit             = vk_buffer_deinit,
    .buf_bind               = vk_buffer_bind,
    .buf_unbind             = vk_buffer_unbind,
    .buf_set_name           = vk_buffer_set_name,
    .va_init                = vk_vertex_array_init,
    .va_done                = vk_vertex_array_done,
    .va_bind                = vk_vertex_array_bind,
    .va_unbind              = vk_vertex_array_unbind,
    .dc_init                = vk_draw_control_init,
    .dc_done                = vk_draw_control_done,
    .dc_bind                = vk_draw_control_bind,
    .dc_unbind              = vk_draw_control_unbind,
    .tex_init               = vk_texture_init,
    .tex_deinit             = vk_texture_deinit,
    .tex_load               = vk_texture_load,
    .tex_resize             = vk_texture_resize,
    .tex_bind               = vk_texture_bind,
    .tex_unbind             = vk_texture_unbind,
    .tex_is_array           = vk_texture_is_array,
    .tex_set_name           = vk_texture_set_name,
    .tex_clone              = vk_texture_clone,
    .tex_id                 = vk_texture_id,
    .tex_format_supported   = vk_texture_format_supported,
    .fbo_prepare            = vk_fbo_prepare,
    .fbo_done               = vk_fbo_done,
    .fbo_resize             = vk_fbo_resize,
    .fbo_attachment_valid   = vk_fbo_attachment_valid,
    .fbo_attachment_format  = vk_fbo_attachment_format,
    .fbo_tex                = vk_fbo_texture,
    .fbo_tex_format         = vk_fbo_texture_format,
    .fbo_tex_supported      = vk_fbo_texture_supported,
    .fbo_create             = vk_fbo_create,
    .fbo_destroy            = vk_fbo_destroy,
    .ubo_init               = vk_uniform_buffer_init,
    .ubo_done               = vk_uniform_buffer_done,
    .ubo_destroy            = vk_uniform_buffer_destroy,
    .ubo_data_alloc         = vk_uniform_buffer_data_alloc,
    .ubo_bind               = vk_uniform_buffer_bind,
    .ubo_update             = vk_uniform_buffer_update,
    .ubo_set                = vk_uniform_buffer_set,
    .shader_init            = vk_shader_init,
    .shader_done            = vk_shader_done,
    .shader_set_vertex_attrs = vk_shader_set_vertex_attrs,
    .shader_id              = vk_shader_id,
    .shader_ubo_bind        = vk_shader_uniform_buffer_bind,
    .shader_attribute       = vk_shader_attribute,
    .shader_uniform         = vk_shader_uniform,
    .shader_use             = vk_shader_use,
    .shader_unuse           = vk_shader_unuse,
    .shader_ubo_offset_query = vk_shader_uniform_offset_query,
    .shader_set_name        = vk_shader_set_name,
};

/****************************************************************************
 * Private interface between render-vk.c and ui-imgui-vk.cpp (render-vk.h)
 ****************************************************************************/

VkInstance renderer_vk_instance(renderer_t *r)                  { return r->vk.instance; }
VkPhysicalDevice renderer_vk_phys_device(renderer_t *r)         { return r->vk.phys_device; }
VkDevice renderer_vk_device(renderer_t *r)                      { return r->vk.device; }
uint32_t renderer_vk_queue_family(renderer_t *r)                { return r->vk.queue_family; }
VkQueue renderer_vk_queue(renderer_t *r)                        { return r->vk.queue; }
VkFormat renderer_vk_swapchain_format(renderer_t *r)            { return r->vk.swapchain_format; }
unsigned int renderer_vk_swapchain_format_gen(renderer_t *r)    { return r->swapchain_gen; }
/* the imgui backend requires at least 2 */
uint32_t renderer_vk_min_image_count(renderer_t *r)             { return max(r->vk.min_image_count, 2u); }
VkCommandBuffer renderer_vk_cmd_buf(renderer_t *r)              { return r->vk.frames[r->vk.frame_idx].cmd_buf; }
bool renderer_vk_pass_open(renderer_t *r)                       { return r->vk.pass_open; }
void renderer_vk_wait_idle(renderer_t *r)                       { if (r->vk.device) vkDeviceWaitIdle(r->vk.device); }

uint32_t renderer_vk_image_count(renderer_t *r)
{
    return max(r->vk.nr_images, renderer_vk_min_image_count(r));
}

cerr vk_renderer_setup(renderer_t *renderer, const renderer_init_options *opts)
{
    list_init(&renderer->vk.ubos);

    renderer->ops = &vk_renderer_ops;

    if (!opts->window)  return CERR_INVALID_ARGUMENTS_REASON(.fmt = "no window");

    CERR_RET_CERR(vk_instance_create(renderer));

    VkResult res = glfwCreateWindowSurface(renderer->vk.instance, opts->window, NULL, &renderer->vk.surface);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("glfwCreateWindowSurface", res);

    CERR_RET_CERR(vk_physical_device_pick(renderer));
    CERR_RET_CERR(vk_device_create(renderer));
    CERR_RET_CERR(vk_frames_init(renderer));

    /*
     * No viewport has been set yet: size the first swapchain to the window's
     * framebuffer, or a surface without a fixed extent (wayland) would get one
     * clamped to minImageExtent, only to be recreated at the first frame
     */
    glfwGetFramebufferSize(opts->window, &renderer->width, &renderer->height);
    CERR_RET_CERR(vk_swapchain_create(renderer));

    return CERR_OK;
}
