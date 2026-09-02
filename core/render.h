/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_RENDER_H__
#define __CLAP_RENDER_H__

#include <stddef.h>
#include <stdlib.h>
#include "error.h"
#include "linmath.h"
#include "object.h"
#include "datatypes.h"
#include "typedef.h"
#include "config.h"

/*
 * These are separate from shader_binding_renderers, because:
 * - these nothing to do with shader bindings
 * - shader binding constants only exist if corresponding renderer is enabled
 */
typedef enum {
    RENDER_OPENGL,
    RENDER_METAL,
    RENDER_WGPU,
    RENDER_VULKAN,
    RENDER_MAX
} render_backend;

typedef struct renderer_caps {
    render_backend  renderer;
    bool            ndc_y_down;
    bool            ndc_z_zero_one;
    bool            origin_top_left;
} renderer_caps;

/** RENDERER_PASS_TIMERS_MAX - passes a backend can time on the GPU in one frame */
#define RENDERER_PASS_TIMERS_MAX 64
/** GL_INFLIGHT_FRAMES - depth of the GL timer query ring: results trail by frames on WebGL */
#define GL_INFLIGHT_FRAMES 4

#ifndef CONFIG_FINAL
/**
 * struct renderer_pass_times - how long each pass took on the GPU
 * @names:      pass labels, borrowed from the fbos, not owned
 * @ms:         GPU time per pass, in milliseconds
 * @count:      passes timed
 * @total_ms:   the whole frame where a backend can only measure that much,
 *              0 when it can't measure at all
 *
 * Filled in by whichever backend can measure it and displayed by the shared
 * "renderer" debug module.
 */
typedef struct renderer_pass_times {
    const char      *names[RENDERER_PASS_TIMERS_MAX];
    float           ms[RENDERER_PASS_TIMERS_MAX];
    unsigned int    count;
    float           total_ms;
} renderer_pass_times;
#endif /* !CONFIG_FINAL */

#ifdef CONFIG_RENDERER_OPENGL
# ifdef IMPLEMENTOR
#  if defined(CONFIG_BROWSER) || defined(CONFIG_GLES)
#   include <GLES3/gl3.h>
#   include <EGL/egl.h>
#   define GL_GLEXT_PROTOTYPES
#   include <GLES2/gl2ext.h>
#   ifndef GL_CLAMP_TO_BORDER
#    define GL_CLAMP_TO_BORDER GL_CLAMP_TO_EDGE
#   endif /* !GL_CLAMP_TO_BORDER */
#  else /* Full GL */
#   ifdef __APPLE__
#    define GL_SILENCE_DEPRECATION 1
#    include <OpenGL/gl3.h>
#    include <OpenGL/gl3ext.h>
#   else
#    include <glad/glad.h>
#   endif /* __APPLE__ */
#  endif
# else /* !IMPLEMENTOR */
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned short GLushort;
typedef ptrdiff_t GLsizeiptr;
# endif /* !IMPLEMENTOR */
#endif /* CONFIG_RENDERER_OPENGL */

#ifdef CONFIG_RENDERER_METAL
# if defined(IMPLEMENTOR) && defined(__OBJC__)
/*
 * XXX: "weak" clashes with something in the guts of Metal, rename it in
 * the tree
 */
#  undef weak
#  import <Metal/Metal.h>
#  import <QuartzCore/CoreAnimation.h>
#  import <CoreGraphics/CGColorSpace.h>
typedef id<MTLDevice> mtl_device_t;
typedef id<MTLLibrary> mtl_library_t;
typedef id<MTLFunction> mtl_function_t;
typedef id<MTLCommandQueue> mtl_command_queue_t;
typedef id<MTLCommandBuffer> mtl_command_buffer_t;
typedef id<MTLCounterSampleBuffer> mtl_counter_sample_buffer_t;
typedef id<MTLRenderCommandEncoder> mtl_render_command_encoder_t;
typedef MTLRenderPassDescriptor *mtl_render_pass_descriptor_t;
typedef MTLRenderPipelineDescriptor *mtl_render_pipeline_descriptor_t;
typedef MTLRenderPipelineReflection *mtl_render_pipeline_reflection_t;
typedef MTLDepthStencilDescriptor *mtl_depth_stencil_descriptor_t;
typedef id<MTLDepthStencilState> mtl_depth_stencil_state_t;
typedef id<MTLRenderPipelineState> mtl_render_pipeline_state_t;
typedef MTLVertexDescriptor *mtl_vertex_descriptor_t;
typedef id<CAMetalDrawable> mtl_ca_drawable_t;
typedef id<MTLSamplerState> mtl_sampler_state_t;
typedef id<MTLTexture> mtl_texture_t;
typedef CAMetalLayer *mtl_ca_layer_t;
typedef id<MTLBuffer> mtl_buffer_t;
typedef CGColorSpaceRef cg_colorspace_ref_t;
typedef NSAutoreleasePool *ns_autorelease_pool_t;
# else /* !IMPLEMENTOR || !__OBJC__ */
typedef void *mtl_device_t;
typedef void *mtl_library_t;
typedef void *mtl_function_t;
typedef void *mtl_command_queue_t;
typedef void *mtl_command_buffer_t;
typedef void *mtl_counter_sample_buffer_t;
typedef void *mtl_render_command_encoder_t;
typedef void *mtl_render_pass_descriptor_t;
typedef void *mtl_render_pipeline_descriptor_t;
typedef void *mtl_render_pipeline_reflection_t;
typedef void *mtl_depth_stencil_descriptor_t;
typedef void *mtl_depth_stencil_state_t;
typedef void *mtl_render_pipeline_state_t;
typedef void *mtl_vertex_descriptor_t;
typedef void *mtl_ca_drawable_t;
typedef void *mtl_ca_layer_t;
typedef void *mtl_texture_t;
typedef void *mtl_sampler_state_t;
typedef void *mtl_buffer_t;
typedef void *cg_colorspace_ref_t;
typedef void *ns_autorelease_pool_t;
#  ifndef __OBJC__
typedef void *dispatch_semaphore_t;
#  endif /* !__OBJC__ */
# endif /* !IMPLEMENTOR || !__OBJC__ */
// Metal's integral types are landmines between C and ObjC
typedef uint64_t mtl_cull_mode_t;
#endif /* CONFIG_RENDERER_METAL */

#ifdef CONFIG_RENDERER_WGPU
# include <stdatomic.h>
# include "bindings/render-bindings.h" /* for BINDING_TEXTURE_MAX / BINDING_UBO_MAX */
enum { WGPU_VERTEX_ATTRIBUTE_SIZE = 24, WGPU_VERTEX_ATTRIBUTE_ALIGN = 8 };
/*
 * WGPURenderPass{ColorAttachment,DepthStencilAttachment,Descriptor}, kept by
 * value in fbo_t and mirrored below for the non-IMPLEMENTOR side
 */
enum {
    WGPU_COLOR_ATTACHMENT_SIZE  = 56, WGPU_COLOR_ATTACHMENT_ALIGN = 8,
    WGPU_DEPTH_ATTACHMENT_SIZE  = 40, WGPU_DEPTH_ATTACHMENT_ALIGN = 4,
    WGPU_PASS_DESCRIPTOR_SIZE   = 32, WGPU_PASS_DESCRIPTOR_ALIGN  = 4,
};
# ifdef IMPLEMENTOR
#  include <webgpu/webgpu.h>
typedef WGPUInstance wgpu_instance_t;
typedef WGPUAdapter wgpu_adapter_t;
typedef WGPUDevice wgpu_device_t;
typedef WGPUQueue wgpu_queue_t;
typedef WGPURenderPassEncoder wgpu_render_pass_encoder_t;
typedef WGPUSurface wgpu_surface_t;
typedef WGPUTexture wgpu_texture_t;
typedef WGPUTextureView wgpu_texture_view_t;
typedef WGPUCommandEncoder wgpu_command_encoder_t;
typedef WGPUTextureFormat wgpu_texture_format_t;
static_assert(sizeof(wgpu_texture_format_t) == sizeof(int), "WGPUTextureFormat width mismatch");
typedef WGPUShaderModule wgpu_shader_module_t;
typedef WGPUBuffer wgpu_buffer_t;
typedef WGPUSampler wgpu_sampler_t;
typedef WGPURenderPipeline wgpu_render_pipeline_t;
typedef WGPUVertexAttribute wgpu_vertex_attribute_t;
static_assert(sizeof(WGPUVertexAttribute) == WGPU_VERTEX_ATTRIBUTE_SIZE,
              "WGPUVertexAttribute size mismatch");
static_assert(_Alignof(WGPUVertexAttribute) == WGPU_VERTEX_ATTRIBUTE_ALIGN,
              "WGPUVertexAttribute align mismatch");
typedef WGPURenderPassColorAttachment wgpu_color_attachment_t;
typedef WGPURenderPassDepthStencilAttachment wgpu_depth_attachment_t;
typedef WGPURenderPassDescriptor wgpu_pass_descriptor_t;
static_assert(sizeof(WGPURenderPassColorAttachment) == WGPU_COLOR_ATTACHMENT_SIZE &&
              _Alignof(WGPURenderPassColorAttachment) == WGPU_COLOR_ATTACHMENT_ALIGN,
              "WGPURenderPassColorAttachment layout mismatch");
static_assert(sizeof(WGPURenderPassDepthStencilAttachment) == WGPU_DEPTH_ATTACHMENT_SIZE &&
              _Alignof(WGPURenderPassDepthStencilAttachment) == WGPU_DEPTH_ATTACHMENT_ALIGN,
              "WGPURenderPassDepthStencilAttachment layout mismatch");
static_assert(sizeof(WGPURenderPassDescriptor) == WGPU_PASS_DESCRIPTOR_SIZE &&
              _Alignof(WGPURenderPassDescriptor) == WGPU_PASS_DESCRIPTOR_ALIGN,
              "WGPURenderPassDescriptor layout mismatch");
# else /* !IMPLEMENTOR */
typedef void *wgpu_instance_t;
typedef void *wgpu_adapter_t;
typedef void *wgpu_device_t;
typedef void *wgpu_queue_t;
typedef void *wgpu_render_pass_encoder_t;
typedef void *wgpu_surface_t;
typedef void *wgpu_texture_t;
typedef void *wgpu_texture_view_t;
typedef void *wgpu_command_encoder_t;
typedef int wgpu_texture_format_t;
typedef void *wgpu_shader_module_t;
typedef void *wgpu_buffer_t;
typedef void *wgpu_sampler_t;
typedef void *wgpu_render_pipeline_t;
typedef struct { _Alignas(WGPU_VERTEX_ATTRIBUTE_ALIGN) char _[WGPU_VERTEX_ATTRIBUTE_SIZE]; } wgpu_vertex_attribute_t;
typedef struct { _Alignas(WGPU_COLOR_ATTACHMENT_ALIGN) char _[WGPU_COLOR_ATTACHMENT_SIZE]; } wgpu_color_attachment_t;
typedef struct { _Alignas(WGPU_DEPTH_ATTACHMENT_ALIGN) char _[WGPU_DEPTH_ATTACHMENT_SIZE]; } wgpu_depth_attachment_t;
typedef struct { _Alignas(WGPU_PASS_DESCRIPTOR_ALIGN) char _[WGPU_PASS_DESCRIPTOR_SIZE]; } wgpu_pass_descriptor_t;
# endif /* !IMPLEMENTOR */
#endif /* CONFIG_RENDERER_WGPU */

#ifdef CONFIG_RENDERER_VULKAN
# include "bindings/render-bindings.h" /* for BINDING_TEXTURE_MAX / BINDING_UBO_MAX */
/** VK_INFLIGHT_FRAMES - frames the Vulkan backend records ahead of the GPU */
# define VK_INFLIGHT_FRAMES 3
/** VK_SWAPCHAIN_IMAGES_MAX - swapchain images the Vulkan backend can track */
# define VK_SWAPCHAIN_IMAGES_MAX 8
/* VkRenderingAttachmentInfo / VkRenderingInfo, mirrored below for the non-IMPLEMENTOR side */
enum { VK_RENDERING_ATTACHMENT_INFO_SIZE = 72, VK_RENDERING_INFO_SIZE = 72, VK_RENDERING_INFO_ALIGN = 8 };
# ifdef IMPLEMENTOR
#  include <vulkan/vulkan.h>
/* Dispatchable handles are always pointers */
typedef VkInstance vk_instance_t;
typedef VkPhysicalDevice vk_physical_device_t;
typedef VkDevice vk_device_t;
typedef VkQueue vk_queue_t;
typedef VkCommandBuffer vk_command_buffer_t;
/*
 * Non-dispatchable handles are pointers on 64-bit and uint64_t on 32-bit;
 * the non-IMPLEMENTOR mirrors below use uint64_t, which matches both in
 * size and alignment
 */
typedef VkSurfaceKHR vk_surface_t;
typedef VkSwapchainKHR vk_swapchain_t;
typedef VkImage vk_image_t;
typedef VkImageView vk_image_view_t;
typedef VkSampler vk_sampler_t;
typedef VkDeviceMemory vk_device_memory_t;
typedef VkBuffer vk_buffer_t;
typedef VkShaderModule vk_shader_module_t;
typedef VkPipeline vk_pipeline_t;
typedef VkPipelineLayout vk_pipeline_layout_t;
typedef VkDescriptorSetLayout vk_descriptor_set_layout_t;
typedef VkDescriptorSet vk_descriptor_set_t;
typedef VkDescriptorPool vk_descriptor_pool_t;
typedef VkCommandPool vk_command_pool_t;
typedef VkSemaphore vk_semaphore_t;
typedef VkFence vk_fence_t;
typedef VkDebugUtilsMessengerEXT vk_debug_utils_messenger_t;
typedef VkRenderingAttachmentInfo vk_rendering_attachment_info_t;
typedef VkRenderingInfo vk_rendering_info_t;
typedef VkFormat vk_format_t;
typedef VkColorSpaceKHR vk_color_space_t;
typedef VkImageLayout vk_image_layout_t;
typedef VkQueryPool vk_query_pool_t;
static_assert(sizeof(VkImage) == sizeof(uint64_t), "VkImage handle width mismatch");
static_assert(sizeof(VkInstance) == sizeof(void *), "VkInstance handle width mismatch");
static_assert(sizeof(VkRenderingAttachmentInfo) == VK_RENDERING_ATTACHMENT_INFO_SIZE,
              "VkRenderingAttachmentInfo size mismatch");
static_assert(sizeof(VkRenderingInfo) == VK_RENDERING_INFO_SIZE, "VkRenderingInfo size mismatch");
static_assert(_Alignof(VkRenderingAttachmentInfo) == VK_RENDERING_INFO_ALIGN &&
              _Alignof(VkRenderingInfo) == VK_RENDERING_INFO_ALIGN, "VkRenderingInfo alignment mismatch");
static_assert(sizeof(VkFormat) == sizeof(int), "VkFormat width mismatch");
static_assert(sizeof(VkColorSpaceKHR) == sizeof(int), "VkColorSpaceKHR width mismatch");
static_assert(sizeof(VkImageLayout) == sizeof(int), "VkImageLayout width mismatch");
# else /* !IMPLEMENTOR */
typedef void *vk_instance_t;
typedef void *vk_physical_device_t;
typedef void *vk_device_t;
typedef void *vk_queue_t;
typedef void *vk_command_buffer_t;
typedef uint64_t vk_surface_t;
typedef uint64_t vk_swapchain_t;
typedef uint64_t vk_image_t;
typedef uint64_t vk_image_view_t;
typedef uint64_t vk_sampler_t;
typedef uint64_t vk_device_memory_t;
typedef uint64_t vk_buffer_t;
typedef uint64_t vk_shader_module_t;
typedef uint64_t vk_pipeline_t;
typedef uint64_t vk_pipeline_layout_t;
typedef uint64_t vk_descriptor_set_layout_t;
typedef uint64_t vk_descriptor_set_t;
typedef uint64_t vk_descriptor_pool_t;
typedef uint64_t vk_command_pool_t;
typedef uint64_t vk_semaphore_t;
typedef uint64_t vk_fence_t;
typedef uint64_t vk_debug_utils_messenger_t;
/* the C++ side (ui-imgui-vk.cpp) sees these too, hence no _Alignas */
typedef struct { char _[VK_RENDERING_ATTACHMENT_INFO_SIZE]; } __attribute__((aligned(VK_RENDERING_INFO_ALIGN)))
    vk_rendering_attachment_info_t;
typedef struct { char _[VK_RENDERING_INFO_SIZE]; } __attribute__((aligned(VK_RENDERING_INFO_ALIGN)))
    vk_rendering_info_t;
typedef int vk_format_t;
typedef int vk_color_space_t;
typedef int vk_image_layout_t;
typedef uint64_t vk_query_pool_t;
# endif /* !IMPLEMENTOR */
/**
 * struct vk_vertex_attr - vertex attribute description for pipeline creation
 * @format:     the attribute's VkFormat
 * @offset:     byte offset within the vertex
 * @location:   shader input location
 */
typedef struct vk_vertex_attr {
    vk_format_t     format;
    unsigned int    offset;
    unsigned int    location;
} vk_vertex_attr_t;

/*
 * Typed dummy images: a statically used sampler must see an image of the
 * declared type, so descriptor slots with nothing (compatible) bound get a
 * dummy of the matching dimensionality/multisampledness
 */
enum vk_dummy_kind {
    VK_DUMMY_2D = 0,
    VK_DUMMY_2D_MS,
    VK_DUMMY_2D_ARRAY,
    VK_DUMMY_2D_ARRAY_MS,
    VK_DUMMY_3D,
    VK_DUMMY_MAX,
};

/* Per texture binding image traits, collected from the shader's SPIR-V */
/** VK_TEX_DECLARED - the shader declares a sampler at this binding */
#define VK_TEX_DECLARED (1 << 0)
/** VK_TEX_MS - the sampler expects a multisampled image */
#define VK_TEX_MS       (1 << 1)
/** VK_TEX_ARRAYED - the sampler expects an array image */
#define VK_TEX_ARRAYED  (1 << 2)
/** VK_TEX_3D - the sampler expects a 3D image */
#define VK_TEX_3D       (1 << 3)
#endif /* CONFIG_RENDERER_VULKAN */

typedef enum buffer_type {
    BUF_ARRAY,
    BUF_ELEMENT_ARRAY,
} buffer_type;

typedef enum buffer_usage {
    BUF_STATIC,
    BUF_DYNAMIC
} buffer_usage;

typedef int uniform_t;
typedef int attr_t;

enum {
    UA_UNKNOWN     = -2,
    UA_NOT_PRESENT = -1,
};

TYPE_FORWARD(buffer);
TYPE_FORWARD(renderer);

typedef struct buffer_init_options {
    renderer_t          *renderer;
    buffer_type         type;
    buffer_usage        usage;
    data_type           comp_type;
    unsigned int        comp_count;
    uniform_t           loc;
    /* offset of the attribute in an interleaved buffer */
    unsigned int        off;
    /* bytes until the next element of the attribute */
    unsigned int        stride;
    /* the buffer that contains all interleaved attributes */
    buffer_t            *main;
    void                *data;
    size_t              size;
    const char          *name;
} buffer_init_options;

TYPE(buffer,
    struct ref          ref;
    renderer_t          *renderer;
    buffer_t            *main;
    size_t              off;
    unsigned int        comp_count;
    data_type           comp_type;
#ifdef CONFIG_RENDERER_OPENGL
    struct {
        GLenum          type;
        GLenum          usage;
        GLuint          id;
        GLsizei         stride;
    } gl;
#endif /* CONFIG_RENDERER_OPENGL */
#ifdef CONFIG_RENDERER_WGPU
    struct {
        wgpu_buffer_t   buf;
        size_t          size;
        buffer_type     type;
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */
#ifdef CONFIG_RENDERER_METAL
    struct {
        mtl_buffer_t    buf;
        size_t          stride;
        size_t          size;
        char            *name;
    } mtl;
#endif  /* CONFIG_RENDERER_METAL */
#ifdef CONFIG_RENDERER_VULKAN
    struct {
        vk_buffer_t         buf;
        vk_device_memory_t  mem;
        size_t              size;
        buffer_type         type;
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */
    uniform_t           loc;
    int                 use_count;
    bool                loaded;
#ifndef CONFIG_FINAL
    buffer_init_options opts;
#endif /* CONFIG_FINAL */
);

#define buffer_init(_b, args...) \
    _buffer_init((_b), &(buffer_init_options){ args })
cerr_check _buffer_init(buffer_t *buf, const buffer_init_options *opts);
void buffer_deinit(buffer_t *buf);
void buffer_bind(buffer_t *buf, uniform_t loc);
void buffer_unbind(buffer_t *buf, uniform_t loc);
bool buffer_loaded(buffer_t *buf);
#ifdef CONFIG_FINAL
static inline cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...) { return cres_val(int, 0); }
#else
cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...);
#endif /* !CONFIG_FINAL */

TYPE(vertex_array,
    struct ref  ref;
    renderer_t      *renderer;
    buffer_t        *index;
#ifdef CONFIG_RENDERER_OPENGL
    struct {
        GLuint      vao;
    } gl;
#endif /* CONFIG_RENDERER_OPENGL */
#ifdef CONFIG_RENDERER_WGPU
    struct {
        buffer_t        *vbuf;
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */
#ifdef CONFIG_RENDERER_VULKAN
    struct {
        buffer_t        *vbuf;
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */
);

DEFINE_REFCLASS_INIT_OPTIONS(vertex_array,
    renderer_t  *renderer;
);

cerr_check vertex_array_init(vertex_array_t *va, renderer_t *r);
void vertex_array_done(vertex_array_t *va);
void vertex_array_bind(vertex_array_t *va);
void vertex_array_unbind(vertex_array_t *va);

TYPE_FORWARD(shader);
TYPE_FORWARD(fbo);
TYPE_FORWARD(draw_control);

typedef enum texture_type {
    TEX_2D,         /* default comes first */
    TEX_2D_ARRAY,
    TEX_3D,
    TEX_1D,
} texture_type;

typedef enum texture_wrap {
    TEX_CLAMP_TO_EDGE,
    TEX_CLAMP_TO_BORDER,
    TEX_WRAP_REPEAT,
    TEX_WRAP_MIRRORED_REPEAT,
} texture_wrap;

typedef enum texture_filter {
    TEX_FLT_LINEAR,
    TEX_FLT_NEAREST,
    /* TODO {LINEAR,NEAREST}_MIPMAP_{NEAREST,LINEAR} */
} texture_filter;

typedef enum texture_format {
    TEX_FMT_DEFAULT,
    TEX_FMT_RGBA8 = TEX_FMT_DEFAULT,
    TEX_FMT_RGB8,
    TEX_FMT_RGBA8_SRGB,
    TEX_FMT_RGB8_SRGB,
    TEX_FMT_RGBA16,
    TEX_FMT_RGBA16F,
    TEX_FMT_RGB16F,
    TEX_FMT_RGBA32F,
    TEX_FMT_RGB32F,
    TEX_FMT_DEPTH16F,
    TEX_FMT_DEPTH24F,
    TEX_FMT_DEPTH32F,
    TEX_FMT_R8,
    TEX_FMT_R16F,
    TEX_FMT_R32F,
    TEX_FMT_RG8,
    TEX_FMT_RG16,
    TEX_FMT_RG16F,
    TEX_FMT_RG32F,
    TEX_FMT_R32UI,
    TEX_FMT_RG32UI,
    TEX_FMT_RGBA32UI,
    TEX_FMT_BGRA8,
    TEX_FMT_BGR10A2,
    TEX_FMT_BGRA10XR,
    TEX_FMT_MAX,
} texture_format;

cres_ret(texture_format);

typedef struct texture_init_options {
    renderer_t          *renderer;
    unsigned int        target;
    texture_type        type;
    texture_wrap        wrap;
    texture_filter      min_filter;
    texture_filter      mag_filter;
    texture_format      format;
    unsigned int        layers;
    bool                multisampled;
    bool                render_target;
    float               *border;
    const char          *name;
} texture_init_options;

TYPE(texture,
    struct ref      ref;
    renderer_t      *renderer;
    unsigned int    width;
    unsigned int    height;
    unsigned int    layers;
    bool            loaded;
    bool            multisampled;
#ifdef CONFIG_RENDERER_OPENGL
    struct {
        GLuint          id;
        GLenum          format;
        GLenum          internal_format;
        GLenum          component_type;
        GLenum          type;
        GLint           wrap;
        GLint           min_filter;
        GLint           mag_filter;
        GLint           target;
        bool            sampler_updated;
        float           border[4];
    } gl;
#endif /* CONFIG_RENDERER_OPENGL */

#ifdef CONFIG_RENDERER_WGPU
    struct {
        wgpu_texture_t      texture;
        wgpu_texture_view_t view;
        wgpu_sampler_t      sampler;
        texture_type        type;
        texture_format      format;
        bool                render_target;
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */

#ifdef CONFIG_RENDERER_METAL
    struct {
        mtl_texture_t       texture;
        mtl_sampler_state_t sampler;
        texture_type        type;
        texture_format      format;
        int                 target;
        bool                render_target;
        char                *name;
    } mtl;
#endif /* CONFIG_RENDERER_METAL */

#ifdef CONFIG_RENDERER_VULKAN
    struct {
        vk_image_t          image;
        vk_device_memory_t  mem;
        vk_image_view_t     view;
        vk_sampler_t        sampler;
        /* per-frame reloaders keep a persistently mapped staging ring */
        vk_buffer_t         staging_buf;
        vk_device_memory_t  staging_mem;
        void                *staging_map;
        size_t              staging_slice;
#ifndef CONFIG_FINAL
        /* imgui binding for debug previews; owned, epoch-checked */
        vk_descriptor_set_t imgui_ds;
        unsigned int        imgui_ds_epoch;
#endif /* !CONFIG_FINAL */
        vk_image_layout_t   layout;     /* current image layout */
        texture_type        type;
        texture_format      format;
        bool                render_target;
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */

#ifndef CONFIG_FINAL
    texture_init_options    opts;
#endif /* CONFIG_FINAL */
);

typedef uint64_t texid_t;

bool texture_format_supported(renderer_t *r, texture_format format);
const char *texture_format_string(texture_format fmt);
size_t texture_format_comp_size(texture_format fmt);
size_t texture_format_nr_comps(texture_format fmt);
bool texture_format_needs_alpha(texture_format format);
texture_format texture_format_add_alpha(texture_format format);
/* bitmask of formats that texture_format_next_match() has already returned */
typedef uint64_t texture_format_iter;
cres(texture_format) texture_format_next_match(texture_format_iter *iter, size_t min_width,
                                               size_t min_components, data_type_mask type_mask,
                                               bool depth, bool stencil);
void *texture_rgb_to_rgba(texture_format format, const void *buf,
                          size_t nr_pixels, size_t *out_size);
cerr_check _texture_init(texture_t *tex, const texture_init_options *opts);
#define texture_init(_t, args...) \
    _texture_init((_t), &(texture_init_options){ args })
void texture_deinit(texture_t *tex);
void texture_done(texture_t *tex);
cerr_check texture_load(texture_t *tex, texture_format format,
                        unsigned int width, unsigned int height, void *buf);
cerr_check texture_resize(texture_t *tex, unsigned int width, unsigned int height);
texid_t texture_id(texture_t *tex);
void texture_bind(texture_t *tex, unsigned int target, uniform_t uniform);
void texture_unbind(texture_t *tex, unsigned int target);
void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight);
bool texture_loaded(texture_t *tex);
bool texture_is_array(texture_t *tex);
bool texture_is_multisampled(texture_t *tex);
texture_t *texture_clone(texture_t *tex);
cerr_check texture_pixel_init(renderer_t *renderer, texture_t *tex, float color[4]);
void textures_init(renderer_t *renderer);
void textures_done(void);
texture_t *white_pixel(void);
texture_t *black_pixel(void);
texture_t *transparent_pixel(void);
texture_t *grey_3d_pixel(void);
texture_t *black_3d_pixel(void);
texture_t *black_uint_pixel(void);
#ifdef CONFIG_FINAL
static inline cres(int) texture_set_name(texture_t *tex, const char *fmt, ...) { return cres_val(int, 0); }
#else
cres(int) texture_set_name(texture_t *tex, const char *fmt, ...);
#endif /* CONFIG_FINAL */


/*
 * fbo_attachment describes both individual attachments
 * and fbo's whole attachment configuration
 */
typedef union fbo_attachment {
    /*
     * These are currently used in the code somewhat, but could be replaced
     * by bitwise tests if need be
     */
    struct {
        uint64_t    color_buffer0           : 1,
                    color_buffer1           : 1,
                    color_buffer2           : 1,
                    color_buffer3           : 1,
                    color_buffer4           : 1,
                    color_buffer5           : 1,
                    color_buffer6           : 1,
                    color_buffer7           : 1,
                    depth_buffer            : 1,
                    stencil_buffer          : 1,
                    depth_stencil_buffer    : 1,
                    _reserved1              : 5, /* 16 */
                    color_texture0          : 1,
                    color_texture1          : 1,
                    color_texture2          : 1,
                    color_texture3          : 1,
                    color_texture4          : 1,
                    color_texture5          : 1,
                    color_texture6          : 1,
                    color_texture7          : 1,
                    depth_texture           : 1,
                    stencil_texture         : 1,
                    depth_stencil_texture   : 1,
                    _reserved2              : 5, /* 32 */
                    _reserved3              : 32;
    };
    uint64_t    mask;
    struct {
        uint8_t     color_buffers;
        uint8_t     depth_buffers;
        uint8_t     color_textures;
        uint8_t     depth_textures;
        uint32_t    _pad;
    };
} fbo_attachment;

#define FBO_COLOR_TEXTURE(_n)       (fbo_attachment){ .color_textures = (1 << ((_n) + 1)) - 1 }
#define FBO_DEPTH_TEXTURE(_n)       (fbo_attachment){ .depth_textures = (1 << ((_n) + 1)) - 1 }
#define FBO_COLOR_DEPTH_TEXTURE(_n) (fbo_attachment){ .color_textures = (1 << ((_n) + 1)) - 1, \
                                                      .depth_textures = 1 }
#define FBO_COLOR_BUFFER(_n)        (fbo_attachment){ .color_buffers = (1 << ((_n) + 1)) - 1 }
#define FBO_COLOR_DEPTH_BUFFER(_n)  (fbo_attachment){ .color_buffers = (1 << ((_n) + 1)) - 1, \
                                                      .depth_buffers = 1 }
#define FBO_COLOR_ATTACHMENTS_MAX 8

/*
 * Per-attachment format map: every color attachment's format, offset by
 * one so that an empty slot doesn't read as TEX_FMT_DEFAULT, packed
 * TEX_FMT_BITS apart into longs. Render targets with equal maps are
 * pipeline-compatible, so draw_control lookup compares maps instead of
 * walking the attachments.
 */
/** TEX_FMT_BITS - bits to hold a texture_format (TEX_FMT_MAX - 1), rounded up to a power of 2 */
#define TEX_FMT_BITS            8
/** ATTACHMENTS_PER_LONG - attachment format slots packed into one long of an att_map */
#define ATTACHMENTS_PER_LONG    (BITS_PER_LONG / TEX_FMT_BITS)
/** FBO_ATT_MAP_SIZE - longs an att_map needs for FBO_COLOR_ATTACHMENTS_MAX attachments */
#define FBO_ATT_MAP_SIZE \
    ((FBO_COLOR_ATTACHMENTS_MAX + ATTACHMENTS_PER_LONG - 1) / ATTACHMENTS_PER_LONG)
static_assert(TEX_FMT_MAX < (1 << TEX_FMT_BITS), "texture_format overflows a TEX_FMT_BITS slot");

static inline unsigned int fa_nr_color_buffer(fbo_attachment attachment)
{
    return fls(attachment.color_buffers);
}

static inline unsigned int fa_nr_color_texture(fbo_attachment attachment)
{
    return fls(attachment.color_textures);
}

static inline bool fbo_attachment_is_buffers(fbo_attachment attachment)
{
    return attachment.color_buffers ||
           attachment.depth_buffer  ||
           attachment.stencil_buffer;
}

static inline const char *fbo_attachment_string(fbo_attachment attachment)
{
    if (attachment.color_buffers)
        switch (fa_nr_color_buffer(attachment)) {
            case 0:  return "color buffer0";
            case 1:  return "color buffer1";
            case 2:  return "color buffer2";
            case 3:  return "color buffer3";
            case 4:  return "color buffer4";
            case 5:  return "color buffer5";
            case 6:  return "color buffer6";
            case 7:  return "color buffer7";
            default: break;
        }

    if (attachment.color_textures)
        switch (fa_nr_color_texture(attachment)) {
            case 0:  return "color texture0";
            case 1:  return "color texture1";
            case 2:  return "color texture2";
            case 3:  return "color texture3";
            case 4:  return "color texture4";
            case 5:  return "color texture5";
            case 6:  return "color texture6";
            case 7:  return "color texture7";
            default: break;
        }

    if (attachment.depth_buffer)
        return "depth buffer";
    if (attachment.depth_texture)
        return "depth texture";

    return "implement me";
}

static inline int fbo_attachment_color(fbo_attachment attachment)
{
    return (fa_nr_color_buffer(attachment) ? : fa_nr_color_texture(attachment)) - 1;
}

#define fa_for_each(_it, _fa, _kind) \
    for (fbo_attachment (_it) = { .color_ ## _kind ## s = 1 }; \
         fa_nr_color_ ## _kind((_it)) <= fa_nr_color_ ## _kind(_fa); \
         (_it).color_ ## _kind ## s = (_it).color_ ## _kind ## s * 2 + 1)

typedef enum fbo_load_action {
    FBOLOAD_LOAD = 0,
    FBOLOAD_CLEAR,
    FBOLOAD_DONTCARE,
} fbo_load_action;

typedef enum fbo_store_action {
    FBOSTORE_STORE = 0,
    FBOSTORE_MS_RESOLVE,
    FBOSTORE_DONTCARE,
} fbo_store_action;

typedef enum {
    DEPTH_FN_ALWAYS,
    DEPTH_FN_NEVER,
    DEPTH_FN_LESS,
    DEPTH_FN_EQUAL,
    DEPTH_FN_LESS_OR_EQUAL,
    DEPTH_FN_GREATER,
    DEPTH_FN_NOT_EQUAL,
    DEPTH_FN_GREATER_OR_EQUAL,
} depth_func;

/*
 * What a blended draw (renderer_blend(true)) does to a color attachment.
 * The modes that blend do it with (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) for
 * the color components and differ in alpha. The blend factor is the alpha
 * the draw writes to that same attachment, so an attachment that stores
 * other data in alpha can't blend its color by it.
 */
typedef enum fbo_blend_mode {
    /* the default, coverage accumulation: a' = src.a + dst.a * (1 - src.a) */
    FBO_BLEND_COVERAGE = 0,
    /*
     * Transmittance accumulation: a' = dst.a * (1 - src.a). With the
     * attachment cleared to 1, .a becomes the product of (1 - alpha) of
     * every blended layer -- a "how much translucency is in front of
     * this pixel" mask for downstream passes. Non-blended draws write
     * their alpha straight through: opaque geometry (alpha 1) resets the
     * mask under itself, which keeps stale translucency from showing
     * through occluders; opaque surfaces with textured alpha < 1 leak
     * into the mask on every backend alike.
     */
    FBO_BLEND_TRANSMIT,
    /* a' = src.a: the top surface's alpha wins */
    FBO_BLEND_REPLACE,
    /*
     * Blended draws leave the attachment alone, color and alpha alike:
     * for data that has to agree with the depth buffer, which they don't
     * write either, and for control channels translucency must not touch
     */
    FBO_BLEND_KEEP,
} fbo_blend_mode;

/** FBO_BLEND_MODE_BITS - bits to hold any fbo_blend_mode in a blend mode map */
#define FBO_BLEND_MODE_BITS     2
static_assert(FBO_BLEND_KEEP < (1 << FBO_BLEND_MODE_BITS), "fbo_blend_mode overflows a FBO_BLEND_MODE_BITS slot");
static_assert(FBO_COLOR_ATTACHMENTS_MAX * FBO_BLEND_MODE_BITS <= 32, "a blend mode map overflows 32 bits");

/**
 * fbo_blend_mode_get() - look a color attachment's blend mode up in a blend mode map
 * @map:    blend modes packed FBO_BLEND_MODE_BITS apart (fbo::blend_mode_map)
 * @idx:    color attachment index
 *
 * Return: the attachment's fbo_attconfig::blend_mode
 */
static inline fbo_blend_mode fbo_blend_mode_get(uint32_t map, unsigned int idx)
{
    return (fbo_blend_mode)((map >> (idx * FBO_BLEND_MODE_BITS)) & ((1u << FBO_BLEND_MODE_BITS) - 1));
}

typedef struct fbo_attconfig {
    texture_format      format;
    fbo_load_action     load_action;
    fbo_store_action    store_action;
    depth_func          depth_func;
    vec4                clear_color;
    double              clear_depth;
    uint32_t            clear_stencil;
    /* what blended draws do to the attachment, see &enum fbo_blend_mode */
    fbo_blend_mode      blend_mode;
    /*
     * When set, this attachment opts out of the renderer's global alpha blend
     * state: writes always overwrite the target regardless of
     * renderer_blend(true) and @blend_mode. Intended for G-buffer-like RTs
     * (motion vectors) where blending is nonsensical. Default (false) blends
     * as @blend_mode says.
     */
    bool                no_blend;
} fbo_attconfig;

TYPE(fbo,
    struct ref      ref;
    renderer_t      *renderer;
    unsigned int    nr_samples;
    unsigned int    width;
    unsigned int    height;
    unsigned int    layers;
    texture_t       color_tex[FBO_COLOR_ATTACHMENTS_MAX];
    texture_t       depth_tex;
    fbo_attconfig   *color_config;
    fbo_attconfig   depth_config;
    fbo_attachment  layout;
    /*
     * Pipeline-compatibility key of this render target: the attachment
     * formats packed by fbo_att_map_init(), and the fbo_attconfig::no_blend
     * bits and blend modes beside them; draw_control_get() matches targets
     * on these
     */
    unsigned long   att_map[FBO_ATT_MAP_SIZE];
#ifdef CONFIG_RENDERER_OPENGL
    struct {
        const char      *name;
        unsigned int    fbo;
        int             color_buf[FBO_COLOR_ATTACHMENTS_MAX];
        int             depth_buf;
    } gl;
#endif /* CONFIG_RENDERER_OPENGL */

#ifdef CONFIG_RENDERER_WGPU
    struct {
        /* render pass setup over the attachments, fixed while they exist */
        wgpu_color_attachment_t     color_atts[FBO_COLOR_ATTACHMENTS_MAX];
        wgpu_depth_attachment_t     depth_att;
        wgpu_pass_descriptor_t      desc;
        const char                  *name;
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */

#ifdef CONFIG_RENDERER_METAL
    struct {
        mtl_render_pass_descriptor_t    desc;
        mtl_render_command_encoder_t    cmd_encoder;
        unsigned int                    id;
        const char                      *name;
    } mtl;
#endif /* CONFIG_RENDERER_METAL */

#ifdef CONFIG_RENDERER_VULKAN
    struct {
        /* dynamic rendering setup over the attachments, fixed while they exist */
        vk_rendering_attachment_info_t  color_atts[FBO_COLOR_ATTACHMENTS_MAX];
        vk_rendering_attachment_info_t  depth_att;
        vk_rendering_info_t             rendering_info;
        const char                      *name;
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */
    /* att_map's companions, at the tail so they cost padding rather than a hole */
    uint32_t        no_blend_mask;
    /* fbo_attconfig::blend_mode of each attachment, see fbo_blend_mode_get() */
    uint32_t        blend_mode_map;
);

cresp_ret(fbo_t);

/*
 * width and height correspond to the old fbo_new()'s parameters,
 * so the old users won't need to be converted.
 */
typedef struct fbo_init_options {
    renderer_t      *renderer;
    unsigned int    width;
    unsigned int    height;
    unsigned int    layers;
    fbo_attachment  layout;
    unsigned int    nr_samples;
    fbo_attconfig   *color_config;
    fbo_attconfig   depth_config;
    bool            multisampled;
    const char      *name;
} fbo_init_options;

bool fbo_texture_supported(renderer_t *r, texture_format format);
texture_format fbo_texture_format(fbo_t *fbo, fbo_attachment attachment);
must_check cresp(fbo_t) _fbo_new(const fbo_init_options *opts);
#define fbo_new(args...) \
    _fbo_new(&(fbo_init_options){ args })
fbo_t *fbo_get(fbo_t *fbo);
void fbo_put(fbo_t *fbo);
void fbo_put_last(fbo_t *fbo);
void fbo_prepare(fbo_t *fbo);
void fbo_done(fbo_t *fbo, unsigned int width, unsigned int height);
void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment);
cerr_check fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height);
texture_t *fbo_texture(fbo_t *fbo, fbo_attachment attachment);
int fbo_width(fbo_t *fbo);
int fbo_height(fbo_t *fbo);
bool fbo_is_multisampled(fbo_t *fbo);
bool fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment);
texture_format fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment);

DEFINE_REFCLASS_INIT_OPTIONS(draw_control,
    renderer_t  *renderer;
    shader_t    *shader;
    fbo_t       *fbo;
    const char  *name;
);

/*
 * A draw_control caches the pipeline state objects a (shader, render
 * target) combination draws with. They live in renderer_t::dc_cache,
 * keyed on the target's attachment map rather than its identity, and
 * are dropped with the fbo or shader they were created against.
 */
TYPE(draw_control,
    struct ref      ref;
    renderer_t      *renderer;
    shader_t        *shader;
    /* creator (NULL: the swapchain); its destruction drops this dc */
    fbo_t           *fbo;
    struct list     cache_entry;    /* renderer_t::dc_cache */
    struct list     shader_entry;   /* shader_t::dc_list */
    /*
     * Pipeline-compatibility key: a copy of the creator fbo's att_map /
     * no_blend_mask / blend_mode_map plus its depth and multisample state;
     * swapchain dcs match on swapchain_gen instead
     */
    unsigned long   att_map[FBO_ATT_MAP_SIZE];
    unsigned int    nr_samples;
    unsigned int    swapchain_gen;
    texture_format  depth_format;
    depth_func      depth_fn;
#ifdef CONFIG_RENDERER_WGPU
    struct {
        wgpu_render_pipeline_t      pipeline[2]; /* [0] no blend, [1] blend */
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */
#ifdef CONFIG_RENDERER_METAL
    struct {
        /* [0] no blend, [1] blend, which doesn't write depth */
        mtl_depth_stencil_state_t           depth_stencil[2];
        mtl_render_pipeline_state_t         pipeline[2];
        mtl_render_pipeline_reflection_t    reflection;
    } mtl;
#endif /* CONFIG_RENDERER_METAL */
#ifdef CONFIG_RENDERER_VULKAN
    struct {
        vk_pipeline_t               pipeline[2]; /* [0] no blend, [1] blend */
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */
    /* part of the key too; at the tail so they cost padding rather than a hole */
    uint32_t        no_blend_mask;
    uint32_t        blend_mode_map;
    bool            has_depth;
);

/**
 * draw_control_bind() - hand the backend's pipeline state to the current pass
 * @dc: draw_control to bind
 */
void draw_control_bind(draw_control_t *dc);

/**
 * draw_control_unbind() - release the pipeline state bound by @dc
 * @dc: draw_control to unbind
 */
void draw_control_unbind(draw_control_t *dc);

typedef enum {
    SHADER_STAGE_VERTEX,
    SHADER_STAGE_FRAGMENT,
    SHADER_STAGE_GEOMETRY,
    SHADER_STAGE_COMPUTE,
    SHADER_STAGES_MAX,
} shader_stage;

#define SHADER_STAGE_VERTEX_BIT     (1 << SHADER_STAGE_VERTEX)
#define SHADER_STAGE_FRAGMENT_BIT   (1 << SHADER_STAGE_FRAGMENT)
#define SHADER_STAGE_GEOMETRY_BIT   (1 << SHADER_STAGE_GEOMETRY)
#define SHADER_STAGE_COMPUTE_BIT    (1 << SHADER_STAGE_COMPUTE)

#if defined(CONFIG_RENDERER_OPENGL) || defined(CONFIG_RENDERER_WGPU) || \
    defined(CONFIG_RENDERER_METAL) || defined(CONFIG_RENDERER_VULKAN)
TYPE(binding_points,
    int binding;
    int stages;
);
#endif

void binding_points_init(binding_points_t *bps);
void binding_points_done(binding_points_t *bps);
void binding_points_add(binding_points_t *bps, shader_stage stage, int binding);

#define MTL_INFLIGHT_FRAMES 3

TYPE(uniform_buffer,
    struct ref  ref;
    renderer_t  *renderer;
    size_t      size;     /* Single slot size in bytes (16-byte aligned) */
    void        *data;    /* CPU-side shadow buffer */
    bool        dirty;    /* Flag for updates */
#ifdef CONFIG_RENDERER_OPENGL
    struct {
        GLuint      id;       /* GL buffer object */
        GLuint      binding;  /* UBO binding point */
    } gl;
#endif /* CONFIG_RENDERER_OPENGL */

#ifdef CONFIG_RENDERER_WGPU
    struct {
        wgpu_buffer_t           buf;        /* GPU-side uniform buffer (large, multi-slot) */
        size_t                  slot_size;  /* 256-byte aligned slot stride */
        size_t                  total_size; /* Total GPU buffer size */
        void                    *prev;      /* Previous slot pointer (for COW) */
        size_t                  item;       /* Current slot index */
        struct list             entry;      /* Link into renderer->ubos */
        int                     binding;    /* WGPU binding point */
        bool                    advance;    /* Next write should advance to new slot */
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */

#ifdef CONFIG_RENDERER_METAL
    struct {
        mtl_buffer_t            buf[MTL_INFLIGHT_FRAMES];
        const char              *name;
        size_t                  total_size;
        void                    *prev;    /* previous shadow buffer */
        size_t                  item;
        struct list             entry;
        int                     binding;  /* UBO binding point */
        bool                    advance;
    } mtl;
#endif /* CONFIG_RENDERER_METAL */

#ifdef CONFIG_RENDERER_VULKAN
    struct {
        vk_buffer_t             buf;        /* GPU-side buffer: per-frame regions x slot ring */
        vk_device_memory_t      mem;
        void                    *mapped;    /* persistently mapped base pointer */
        void                    *shadow;    /* CPU shadow base; data points inside it */
        size_t                  slot_size;  /* aligned slot stride */
        size_t                  frame_size; /* one in-flight frame's region size */
        size_t                  item;       /* current slot index */
        struct list             entry;      /* link into renderer->vk.ubos */
        int                     binding;    /* set-0 binding point */
        bool                    advance;    /* next write should advance to a new slot */
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */
);

DEFINE_REFCLASS_INIT_OPTIONS(uniform_buffer,
    renderer_t              *renderer;
    const char              *name;
    int                     binding;
);

cerr_check uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name,
                               int binding);
cerr_check uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size);
void uniform_buffer_done(uniform_buffer_t *ubo);
void uniform_buffer_update(uniform_buffer_t *ubo, binding_points_t *binding_points);
cerr_check uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points);

/*
 * Put data into a uniform buffer in conformance with whatever data
 * layout rules the renderer imposes (i.e. GL's std140), adjust the
 * offset to point past the copied data. value==NULL is allowed, in
 * which case just adjust the offset.
 */
cerr_check uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                              unsigned int count, const void *value);

TYPE(shader,
    renderer_t  *renderer;
    /* draw_controls created against this shader, dropped with it */
    struct list dc_list;
#ifdef CONFIG_RENDERER_OPENGL
    struct {
        GLuint  vert;
        GLuint  frag;
        GLuint  geom;
        GLuint  prog;
    } gl;
#endif /* CONFIG_RENDERER_OPENGL */

#ifdef CONFIG_RENDERER_WGPU
    struct {
        renderer_t                          *renderer;
        wgpu_shader_module_t                vert;
        wgpu_shader_module_t                frag;
        wgpu_vertex_attribute_t             vertex_attrs[6];
        unsigned int                        nr_vertex_attrs;
        size_t                              vertex_stride;
        uint64_t                            binding_mask;
        char                                *name;
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */

#ifdef CONFIG_RENDERER_METAL
    struct {
        renderer_t                          *renderer;
        mtl_function_t                      vert;
        mtl_function_t                      frag;
        mtl_vertex_descriptor_t             vdesc;
        mtl_render_pipeline_reflection_t    reflection;
        const char                          *name;
        unsigned int                        id;
    } mtl;
#endif /* CONFIG_RENDERER_METAL */

#ifdef CONFIG_RENDERER_VULKAN
    struct {
        renderer_t                          *renderer;
        vk_shader_module_t                  vert;
        vk_shader_module_t                  geom;
        vk_shader_module_t                  frag;
        vk_vertex_attr_t                    vertex_attrs[6];
        size_t                              vertex_stride;
        uint64_t                            binding_mask;
        char                                *name;
        unsigned int                        nr_vertex_attrs;
        unsigned int                        id;
        /* VK_TEX_* traits per texture binding, from the SPIR-V */
        uint8_t                             tex_traits[BINDING_TEXTURE_MAX];
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */
);

cerr shader_init(renderer_t *r, shader_t *shader,
                 const char *vertex, size_t vert_size,
                 const char *geometry, size_t geom_size,
                 const char *fragment, size_t frag_size);
void shader_set_vertex_attrs(shader_t *shader, size_t stride,
                             size_t *offs, data_type *types, size_t *comp_counts,
                             unsigned int *locs, unsigned int nr_attrs);
void shader_done(shader_t *shader);
int shader_id(shader_t *shader);
cerr_check shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name);
attr_t shader_attribute(shader_t *shader, const char *name, attr_t attr);
uniform_t shader_uniform(shader_t *shader, const char *name);

void shader_set_name(shader_t *shader, const char *name);

cerr_check shader_use(shader_t *shader, bool draw);
void shader_unuse(shader_t *shader, bool draw);
/* Query a uniform offset within a uniform block as expected by the shader */
cres(size_t) shader_uniform_offset_query(shader_t *shader, const char *ubo_name, const char *var_name);

typedef enum {
    RENDERER_CORE_PROFILE,
    RENDERER_ANY_PROFILE,
} renderer_profile;

typedef enum render_limit {
    RENDER_LIMIT_MAX_TEXTURE_SIZE,
    RENDER_LIMIT_MAX_TEXTURE_UNITS,
    RENDER_LIMIT_MAX_TEXTURE_ARRAY_LAYERS,
    RENDER_LIMIT_MAX_COLOR_ATTACHMENTS,
    RENDER_LIMIT_MAX_COLOR_TEXTURE_SAMPLES,
    RENDER_LIMIT_MAX_DEPTH_TEXTURE_SAMPLES,
    RENDER_LIMIT_MAX_SAMPLES,
    RENDER_LIMIT_MAX_DRAW_BUFFERS,
    RENDER_LIMIT_MAX_ANISOTROPY,
    RENDER_LIMIT_MAX_UBO_SIZE,
    RENDER_LIMIT_MAX_UBO_BINDINGS,
    RENDER_LIMIT_MAX_VERTEX_UNIFORM_BLOCKS,
    RENDER_LIMIT_MAX_GEOMETRY_UNIFORM_BLOCKS,
    RENDER_LIMIT_MAX_FRAGMENT_UNIFORM_BLOCKS,
    RENDER_LIMIT_MAX,
} render_limit;

#define SLOTS_MAX   32

typedef struct renderer_ops renderer_ops;

void renderer_mat4x4_ortho(renderer_t *renderer, mat4x4 M, float l, float r, float b, float t, float n, float f);
void renderer_mat4x4_perspective(renderer_t *renderer, mat4x4 m, float y_fov, float aspect, float n, float f);

TYPE(renderer,
    const renderer_ops  *ops;
    int                 x;
    int                 y;
    int                 width;
    int                 height;
    vertex_array_t      *va;
    bool                blend;
    /* bumped on swapchain format changes: keys the swapchain draw_controls */
    unsigned int        swapchain_gen;
    /* the draw_control (pipeline) cache; entries drop with their fbo/shader */
    struct list         dc_cache;
#ifdef CONFIG_RENDERER_OPENGL
    struct {
        GLenum              cull_face;
        GLenum              blend_sfactor;
        GLenum              blend_dfactor;
        GLenum              depth_func;
        double              clear_depth;
        bool                fbo_texture_supported[TEX_FMT_MAX];
        bool                texture_format_supported[TEX_FMT_MAX];
        int                 major;
        int                 minor;
        renderer_profile    profile;
        /* Bit i set: color attachment i opts out of blending (fbo_attconfig.no_blend) */
        uint32_t            no_blend_mask;
        /* The bound FBO's fbo_attconfig::blend_mode of each color attachment */
        uint32_t            blend_mode_map;
        /* Number of color attachments the currently bound FBO exposes */
        unsigned int        nr_color_targets;
#ifndef CONFIG_FINAL
        /*
         * Per-pass GL_TIME_ELAPSED queries; results land in
         * renderer::pass_times. A deeper ring than the other backends'
         * in-flight counts: query results trail by several frames on
         * WebGL, and an unavailable slot just skips a publish.
         */
        unsigned int        ts_count[GL_INFLIGHT_FRAMES];
        unsigned int        ts_frame;
        GLuint              ts_queries[GL_INFLIGHT_FRAMES][RENDERER_PASS_TIMERS_MAX];
        const char          *ts_names[GL_INFLIGHT_FRAMES][RENDERER_PASS_TIMERS_MAX];
        /* first fbo of the pass cycle: its reappearance marks a new frame */
        fbo_t               *ts_first_fbo;
        /* the fbo whose query is open (NULL: the swapchain pass) */
        fbo_t               *ts_fbo;
        bool                ts_open;
        bool                ts_supported;
#endif /* !CONFIG_FINAL */
        /* at the tail so they cost padding rather than a hole */
        bool                depth_test;
        bool                mac_amd_quirk;
    } gl;
#endif /* CONFIG_RENDERER_OPENGL */
#ifdef CONFIG_RENDERER_WGPU
    struct {
        wgpu_instance_t                     instance;
        wgpu_adapter_t                      adapter;
        wgpu_device_t                       device;
        wgpu_queue_t                        queue;
        wgpu_command_encoder_t              cmd_encoder;
        wgpu_surface_t                      surface;
        /* this frame's swapchain texture, released again in present */
        wgpu_texture_t                      surface_texture;
        wgpu_texture_view_t                 texture_view;
        wgpu_render_pass_encoder_t          pass_encoder;
        fbo_t                               *fbo;
        draw_control_t                      *dc;
        struct list                         ubos;
        texture_t                           *bound_textures[BINDING_TEXTURE_MAX];
        uniform_buffer_t                    *bound_ubos[BINDING_UBO_MAX];
        bool                                hdr;
        bool                                edr_supported;
        /* HDR_TRANSFER_*: requested, and what the surface config used */
        int                                 hdr_transfer_want;
        int                                 hdr_transfer;
        int                                 limits[RENDER_LIMIT_MAX];
        atomic_uint                         error;
        char                                wgpu_message[128];
    } wgpu;
#endif /* CONFIG_RENDERER_WGPU */
#ifdef CONFIG_RENDERER_METAL
#define FBOS_MAX 1024
#define SHADERS_MAX 1024
    struct {
        ns_autorelease_pool_t               frame_pool;
        mtl_device_t                        device;
        mtl_command_queue_t                 cmd_queue;
        mtl_command_buffer_t                cmd_buffer;
        dispatch_semaphore_t                sem;
        mtl_ca_layer_t                      layer;
        /* the NSWindow, kept to read its NSScreen's EDR headroom */
        void                                *window;
        mtl_ca_drawable_t                   drawable;
        draw_control_t                      *dc;
        fbo_t                               *screen_fbo;
        fbo_t                               *fbo;
        void                                *vbuffer_cache[SLOTS_MAX];
        void                                *fbuffer_cache[SLOTS_MAX];
        size_t                              voffset_cache[SLOTS_MAX];
        size_t                              foffset_cache[SLOTS_MAX];
        void                                *texture_cache[SLOTS_MAX];
        void                                *sampler_cache[SLOTS_MAX];
        struct bitmap                       fbo_ids;
        struct bitmap                       shader_ids;
        struct list                         ubos;
        cg_colorspace_ref_t                 colorspace;
        int                                 major;
        int                                 minor;
        int                                 fps_cap;
        mtl_cull_mode_t                     cull_mode;
        size_t                              nr_draws;
        double                              cmdbuf_time;
        int                                 cmdbuf_count;
        int                                 frame_idx;
#ifndef CONFIG_FINAL
        /*
         * Per-pass GPU timestamps, sampled at encoder stage boundaries
         * into one counter buffer per in-flight frame. Absent when the
         * device can't sample counters at stage boundaries, and then only
         * the whole command buffer's GPU time is available.
         */
        mtl_counter_sample_buffer_t         ts_bufs[MTL_INFLIGHT_FRAMES];
        const char                          *ts_names[MTL_INFLIGHT_FRAMES][RENDERER_PASS_TIMERS_MAX];
        /* GPU ticks -> nanoseconds, from a CPU/GPU timestamp correlation */
        double                              ts_period;
        uint64_t                            ts_cpu0;
        uint64_t                            ts_gpu0;
        /*
         * Results per in-flight slot, written by that command buffer's
         * completion handler, which then publishes the slot in @ts_ready
         * (-1 until the first one completes, accessed through the __atomic
         * builtins); renderer_frame_advance() copies the published slot
         * into renderer::pass_times on the main thread. A slot is only
         * rewritten after the main thread has waited for its previous
         * command buffer, so the copy never races the handler.
         */
        renderer_pass_times                 ts_results[MTL_INFLIGHT_FRAMES];
        int                                 ts_ready;
        unsigned int                        ts_count[MTL_INFLIGHT_FRAMES];
#endif /* !CONFIG_FINAL */
        bool                                hdr;
    } mtl;
#endif /* CONFIG_RENDERER_METAL */
#ifdef CONFIG_RENDERER_VULKAN
    struct {
        vk_instance_t                       instance;
        vk_debug_utils_messenger_t          messenger;
        vk_physical_device_t                phys_device;
        vk_device_t                         device;
        vk_queue_t                          queue;
        vk_surface_t                        surface;
        vk_swapchain_t                      swapchain;
        vk_image_t                          images[VK_SWAPCHAIN_IMAGES_MAX];
        vk_image_view_t                     views[VK_SWAPCHAIN_IMAGES_MAX];
        /* per swapchain image: render-finished, waited on by present */
        vk_semaphore_t                      render_sems[VK_SWAPCHAIN_IMAGES_MAX];
        struct {
            vk_command_pool_t               cmd_pool;
            vk_command_buffer_t             cmd_buf;
            vk_fence_t                      fence;
            vk_semaphore_t                  acquire_sem;
            vk_descriptor_pool_t            desc_pool;
            /* resources destroyed while this frame may still be in flight */
            struct list                     zombies;
            /* the fence is only ever signalled for a frame whose submit succeeded */
            bool                            submitted;
        }                                   frames[VK_INFLIGHT_FRAMES];
        /* synchronous resource upload path */
        vk_command_pool_t                   upload_pool;
        /* one global set-0 layout: every shader's bindings are a subset */
        vk_descriptor_set_layout_t          desc_layout;
        vk_pipeline_layout_t                pipeline_layout;
        /* fallbacks for descriptor slots with nothing (compatible) bound */
        vk_image_t                          dummy_image[VK_DUMMY_MAX];
        vk_device_memory_t                  dummy_mem[VK_DUMMY_MAX];
        vk_image_view_t                     dummy_view[VK_DUMMY_MAX];
        vk_sampler_t                        dummy_sampler;
        vk_buffer_t                         dummy_ubo;
        vk_device_memory_t                  dummy_ubo_mem;
        draw_control_t                      *dc;
        fbo_t                               *fbo;
        struct list                         ubos;
        texture_t                           *bound_textures[BINDING_TEXTURE_MAX];
        uniform_buffer_t                    *bound_ubos[BINDING_UBO_MAX];
#ifndef CONFIG_FINAL
        /* per-pass GPU timestamps; the results land in renderer::pass_times */
        vk_query_pool_t                     ts_pool;
        const char                          *ts_names[VK_INFLIGHT_FRAMES][RENDERER_PASS_TIMERS_MAX];
        float                               timestamp_period;
        unsigned int                        ts_count[VK_INFLIGHT_FRAMES];
#endif /* !CONFIG_FINAL */
        int                                 limits[RENDER_LIMIT_MAX];
        vk_format_t                         swapchain_format;
        vk_color_space_t                    swapchain_colorspace;
        unsigned int                        queue_family;
        unsigned int                        sc_width;
        unsigned int                        sc_height;
        unsigned int                        nr_images;
        unsigned int                        min_image_count;
        unsigned int                        frame_idx;
        unsigned int                        image_idx;
        int                                 cull;       /* cull_face: declared below */
        /* HDR_TRANSFER_*: requested, and what the surface actually offered */
        int                                 hdr_transfer_want;
        int                                 hdr_transfer;
        /* HDR_TRANSFER_* the surface can present, as a bitmask; the setup probe's */
        unsigned int                        hdr_transfers;
        bool                                swapchain_dirty;
        /* the surface offers a PQ color space; probed once at setup */
        bool                                edr_supported;
        /* HDR output wanted and possible: drives the swapchain format pick */
        bool                                hdr;
        /* frame-state machine: see render-vk.c */
        bool                                cb_open;
        bool                                image_acquired;
        bool                                pass_open;
        bool                                validation;
#ifndef CONFIG_FINAL
        /* a pass's begin timestamp is written and its end isn't yet */
        bool                                ts_open;
#endif /* !CONFIG_FINAL */
    } vk;
#endif /* CONFIG_RENDERER_VULKAN */
#ifndef CONFIG_FINAL
    /* last completed frame's GPU times, written on the main thread only */
    renderer_pass_times pass_times;
#endif /* !CONFIG_FINAL */
);

int renderer_query_limits(renderer_t *renderer, render_limit limit);

typedef struct renderer_init_options {
    /*
     * The platform window: vulkan creates its VkSurfaceKHR from it (a
     * GLFWwindow), metal reads the NSScreen off it (an NSWindow) to answer
     * supports_edr(). Unused where there is no window to speak of.
     */
    void            *window;
    render_backend  backend;
#ifdef CONFIG_RENDERER_METAL
    void    *device;
    void    *layer;
#endif /* CONFIG_RENDERER_METAL */
} renderer_init_options;

#define renderer_init(_r, args...) \
    _renderer_init((_r), &(renderer_init_options){ args })
cerr _renderer_init(renderer_t *renderer, const renderer_init_options *opts);
void renderer_done(renderer_t *r);
const renderer_caps *renderer_get_caps(renderer_t *r);
const char *renderer_shader_ext(renderer_t *r);

/**
 * renderer_backend_name() - human-readable name of a rendering backend
 * @backend:    RENDER_* constant
 *
 * Return: "OpenGL", "Vulkan" and so on; "unknown" for anything out of range.
 */
const char *renderer_backend_name(render_backend backend);

cres_ret(render_backend);

/**
 * renderer_backend_by_name() - look a rendering backend up by name
 * @name:   backend name as renderer_backend_name() spells it, in any case
 *
 * Return: the RENDER_* constant, or CERR_NOT_FOUND when @name matches none.
 */
cres(render_backend) renderer_backend_by_name(const char *name);
void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile);
void renderer_viewport(renderer_t *r, int x, int y, int width, int height);
void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight);

/**
 * renderer_supports_edr() - can the thing we present to show HDR
 * @r:  renderer
 *
 * Answered by the backend, because that is where the answer lives: metal
 * reads the NSScreen the window is on, a backend with a surface to probe
 * asks the surface. Callable per frame -- metal's answer changes when the
 * window moves to another display, so backends that can track it should.
 *
 * Return: %true if HDR output can reach the display.
 */
bool renderer_supports_edr(renderer_t *r);

void renderer_hdr_enable(renderer_t *r, bool enable);

/**
 * renderer_hdr_transfer_set() - request an HDR output encoding
 * @r:          renderer
 * @transfer:   one of HDR_TRANSFER_* (shader_constants.h)
 *
 * A request: the backend falls back to whatever its surface does offer.
 * renderer_hdr_transfer() reports what it settled on.
 */
void renderer_hdr_transfer_set(renderer_t *r, int transfer);

/**
 * renderer_hdr_transfer() - the HDR output encoding actually in effect
 * @r:  renderer
 *
 * Return: one of HDR_TRANSFER_* (shader_constants.h); %HDR_TRANSFER_PQ on
 *         backends that don't implement the query. Moot on GL, which never
 *         sets render_options::hdr_output; correct on metal only because its
 *         CAMetalLayer sits on ITUR_2100_PQ -- metal doesn't take
 *         renderer_hdr_transfer_set() either, so the debug panel's radio
 *         buttons are inert there and report the surface's own choice.
 */
int renderer_hdr_transfer(renderer_t *r);

/**
 * renderer_hdr_transfers() - the HDR output encodings the surface can present
 * @r:  renderer
 *
 * Return: bitmask over HDR_TRANSFER_* (shader_constants.h), 1 << transfer per
 *         encoding renderer_hdr_transfer_set() can be granted; the PQ bit
 *         alone on backends that don't implement the query, 0 where the
 *         surface can't present HDR at all.
 */
unsigned int renderer_hdr_transfers(renderer_t *r);

/**
 * renderer_swapchain_begin() - open the pass that renders to the swapchain
 * @renderer:   renderer
 * @load:       FBOLOAD_DONTCARE when the caller overwrites every pixel of the
 *              image, FBOLOAD_CLEAR otherwise; a backend whose clear is free may
 *              clear regardless
 */
void renderer_swapchain_begin(renderer_t *renderer, fbo_load_action load);

void renderer_frame_begin(renderer_t *renderer);

void renderer_frame_end(renderer_t *renderer);

void renderer_swapchain_end(renderer_t *r);

#ifndef CONFIG_FINAL
void buffer_debug_header(void);
void buffer_debug(buffer_t *buf, const char *name);
void texture_debug_header(void);
void texture_debug(texture_t *tex, const char *name);
#else
static inline void buffer_debug_header(void) {}
static inline void buffer_debug(buffer_t *buf) {}
#endif /* CONFIG_FINAL */

void renderer_debug(renderer_t *r);

typedef enum {
    CULL_FACE_NONE = 0,
    CULL_FACE_FRONT,
    CULL_FACE_BACK,
} cull_face;

void renderer_cull_face(renderer_t *r, cull_face cull);

typedef enum {
    BLEND_NONE = 0,
    BLEND_SRC_ALPHA,
    BLEND_ONE_MINUS_SRC_ALPHA,
} blend;

/**
 * renderer_blend() - switch alpha blending for the draws that follow
 * @r:          renderer
 * @_blend:     blend the draws over the render target
 * @sfactor:    source blend factor
 * @dfactor:    destination blend factor
 *
 * Blended draws don't write the depth buffer: passes that read it see the
 * surfaces behind the translucent ones. Only GL takes @sfactor/@dfactor,
 * the other backends bake (BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA)
 * into their pipeline state, which is all the engine blends with.
 */
void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor);


typedef enum {
    DRAW_TYPE_POINTS = 0,
    DRAW_TYPE_LINE_STRIP,
    DRAW_TYPE_LINE_LOOP,
    DRAW_TYPE_LINES,
    DRAW_TYPE_LINE_STRIP_ADJACENCY,
    DRAW_TYPE_LINES_ADJACENCY,
    DRAW_TYPE_TRIANGLE_STRIP,
    DRAW_TYPE_TRIANGLE_FAN,
    DRAW_TYPE_TRIANGLES,
    DRAW_TYPE_TRIANGLE_STRIP_ADJACENCY,
    DRAW_TYPE_TRIANGLES_ADJACENCY,
    DRAW_TYPE_PATCHES
} draw_type;

cerr_check renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                         data_type idx_type, unsigned int nr_instances);

DECLARE_REFCLASS(buffer);
DECLARE_REFCLASS(vertex_array);
DECLARE_REFCLASS(texture);
DECLARE_REFCLASS(fbo);
DECLARE_REFCLASS(uniform_buffer);
DECLARE_REFCLASS(draw_control);

/**
 * draw_control_get() - the cached draw_control for (@fbo, @shader)
 * @r:      renderer owning the cache
 * @fbo:    render target to match, %NULL for the swapchain
 * @shader: shader to match
 *
 * Targets match on their attachment maps, so pipeline-compatible fbos
 * share a draw_control. Created on the first miss; the cache holds the
 * only reference.
 * Return: the draw_control, or an error when one can't be created.
 */
must_check cresp(draw_control) draw_control_get(renderer_t *r, fbo_t *fbo, shader_t *shader);

/**
 * draw_control_cache_drop() - drop the draw_controls created against @fbo
 * @r:      renderer owning the cache
 * @fbo:    render target being dropped, %NULL for the swapchain
 */
void draw_control_cache_drop(renderer_t *r, fbo_t *fbo);

/**
 * draw_control_cache_done() - drop the whole draw_control cache
 * @r:  renderer owning the cache
 */
void draw_control_cache_done(renderer_t *r);

#ifdef IMPLEMENTOR
#ifdef CONFIG_RENDERER_OPENGL
cerr gl_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts);
#else /* !CONFIG_RENDERER_OPENGL */
static inline cerr gl_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts)
{
    return CERR_NOT_SUPPORTED_REASON(.fmt = "OpenGL support is not compiled in");
}
#endif /* !CONFIG_RENDERER_OPENGL */

#ifdef CONFIG_RENDERER_METAL
cerr mtl_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts);
#else /* !CONFIG_RENDERER_METAL */
static inline cerr mtl_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts)
{
    return CERR_NOT_SUPPORTED_REASON(.fmt = "Metal support is not compiled in");
}
#endif /* !CONFIG_RENDERER_METAL */

#ifdef CONFIG_RENDERER_WGPU
cerr wgpu_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts);
#else /* !CONFIG_RENDERER_WGPU */
static inline cerr wgpu_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts)
{
    return CERR_NOT_SUPPORTED_REASON(.fmt = "WebGPU support is not compiled in");
}
#endif /* !CONFIG_RENDERER_WGPU */

#ifdef CONFIG_RENDERER_VULKAN
cerr vk_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts);
#else /* !CONFIG_RENDERER_VULKAN */
static inline cerr vk_renderer_setup(renderer_t *rendnerer, const renderer_init_options *opts)
{
    return CERR_NOT_SUPPORTED_REASON(.fmt = "Vulkan support is not compiled in");
}
#endif /* !CONFIG_RENDERER_VULKAN */

typedef struct renderer_ops {
    /*
     * sizeof(renderer_t) as the implementing TU sees it. The backend
     * sub-structs are made of typedefs that the non-IMPLEMENTOR side
     * shims, and a size mismatch in any of them splits renderer_t into
     * two layouts linked into one binary; checked in _renderer_init().
     */
    size_t              renderer_size;
    const renderer_caps *(*get_caps)(void);
    void                (*mat4x4_ortho)(mat4x4 M, float l, float r, float b, float t, float n, float f);
    void                (*mat4x4_perspective)(mat4x4 m, float y_fov, float aspect, float n, float f);
    int                 (*query_limits)(renderer_t *renderer, render_limit limit);
    cerr                (*init)(renderer_t *renderer, const renderer_init_options *opts);
    void                (*done)(renderer_t *r);
    void                (*set_version)(renderer_t *r, int major, int minor, renderer_profile profile);
    void                (*viewport)(renderer_t *r, int x, int y, int width, int height);
    void                (*get_viewport)(renderer_t *r, int *px, int *py, int *pwidth, int *pheight);
    bool                (*supports_edr)(renderer_t *r);
    void                (*hdr_enable)(renderer_t *r, bool enable);
    void                (*hdr_transfer_set)(renderer_t *r, int transfer);
    int                 (*hdr_transfer_get)(renderer_t *r);
    unsigned int        (*hdr_transfers)(renderer_t *r);
    void                (*swapchain_begin)(renderer_t *r, fbo_load_action load);
    void                (*frame_begin)(renderer_t *r);
    void                (*frame_end)(renderer_t *r);
    void                (*swapchain_end)(renderer_t *r);
    void                (*debug)(renderer_t *r);
    void                (*cull_face)(renderer_t *r, cull_face cull);
    void                (*blend)(renderer_t *r, bool _blend, blend sfactor, blend dfactor);
    cerr                (*draw)(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                                data_type idx_type, unsigned int nr_instances);
    cerr                (*buf_init)(buffer_t *buf, const buffer_init_options *opts);
    void                (*buf_deinit)(buffer_t *buf);
    void                (*buf_bind)(buffer_t *buf, uniform_t loc);
    void                (*buf_unbind)(buffer_t *buf, uniform_t loc);
    void                (*buf_set_name)(buffer_t *buf, const char *name);
    cerr                (*va_init)(vertex_array_t *va, renderer_t *r);
    void                (*va_done)(vertex_array_t *va);
    void                (*va_bind)(vertex_array_t *va);
    void                (*va_unbind)(vertex_array_t *va);
    cerr                (*dc_init)(draw_control_t *dc, const draw_control_init_options *opts);
    void                (*dc_done)(draw_control_t *dc);
    void                (*dc_bind)(draw_control_t *dc);
    void                (*dc_unbind)(draw_control_t *dc);
    cerr                (*tex_init)(texture_t *tex, const texture_init_options *opts);
    void                (*tex_deinit)(texture_t *tex);
    cerr                (*tex_load)(texture_t *tex, texture_format format,
                                    unsigned int width, unsigned int height, void *buf);
    cerr                (*tex_resize)(texture_t *tex, unsigned int width, unsigned int height);
    void                (*tex_bind)(texture_t *tex, unsigned int target, uniform_t uniform);
    void                (*tex_unbind)(texture_t *tex, unsigned int target);
    bool                (*tex_is_array)(texture_t *tex);
    void                (*tex_set_name)(texture_t *tex, const char *name);
    texture_t           *(*tex_clone)(texture_t *tex);
    texid_t             (*tex_id)(texture_t *tex);
    bool                (*tex_format_supported)(renderer_t *r, texture_format format);
    void                (*fbo_prepare)(fbo_t *fbo);
    void                (*fbo_done)(fbo_t *fbo, unsigned int width, unsigned int height);
    void                (*fbo_blit)(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment);
    cerr                (*fbo_resize)(fbo_t *fbo, unsigned int width, unsigned int height);
    bool                (*fbo_attachment_valid)(fbo_t *fbo, fbo_attachment attachment);
    texture_format      (*fbo_attachment_format)(fbo_t *fbo, fbo_attachment attachment);
    texture_t           *(*fbo_tex)(fbo_t *fbo, fbo_attachment attachment);
    texture_format      (*fbo_tex_format)(fbo_t *fbo, fbo_attachment attachment);
    bool                (*fbo_tex_supported)(renderer_t *r, texture_format format);
    cresp(fbo_t)        (*fbo_create)(const fbo_init_options *opts);
    void                (*fbo_destroy)(fbo_t *fbo);
    cerr                (*ubo_init)(renderer_t *r, uniform_buffer_t *ubo, const char *name, int binding);
    void                (*ubo_done)(uniform_buffer_t *ubo);
    void                (*ubo_destroy)(uniform_buffer_t *ubo);
    cerr                (*ubo_data_alloc)(uniform_buffer_t *ubo, size_t size);
    cerr                (*ubo_bind)(uniform_buffer_t *ubo, binding_points_t *binding_points);
    void                (*ubo_update)(uniform_buffer_t *ubo, binding_points_t *binding_points);
    cerr                (*ubo_set)(uniform_buffer_t *ubo, data_type type, size_t *offset,
                                   size_t *size, unsigned int count, const void *value);
    cerr                (*shader_init)(renderer_t *r, shader_t *shader,
                                      const char *vertex, size_t vert_size,
                                      const char *geometry, size_t geom_size,
                                      const char *fragment, size_t frag_size);
    void                (*shader_done)(shader_t *shader);
    void                (*shader_set_vertex_attrs)(shader_t *shader, size_t stride,
                                                   size_t *offs, data_type *types,
                                                   size_t *comp_counts, unsigned int *locs,
                                                   unsigned int nr_attrs);
    int                 (*shader_id)(shader_t *shader);
    cerr                (*shader_ubo_bind)(shader_t *shader, binding_points_t *bpt, const char *name);
    attr_t              (*shader_attribute)(shader_t *shader, const char *name, attr_t attr);
    uniform_t           (*shader_uniform)(shader_t *shader, const char *name);
    cerr                (*shader_use)(shader_t *shader, bool draw);
    void                (*shader_unuse)(shader_t *shader, bool draw);
    cres(size_t)        (*shader_ubo_offset_query)(shader_t *shader, const char *ubo_name,
                                                   const char *var_name);
    void                (*shader_set_name)(shader_t *shader, const char *name);
} renderer_ops;
#endif /* IMPLEMENTOR */

#endif /* __CLAP_RENDER_H__ */
