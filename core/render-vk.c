// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "display.h"
#include "render.h"
#undef IMPLEMENTOR

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string.h>

#include "bindings/render-bindings-common.h"
#include "common.h"
#include "logger.h"
#include "memory.h"
#include "render-vk.h"
#include "util.h"

/** VK_UBO_SLOTS_PER_FRAME - per-draw UBO slots in each in-flight frame's region: the busiest scenes run ~850 draws */
#define VK_UBO_SLOTS_PER_FRAME 1024

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
        VkPhysicalDeviceVulkan12Features f12 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &f13,
        };
        VkPhysicalDeviceFeatures2 f2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &f12,
        };
        vkGetPhysicalDeviceFeatures2(devs[i], &f2);
        /*
         * TODO: these want to become renderer capabilities with runtime fallbacks;
         * RPi5/v3dv does have geometry shaders but no shaderFloat16, hence the
         * CONFIG_SHADER_NO_FP16 build
         */
        if (!f13.dynamicRendering || !f13.shaderDemoteToHelperInvocation ||
            (!IS_DEFINED(CONFIG_SHADER_NO_FP16) && !f12.shaderFloat16) ||
            !f2.features.geometryShader ||
            !f2.features.independentBlend)
            continue;

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
            .fmt = "no Vulkan 1.3 device with the required features and a graphics+present queue"
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
        .sType                          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering               = VK_TRUE,
        /* discard compiles to OpDemoteToHelperInvocation in SPIR-V 1.6 */
        .shaderDemoteToHelperInvocation = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext              = &f13,
        /* the postprocessing shaders use explicit fp16 (half.glsl) */
        .shaderFloat16      = IS_DEFINED(CONFIG_SHADER_NO_FP16) ? VK_FALSE : VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 f2 = {
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext              = &f12,
        .features           = {
            /* the shadow passes emit cascade layers from geometry shaders */
            .geometryShader     = VK_TRUE,
            /* G-buffer-ish attachments opt out of blending (no_blend) */
            .independentBlend   = VK_TRUE,
        },
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

        /* one descriptor set per draw, reset wholesale each frame */
        VkDescriptorPoolSize pool_sizes[] = {
            {
                .type               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount    = VK_UBO_SLOTS_PER_FRAME * VULKAN_BINDING_UBO_BASE,
            },
            {
                .type               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount    = VK_UBO_SLOTS_PER_FRAME * BINDING_UBO_MAX,
            },
        };
        VkDescriptorPoolCreateInfo dp_info = {
            .sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets        = VK_UBO_SLOTS_PER_FRAME,
            .poolSizeCount  = array_size(pool_sizes),
            .pPoolSizes     = pool_sizes,
        };
        if (vkCreateDescriptorPool(r->vk.device, &dp_info, NULL, &f->desc_pool) != VK_SUCCESS)
            return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreateDescriptorPool failed");

        list_init(&f->zombies);
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

/**
 * struct vk_zombie - handles parked for destruction once their frame retires
 * @entry:      link in the frame slot's zombie list
 * @image:      image to destroy, or VK_NULL_HANDLE
 * @view:       image view to destroy, or VK_NULL_HANDLE
 * @sampler:    sampler to destroy, or VK_NULL_HANDLE
 * @buf:        buffer to destroy, or VK_NULL_HANDLE
 * @mem:        memory to free, or VK_NULL_HANDLE
 * @pipeline:   a dropped draw_control's pipeline, possibly still bound
 * @cmd_buf:    an upload's command buffer, freed back to the upload pool
 */
typedef struct vk_zombie {
    struct list         entry;
    VkImage             image;
    VkImageView         view;
    VkSampler           sampler;
    VkBuffer            buf;
    VkDeviceMemory      mem;
    VkPipeline          pipeline;
    VkCommandBuffer     cmd_buf;
} vk_zombie;

static void vk_zombie_add(renderer_t *r, vk_zombie zombie);

/*
 * Submit an upload, taking ownership of its staging buffer. With a frame
 * recording, the upload retires with that frame: it's submitted ahead of the
 * frame's command buffer on the same queue, so the frame fence covers it and
 * the transients just go on the frame's zombie list. Without one (device
 * init), drain the queue and clean up on the spot -- a mid-frame
 * vkQueueWaitIdle() is not an option, it also absorbs FIFO's vblank wait and
 * halves the frame rate.
 */
static cerr vk_upload_end(renderer_t *r, VkCommandBuffer cb, VkBuffer staging,
                          VkDeviceMemory staging_mem)
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

    if (res == VK_SUCCESS && r->vk.cb_open) {
        vk_zombie_add(r, (vk_zombie){ .buf = staging, .mem = staging_mem, .cmd_buf = cb });
        return CERR_OK;
    }

    if (res == VK_SUCCESS)  vkQueueWaitIdle(r->vk.queue);

    vkFreeCommandBuffers(r->vk.device, r->vk.upload_pool, 1, &cb);
    if (staging)            vkDestroyBuffer(r->vk.device, staging, NULL);
    if (staging_mem)        vkFreeMemory(r->vk.device, staging_mem, NULL);

    if (res != VK_SUCCESS)
        return CERR_INVALID_OPERATION_REASON(
            .fmt    = "upload vkQueueSubmit failed: %d",
            .arg0   = (void *)(intptr_t)res
        );

    return CERR_OK;
}

/****************************************************************************
 * Deferred destruction
 *
 * GPU resources can be released by the engine while frames that reference
 * them are still in flight (ui_printf() replaces its text textures every
 * time, for one); their handles are parked on the current frame slot's
 * zombie list and destroyed once that slot's fence proves the frame -- and
 * every earlier one -- has retired
 ****************************************************************************/

/**
 * vk_zombie_list() - the zombie list a deferred destroy retires with
 * @r:  renderer
 *
 * Inside a recording frame that is
 * simply the current one. Outside one it is NOT: frame_end() has already
 * advanced frame_idx, so frames[frame_idx] is the next frame to record and its
 * fence is VK_INFLIGHT_FRAMES old -- retiring against it frees the object at
 * the next frame_begin(), while the frame just submitted may still be reading
 * it. Destroys do land here: clap_frame() polls input after frame_end(), so
 * any handler that drops a UI element (closing a menu, say) runs in that gap.
 * Charge those to the frame that was actually submitted last.
 *
 * Return: the list of the frame whose fence retires the destroyed objects
 */
static struct list *vk_zombie_list(renderer_t *r)
{
    unsigned int frame = r->vk.cb_open ? r->vk.frame_idx :
        (r->vk.frame_idx + VK_INFLIGHT_FRAMES - 1) % VK_INFLIGHT_FRAMES;

    return &r->vk.frames[frame].zombies;
}

/**
 * vk_zombie_add() - park handles for destruction once their frame retires
 * @r:      renderer
 * @zombie: the handles to destroy, unset ones VK_NULL_HANDLE
 */
static void vk_zombie_add(renderer_t *r, vk_zombie zombie)
{
    vk_zombie *z = memdup(&zombie, sizeof(zombie));

    if (!z) {
        /* better a validation complaint than a leak we can't see */
        warn("out of memory deferring a destroy\n");
        return;
    }

    list_append(vk_zombie_list(r), &z->entry);
}

/**
 * vk_zombies_drain() - destroy everything parked on a retired frame's list
 * @r:      renderer
 * @frame:  frame slot whose fence has been waited for
 */
static void vk_zombies_drain(renderer_t *r, unsigned int frame)
{
    vk_zombie *z, *iter;

    list_for_each_entry_iter(z, iter, &r->vk.frames[frame].zombies, entry) {
        if (z->view)        vkDestroyImageView(r->vk.device, z->view, NULL);
        if (z->image)       vkDestroyImage(r->vk.device, z->image, NULL);
        if (z->sampler)     vkDestroySampler(r->vk.device, z->sampler, NULL);
        if (z->buf)         vkDestroyBuffer(r->vk.device, z->buf, NULL);
        if (z->mem)         vkFreeMemory(r->vk.device, z->mem, NULL);
        if (z->pipeline)    vkDestroyPipeline(r->vk.device, z->pipeline, NULL);
        if (z->cmd_buf)     vkFreeCommandBuffers(r->vk.device, r->vk.upload_pool, 1, &z->cmd_buf);
        list_del(&z->entry);
        mem_free(z);
    }
}

/****************************************************************************
 * Descriptors
 *
 * One global set-0 layout shared by every pipeline: combined image samplers
 * at the texture bindings (0 .. VULKAN_BINDING_UBO_BASE - 1) and plain
 * uniform buffers above them. Every shader's bindings are a subset of it by
 * construction, which kills the layout-vs-reflection mismatch class the
 * WGPU backend still fights. Per draw, a set is allocated from the frame's
 * pool and every slot is written, with dummy resources filling the unbound
 * ones.
 ****************************************************************************/

/**
 * vk_descriptors_init() - create the global set-0 layout, the pipeline layout and the per-frame pools
 * @r:  renderer
 *
 * Return: CERR_OK, or the creation error
 */
static cerr vk_descriptors_init(renderer_t *r)
{
    VkDescriptorSetLayoutBinding bindings[VULKAN_BINDING_UBO_BASE + BINDING_UBO_MAX];
    uint32_t n = 0;

    VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                VK_SHADER_STAGE_GEOMETRY_BIT;

    for (unsigned int i = 0; i < VULKAN_BINDING_UBO_BASE; i++)
        bindings[n++] = (VkDescriptorSetLayoutBinding){
            .binding            = i,
            .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount    = 1,
            .stageFlags         = stages,
        };

    for (unsigned int i = 0; i < BINDING_UBO_MAX; i++)
        bindings[n++] = (VkDescriptorSetLayoutBinding){
            .binding            = VULKAN_BINDING_UBO_BASE + i,
            .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = stages,
        };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType          = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount   = n,
        .pBindings      = bindings,
    };
    if (vkCreateDescriptorSetLayout(r->vk.device, &layout_info, NULL, &r->vk.desc_layout) != VK_SUCCESS)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreateDescriptorSetLayout failed");

    VkPipelineLayoutCreateInfo pl_info = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &r->vk.desc_layout,
    };
    if (vkCreatePipelineLayout(r->vk.device, &pl_info, NULL, &r->vk.pipeline_layout) != VK_SUCCESS)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "vkCreatePipelineLayout failed");

    return CERR_OK;
}

/**
 * vk_dummies_init() - create the 1x1 image and the 256-byte UBO that fill unbound slots
 * @r:  renderer
 *
 * Return: CERR_OK, or the creation error
 */
static cerr vk_dummies_init(renderer_t *r)
{
    CERR_RET_CERR(vk_image_create(r, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, (VkExtent3D){ 1, 1, 1 },
                                  1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT,
                                  &r->vk.dummy_image, &r->vk.dummy_mem));
    CERR_RET_CERR(vk_image_view_create(r, r->vk.dummy_image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                       VK_IMAGE_ASPECT_COLOR_BIT, 1, &r->vk.dummy_view));

    VkSamplerCreateInfo sampler_info = {
        .sType          = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter      = VK_FILTER_NEAREST,
        .minFilter      = VK_FILTER_NEAREST,
        .addressModeU   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(r->vk.device, &sampler_info, NULL, &r->vk.dummy_sampler) != VK_SUCCESS)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "dummy vkCreateSampler failed");

    /* the shaders never read it, but the layout must still be right */
    VkCommandBuffer cb;
    CERR_RET_CERR(vk_upload_begin(r, &cb));
    vk_image_layout_transition(cb, r->vk.dummy_image, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               0, VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    CERR_RET_CERR(vk_upload_end(r, cb, VK_NULL_HANDLE, VK_NULL_HANDLE));

    return vk_buffer_create(r, 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &r->vk.dummy_ubo, &r->vk.dummy_ubo_mem);
}

/**
 * vk_descriptors_done() - destroy the dummies, the pools and the layouts
 * @r:  renderer
 *
 * Tolerates a partial vk_descriptors_init()/vk_dummies_init().
 */
static void vk_descriptors_done(renderer_t *r)
{
    if (r->vk.dummy_ubo)        vkDestroyBuffer(r->vk.device, r->vk.dummy_ubo, NULL);
    if (r->vk.dummy_ubo_mem)    vkFreeMemory(r->vk.device, r->vk.dummy_ubo_mem, NULL);
    if (r->vk.dummy_sampler)    vkDestroySampler(r->vk.device, r->vk.dummy_sampler, NULL);
    if (r->vk.dummy_view)       vkDestroyImageView(r->vk.device, r->vk.dummy_view, NULL);
    if (r->vk.dummy_image)      vkDestroyImage(r->vk.device, r->vk.dummy_image, NULL);
    if (r->vk.dummy_mem)        vkFreeMemory(r->vk.device, r->vk.dummy_mem, NULL);
    if (r->vk.pipeline_layout)  vkDestroyPipelineLayout(r->vk.device, r->vk.pipeline_layout, NULL);
    if (r->vk.desc_layout)      vkDestroyDescriptorSetLayout(r->vk.device, r->vk.desc_layout, NULL);
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

        /* the pipeline cache holds the only reference to its draw_controls */
        draw_control_cache_done(r);

        for (unsigned int i = 0; i < VK_INFLIGHT_FRAMES; i++)   vk_zombies_drain(r, i);

        vk_swapchain_destroy_resources(r);
        if (r->vk.swapchain)    vkDestroySwapchainKHR(r->vk.device, r->vk.swapchain, NULL);

        for (unsigned int i = 0; i < VK_INFLIGHT_FRAMES; i++) {
            auto f = &r->vk.frames[i];

            if (f->fence)       vkDestroyFence(r->vk.device, f->fence, NULL);
            if (f->acquire_sem) vkDestroySemaphore(r->vk.device, f->acquire_sem, NULL);
            if (f->cmd_pool)    vkDestroyCommandPool(r->vk.device, f->cmd_pool, NULL);
            if (f->desc_pool)   vkDestroyDescriptorPool(r->vk.device, f->desc_pool, NULL);
        }

        vk_descriptors_done(r);

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

static void vk_ubos_frame_advance(renderer_t *r);

/**
 * vk_viewport_apply() - set the viewport and scissor for a @width x @height target
 * @cb:     command buffer being recorded
 * @width:  target width
 * @height: target height
 *
 * Negative height: y-up clip space, same convention as WGPU/Metal.
 */
static void vk_viewport_apply(VkCommandBuffer cb, unsigned int width, unsigned int height)
{
    VkViewport viewport = {
        .y          = (float)height,
        .width      = (float)width,
        .height     = -(float)height,
        .maxDepth   = 1.0,
    };
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor = { .extent = { width, height } };
    vkCmdSetScissor(cb, 0, 1, &scissor);
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
    vkResetDescriptorPool(r->vk.device, f->desc_pool, 0);

    /* The fence guarantees this frame's UBO regions are no longer read */
    vk_ubos_frame_advance(r);
    /* ... and that its parked resources are safe to destroy */
    vk_zombies_drain(r, r->vk.frame_idx);

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

    vk_viewport_apply(f->cmd_buf, r->vk.sc_width, r->vk.sc_height);
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

/* recorded into the command buffer at draw time */
static void vk_renderer_cull_face(renderer_t *r, cull_face cull)                    { r->vk.cull = cull; }
static void vk_renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor) { r->blend = _blend; }

static cerr vk_renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                             data_type idx_type, unsigned int nr_instances);

/****************************************************************************
 * Buffers
 ****************************************************************************/

static VkBufferUsageFlags vk_buffer_usage(buffer_type type)
{
    switch (type) {
        case BUF_ARRAY:         return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BUF_ELEMENT_ARRAY: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        default:                break;
    }

    clap_unreachable();
    return 0;
}

static cerr vk_buffer_load(buffer_t *buf, renderer_t *r, void *data, size_t size)
{
    if (buf->main) {
        /* interleaved attributes alias the main buffer */
        buf->vk.buf = buf->main->vk.buf;
    } else {
        /*
         * Host-visible is plenty for the UI-scale geometry; a device-local
         * staging path can come with the 3D pipeline if it proves to matter
         */
        CERR_RET_CERR(vk_buffer_create(r, size, vk_buffer_usage(buf->vk.type),
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       &buf->vk.buf, &buf->vk.mem));

        void *mapped;
        if (vkMapMemory(r->vk.device, buf->vk.mem, 0, size, 0, &mapped) != VK_SUCCESS)
            return CERR_INVALID_OPERATION_REASON(.fmt = "vkMapMemory failed");
        memcpy(mapped, data, size);
        vkUnmapMemory(r->vk.device, buf->vk.mem);
    }

    buf->vk.size = size;
    buf->loaded = true;

    return CERR_OK;
}

static cerr vk_buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    CERR_RET_CERR(ref_embed(buffer, buf));

    data_type comp_type = opts->comp_type;
    if (comp_type == DT_NONE)   comp_type = DT_FLOAT;

    buf->vk.type = opts->type;
    buf->off = opts->off;
    buf->comp_count = max(opts->comp_count, data_comp_count(comp_type));
    buf->comp_type = opts->comp_type;
    buf->loc = opts->loc;
    buf->main = opts->main;
#ifndef CONFIG_FINAL
    buf->opts = *opts;
    buf->opts.comp_type = comp_type;
    buf->opts.comp_count = buf->comp_count;
#endif /* !CONFIG_FINAL */

    if (opts->data && opts->size)   return vk_buffer_load(buf, opts->renderer, opts->data, opts->size);

    return CERR_OK;
}

static void vk_buffer_deinit(buffer_t *buf)
{
    if (!buf)                       return;

    if (buf->vk.buf && !buf->main)  vk_zombie_add(buf->renderer, (vk_zombie){ .buf = buf->vk.buf, .mem = buf->vk.mem });

    buf->vk.buf = VK_NULL_HANDLE;
    buf->vk.mem = VK_NULL_HANDLE;
    buf->off = 0;
    buf->vk.size = 0;
    buf->comp_count = 0;
    buf->loc = 0;
    buf->loaded = false;
#ifndef CONFIG_FINAL
    memset(&buf->opts, 0, sizeof(buf->opts));
#endif /* !CONFIG_FINAL */
}

static void vk_buffer_bind(buffer_t *buf, uniform_t loc)
{
    if (!buf || !buf->loaded)   return;

    buf->loc = loc;

    auto r = buf->renderer;
    if (!r || !r->va)           return;

    if (loc < 0) {
        /* index buffer */
        r->va->index = buf;
    } else if (loc == 0) {
        /* first vertex attribute: record the interleaved vertex buffer */
        r->va->vk.vbuf = buf->main ? buf->main : buf;
    }
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

static void vk_vertex_array_done(vertex_array_t *va)                { va->index = NULL; va->vk.vbuf = NULL; }
static void vk_vertex_array_bind(vertex_array_t *va)                { if (va && va->renderer) va->renderer->va = va; }
static void vk_vertex_array_unbind(vertex_array_t *va)              { if (va && va->renderer) va->renderer->va = NULL; }

/****************************************************************************
 * Draw controls
 *
 * A draw_control caches the two VkPipeline variants ([0] no blend,
 * [1] alpha blend) for a (shader, color format, target count, depth)
 * combination, looked up in shader_use() for the currently bound target
 ****************************************************************************/

static VkFormat vk_texture_format(texture_format format);

/**
 * vk_format_supports_alpha_blend() - whether a target format takes alpha blending
 * @format: attachment format
 *
 * Blend state must be compatible with the render target format. Mirrors the
 * WGPU matrix: 32-bit formats aren't universally blendable, and formats
 * without an alpha channel make no sense with SrcAlpha/OneMinusSrcAlpha
 * factors -- keeping the set identical keeps the backends' output identical.
 *
 * Return: true if pipelines on this format may blend
 */
static bool vk_format_supports_alpha_blend(VkFormat format)
{
    switch (format) {
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_UNORM:
            return false;
        default:
            return true;
    }
}

/**
 * vk_cull_mode() - the VkCullModeFlags for the engine's cull face setting
 * @cull:   the cull face setting
 *
 * Return: the cull mode
 */
static VkCullModeFlags vk_cull_mode(cull_face cull)
{
    switch (cull) {
        case CULL_FACE_NONE:    return VK_CULL_MODE_NONE;
        case CULL_FACE_FRONT:   return VK_CULL_MODE_FRONT_BIT;
        case CULL_FACE_BACK:    return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

/**
 * vk_compare_op() - the VkCompareOp for a depth function
 * @fn: the engine's depth function
 *
 * Return: the compare op
 */
static VkCompareOp vk_compare_op(depth_func fn)
{
    switch (fn) {
        case DEPTH_FN_ALWAYS:           return VK_COMPARE_OP_ALWAYS;
        case DEPTH_FN_NEVER:            return VK_COMPARE_OP_NEVER;
        case DEPTH_FN_LESS:             return VK_COMPARE_OP_LESS;
        case DEPTH_FN_EQUAL:            return VK_COMPARE_OP_EQUAL;
        case DEPTH_FN_LESS_OR_EQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case DEPTH_FN_GREATER:          return VK_COMPARE_OP_GREATER;
        case DEPTH_FN_NOT_EQUAL:        return VK_COMPARE_OP_NOT_EQUAL;
        case DEPTH_FN_GREATER_OR_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    }
    return VK_COMPARE_OP_ALWAYS;
}

/**
 * struct vk_target_state - attachment state of a render target
 * @color_formats:      one format per color attachment
 * @depth_format:       the depth attachment's format, VK_FORMAT_UNDEFINED without one
 * @nr_color_targets:   color attachments in use
 * @no_blend:           bit i set: color attachment i opts out of blending
 * @depth_fn:           the depth test
 * @has_depth:          whether there is a depth attachment
 *
 * The bound FBO's textures, or the swapchain; pipelines are created against
 * and cache-keyed on this.
 */
typedef struct vk_target_state {
    VkFormat        color_formats[FBO_COLOR_ATTACHMENTS_MAX];
    VkFormat        depth_format;
    unsigned int    nr_color_targets;
    uint32_t        no_blend;
    depth_func      depth_fn;
    bool            has_depth;
} vk_target_state;

/**
 * vk_target_state_get() - describe the render target a pipeline is built for
 * @r:      renderer
 * @fbo:    the FBO, NULL for the swapchain
 * @ts:     filled in
 */
static void vk_target_state_get(renderer_t *r, fbo_t *fbo, vk_target_state *ts)
{
    *ts = (vk_target_state){ .depth_format = VK_FORMAT_UNDEFINED };

    if (!fbo) {
        ts->color_formats[0] = r->vk.swapchain_format;
        ts->nr_color_targets = 1;
        return;
    }

    fa_for_each(fa, fbo->layout, texture) {
        int idx = fbo_attachment_color(fa);

        ts->color_formats[ts->nr_color_targets] = vk_texture_format(fbo->color_config[idx].format);
        if (fbo->color_config[idx].no_blend)    ts->no_blend |= 1u << ts->nr_color_targets;
        ts->nr_color_targets++;
    }

    if (fbo->layout.depth_texture) {
        ts->has_depth = true;
        ts->depth_format = vk_texture_format(fbo->depth_config.format);
        ts->depth_fn = fbo->depth_config.depth_func;
    }
}

/**
 * vk_shader_stage() - a pipeline stage description for a shader module
 * @stage:  the stage
 * @module: its module, entry point "main"
 *
 * Return: the stage create info
 */
static VkPipelineShaderStageCreateInfo vk_shader_stage(VkShaderStageFlagBits stage, VkShaderModule module)
{
    return (VkPipelineShaderStageCreateInfo){
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = stage,
        .module = module,
        .pName  = "main",
    };
}

/**
 * vk_pipeline_create() - build one of a draw_control's dynamic-rendering pipelines
 * @r:          renderer
 * @dc:         the draw_control: its shader supplies the stages and vertex attributes
 * @ts:         the render target's attachment formats and depth state
 * @blend:      build the alpha-blending variant instead of the opaque one
 * @pipeline:   filled with the pipeline
 *
 * Return: CERR_OK, or the creation error
 */
static cerr vk_pipeline_create(renderer_t *r, draw_control_t *dc, const vk_target_state *ts,
                               bool blend, VkPipeline *pipeline)
{
    shader_t *shader = dc->shader;

    VkPipelineShaderStageCreateInfo stages[3];
    uint32_t nr_stages = 0;

    stages[nr_stages++] = vk_shader_stage(VK_SHADER_STAGE_VERTEX_BIT, shader->vk.vert);
    if (shader->vk.geom)    stages[nr_stages++] = vk_shader_stage(VK_SHADER_STAGE_GEOMETRY_BIT, shader->vk.geom);
    stages[nr_stages++] = vk_shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, shader->vk.frag);

    VkVertexInputAttributeDescription attrs[array_size(shader->vk.vertex_attrs)];
    for (unsigned int i = 0; i < shader->vk.nr_vertex_attrs; i++)
        attrs[i] = (VkVertexInputAttributeDescription){
            .location   = shader->vk.vertex_attrs[i].location,
            .format     = shader->vk.vertex_attrs[i].format,
            .offset     = shader->vk.vertex_attrs[i].offset,
        };

    VkVertexInputBindingDescription vbinding = {
        .stride     = shader->vk.vertex_stride,
        .inputRate  = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                              = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount      = shader->vk.nr_vertex_attrs ? 1 : 0,
        .pVertexBindingDescriptions         = &vbinding,
        .vertexAttributeDescriptionCount    = shader->vk.nr_vertex_attrs,
        .pVertexAttributeDescriptions       = attrs,
    };

    /* draw_type is baked as triangles, same as the WGPU backend */
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType      = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology   = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount  = 1,
        .scissorCount   = 1,
    };

    /*
     * Cull mode is dynamic (renderer_cull_face() flips per model and per
     * pass). Winding is evaluated after the negative-height viewport
     * mirrors y, and the engine's projection matrices already flip once for
     * the y-down conventions, so the engine's front faces come out CCW here
     * (verified against the GL renderer's culling, not derived).
     */
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType      = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .cullMode   = VK_CULL_MODE_NONE,
        .frontFace  = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth  = 1.0,
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples   = VK_SAMPLE_COUNT_1_BIT,
    };

    /* SrcAlpha/OneMinusSrcAlpha color, One/OneMinusSrcAlpha alpha: the only
     * blend mode the engine uses (model.c), same equations as WGPU/Metal */
    VkPipelineColorBlendAttachmentState blend_attachments[FBO_COLOR_ATTACHMENTS_MAX];
    for (unsigned int i = 0; i < ts->nr_color_targets; i++) {
        bool att_blend = blend && !(ts->no_blend & (1u << i)) &&
                         vk_format_supports_alpha_blend(ts->color_formats[i]);

        blend_attachments[i] = (VkPipelineColorBlendAttachmentState){
            .blendEnable            = att_blend ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor    = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp           = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor    = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp           = VK_BLEND_OP_ADD,
            .colorWriteMask         = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
    }
    VkPipelineColorBlendStateCreateInfo blend_state = {
        .sType              = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount    = ts->nr_color_targets,
        .pAttachments       = blend_attachments,
    };

    VkPipelineDepthStencilStateCreateInfo depth_state = {
        .sType              = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable    = VK_TRUE,
        .depthWriteEnable   = VK_TRUE,
        .depthCompareOp     = vk_compare_op(ts->depth_fn),
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType              = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount  = array_size(dynamic_states),
        .pDynamicStates     = dynamic_states,
    };

    VkPipelineRenderingCreateInfo rendering_info = {
        .sType                      = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount       = ts->nr_color_targets,
        .pColorAttachmentFormats    = ts->color_formats,
        .depthAttachmentFormat      = ts->depth_format,
    };

    VkGraphicsPipelineCreateInfo info = {
        .sType                  = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext                  = &rendering_info,
        .stageCount             = nr_stages,
        .pStages                = stages,
        .pVertexInputState      = &vertex_input,
        .pInputAssemblyState    = &input_assembly,
        .pViewportState         = &viewport_state,
        .pRasterizationState    = &raster,
        .pMultisampleState      = &multisample,
        .pDepthStencilState     = ts->has_depth ? &depth_state : NULL,
        .pColorBlendState       = &blend_state,
        .pDynamicState          = &dynamic_state,
        .layout                 = r->vk.pipeline_layout,
    };

    VkResult res = vkCreateGraphicsPipelines(r->vk.device, VK_NULL_HANDLE, 1, &info, NULL, pipeline);
    if (res != VK_SUCCESS)  return VK_INIT_FAILED("vkCreateGraphicsPipelines", res);

    return CERR_OK;
}

static cerr vk_draw_control_init(draw_control_t *dc, const draw_control_init_options *opts)
{
    auto r = opts->renderer;
    vk_target_state ts;

    vk_target_state_get(r, opts->fbo, &ts);

    CERR_RET_CERR(vk_pipeline_create(r, dc, &ts, false, &dc->vk.pipeline[0]));
    /* a failed make skips the drop: nothing else releases the first pipeline */
    CERR_RET(vk_pipeline_create(r, dc, &ts, true, &dc->vk.pipeline[1]), {
        vkDestroyPipeline(r->vk.device, dc->vk.pipeline[0], NULL);
        dc->vk.pipeline[0] = VK_NULL_HANDLE;
        return __cerr;
    });

    return CERR_OK;
}

static void vk_draw_control_done(draw_control_t *dc)
{
    auto r = dc->renderer;
    if (!r)             return;

    if (r->vk.dc == dc) r->vk.dc = NULL;

    /* frames in flight may still reference the pipelines: defer */
    for (int i = 0; i < array_size(dc->vk.pipeline); i++)
        if (dc->vk.pipeline[i]) {
            vk_zombie_add(r, (vk_zombie){ .pipeline = dc->vk.pipeline[i] });
            dc->vk.pipeline[i] = VK_NULL_HANDLE;
        }
}

/* the blend variant is picked in renderer_draw() */
static void vk_draw_control_bind(draw_control_t *dc)     { dc->renderer->vk.dc = dc; }
static void vk_draw_control_unbind(draw_control_t *dc)   { dc->renderer->vk.dc = NULL; }

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

    if (tex->vk.image || tex->vk.view || tex->vk.mem)
        vk_zombie_add(r, (vk_zombie){ .image = tex->vk.image, .view = tex->vk.view, .mem = tex->vk.mem });

    tex->vk.view = VK_NULL_HANDLE;
    tex->vk.image = VK_NULL_HANDLE;
    tex->vk.mem = VK_NULL_HANDLE;
}

static void vk_texture_deinit(texture_t *tex)
{
    if (!tex || !tex->renderer) return;

    auto r = tex->renderer;

    if (tex->vk.image || tex->vk.view || tex->vk.mem || tex->vk.sampler)
        vk_zombie_add(r, (vk_zombie){
            .image = tex->vk.image, .view = tex->vk.view, .sampler = tex->vk.sampler, .mem = tex->vk.mem
        });

    tex->vk.view = VK_NULL_HANDLE;
    tex->vk.image = VK_NULL_HANDLE;
    tex->vk.mem = VK_NULL_HANDLE;
    tex->vk.sampler = VK_NULL_HANDLE;

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
    tex->vk.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    return vk_image_view_create(r, tex->vk.image, vk_image_view_type(tex->vk.type), format,
                                vk_texture_aspect(tex), nr_layers, &tex->vk.view);
}

/**
 * vk_layout_access() - the access mask and pipeline stage an image layout implies
 * @layout: the layout
 * @access: filled with the access mask
 * @stage:  filled with the pipeline stage
 */
static void vk_layout_access(VkImageLayout layout, VkAccessFlags *access,
                             VkPipelineStageFlags *stage)
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            *access = 0;
            *stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            *access = VK_ACCESS_TRANSFER_WRITE_BIT;
            *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            *access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            *stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            *stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        default:
            *access = VK_ACCESS_SHADER_READ_BIT;
            *stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
    }
}

/**
 * vk_texture_layout_to() - transition a texture's image to a layout
 * @cb:     command buffer being recorded
 * @tex:    the texture, whose tracked layout is the source and gets updated
 * @to:     the target layout
 */
static void vk_texture_layout_to(VkCommandBuffer cb, texture_t *tex, VkImageLayout to)
{
    if (tex->vk.layout == to)   return;

    VkAccessFlags src_access, dst_access;
    VkPipelineStageFlags src_stage, dst_stage;

    vk_layout_access(tex->vk.layout, &src_access, &src_stage);
    vk_layout_access(to, &dst_access, &dst_stage);
    vk_image_layout_transition(cb, tex->vk.image, vk_texture_aspect(tex),
                               VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS,
                               tex->vk.layout, to, src_access, dst_access, src_stage, dst_stage);

    tex->vk.layout = to;
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
                 { vk_upload_end(r, cb, VK_NULL_HANDLE, VK_NULL_HANDLE); return __cerr; });

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

        CERR_RET_CERR(vk_upload_end(r, cb, staging, staging_mem));
    } else {
        /* no data: still leave the image in a sampleable layout */
        vk_image_layout_transition(cb, tex->vk.image, aspect,
                                   VK_REMAINING_MIP_LEVELS, VK_REMAINING_ARRAY_LAYERS,
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   0, VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        CERR_RET_CERR(vk_upload_end(r, cb, VK_NULL_HANDLE, VK_NULL_HANDLE));
    }

    tex->vk.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    auto r = tex ? tex->renderer : NULL;

    /* @target is the set-0 binding number; aliased names share a slot */
    if (!r || target >= array_size(r->vk.bound_textures))   return;

    r->vk.bound_textures[target] = tex;
}

static void vk_texture_unbind(texture_t *tex, unsigned int target)
{
    auto r = tex ? tex->renderer : NULL;

    if (!r || target >= array_size(r->vk.bound_textures))   return;

    r->vk.bound_textures[target] = NULL;
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
 *
 * Dynamic rendering: fbo_prepare()/fbo_done() bracket a vkCmdBeginRendering/
 * vkCmdEndRendering pair, and fbo_done() moves the attachments to
 * SHADER_READ_ONLY so downstream passes can sample them
 ****************************************************************************/

/**
 * fbo_texture_fallback() - the dummy texture standing in for a missing attachment
 * @attachment: the attachment being asked for
 *
 * Return: a static pixel texture of the attachment's kind
 */
static texture_t *fbo_texture_fallback(fbo_attachment attachment)
{
    if (attachment.depth_texture || attachment.depth_buffer)    return black_pixel();

    return transparent_pixel();
}

static bool vk_fbo_texture_supported(renderer_t *r, texture_format format)
{
    return vk_texture_format_supported(r, format);
}

static texture_format vk_fbo_texture_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (attachment.depth_texture || attachment.depth_buffer)
        return fbo ? fbo->depth_config.format : TEX_FMT_DEPTH32F;

    if (fbo && attachment.color_textures) {
        int idx = fbo_attachment_color(attachment);
        if (idx >= 0 && fbo->color_config)  return fbo->color_config[idx].format;
    }

    return TEX_FMT_RGBA8;
}

/**
 * vk_load_op() - the load op for an attachment's configured load action
 * @action: the engine's load action
 *
 * Return: the load op
 */
static VkAttachmentLoadOp vk_load_op(fbo_load_action action)
{
    switch (action) {
        case FBOLOAD_LOAD:      return VK_ATTACHMENT_LOAD_OP_LOAD;
        case FBOLOAD_CLEAR:     return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case FBOLOAD_DONTCARE:  return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

/**
 * vk_store_op() - the store op for an attachment's configured store action
 * @action: the engine's store action
 *
 * Return: the store op
 */
static VkAttachmentStoreOp vk_store_op(fbo_store_action action)
{
    switch (action) {
        case FBOSTORE_STORE:        return VK_ATTACHMENT_STORE_OP_STORE;
        /* TODO: MSAA resolve attachments */
        case FBOSTORE_MS_RESOLVE:   return VK_ATTACHMENT_STORE_OP_STORE;
        case FBOSTORE_DONTCARE:     return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_STORE;
}

/**
 * fbo_attachments_deinit() - release the FBO's attachment textures
 * @fbo:    the FBO; attachments that were never created are skipped
 */
static void fbo_attachments_deinit(fbo_t *fbo)
{
    fa_for_each(fa, fbo->layout, texture) {
        int idx = fbo_attachment_color(fa);
        texture_deinit(&fbo->color_tex[idx]);
    }
    if (fbo->layout.depth_texture)  texture_deinit(&fbo->depth_tex);
}

/**
 * fbo_depth_texture_init() - create the FBO's depth attachment at its current size
 * @fbo:    the FBO
 *
 * Return: CERR_OK, or the texture creation error
 */
static cerr fbo_depth_texture_init(fbo_t *fbo)
{
    CERR_RET_CERR(
        texture_init(
            &fbo->depth_tex,
            .renderer       = fbo->renderer,
            .type           = fbo->layers > 1 ? TEX_2D_ARRAY : TEX_2D,
            .layers         = fbo->layers,
            .format         = fbo->depth_config.format,
            .render_target  = true,
            .wrap           = TEX_CLAMP_TO_EDGE,
            .min_filter     = TEX_FLT_NEAREST,
            .mag_filter     = TEX_FLT_NEAREST
        )
    );

    texture_set_name(&fbo->depth_tex, "%s:depth", fbo->vk.name);

    CERR_RET_CERR(texture_load(&fbo->depth_tex, fbo->depth_config.format,
                               fbo->width, fbo->height, NULL));

    return CERR_OK;
}

/**
 * fbo_texture_init() - create one color attachment at the FBO's current size
 * @fbo:        the FBO
 * @attachment: which color attachment
 *
 * Return: CERR_OK, or the texture creation error
 */
static cerr_check fbo_texture_init(fbo_t *fbo, fbo_attachment attachment)
{
    int idx = fbo_attachment_color(attachment);

    CERR_RET_CERR(
        texture_init(
            &fbo->color_tex[idx],
            .renderer       = fbo->renderer,
            .type           = fbo->layers > 1 ? TEX_2D_ARRAY : TEX_2D,
            .layers         = fbo->layers,
            .format         = vk_fbo_texture_format(fbo, attachment),
            .render_target  = true,
            .wrap           = TEX_CLAMP_TO_EDGE,
            .min_filter     = TEX_FLT_LINEAR,
            .mag_filter     = TEX_FLT_LINEAR
        )
    );

    texture_set_name(&fbo->color_tex[idx], "%s:rt%d", fbo->vk.name, idx);

    CERR_RET_CERR(texture_load(&fbo->color_tex[idx], vk_fbo_texture_format(fbo, attachment),
                               fbo->width, fbo->height, NULL));

    /* texture_load() may have promoted the format (RGB -> RGBA) */
    fbo->color_config[idx].format = fbo->color_tex[idx].vk.format;

    return CERR_OK;
}

/**
 * fbo_textures_init() - create every color attachment in @layout
 * @fbo:    the FBO
 * @layout: the attachments to create
 *
 * Return: CERR_OK, or the first creation error, the attachments before it left live
 */
static cerr_check fbo_textures_init(fbo_t *fbo, fbo_attachment layout)
{
    fa_for_each(fa, layout, texture)
        CERR_RET(fbo_texture_init(fbo, fa), return __cerr);

    return CERR_OK;
}

/**
 * fbo_rendering_info_update() - rebuild the FBO's VkRenderingInfo
 * @fbo:    FBO whose attachments have just been (re)created
 *
 * The dynamic rendering setup over the attachments: their views and the
 * load/store/clear config don't change for as long as the textures exist,
 * so it's rebuilt with them and fbo_prepare() only records the transitions.
 */
static void fbo_rendering_info_update(fbo_t *fbo)
{
    unsigned int nr_colors = 0;

    fa_for_each(fa, fbo->layout, texture) {
        int idx = fbo_attachment_color(fa);
        auto tex = &fbo->color_tex[idx];
        auto cfg = &fbo->color_config[idx];

        if (!tex->vk.view)  continue;

        fbo->vk.color_atts[nr_colors++] = vk_attachment_info(
            tex->vk.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            vk_load_op(cfg->load_action), vk_store_op(cfg->store_action),
            (VkClearValue){ .color = { .float32 = {
                cfg->clear_color[0], cfg->clear_color[1], cfg->clear_color[2], cfg->clear_color[3]
            } } }
        );
    }

    bool has_depth = fbo->layout.depth_texture && fbo->depth_tex.vk.view;
    if (has_depth)
        fbo->vk.depth_att = vk_attachment_info(
            fbo->depth_tex.vk.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            vk_load_op(fbo->depth_config.load_action), vk_store_op(fbo->depth_config.store_action),
            (VkClearValue){ .depthStencil = { .depth = fbo->depth_config.clear_depth } }
        );

    fbo->vk.rendering_info = (VkRenderingInfo){
        .sType                  = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea             = { .extent = { fbo->width, fbo->height } },
        /* layered target: the geometry shader routes shadow cascades */
        .layerCount             = max(fbo->layers, 1u),
        .colorAttachmentCount   = nr_colors,
        .pColorAttachments      = fbo->vk.color_atts,
        .pDepthAttachment       = has_depth ? &fbo->vk.depth_att : NULL,
    };
}

/**
 * fbo_attachments_init() - create all the attachments at the FBO's current size
 * @fbo:    the FBO
 *
 * Return: CERR_OK with every attachment created and the rendering info
 *         rebuilt, or the first error with none of them left behind
 */
static cerr fbo_attachments_init(fbo_t *fbo)
{
    if (fbo->layout.depth_texture)
        CERR_RET(fbo_depth_texture_init(fbo), { fbo_attachments_deinit(fbo); return __cerr; });
    if (fbo->layout.color_textures)
        CERR_RET(fbo_textures_init(fbo, fbo->layout), { fbo_attachments_deinit(fbo); return __cerr; });

    fbo_rendering_info_update(fbo);

    return CERR_OK;
}

static void vk_fbo_destroy(fbo_t *fbo)
{
    fbo_attachments_deinit(fbo);
    mem_free(fbo->color_config);
}

static cresp(fbo_t) vk_fbo_create(const fbo_init_options *opts)
{
    if (!opts || !opts->width || !opts->height) return cresp_error(fbo_t, CERR_INVALID_ARGUMENTS);

    fbo_t *fbo = ref_new(fbo);
    if (!fbo)   return cresp_error(fbo_t, CERR_NOMEM);

    fbo->renderer     = opts->renderer;
    fbo->width        = opts->width;
    fbo->height       = opts->height;
    fbo->layers       = opts->layers ? : 1;
    fbo->depth_config = opts->depth_config;
    fbo->nr_samples   = opts->nr_samples ? : 1;
    fbo->layout       = opts->layout;
    fbo->vk.name      = opts->name ? : "<unset>";

    if (!fbo->layout.mask)  fbo->layout.color_texture0 = 1;

    unsigned int nr_color_configs = fa_nr_color_buffer(fbo->layout) ? :
                                    fa_nr_color_texture(fbo->layout);
    if (nr_color_configs) {
        if (opts->color_config) {
            fbo->color_config = memdup(opts->color_config,  nr_color_configs * sizeof(fbo_attconfig));
        } else {
            fbo->color_config = mem_alloc(sizeof(fbo_attconfig), .nr = nr_color_configs);
            for (unsigned int i = 0; fbo->color_config && i < nr_color_configs; i++)
                fbo->color_config[i] = (fbo_attconfig){ .format = TEX_FMT_DEFAULT };
        }
        if (!fbo->color_config) {
            ref_put_last(fbo);
            return cresp_error(fbo_t, CERR_NOMEM);
        }
    }

    CERR_RET(fbo_attachments_init(fbo), { ref_put_last(fbo); return cresp_error_cerr(fbo_t, __cerr); });

    return cresp_val(fbo_t, fbo);
}

static void vk_fbo_prepare(fbo_t *fbo)
{
    auto r = fbo->renderer;

    if (!r->vk.cb_open) {
        err("fbo_prepare: no command buffer\n");
        return;
    }

    /* Passes don't nest: the pipeline runs before the swapchain pass */
    if (r->vk.pass_open) {
        err("fbo_prepare: %s: another pass is open\n", fbo->vk.name);
        return;
    }

    auto f = &r->vk.frames[r->vk.frame_idx];

    fa_for_each(fa, fbo->layout, texture) {
        auto tex = &fbo->color_tex[fbo_attachment_color(fa)];

        if (tex->vk.view)   vk_texture_layout_to(f->cmd_buf, tex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    if (fbo->layout.depth_texture && fbo->depth_tex.vk.view)
        vk_texture_layout_to(f->cmd_buf, &fbo->depth_tex, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    vkCmdBeginRendering(f->cmd_buf, &fbo->vk.rendering_info);
    r->vk.pass_open = true;
    r->vk.fbo = fbo;

    vk_viewport_apply(f->cmd_buf, fbo->width, fbo->height);
}

static void vk_fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
    auto r = fbo->renderer;

    if (r->vk.fbo != fbo) {
        err("fbo_done: wrong fbo: %s (%p)\n", fbo->vk.name, fbo);
        return;
    }

    auto f = &r->vk.frames[r->vk.frame_idx];

    if (r->vk.pass_open) {
        vkCmdEndRendering(f->cmd_buf);
        r->vk.pass_open = false;
    }

    /* Downstream passes sample what this one rendered */
    fa_for_each(fa, fbo->layout, texture) {
        auto tex = &fbo->color_tex[fbo_attachment_color(fa)];

        if (tex->vk.view)   vk_texture_layout_to(f->cmd_buf, tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (fbo->layout.depth_texture && fbo->depth_tex.vk.view)
        vk_texture_layout_to(f->cmd_buf, &fbo->depth_tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    r->vk.fbo = NULL;
}

static cerr vk_fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo)   return CERR_INVALID_ARGUMENTS;

    fbo_attachments_deinit(fbo);

    unsigned int orig_width = fbo->width;
    unsigned int orig_height = fbo->height;

    fbo->width = width;
    fbo->height = height;

    cerr err = fbo_attachments_init(fbo);
    if (!IS_CERR(err))  return CERR_OK;

    /* roll back to the previous size */
    fbo->width = orig_width;
    fbo->height = orig_height;
    CERR_RET_CERR(fbo_attachments_init(fbo));

    return err;
}

static bool vk_fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment)    { return !!attachment.mask; }

static texture_format vk_fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (!attachment.mask)   return TEX_FMT_MAX;

    return vk_fbo_texture_format(fbo, attachment);
}

static texture_t *vk_fbo_texture(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo)                       return fbo_texture_fallback(attachment);

    if (attachment.depth_texture)   return &fbo->depth_tex;

    if (attachment.color_textures) {
        int idx = fbo_attachment_color(attachment);
        if (idx >= 0 && idx < FBO_COLOR_ATTACHMENTS_MAX)    return &fbo->color_tex[idx];
    }

    return fbo_texture_fallback(attachment);
}

/****************************************************************************
 * Uniform buffers
 *
 * The WGPU slot-ring model crossed with Metal's per-frame regions: each
 * in-flight frame owns a region of VK_UBO_SLOTS_PER_FRAME slots, so a
 * frame's writes never touch memory a previous frame may still be reading
 * (Vulkan mapped writes aren't queue-ordered like wgpuQueueWriteBuffer())
 ****************************************************************************/

cerr_check _uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                               unsigned int count, const void *value);

static cerr vk_uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name,
                                   int binding)
{
    CERR_RET_CERR(ref_embed(uniform_buffer, ubo, .renderer = r, .name = name, .binding = binding));

    ubo->renderer = r;
    ubo->vk.binding = binding;
    list_append(&r->vk.ubos, &ubo->vk.entry);

    return CERR_OK;
}

/* Offset of the current slot in both the GPU buffer and the CPU shadow */
static inline size_t vk_ubo_offset(uniform_buffer_t *ubo)
{
    auto r = ubo->renderer;

    return r->vk.frame_idx * ubo->vk.frame_size + ubo->vk.item * ubo->vk.slot_size;
}

static cerr vk_uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size)
{
    /* There's no reason to ever want to reallocate a UBO */
    if (ubo->data || ubo->size) return CERR_INVALID_OPERATION;

    auto r = ubo->renderer;

    ubo->size = round_up(size, 16);
    ubo->vk.slot_size = round_up(ubo->size, 256);
    ubo->vk.frame_size = ubo->vk.slot_size * VK_UBO_SLOTS_PER_FRAME;

    size_t total_size = ubo->vk.frame_size * VK_INFLIGHT_FRAMES;

    CERR_RET_CERR(vk_buffer_create(r, total_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &ubo->vk.buf, &ubo->vk.mem));

    if (vkMapMemory(r->vk.device, ubo->vk.mem, 0, total_size, 0, &ubo->vk.mapped) != VK_SUCCESS)
        return CERR_INVALID_OPERATION_REASON(.fmt = "vkMapMemory failed");

    /* CPU shadow covers the full allocation */
    ubo->vk.shadow = mem_alloc(total_size, .zero = true);
    if (!ubo->vk.shadow)    return CERR_NOMEM;

    ubo->data = (char *)ubo->vk.shadow + vk_ubo_offset(ubo);

    return CERR_OK;
}

static void vk_uniform_buffer_free(uniform_buffer_t *ubo)
{
    auto r = ubo->renderer;

    if (ubo->vk.buf) {
        if (ubo->vk.mapped) vkUnmapMemory(r->vk.device, ubo->vk.mem);
        vk_zombie_add(r, (vk_zombie){ .buf = ubo->vk.buf, .mem = ubo->vk.mem });
        ubo->vk.buf = VK_NULL_HANDLE;
        ubo->vk.mem = VK_NULL_HANDLE;
        ubo->vk.mapped = NULL;
    }
    /* linked at init, with or without a GPU buffer */
    list_del(&ubo->vk.entry);

    if (ubo->vk.shadow) {
        mem_free(ubo->vk.shadow);
        ubo->vk.shadow = NULL;
        ubo->data = NULL;
    }

    ubo->size = 0;
    ubo->vk.frame_size = 0;
    ubo->vk.item = 0;
    ubo->dirty = false;
    ubo->vk.advance = false;
}

static void vk_uniform_buffer_done(uniform_buffer_t *ubo)       { vk_uniform_buffer_free(ubo); }
static void vk_uniform_buffer_destroy(uniform_buffer_t *ubo)    { vk_uniform_buffer_free(ubo); }

static cerr vk_uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points)   { return CERR_OK; }

static void vk_uniform_buffer_update(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    if (!ubo || !ubo->data || !ubo->size || !ubo->vk.mapped)    return;

    auto r = ubo->renderer;
    size_t offset = vk_ubo_offset(ubo);

    /* Publish the current slot; the memory is host-coherent */
    memcpy((char *)ubo->vk.mapped + offset, ubo->data, ubo->size);

    ubo->vk.advance = true;
    ubo->dirty = false;

    /* Track this UBO for descriptor set creation at draw time */
    int slot = ubo->vk.binding - VULKAN_BINDING_UBO_BASE;
    if (slot >= 0 && slot < BINDING_UBO_MAX)    r->vk.bound_ubos[slot] = ubo;
}

static cerr vk_uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset,
                                  size_t *size, unsigned int count, const void *value)
{
    if (!value || !ubo->vk.advance) goto out;

    /* Advance to a new slot on the first write after uniform_buffer_update() */
    void *prev_data = ubo->data;

    if (ubo->vk.item + 1 < VK_UBO_SLOTS_PER_FRAME)
        ubo->vk.item++;
    else
        warn_once("UBO %d ran out of slots (%u per frame)\n", ubo->vk.binding, VK_UBO_SLOTS_PER_FRAME);
    ubo->data = (char *)ubo->vk.shadow + vk_ubo_offset(ubo);

    /* COW: seed the new slot from the previous one */
    if (ubo->data != prev_data) memcpy(ubo->data, prev_data, ubo->size);
    ubo->vk.advance = false;

out:
    return _uniform_buffer_set(ubo, type, offset, size, count, value);
}

/* Rewind every UBO ring into the new frame's region */
static void vk_ubos_frame_advance(renderer_t *r)
{
    uniform_buffer_t *ubo;

    list_for_each_entry(ubo, &r->vk.ubos, vk.entry) {
        if (!ubo->data) continue;

        void *prev_data = ubo->data;

        ubo->vk.item = 0;
        ubo->data = (char *)ubo->vk.shadow + vk_ubo_offset(ubo);
        ubo->dirty = false;
        ubo->vk.advance = false;

        /* Seed the frame's first slot from the last one written */
        if (prev_data != ubo->data) memcpy(ubo->data, prev_data, ubo->size);
    }
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
    if (!draw)  return CERR_OK;

    auto r = shader->vk.renderer;

    /*
     * Clear the per-draw binding state so only resources bound for this
     * shader make it into the descriptor set
     */
    memset(r->vk.bound_textures, 0, sizeof(r->vk.bound_textures));
    memset(r->vk.bound_ubos, 0, sizeof(r->vk.bound_ubos));

    auto dc = CRES_RET_CERR(draw_control_get(r, r->vk.fbo, shader));
    draw_control_bind(dc);

    return CERR_OK;
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

/****************************************************************************
 * Drawing
 ****************************************************************************/

/**
 * vk_descriptor_write() - one descriptor write into a set
 * @ds:             the descriptor set
 * @binding:        set-0 binding number
 * @type:           the descriptor type
 * @image_info:     the image descriptor, NULL for a buffer binding
 * @buffer_info:    the buffer descriptor, NULL for an image binding
 *
 * Return: the write, for a batched vkUpdateDescriptorSets()
 */
static VkWriteDescriptorSet vk_descriptor_write(VkDescriptorSet ds, uint32_t binding, VkDescriptorType type,
                                                const VkDescriptorImageInfo *image_info,
                                                const VkDescriptorBufferInfo *buffer_info)
{
    return (VkWriteDescriptorSet){
        .sType              = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet             = ds,
        .dstBinding         = binding,
        .descriptorCount    = 1,
        .descriptorType     = type,
        .pImageInfo         = image_info,
        .pBufferInfo        = buffer_info,
    };
}

/**
 * vk_renderer_draw() - the draw op: descriptor set, pipeline, buffers, then the indexed draw
 * @r:              renderer
 * @draw_type:      primitive type
 * @nr_faces:       faces to draw (the whole index buffer)
 * @idx_type:       index element type
 * @nr_instances:   instances to draw
 *
 * Return: CERR_OK, CERR_INVALID_OPERATION without an open pass, draw_control
 *         or index buffer, CERR_NOMEM when the frame's descriptor pool is
 *         exhausted
 */
static cerr vk_renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                             data_type idx_type, unsigned int nr_instances)
{
    if (!r->vk.pass_open || !r->vk.dc || !r->va || !r->va->index)
        return CERR_INVALID_OPERATION_REASON(
            .fmt    = "pass(%d)/draw_control(%p)/index buffer(%p) not bound",
            .arg0   = (void *)(uintptr_t)r->vk.pass_open,
            .arg1   = r->vk.dc,
            .arg2   = r->va ? r->va->index : NULL
        );

    buffer_t *index = r->va->index;
    buffer_t *vbuf = r->va->vk.vbuf;

    if (!buffer_loaded(index) || !vbuf || !buffer_loaded(vbuf))
        return CERR_INVALID_OPERATION_REASON(.fmt = "vertex/index buffers not loaded");

    auto f = &r->vk.frames[r->vk.frame_idx];

    /* Pick the blend variant here: blend state may change after shader_use() */
    vkCmdBindPipeline(f->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      r->vk.dc->vk.pipeline[r->blend ? 1 : 0]);
    vkCmdSetCullMode(f->cmd_buf, vk_cull_mode(r->vk.cull));

    /* One descriptor set per draw: every slot written, dummies where unbound */
    VkDescriptorSetAllocateInfo ds_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = f->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &r->vk.desc_layout,
    };
    VkDescriptorSet ds;
    VkResult res = vkAllocateDescriptorSets(r->vk.device, &ds_info, &ds);
    if (res != VK_SUCCESS)
        return CERR_NOMEM_REASON(
            .fmt    = "vkAllocateDescriptorSets failed: %d",
            .arg0   = (void *)(intptr_t)res
        );

    VkDescriptorImageInfo image_infos[VULKAN_BINDING_UBO_BASE];
    VkDescriptorBufferInfo buffer_infos[BINDING_UBO_MAX];
    VkWriteDescriptorSet writes[VULKAN_BINDING_UBO_BASE + BINDING_UBO_MAX];
    uint32_t n = 0;

    for (unsigned int i = 0; i < VULKAN_BINDING_UBO_BASE; i++) {
        texture_t *tex = i < array_size(r->vk.bound_textures) ?
            r->vk.bound_textures[i] : NULL;
        bool live = tex && texture_loaded(tex) && tex->vk.view;

        image_infos[i] = (VkDescriptorImageInfo){
            .sampler        = live ? tex->vk.sampler : r->vk.dummy_sampler,
            .imageView      = live ? tex->vk.view : r->vk.dummy_view,
            .imageLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        writes[n++] = vk_descriptor_write(ds, i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &image_infos[i], NULL);
    }

    for (unsigned int i = 0; i < BINDING_UBO_MAX; i++) {
        uniform_buffer_t *ubo = r->vk.bound_ubos[i];
        bool live = ubo && ubo->vk.buf;

        buffer_infos[i] = (VkDescriptorBufferInfo){
            .buffer = live ? ubo->vk.buf : r->vk.dummy_ubo,
            .offset = live ? vk_ubo_offset(ubo) : 0,
            .range  = live ? ubo->size : 256,
        };
        writes[n++] = vk_descriptor_write(ds, VULKAN_BINDING_UBO_BASE + i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                          NULL, &buffer_infos[i]);
    }

    vkUpdateDescriptorSets(r->vk.device, n, writes, 0, NULL);
    vkCmdBindDescriptorSets(f->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r->vk.pipeline_layout, 0, 1, &ds, 0, NULL);

    VkDeviceSize vbuf_offset = 0;
    vkCmdBindVertexBuffers(f->cmd_buf, 0, 1, &vbuf->vk.buf, &vbuf_offset);
    vkCmdBindIndexBuffer(f->cmd_buf, index->vk.buf, 0,
                         data_comp_size(index->comp_type) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

    /* Draw the whole index buffer, same as the WGPU backend */
    size_t idx_count = index->vk.size / data_comp_size(index->comp_type);
    vkCmdDrawIndexed(f->cmd_buf, idx_count, max(nr_instances, 1u), 0, 0, 0);

    return CERR_OK;
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
    CERR_RET_CERR(vk_descriptors_init(renderer));
    CERR_RET_CERR(vk_dummies_init(renderer));

    /*
     * No viewport has been set yet: size the first swapchain to the window's
     * framebuffer, or a surface without a fixed extent (wayland) would get one
     * clamped to minImageExtent, only to be recreated at the first frame
     */
    glfwGetFramebufferSize(opts->window, &renderer->width, &renderer->height);
    CERR_RET_CERR(vk_swapchain_create(renderer));

    return CERR_OK;
}
