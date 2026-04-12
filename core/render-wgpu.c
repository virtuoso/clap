// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "display.h"
#include "render.h"
#undef IMPLEMENTOR

#include <stdatomic.h>
#include "bindings/render-bindings.h"
#include "bindings/render-bindings-common.h"
#include "str.h"

const renderer_caps wgpu_renderer_caps = {
    .renderer           = RENDER_WGPU,
    .ndc_y_down         = true,
    .ndc_z_zero_one     = true,
    .origin_top_left    = true,
};

static const renderer_caps *wgpu_renderer_get_caps(void)
{
    return &wgpu_renderer_caps;
}

static const renderer_ops wgpu_renderer_ops = {
    .get_caps       = wgpu_renderer_get_caps,
};

enum {
    WGPU_MSAA_SAMPLES = 4,
};

static inline WGPUStringView to_wgpu_stringview(const char *str)
{
    if (str)    return (WGPUStringView) { str, strlen(str) };
    return             (WGPUStringView) { nullptr, 0 };
}

#define literal_to_wgpu_stringview(x)   (WGPUStringView) { (x), sizeof(x) - 1 }

static inline wgpu_texture_format_t wgpu_swapchain_format(renderer_t *r);
static WGPUTextureFormat wgpu_texture_format(texture_format format);

static inline cerr wgpu_previous_errors(renderer_t *r)
{
    auto error = atomic_load_explicit(&r->wgpu.error, memory_order_acquire);
    if (error)
        return CERR_PREVIOUS_ERRORS_REASON(
            .fmt    = "previous errors reported: %u",
            .arg0   = (void *)(uintptr_t)error
        );

    return CERR_OK;
}

static texture_t *fbo_texture_fallback(fbo_attachment attachment)
{
    if (attachment.depth_texture || attachment.depth_buffer)
        return black_pixel();

    return transparent_pixel();
}

/****************************************************************************
 * Buffer
 ****************************************************************************/

static WGPUBufferUsage wgpu_buffer_usage(buffer_type type)
{
    switch (type) {
        case BUF_ARRAY:         return WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        case BUF_ELEMENT_ARRAY: return WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        default:                break;
    }

    clap_unreachable();
    return WGPUBufferUsage_None;
}

static WGPUBuffer wgpu_create_buffer(renderer_t *r, const char *label,
                                     WGPUBufferUsage usage, size_t size)
{
    WGPUBufferDescriptor desc = {
        .usage  = usage,
        .size   = size,
    };
    if (label)
        desc.label = (WGPUStringView){ label, strlen(label) };

    return wgpuDeviceCreateBuffer(r->wgpu.device, &desc);
}

static void buffer_load(buffer_t *buf, renderer_t *r, const char *name,
                         void *data, size_t sz)
{
    if (buf->main) {
        buf->wgpu.buf = buf->main->wgpu.buf;
    } else {
        size_t gpu_sz = round_up(sz, 4);
        buf->wgpu.buf = wgpu_create_buffer(r, name, wgpu_buffer_usage(buf->wgpu.type), gpu_sz);
        if (!buf->wgpu.buf)
            return;

        if (gpu_sz == sz) {
            wgpuQueueWriteBuffer(r->wgpu.queue, buf->wgpu.buf, 0, data, sz);
        } else {
            /* WGPU requires write size to be a multiple of 4; zero-pad the tail */
            void *padded = mem_alloc(gpu_sz, .zero = 1);
            if (padded) {
                memcpy(padded, data, sz);
                wgpuQueueWriteBuffer(r->wgpu.queue, buf->wgpu.buf, 0, padded, gpu_sz);
                mem_free(padded);
            }
        }
    }

    buf->wgpu.size = sz;
    buf->loaded = true;
}

static cerr buffer_make(struct ref *ref, void *opts)
{
    return CERR_OK;
}

static void buffer_drop(struct ref *ref)
{
    buffer_t *buf = container_of(ref, buffer_t, ref);
    buffer_deinit(buf);
}

DEFINE_REFCLASS2(buffer);

bool buffer_loaded(buffer_t *buf)
{
    return buf->loaded;
}

cerr _buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    cerr err = ref_embed(buffer, buf);
    if (IS_CERR(err))
        return err;

    data_type comp_type = opts->comp_type;
    if (comp_type == DT_NONE)
        comp_type = DT_FLOAT;

    buf->wgpu.renderer = opts->renderer;
    buf->wgpu.type = opts->type;
    buf->off = opts->off;
    buf->comp_count = max(opts->comp_count, data_comp_count(comp_type));
    buf->loc = opts->loc;
    buf->main = opts->main;
#ifndef CONFIG_FINAL
    buf->opts = *opts;
    buf->opts.comp_type = comp_type;
    buf->opts.comp_count = buf->comp_count;
#endif /* CONFIG_FINAL */

    if (opts->data && opts->size)
        buffer_load(buf, opts->renderer, opts->name, opts->data, opts->size);

    return CERR_OK;
}

void buffer_deinit(buffer_t *buf)
{
    if (!buf)
        return;

    if (buf->wgpu.buf && !buf->main)
        wgpuBufferRelease(buf->wgpu.buf);

    buf->wgpu.buf = NULL;
    buf->off = 0;
    buf->wgpu.size = 0;
    buf->comp_count = 0;
    buf->loc = 0;
    buf->loaded = false;
#ifndef CONFIG_FINAL
    memset(&buf->opts, 0, sizeof(buf->opts));
#endif /* CONFIG_FINAL */
}

void buffer_bind(buffer_t *buf, uniform_t loc)
{
    if (!buf || !buf->loaded)
        return;

    buf->loc = loc;

    renderer_t *r = buf->wgpu.renderer;
    if (!r || !r->wgpu.va)
        return;

    if (loc < 0) {
        /* index buffer */
        r->wgpu.va->index = buf;
    } else if (loc == 0) {
        /* first vertex attribute — record the interleaved vertex buffer */
        buffer_t *vbuf = buf->main ? buf->main : buf;
        r->wgpu.va->vbuf = vbuf;
    }
}

void buffer_unbind(buffer_t *buf, uniform_t loc)
{
}

#ifndef CONFIG_FINAL
cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char label[128];
    vsnprintf(label, sizeof(label), fmt, ap);
    va_end(ap);

    if (buf->wgpu.buf) {
        WGPUStringView sv = { label, strlen(label) };
        wgpuBufferSetLabel(buf->wgpu.buf, sv);
    }

    return cres_val(int, 0);
}
#endif /* CONFIG_FINAL */

/****************************************************************************
 * Vertex array
 ****************************************************************************/

static void vertex_array_drop(struct ref *ref)
{
    vertex_array_t *va = container_of(ref, vertex_array_t, ref);
    vertex_array_done(va);
}

DEFINE_REFCLASS(vertex_array);

cerr vertex_array_init(vertex_array_t *va, renderer_t *r)
{
    cerr err = ref_embed(vertex_array, va, .renderer = r);
    if (IS_CERR(err))
        return err;

    va->renderer = r;
    return CERR_OK;
}

void vertex_array_done(vertex_array_t *va)
{
    va->index = NULL;
}

void vertex_array_bind(vertex_array_t *va)
{
    if (va && va->renderer)
        va->renderer->wgpu.va = va;
}

void vertex_array_unbind(vertex_array_t *va)
{
    if (va && va->renderer)
        va->renderer->wgpu.va = NULL;
}

/****************************************************************************
 * Draw control
 ****************************************************************************/

// WebGPU requires pipeline blend state to match the render target capabilities.
// 32-bit integer/float formats are not blendable at all; formats without an
// alpha channel (R*, RG*, RG11B10) can't be used with SrcAlpha/OneMinusSrcAlpha
// blend factors because the fragment output has no alpha to read.
static bool wgpu_format_supports_alpha_blend(WGPUTextureFormat fmt)
{
    switch (fmt) {
    /* 32-bit formats are not blendable at all */
    case WGPUTextureFormat_R32Float:
    case WGPUTextureFormat_RG32Float:
    case WGPUTextureFormat_RGBA32Float:
    case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RGBA32Sint:
    case WGPUTextureFormat_RGBA32Uint:
    /* Formats without alpha — SrcAlpha blend reads a non-existent channel */
    case WGPUTextureFormat_R8Unorm:
    case WGPUTextureFormat_R8Snorm:
    case WGPUTextureFormat_R8Uint:
    case WGPUTextureFormat_R8Sint:
    case WGPUTextureFormat_R16Float:
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_RG8Unorm:
    case WGPUTextureFormat_RG8Snorm:
    case WGPUTextureFormat_RG8Uint:
    case WGPUTextureFormat_RG8Sint:
    case WGPUTextureFormat_RG16Float:
    case WGPUTextureFormat_RG16Uint:
    case WGPUTextureFormat_RG16Sint:
    case WGPUTextureFormat_RG11B10Ufloat:
        return false;
    default:
        return true;
    }
}

static WGPUCompareFunction wgpu_compare_function(depth_func fn)
{
    switch (fn) {
    case DEPTH_FN_ALWAYS:           return WGPUCompareFunction_Always;
    case DEPTH_FN_NEVER:            return WGPUCompareFunction_Never;
    case DEPTH_FN_LESS:             return WGPUCompareFunction_Less;
    case DEPTH_FN_EQUAL:            return WGPUCompareFunction_Equal;
    case DEPTH_FN_LESS_OR_EQUAL:    return WGPUCompareFunction_LessEqual;
    case DEPTH_FN_GREATER:          return WGPUCompareFunction_Greater;
    case DEPTH_FN_NOT_EQUAL:        return WGPUCompareFunction_NotEqual;
    case DEPTH_FN_GREATER_OR_EQUAL: return WGPUCompareFunction_GreaterEqual;
    }
    return WGPUCompareFunction_Always;
}

// WebGPU pipelines are immutable and must exactly match the render pass
// attachment state: color target count and formats, depth format, blend mode.
// draw_control caches pipelines keyed on (shader, color_format[0], nr_targets,
// has_depth) to avoid re-creation.  Per-target blend is skipped for formats
// that don't support alpha blending (see wgpu_format_supports_alpha_blend).
static WGPURenderPipeline wgpu_create_pipeline(renderer_t *r, shader_t *shader,
                                               const WGPUTextureFormat *color_formats,
                                               unsigned int nr_color_targets,
                                               bool blend, bool has_depth,
                                               WGPUTextureFormat depth_format,
                                               WGPUCompareFunction depth_compare)
{
    WGPUVertexBufferLayout vbuf_layout = {
        .arrayStride    = shader->vertex_stride,
        .stepMode       = WGPUVertexStepMode_Vertex,
        .attributeCount = shader->nr_vertex_attrs,
        .attributes     = shader->vertex_attrs,
    };

    WGPUBlendState blend_state = {
        .color = {
            .operation  = WGPUBlendOperation_Add,
            .srcFactor  = WGPUBlendFactor_SrcAlpha,
            .dstFactor  = WGPUBlendFactor_OneMinusSrcAlpha,
        },
        .alpha = {
            .operation  = WGPUBlendOperation_Add,
            .srcFactor  = WGPUBlendFactor_One,
            .dstFactor  = WGPUBlendFactor_OneMinusSrcAlpha,
        },
    };

    WGPUColorTargetState color_targets[FBO_COLOR_ATTACHMENTS_MAX];
    for (unsigned int i = 0; i < nr_color_targets; i++) {
        bool use_blend = blend && wgpu_format_supports_alpha_blend(color_formats[i]);
        color_targets[i] = (WGPUColorTargetState){
            .format     = color_formats[i],
            .blend      = use_blend ? &blend_state : NULL,
            .writeMask  = WGPUColorWriteMask_All,
        };
    }

    WGPUFragmentState frag_state = {
        .module         = shader->frag,
        .entryPoint     = literal_to_wgpu_stringview("main"),
        .targetCount    = nr_color_targets,
        .targets        = color_targets,
    };

    WGPUDepthStencilState depth_stencil = {
        .format             = depth_format,
        .depthWriteEnabled  = WGPUOptionalBool_True,
        .depthCompare       = depth_compare,
    };

    WGPURenderPipelineDescriptor desc = {
        .vertex = {
            .module         = shader->vert,
            .entryPoint     = literal_to_wgpu_stringview("main"),
            .bufferCount    = 1,
            .buffers        = &vbuf_layout,
        },
        .primitive = {
            .topology       = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace      = WGPUFrontFace_CCW,
            .cullMode       = WGPUCullMode_None,
        },
        .depthStencil = has_depth ? &depth_stencil : NULL,
        .multisample = {
            .count  = 1,
            .mask   = ~0u,
        },
        .fragment   = &frag_state,
        .label      = to_wgpu_stringview(shader->name),
    };

    return wgpuDeviceCreateRenderPipeline(r->wgpu.device, &desc);
}

static WGPUTextureFormat wgpu_current_color_format(renderer_t *r)
{
    if (r->wgpu.fbo) {
        texture_format fmt = fbo_texture_format(r->wgpu.fbo, FBO_COLOR_TEXTURE(0));
        return wgpu_texture_format(fmt);
    }
    return wgpu_swapchain_format(r);
}

static cerr draw_control_make(struct ref *ref, void *_opts)
{
    rc_init_opts(draw_control) *opts = _opts;
    draw_control_t *dc = container_of(ref, draw_control_t, ref);

    dc->renderer = opts->renderer;
    dc->shader = opts->shader;

    renderer_t *r = opts->renderer;

    WGPUTextureFormat color_formats[FBO_COLOR_ATTACHMENTS_MAX];
    unsigned int nr_color_targets = 0;

    if (r->wgpu.fbo) {
        fa_for_each(fa, r->wgpu.fbo->layout, texture) {
            int idx = fbo_attachment_color(fa);
            color_formats[nr_color_targets++] = wgpu_texture_format(r->wgpu.fbo->color_config[idx].format);
        }
    }
    if (!nr_color_targets) {
        color_formats[0] = wgpu_swapchain_format(r);
        nr_color_targets = 1;
    }

    dc->color_format = color_formats[0];
    dc->nr_color_targets = nr_color_targets;

    bool has_depth = r->wgpu.fbo && r->wgpu.fbo->layout.depth_texture;
    WGPUTextureFormat depth_fmt = WGPUTextureFormat_Depth32Float;
    WGPUCompareFunction depth_cmp = WGPUCompareFunction_Always;
    if (has_depth) {
        depth_fmt = wgpu_texture_format(r->wgpu.fbo->depth_config.format);
        depth_cmp = wgpu_compare_function(r->wgpu.fbo->depth_config.depth_func);
    }

    dc->has_depth = has_depth;

    dc->pipeline[0] = wgpu_create_pipeline(r, opts->shader,
                                           color_formats, nr_color_targets,
                                           false,
                                           has_depth, depth_fmt, depth_cmp);
    if (!dc->pipeline[0])
        return CERR_INITIALIZATION_FAILED_REASON(
            .fmt  = "pipeline: %u color targets, depth=%s — no-blend variant failed",
            .arg0 = (void *)(uintptr_t)nr_color_targets,
            .arg1 = has_depth ? "yes" : "no"
        );

    dc->pipeline[1] = wgpu_create_pipeline(r, opts->shader,
                                           color_formats, nr_color_targets,
                                           true,
                                           has_depth, depth_fmt, depth_cmp);
    if (!dc->pipeline[1])
        return CERR_INITIALIZATION_FAILED_REASON(
            .fmt  = "pipeline: %u color targets, depth=%s — blend variant failed",
            .arg0 = (void *)(uintptr_t)nr_color_targets,
            .arg1 = has_depth ? "yes" : "no"
        );

    list_append(&opts->renderer->wgpu.dc_cache, &dc->cache_entry);

    return CERR_OK;
}

static void draw_control_drop(struct ref *ref)
{
    draw_control_t *dc = container_of(ref, draw_control_t, ref);
    draw_control_done(dc);
}

DEFINE_REFCLASS2(draw_control);
cresp_struct_ret(draw_control);

cerr _draw_control_init(draw_control_t *dc, const draw_control_init_options *opts)
{
    return ref_embed_opts(draw_control, dc, opts);
}

void draw_control_done(draw_control_t *dc)
{
    if (dc->pipeline[0]) {
        wgpuRenderPipelineRelease(dc->pipeline[0]);
        dc->pipeline[0] = NULL;
    }
    if (dc->pipeline[1]) {
        wgpuRenderPipelineRelease(dc->pipeline[1]);
        dc->pipeline[1] = NULL;
    }
    list_del(&dc->cache_entry);
}

void draw_control_bind(draw_control_t *dc)
{
    renderer_t *r = dc->renderer;
    r->wgpu.dc = dc;

    if (r->wgpu.pass_encoder)
        wgpuRenderPassEncoderSetPipeline(r->wgpu.pass_encoder, dc->pipeline[r->blend ? 1 : 0]);
}

void draw_control_unbind(draw_control_t *dc)
{
}

/****************************************************************************
 * Texture
 ****************************************************************************/

static wgpu_texture_format_t wgpu_texture_format(texture_format format)
{
    switch (format) {
        case TEX_FMT_R32F:      return WGPUTextureFormat_R32Float;
        case TEX_FMT_R16F:      return WGPUTextureFormat_R16Float;
        case TEX_FMT_R8:        return WGPUTextureFormat_R8Unorm;

        case TEX_FMT_RG32F:     return WGPUTextureFormat_RG32Float;
        case TEX_FMT_RG16F:     return WGPUTextureFormat_RG16Float;
        case TEX_FMT_RG8:       return WGPUTextureFormat_RG8Unorm;

        case TEX_FMT_RGB32F:    return WGPUTextureFormat_RGBA32Float; // no RGB format in WebGPU
        case TEX_FMT_RGB16F:    return WGPUTextureFormat_RGBA16Float;
        case TEX_FMT_RGB8:      return WGPUTextureFormat_RGBA8Unorm;
        case TEX_FMT_RGB8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;

        case TEX_FMT_RGBA32F:   return WGPUTextureFormat_RGBA32Float;
        case TEX_FMT_RGBA16F:   return WGPUTextureFormat_RGBA16Float;
        case TEX_FMT_RGBA8:     return WGPUTextureFormat_RGBA8Unorm;
        case TEX_FMT_RGBA8_SRGB:return WGPUTextureFormat_RGBA8UnormSrgb;

        case TEX_FMT_BGRA10XR:  return WGPUTextureFormat_RGB10A2Unorm;
        case TEX_FMT_BGR10A2:   return WGPUTextureFormat_RGB10A2Unorm;
        case TEX_FMT_BGRA8:     return WGPUTextureFormat_BGRA8Unorm;

        case TEX_FMT_DEPTH16F:  return WGPUTextureFormat_Depth16Unorm;
        case TEX_FMT_DEPTH24F:  return WGPUTextureFormat_Depth24Plus;
        case TEX_FMT_DEPTH32F:  return WGPUTextureFormat_Depth32Float;

        case TEX_FMT_R32UI:     return WGPUTextureFormat_R32Uint;
        case TEX_FMT_RG32UI:    return WGPUTextureFormat_RG32Uint;
        case TEX_FMT_RGBA32UI:  return WGPUTextureFormat_RGBA32Uint;

        default:                break;
    }

    clap_unreachable();
    return WGPUTextureFormat_Undefined;
}

static WGPUAddressMode wgpu_address_mode(texture_wrap wrap)
{
    switch (wrap) {
        case TEX_CLAMP_TO_EDGE:         return WGPUAddressMode_ClampToEdge;
        case TEX_CLAMP_TO_BORDER:       return WGPUAddressMode_ClampToEdge; /* no border in WGPU */
        case TEX_WRAP_REPEAT:           return WGPUAddressMode_Repeat;
        case TEX_WRAP_MIRRORED_REPEAT:  return WGPUAddressMode_MirrorRepeat;
        default:                        break;
    }

    clap_unreachable();
    return WGPUAddressMode_ClampToEdge;
}

static WGPUFilterMode wgpu_filter_mode(texture_filter filter)
{
    switch (filter) {
        case TEX_FLT_LINEAR:    return WGPUFilterMode_Linear;
        case TEX_FLT_NEAREST:   return WGPUFilterMode_Nearest;
        default:                break;
    }

    clap_unreachable();
    return WGPUFilterMode_Nearest;
}

static WGPUTextureDimension wgpu_texture_dimension(texture_type type)
{
    switch (type) {
        case TEX_1D:        return WGPUTextureDimension_1D;
        case TEX_2D:
        case TEX_2D_ARRAY:  return WGPUTextureDimension_2D;
        case TEX_3D:        return WGPUTextureDimension_3D;
        default:            break;
    }

    clap_unreachable();
    return WGPUTextureDimension_2D;
}

static WGPUTextureViewDimension wgpu_texture_view_dimension(texture_type type)
{
    switch (type) {
        case TEX_1D:        return WGPUTextureViewDimension_1D;
        case TEX_2D:        return WGPUTextureViewDimension_2D;
        case TEX_2D_ARRAY:  return WGPUTextureViewDimension_2DArray;
        case TEX_3D:        return WGPUTextureViewDimension_3D;
        default:            break;
    }

    clap_unreachable();
    return WGPUTextureViewDimension_2D;
}

static cerr texture_make(struct ref *ref, void *opts)
{
    return CERR_OK;
}

static void texture_drop(struct ref *ref)
{
    texture_t *tex = container_of(ref, texture_t, ref);
    texture_deinit(tex);
}

DEFINE_REFCLASS2(texture);
cresp_struct_ret(texture);

bool texture_format_supported(texture_format format)
{
    return format < TEX_FMT_MAX;
}

bool texture_is_array(texture_t *tex)
{
#ifndef CONFIG_FINAL
    return tex && tex->opts.type == TEX_2D_ARRAY;
#else
    return false;
#endif /* CONFIG_FINAL */
}

bool texture_is_multisampled(texture_t *tex)
{
    return tex && tex->multisampled;
}

cerr _texture_init(texture_t *tex, const texture_init_options *opts)
{
    if (!texture_format_supported(opts->format))
        return CERR_NOT_SUPPORTED_REASON(
            .fmt = "format %s (%ld) not supported",
            .arg0 = texture_format_string(opts->format),
            .arg1 = (const void *)opts->format
        );

    cerr err = ref_embed(texture, tex);
    if (IS_CERR(err))
        return err;

    tex->renderer = opts->renderer;
    tex->type = opts->type;
    tex->format = opts->format;
    tex->width = 0;
    tex->height = 0;
    tex->layers = opts->layers;
    tex->multisampled = opts->multisampled;
    tex->render_target = opts->render_target;

    /* create sampler at init time — sampler state is immutable in WGPU */
    WGPUSamplerDescriptor sampler_desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sampler_desc.addressModeU = wgpu_address_mode(opts->wrap);
    sampler_desc.addressModeV = wgpu_address_mode(opts->wrap);
    sampler_desc.addressModeW = wgpu_address_mode(opts->wrap);
    sampler_desc.minFilter = wgpu_filter_mode(opts->min_filter);
    sampler_desc.magFilter = wgpu_filter_mode(opts->mag_filter);
    tex->sampler = wgpuDeviceCreateSampler(opts->renderer->wgpu.device, &sampler_desc);

#ifndef CONFIG_FINAL
    tex->opts = *opts;
#endif /* CONFIG_FINAL */

    return CERR_OK;
}

texture_t *texture_clone(texture_t *tex)
{
    if (!tex)
        return NULL;

    texture_t *clone = ref_new(texture);
    if (!clone)
        return NULL;

    clone->renderer = tex->renderer;
    clone->type = tex->type;
    clone->format = tex->format;
    clone->width = tex->width;
    clone->height = tex->height;
    clone->layers = tex->layers;
    clone->multisampled = tex->multisampled;
    clone->render_target = false;
    clone->loaded = tex->loaded;
    /* share the GPU objects */
    clone->texture = tex->texture;
    clone->view = tex->view;
    clone->sampler = tex->sampler;
#ifndef CONFIG_FINAL
    clone->opts = tex->opts;
    clone->opts.render_target = false;
#endif /* CONFIG_FINAL */

    return clone;
}

static void wgpu_texture_release(texture_t *tex)
{
    if (tex->view) {
        wgpuTextureViewRelease(tex->view);
        tex->view = NULL;
    }
    if (tex->texture) {
        wgpuTextureRelease(tex->texture);
        tex->texture = NULL;
    }
}

void texture_deinit(texture_t *tex)
{
    if (!tex)
        return;

    wgpu_texture_release(tex);
    if (tex->sampler) {
        wgpuSamplerRelease(tex->sampler);
        tex->sampler = NULL;
    }

    tex->width = 0;
    tex->height = 0;
    tex->layers = 0;
    tex->multisampled = false;
    tex->loaded = false;
}

static cerr wgpu_texture_create(texture_t *tex, unsigned int width, unsigned int height)
{
    renderer_t *r = tex->renderer;
    WGPUTextureFormat wfmt = wgpu_texture_format(tex->format);

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = (WGPUExtent3D){ width, height, max(tex->layers, 1u) };
    desc.format = wfmt;
    desc.dimension = wgpu_texture_dimension(tex->type);
    desc.mipLevelCount = 1;
    desc.sampleCount = tex->multisampled ? WGPU_MSAA_SAMPLES : 1;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    if (tex->render_target)
        desc.usage |= WGPUTextureUsage_RenderAttachment;

    /* release previous, if resizing */
    wgpu_texture_release(tex);

    tex->texture = wgpuDeviceCreateTexture(r->wgpu.device, &desc);
    if (!tex->texture)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "wgpuDeviceCreateTexture failed");

    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.format = wfmt;
    view_desc.dimension = wgpu_texture_view_dimension(tex->type);
    view_desc.mipLevelCount = 1;
    view_desc.arrayLayerCount = tex->type == TEX_2D_ARRAY ? tex->layers : 1;
    tex->view = wgpuTextureCreateView(tex->texture, &view_desc);
    if (!tex->view)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "wgpuTextureCreateView failed");

    return CERR_OK;
}

cerr texture_load(texture_t *tex, texture_format format, unsigned int width, unsigned int height,
                  void *buf)
{
    LOCAL_SET(void, converted) = NULL;

    if (buf && texture_format_needs_alpha(format)) {
        size_t rgba_size;
        converted = texture_rgb_to_rgba(format, buf, (size_t)width * height * max(tex->layers, 1u), &rgba_size);
        if (!converted)
            return CERR_NOMEM;
        buf = converted;
        format = texture_format_add_alpha(format);
    }

    tex->format = format;
    tex->width = width;
    tex->height = height;

    CERR_RET_CERR(wgpu_texture_create(tex, width, height));

    if (buf) {
        unsigned int bpp = texture_format_comp_size(format) * texture_format_nr_comps(format);
        WGPUTexelCopyTextureInfo dest = {
            .texture    = tex->texture,
            .mipLevel   = 0,
        };
        WGPUTexelCopyBufferLayout layout = {
            .bytesPerRow    = width * bpp,
            .rowsPerImage   = height,
        };
        WGPUExtent3D extent = { width, height, max(tex->layers, 1u) };
        wgpuQueueWriteTexture(tex->renderer->wgpu.queue, &dest, buf,
                              (size_t)width * height * max(tex->layers, 1u) * bpp,
                              &layout, &extent);
    }

    tex->loaded = true;
#ifndef CONFIG_FINAL
    tex->opts.format = format;
#endif

    return CERR_OK;
}

cerr texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!texture_loaded(tex))
        return CERR_TEXTURE_NOT_LOADED;

    tex->width = width;
    tex->height = height;

    return wgpu_texture_create(tex, width, height);
}

void texture_bind(texture_t *tex, unsigned int target)
{
    auto r = tex->renderer;
    if (!r) return;

    /* target is the WGPU image binding (0, 2, 4, ...); slot index = target / 2 */
    unsigned int slot = target / 2;
    if (slot >= BINDING_TEXTURE_MAX)    return;

    r->wgpu.bound_textures[slot] = tex;
    r->wgpu.dc->shader->binding_mask |= 3ull << target;
}

void texture_unbind(texture_t *tex, unsigned int target)
{
}

void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight)
{
    // XXX: tex NULL check is blergh
    // crashing is better (ok, not in a browser, because it's impossible to debug)
    // returning an error is better
    if (pwidth)
        *pwidth = tex ? tex->width : 0;
    if (pheight)
        *pheight = tex ? tex->height : 0;
}

void texture_done(texture_t *tex)
{
    if (tex && !ref_is_static(&tex->ref))
        ref_put_last(tex);
}

texid_t texture_id(texture_t *tex)
{
    if (!texture_loaded(tex))
        return 0;

    if (tex->render_target)
        return 0;

    return (texid_t)(uintptr_t)tex;
}

bool texture_loaded(texture_t *tex)
{
    return tex->loaded;
}

#ifndef CONFIG_FINAL
cres(int) texture_set_name(texture_t *tex, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char label[128];
    vsnprintf(label, sizeof(label), fmt, ap);
    va_end(ap);

    if (tex->texture) {
        WGPUStringView sv = { label, strlen(label) };
        wgpuTextureSetLabel(tex->texture, sv);
    }

    return cres_val(int, 0);
}
#endif /* CONFIG_FINAL */

/****************************************************************************
 * FBO
 ****************************************************************************/

bool fbo_texture_supported(texture_format format)
{
    return texture_format_supported(format);
}

texture_format fbo_texture_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (attachment.depth_texture || attachment.depth_buffer)
        return fbo ? fbo->depth_config.format : TEX_FMT_DEPTH32F;

    if (fbo && attachment.color_textures) {
        int idx = fbo_attachment_color(attachment);
        if (idx >= 0 && fbo->color_config)
            return fbo->color_config[idx].format;
    }

    return TEX_FMT_RGBA8;
}

static WGPULoadOp wgpu_load_op(fbo_load_action action)
{
    switch (action) {
        case FBOLOAD_LOAD:      return WGPULoadOp_Load;
        case FBOLOAD_CLEAR:     return WGPULoadOp_Clear;
        case FBOLOAD_DONTCARE:  return WGPULoadOp_Clear;
    }
    return WGPULoadOp_Clear;
}

static WGPUStoreOp wgpu_store_op(fbo_store_action action)
{
    switch (action) {
        case FBOSTORE_STORE:        return WGPUStoreOp_Store;
        case FBOSTORE_MS_RESOLVE:   return WGPUStoreOp_Store; /* XXX: MSAA resolve */
        case FBOSTORE_DONTCARE:     return WGPUStoreOp_Discard;
    }
    return WGPUStoreOp_Store;
}

static void fbo_attachments_deinit(fbo_t *fbo)
{
    fa_for_each(fa, fbo->layout, texture) {
        int idx = fbo_attachment_color(fa);
        texture_deinit(&fbo->color_tex[idx]);
    }
    if (fbo->layout.depth_texture)
        texture_deinit(&fbo->depth_tex);
}

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

    texture_set_name(&fbo->depth_tex, "%s:depth", fbo->name);

    CERR_RET_CERR(texture_load(&fbo->depth_tex, fbo->depth_config.format, fbo->width, fbo->height, NULL));

    return CERR_OK;
}

static cerr_check fbo_texture_init(fbo_t *fbo, fbo_attachment attachment)
{
    int idx = fbo_attachment_color(attachment);

    cerr err = texture_init(&fbo->color_tex[idx],
                            .renderer      = fbo->renderer,
                            .type          = fbo->layers > 1 ? TEX_2D_ARRAY : TEX_2D,
                            .layers        = fbo->layers,
                            .format        = fbo_texture_format(fbo, attachment),
                            .render_target = true,
                            .wrap          = TEX_CLAMP_TO_EDGE,
                            .min_filter    = TEX_FLT_LINEAR,
                            .mag_filter    = TEX_FLT_LINEAR);
    if (IS_CERR(err))
        return err;

    texture_set_name(&fbo->color_tex[idx], "%s:rt%d", fbo->name, idx);

    err = texture_load(&fbo->color_tex[idx], fbo_texture_format(fbo, attachment), fbo->width, fbo->height, NULL);
    fbo->color_config[idx].format = fbo->color_tex[idx].format;

    return err;
}

static cerr_check fbo_textures_init(fbo_t *fbo, fbo_attachment layout)
{
    fa_for_each(fa, layout, texture) {
        CERR_RET(fbo_texture_init(fbo, fa), return __cerr);
    }

    return CERR_OK;
}

static cerr fbo_make(struct ref *ref, void *opts)
{
    return CERR_OK;
}

static void fbo_drop(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    fbo_attachments_deinit(fbo);
    mem_free(fbo->color_config);
}

DEFINE_REFCLASS2(fbo);
cresp_struct_ret(fbo);

must_check cresp(fbo_t) _fbo_new(const fbo_init_options *opts)
{
    if (!opts || !opts->width || !opts->height)
        return cresp_error(fbo_t, CERR_INVALID_ARGUMENTS);

    fbo_t *fbo = ref_new(fbo);
    if (!fbo)
        return cresp_error(fbo_t, CERR_NOMEM);

    fbo->renderer     = opts->renderer;
    fbo->width        = opts->width;
    fbo->height       = opts->height;
    fbo->layers       = opts->layers ? : 1;
    fbo->depth_config = opts->depth_config;
    fbo->nr_samples   = opts->nr_samples ? : 1;
    fbo->layout       = opts->layout;
    fbo->name         = opts->name ? : "<unset>";

    if (!fbo->layout.mask)
        fbo->layout.color_texture0 = 1;

    int nr_color_configs = fa_nr_color_buffer(fbo->layout) ? :
                           fa_nr_color_texture(fbo->layout);
    size_t size = nr_color_configs * sizeof(fbo_attconfig);
    fbo->color_config = memdup(opts->color_config ? : &(fbo_attconfig){ .format = TEX_FMT_DEFAULT }, size);

    if (fbo->layout.depth_texture)
        CERR_RET_T(fbo_depth_texture_init(fbo), fbo_t);
    if (fbo->layout.color_textures)
        CERR_RET_T(fbo_textures_init(fbo, fbo->layout), fbo_t);

    return cresp_val(fbo_t, fbo);
}

fbo_t *fbo_get(fbo_t *fbo)
{
    return ref_get(fbo);
}

void fbo_put(fbo_t *fbo)
{
    ref_put(fbo);
}

void fbo_put_last(fbo_t *fbo)
{
    ref_put_last(fbo);
}

void fbo_prepare(fbo_t *fbo)
{
    renderer_t *r = fbo->renderer;

    if (!r->wgpu.cmd_encoder) {
        err("fbo_prepare: no command encoder\n");
        return;
    }

    /* Build color attachments from FBO textures */
    WGPURenderPassColorAttachment color_atts[FBO_COLOR_ATTACHMENTS_MAX] = {};
    unsigned int nr_colors = 0;

    fa_for_each(fa, fbo->layout, texture) {
        int idx = fbo_attachment_color(fa);
        texture_t *tex = &fbo->color_tex[idx];

        if (!tex->view)
            continue;

        color_atts[nr_colors] = (WGPURenderPassColorAttachment){
            .view           = tex->view,
            .depthSlice     = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp         = wgpu_load_op(fbo->color_config[idx].load_action),
            .storeOp        = wgpu_store_op(fbo->color_config[idx].store_action),
            .clearValue     = {
                fbo->color_config[idx].clear_color[0],
                fbo->color_config[idx].clear_color[1],
                fbo->color_config[idx].clear_color[2],
                fbo->color_config[idx].clear_color[3],
            },
        };
        nr_colors++;
    }

    WGPURenderPassDepthStencilAttachment depth_att = {};
    bool has_depth = fbo->layout.depth_texture && fbo->depth_tex.view;

    if (has_depth) {
        depth_att = (WGPURenderPassDepthStencilAttachment){
            .view               = fbo->depth_tex.view,
            .depthLoadOp        = wgpu_load_op(fbo->depth_config.load_action),
            .depthStoreOp       = wgpu_store_op(fbo->depth_config.store_action),
            .depthClearValue    = fbo->depth_config.clear_depth,
        };
    }

    WGPURenderPassDescriptor desc = {
        .colorAttachmentCount       = nr_colors,
        .colorAttachments           = color_atts,
        .depthStencilAttachment     = has_depth ? &depth_att : NULL,
    };

    r->wgpu.pass_encoder = wgpuCommandEncoderBeginRenderPass(r->wgpu.cmd_encoder, &desc);
    r->wgpu.fbo = fbo;
}

void fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
    renderer_t *r = fbo->renderer;

    if (r->wgpu.fbo != fbo) {
        err("fbo_done: wrong fbo: %s (%p)\n", fbo->name, fbo);
        return;
    }

    if (r->wgpu.pass_encoder) {
        wgpuRenderPassEncoderEnd(r->wgpu.pass_encoder);
        wgpuRenderPassEncoderRelease(r->wgpu.pass_encoder);
        r->wgpu.pass_encoder = NULL;
    }

    r->wgpu.fbo = NULL;
}

void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment)
{
    /* XXX: implement via render pass or copy command */
}

cerr_check fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo)
        return CERR_INVALID_ARGUMENTS;

    fbo_attachments_deinit(fbo);

    unsigned int orig_width = fbo->width;
    unsigned int orig_height = fbo->height;

    fbo->width = width;
    fbo->height = height;

    if (fbo->layout.depth_texture)
        CERR_RET(fbo_depth_texture_init(fbo), goto fail);
    if (fbo->layout.color_textures)
        CERR_RET(fbo_textures_init(fbo, fbo->layout), goto fail_depth);

    return CERR_OK;

fail_depth:
    if (fbo->layout.depth_texture)
        texture_deinit(&fbo->depth_tex);

fail:
    fbo->width = orig_width;
    fbo->height = orig_height;

    if (fbo->layout.depth_texture)
        CERR_RET_CERR(fbo_depth_texture_init(fbo));
    if (fbo->layout.color_textures)
        CERR_RET_CERR(fbo_textures_init(fbo, fbo->layout));

    return CERR_NOMEM;
}

texture_t *fbo_texture(fbo_t *fbo, fbo_attachment attachment)
{
    // XXX: fbo NULL check is blergh, return cresp_check(texture_t)
    if (!fbo)
        return fbo_texture_fallback(attachment);

    if (attachment.depth_texture)
        return &fbo->depth_tex;

    if (attachment.color_textures) {
        int idx = fbo_attachment_color(attachment);
        if (idx >= 0 && idx < FBO_COLOR_ATTACHMENTS_MAX)
            return &fbo->color_tex[idx];
    }

    return fbo_texture_fallback(attachment);
}

int fbo_width(fbo_t *fbo)
{
    return fbo->width;
}

int fbo_height(fbo_t *fbo)
{
    return fbo->height;
}

bool fbo_is_multisampled(fbo_t *fbo)
{
    return fbo->nr_samples > 1;
}

bool fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment)
{
    return !!attachment.mask;
}

texture_format fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (!attachment.mask)
        return TEX_FMT_MAX;

    return fbo_texture_format(fbo, attachment);
}

/****************************************************************************
 * Binding points
 ****************************************************************************/

void binding_points_init(binding_points_t *bps)
{
    bps->binding = -1;
    bps->stages = 0;
}

void binding_points_done(binding_points_t *bps)
{
    binding_points_init(bps);
}

void binding_points_add(binding_points_t *bps, shader_stage stage, int binding)
{
    bps->binding = binding;
    bps->stages |= 1 << stage;
}

/****************************************************************************
 * UBOs
 ****************************************************************************/

static cerr uniform_buffer_make(struct ref *ref, void *_opts)
{
    rc_init_opts(uniform_buffer) *opts = _opts;

    if (opts->binding < 0)
        return CERR_INVALID_ARGUMENTS;

    return CERR_OK;
}

static void uniform_buffer_drop(struct ref *ref)
{
    uniform_buffer_t *ubo = container_of(ref, uniform_buffer_t, ref);
    uniform_buffer_done(ubo);
}

DEFINE_REFCLASS2(uniform_buffer);

cerr uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name, int binding)
{
    cerr err = ref_embed(uniform_buffer, ubo, .renderer = r, .name = name, .binding = binding);
    if (IS_CERR(err))
        return err;

    ubo->renderer = r;
    ubo->binding = binding;
    list_append(&r->wgpu.ubos, &ubo->entry);
    return CERR_OK;
}

static inline size_t wgpu_ubo_offset(uniform_buffer_t *ubo)
{
    return ubo->slot_size * ubo->item;
}

cerr uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size)
{
    /* There's no reason to ever want to reallocate a UBO */
    if (ubo->data || ubo->size)
        return CERR_INVALID_OPERATION;

    ubo->size = round_up(size, 16);
    ubo->slot_size = round_up(ubo->size, 256);
    ubo->total_size = ubo->slot_size * 1024;

    renderer_t *r = ubo->renderer;

    /* One large GPU buffer for all slots */
    ubo->buf = wgpu_create_buffer(r, NULL,
                                   WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                   ubo->total_size);
    if (!ubo->buf)
        return CERR_NOMEM;

    /* CPU shadow covers the full allocation */
    ubo->data = mem_alloc(ubo->total_size, .zero = 1);
    if (!ubo->data)
        return CERR_NOMEM;

    ubo->prev = ubo->data;

    return CERR_OK;
}

void uniform_buffer_done(uniform_buffer_t *ubo)
{
    if (ubo->buf) {
        wgpuBufferRelease(ubo->buf);
        ubo->buf = NULL;
    }
    list_del(&ubo->entry);
    mem_free(ubo->data, .size = ubo->total_size);
    ubo->data = NULL;
    ubo->prev = NULL;
    ubo->size = 0;
    ubo->total_size = 0;
    ubo->item = 0;
    ubo->dirty = false;
    ubo->advance = false;
}

void uniform_buffer_update(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    if (!ubo || !ubo->data || !ubo->size || !ubo->buf)
        return;

    renderer_t *r = ubo->renderer;
    size_t offset = wgpu_ubo_offset(ubo);

    /* Upload the current slot to the GPU */
    wgpuQueueWriteBuffer(r->wgpu.queue, ubo->buf, offset, ubo->data, ubo->size);

    ubo->advance = true;
    ubo->dirty = false;

    /* Track this UBO for bind group creation at draw time */
    int slot = ubo->binding - WGPU_BINDING_UBO_BASE;
    if (slot >= 0 && slot < BINDING_UBO_MAX)
        r->wgpu.bound_ubos[slot] = ubo;
}

cerr uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    return CERR_OK;
}

cerr_check _uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                               unsigned int count, const void *value);

/* data always points to shadow_base + item * slot_size */
static inline void *wgpu_ubo_shadow_base(uniform_buffer_t *ubo)
{
    return (char *)ubo->data - ubo->item * ubo->slot_size;
}

cerr uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                        unsigned int count, const void *value)
{
    if (!value)
        goto out;

    /* Advance to a new slot on first write after uniform_buffer_update() */
    if (ubo->advance) {
        char *base = wgpu_ubo_shadow_base(ubo);
        void *prev_data = ubo->data;

        ubo->item++;
        ubo->data = base + ubo->item * ubo->slot_size;

        /* COW: seed the new slot from the previous one */
        memcpy(ubo->data, prev_data, ubo->size);
        ubo->advance = false;
    }

out:
    return _uniform_buffer_set(ubo, type, offset, size, count, value);
}

/****************************************************************************
 * Shader
 ****************************************************************************/

static DEFINE_CLEANUP_EXACT(wgpu_shader_module_t, if (*p) wgpuShaderModuleRelease(*p));
cres_ret(wgpu_shader_module_t);

static cres(wgpu_shader_module_t) wgpu_create_shader_module(renderer_t *r, const char *label,
                                                             const char *source)
{
    WGPUShaderSourceWGSL wgsl = {
        .chain  = { .sType = WGPUSType_ShaderSourceWGSL },
        .code   = { source, strlen(source) },
    };
    WGPUShaderModuleDescriptor desc = {
        .nextInChain    = &wgsl.chain,
        .label          = { label, strlen(label) },
    };

    WGPUShaderModule module = wgpuDeviceCreateShaderModule(r->wgpu.device, &desc);
    if (!module)
        return cres_error(wgpu_shader_module_t,
            CERR_INVALID_SHADER,
            .fmt    = "failed to create %s shader module",
            .arg0   = label
        );

    return cres_val(wgpu_shader_module_t, module);
}

cerr shader_init(renderer_t *r, shader_t *shader, const char *vertex,
                 const char *geometry, const char *fragment)
{
    shader->renderer = r;
    shader->binding_mask = 0;

    LOCAL_SET_EXACT(wgpu_shader_module_t, vert) = CRES_RET_CERR(wgpu_create_shader_module(r, "vert", vertex));
    LOCAL_SET_EXACT(wgpu_shader_module_t, frag) = CRES_RET_CERR(wgpu_create_shader_module(r, "frag", fragment));

    shader->vert = NOCU(vert);
    shader->frag = NOCU(frag);

    return CERR_OK;
}

static_assert(6 >= ATTR_MAX, "shader_t::vertex_attrs array too small for ATTR_MAX");

static WGPUVertexFormat wgpu_vertex_format(data_type type)
{
    switch (type) {
        case DT_FLOAT:  return WGPUVertexFormat_Float32;
        case DT_VEC2:   return WGPUVertexFormat_Float32x2;
        case DT_VEC3:   return WGPUVertexFormat_Float32x3;
        case DT_VEC4:   return WGPUVertexFormat_Float32x4;
        case DT_INT:    return WGPUVertexFormat_Sint32;
        case DT_IVEC2:  return WGPUVertexFormat_Sint32x2;
        case DT_IVEC3:  return WGPUVertexFormat_Sint32x3;
        case DT_IVEC4:  return WGPUVertexFormat_Sint32x4;
        case DT_UINT:   return WGPUVertexFormat_Uint32;
        case DT_UVEC2:  return WGPUVertexFormat_Uint32x2;
        case DT_UVEC3:  return WGPUVertexFormat_Uint32x3;
        case DT_UVEC4:  return WGPUVertexFormat_Uint32x4;
        default:        break;
    }

    clap_unreachable();
    return WGPUVertexFormat_Force32;
}

void shader_set_vertex_attrs(shader_t *shader, size_t stride,
                             size_t *offs, data_type *types,
                             size_t *comp_counts, unsigned int nr_attrs)
{
    shader->vertex_stride = stride;
    shader->nr_vertex_attrs = nr_attrs;

    for (unsigned int i = 0; i < nr_attrs; i++) {
        WGPUVertexFormat fmt = wgpu_vertex_format(types[i]);
        shader->vertex_attrs[i] = (WGPUVertexAttribute){
            .format         = fmt,
            .offset         = offs[i],
            .shaderLocation = i,
        };
        dbg(" -> @location(%u): offset %zu %s (fmt %d)\n", i, offs[i], data_type_name(types[i]), fmt);
    }
}

void shader_set_name(shader_t *shader, const char *name)
{
    free(shader->name);
    shader->name = strdup(name);
}

void shader_done(shader_t *shader)
{
    free(shader->name);

    if (shader->frag) {
        wgpuShaderModuleRelease(shader->frag);
        shader->frag = NULL;
    }
    if (shader->vert) {
        wgpuShaderModuleRelease(shader->vert);
        shader->vert = NULL;
    }
}

int shader_id(shader_t *shader)
{
    return (int)(uintptr_t)shader;
}

cerr shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt,
                                const char *name)
{
    shader->binding_mask |= 1ull << bpt->binding;
    return CERR_OK;
}

attr_t shader_attribute(shader_t *shader, const char *name, attr_t attr)
{
    return attr;
}

uniform_t shader_uniform(shader_t *shader, const char *name)
{
    /* WGPU has no runtime uniform location query; reflection JSON
     * populates p->vars[] directly via shader_reflection_apply(). */
    return UA_NOT_PRESENT;
}

static cresp(draw_control) wgpu_dc_find_or_create(renderer_t *r, shader_t *shader)
{
    WGPUTextureFormat fmt = wgpu_current_color_format(r);
    bool has_depth = r->wgpu.fbo && r->wgpu.fbo->layout.depth_texture;
    unsigned int nr_targets = r->wgpu.fbo ? fa_nr_color_texture(r->wgpu.fbo->layout) : 1;
    draw_control_t *dc;

    list_for_each_entry(dc, &r->wgpu.dc_cache, cache_entry)
        if (dc->shader == shader && dc->color_format == fmt &&
            dc->nr_color_targets == nr_targets && dc->has_depth == has_depth)
            return cresp_val(draw_control, dc);

    dc = CRES_RET_T(ref_new_checked(draw_control, .renderer = r, .shader = shader), draw_control);

    return cresp_val(draw_control, dc);
}

cerr shader_use(shader_t *shader, bool draw)
{
    if (!draw)  return CERR_OK;

    renderer_t *r = shader->renderer;
    CERR_RET_CERR(wgpu_previous_errors(r));

    // Clear per-draw binding state so only resources bound for this
    // shader contribute to the bind group (auto layout only contains
    // bindings the shader actually declares).
    memset(r->wgpu.bound_textures, 0, sizeof(r->wgpu.bound_textures));
    memset(r->wgpu.bound_ubos, 0, sizeof(r->wgpu.bound_ubos));

    auto dc = CRES_RET_CERR(wgpu_dc_find_or_create(r, shader));
    draw_control_bind(dc);

    return CERR_OK;
}

void shader_unuse(shader_t *shader, bool draw)
{
}

cres(size_t)
shader_uniform_offset_query(shader_t *shader, const char *ubo_name,
                            const char *var_name)
{
    return cres_error(size_t, CERR_NOT_FOUND);
}

void uniform_set_ptr(uniform_t uniform, data_type type,
                     unsigned int count, const void *value)
{
}

/****************************************************************************
 * Renderer
 ****************************************************************************/

int renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    if (limit >= RENDER_LIMIT_MAX)
        return 0;

    return renderer->wgpu.limits[limit];
}

static void wgpu_message_save(renderer_t *r, WGPUStringView str)
{
    declare_sv_from(msg, r->wgpu.wgpu_message);
    sv_reset(&sv(msg));
    sv_append(&sv(msg), "%-*s", (int)str.length, str.data);
}

/****************************************************************************
 * Async calls / futures plumbing
 ****************************************************************************/

static cerr_check wgpu_wait(renderer_t *r, WGPUFuture future)
{
    WGPUFutureWaitInfo future_info = { .future = future };

    auto status = wgpuInstanceWaitAny(r->wgpu.instance, 1, &future_info, UINT64_MAX);
    if (status != WGPUWaitStatus_Success) {
        return CERR_INITIALIZATION_FAILED_REASON(
            .fmt    = "wait status: %d",
            .arg0   = (void *)status
        );
    }

    if (!future_info.completed)
        return CERR_INITIALIZATION_FAILED_REASON(.fmt = "future wait not completed");

    return CERR_OK;
}

struct wgpu_callback_data {
    renderer_t  *r;
    cerr        status;
};

static void request_adapter_cb(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                               WGPUStringView message, void *data1, void *data2)
{
    struct wgpu_callback_data *cd = data1;
    if (status != WGPURequestAdapterStatus_Success) {
        wgpu_message_save(cd->r, message);
        cd->status = CERR_INITIALIZATION_FAILED_REASON(
            .fmt    = "wgpu request adapter failed with '%s'",
            .arg0   = cd->r->wgpu.wgpu_message,
        );

        return;
    }

    cd->status = CERR_OK;
    cd->r->wgpu.adapter = adapter;
}

static void request_device_cb(WGPURequestDeviceStatus status, WGPUDevice device,
                              WGPUStringView message, void *data1, void *data2)
{
    struct wgpu_callback_data *cd = data1;
    if (status != WGPURequestDeviceStatus_Success) {
        wgpu_message_save(cd->r, message);
        cd->status = CERR_INITIALIZATION_FAILED_REASON(
            .fmt    = "wgpu request device failed with '%s'",
            .arg0   = cd->r->wgpu.wgpu_message,
        );

        return;
    }

    cd->status = CERR_OK;
    cd->r->wgpu.device = device;
}

static cerr_check wgpu_request_adapter(renderer_t *r)
{
    WGPUInstanceFeatureName features[] = { WGPUInstanceFeatureName_TimedWaitAny };
    WGPUInstanceDescriptor desc = {
        .requiredFeatures       = features,
        .requiredFeatureCount   = array_size(features),
    };
    r->wgpu.instance = wgpuCreateInstance(&desc);
    if (!r->wgpu.instance)  return CERR_INITIALIZATION_FAILED_REASON(.fmt = "wgpuCreateInstance() failed");

    struct wgpu_callback_data cd = { .r = r };
    WGPURequestAdapterCallbackInfo adapter_cb = {
        .mode       = WGPUCallbackMode_WaitAnyOnly,
        .callback   = request_adapter_cb,
        .userdata1  = &cd,
    };
    WGPURequestAdapterOptions adapter_opts = {
        .powerPreference    = WGPUPowerPreference_HighPerformance,
    };

    auto af = wgpuInstanceRequestAdapter(r->wgpu.instance, &adapter_opts, adapter_cb);

    CERR_RET_CERR(wgpu_wait(r, af));

    return cd.status;
}

static void wgpu_uncaptured_log(WGPUDevice const *device, WGPUErrorType type,
                                WGPUStringView message, void *data1, void *data2)
{
    static unsigned int limit = 0;

    if (limit++ > 300)  return;

    renderer_t *r = data1;
    err("### '%-*s'\n", (int)message.length, message.data);
    atomic_fetch_add_explicit(&r->wgpu.error, 1, memory_order_release);
}

static cerr_check wgpu_request_device(renderer_t *r)
{
    struct wgpu_callback_data cd = { .r = r };
    WGPURequestDeviceCallbackInfo device_cb = {
        .mode       = WGPUCallbackMode_WaitAnyOnly,
        .callback   = request_device_cb,
        .userdata1  = &cd,
    };

    WGPUFeatureName required_features[] = {
        WGPUFeatureName_Float32Filterable,
    };

    WGPUDeviceDescriptor desc = {
        .uncapturedErrorCallbackInfo = { .callback = wgpu_uncaptured_log, .userdata1 = r },
        .label          = literal_to_wgpu_stringview("Clap WebGPU"),
        .requiredFeatureCount = array_size(required_features),
        .requiredFeatures     = required_features,
        .defaultQueue   = (WGPUQueueDescriptor){
            .label      = literal_to_wgpu_stringview("Clap WebGPU queue"),
        }
    };

    auto df = wgpuAdapterRequestDevice(r->wgpu.adapter, &desc, device_cb);

    CERR_RET_CERR(wgpu_wait(r, df));

    return cd.status;
}

static const char *wgpu_feature_name_str(WGPUFeatureName f)
{
    switch (f) {
        case WGPUFeatureName_CoreFeaturesAndLimits:         return "CoreFeaturesAndLimits";
        case WGPUFeatureName_DepthClipControl:              return "DepthClipControl";
        case WGPUFeatureName_Depth32FloatStencil8:          return "Depth32FloatStencil8";
        case WGPUFeatureName_TextureCompressionBC:          return "TextureCompressionBC";
        case WGPUFeatureName_TextureCompressionBCSliced3D:  return "TextureCompressionBCSliced3D";
        case WGPUFeatureName_TextureCompressionETC2:        return "TextureCompressionETC2";
        case WGPUFeatureName_TextureCompressionASTC:        return "TextureCompressionASTC";
        case WGPUFeatureName_TextureCompressionASTCSliced3D: return "TextureCompressionASTCSliced3D";
        case WGPUFeatureName_TimestampQuery:                return "TimestampQuery";
        case WGPUFeatureName_IndirectFirstInstance:         return "IndirectFirstInstance";
        case WGPUFeatureName_ShaderF16:                     return "ShaderF16";
        case WGPUFeatureName_RG11B10UfloatRenderable:       return "RG11B10UfloatRenderable";
        case WGPUFeatureName_BGRA8UnormStorage:             return "BGRA8UnormStorage";
        case WGPUFeatureName_Float32Filterable:             return "Float32Filterable";
        case WGPUFeatureName_Float32Blendable:              return "Float32Blendable";
        case WGPUFeatureName_ClipDistances:                 return "ClipDistances";
        case WGPUFeatureName_DualSourceBlending:            return "DualSourceBlending";
        case WGPUFeatureName_Subgroups:                     return "Subgroups";
        case WGPUFeatureName_TextureFormatsTier1:           return "TextureFormatsTier1";
        case WGPUFeatureName_TextureFormatsTier2:           return "TextureFormatsTier2";
        case WGPUFeatureName_PrimitiveIndex:                return "PrimitiveIndex";
        case WGPUFeatureName_Unorm16TextureFormats:         return "Unorm16TextureFormats";
        case WGPUFeatureName_Snorm16TextureFormats:         return "Snorm16TextureFormats";
        case WGPUFeatureName_MultiDrawIndirect:             return "MultiDrawIndirect";
        case WGPUFeatureName_Force32:                       return "Force32";
        default:                                            return "<unknown>";
    }
}

static inline bool wgpu_ok(WGPUStatus status)   { return status == WGPUStatus_Success; }

static void wgpu_query_device(renderer_t *r)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    auto status = wgpuDeviceGetAdapterInfo(r->wgpu.device, &info);
    if (!wgpu_ok(status)) {
        err("failed to get adapter info\n");
        return;
    }

    dbg("### vendor: '%-*s' device: '%-*s' arch: '%-*s' desc: '%-*s'\n",
        (int)info.vendor.length, info.vendor.data,
        (int)info.device.length, info.device.data,
        (int)info.architecture.length, info.architecture.data,
        (int)info.description.length, info.description.data);

    wgpuAdapterInfoFreeMembers(info);

    // wgpuDeviceHasFeature(r->device, WGPUFeatureName_RG11B10UfloatRenderable)
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(r->wgpu.adapter, &features);

    // TODO: store this in renderer_t under !CONFIG_FINAL for debug UI
    for (size_t i = 0; i < features.featureCount; i++)
        dbg(" -> %s (%d)\n", wgpu_feature_name_str(features.features[i]), features.features[i]);

    wgpuSupportedFeaturesFreeMembers(features);

    // EDR (HDR display output) needs two things:
    //  1) the display itself can present extended-range content — asked via
    //     display_supports_edr() (CSS '(dynamic-range: high)' on the web).
    //  2) the renderer can produce a half-float swapchain. RGBA16Float is
    //     mandatory in core WebGPU, so the renderer side is always satisfied;
    //     we don't gate on RG11B10UfloatRenderable (that's an alternative
    //     compact HDR format we may opt into later).
    //
    // Initial r->hdr is left to renderer_hdr_enable() which gets called from
    // clap_init() / scene UI based on the user's render_options.
    r->wgpu.edr_supported = display_supports_edr();
    r->wgpu.hdr           = r->wgpu.edr_supported;

    WGPULimits limits = WGPU_LIMITS_INIT;
    status = wgpuDeviceGetLimits(r->wgpu.device, &limits);
    if (!wgpu_ok(status)) {
        err("failed to get device limits\n");
        return;
    }

    r->wgpu.limits[RENDER_LIMIT_MAX_TEXTURE_SIZE]            = limits.maxTextureDimension2D; // Existing users mean 2D when they query this; XXX: split
    r->wgpu.limits[RENDER_LIMIT_MAX_TEXTURE_UNITS]           = limits.maxSampledTexturesPerShaderStage; // Close enough; nothing is querying this at the moment
    r->wgpu.limits[RENDER_LIMIT_MAX_TEXTURE_ARRAY_LAYERS]    = limits.maxTextureArrayLayers;
    r->wgpu.limits[RENDER_LIMIT_MAX_COLOR_ATTACHMENTS]       = limits.maxColorAttachments;
    r->wgpu.limits[RENDER_LIMIT_MAX_COLOR_TEXTURE_SAMPLES]   = WGPU_MSAA_SAMPLES;
    r->wgpu.limits[RENDER_LIMIT_MAX_DEPTH_TEXTURE_SAMPLES]   = WGPU_MSAA_SAMPLES;
    r->wgpu.limits[RENDER_LIMIT_MAX_SAMPLES]                 = WGPU_MSAA_SAMPLES;
    r->wgpu.limits[RENDER_LIMIT_MAX_DRAW_BUFFERS]            = 8;
    r->wgpu.limits[RENDER_LIMIT_MAX_ANISOTROPY]              = 1;
    r->wgpu.limits[RENDER_LIMIT_MAX_UBO_SIZE]                = limits.maxUniformBufferBindingSize;
    r->wgpu.limits[RENDER_LIMIT_MAX_VERTEX_UNIFORM_BLOCKS]   =
    r->wgpu.limits[RENDER_LIMIT_MAX_FRAGMENT_UNIFORM_BLOCKS] =
    r->wgpu.limits[RENDER_LIMIT_MAX_UBO_BINDINGS]            = limits.maxUniformBuffersPerShaderStage;
    r->wgpu.limits[RENDER_LIMIT_MAX_GEOMETRY_UNIFORM_BLOCKS] = 0;
}

static inline wgpu_texture_format_t wgpu_swapchain_format(renderer_t *r)
{
    // HDR output uses RGBA16Float — the only HDR-capable swapchain format
    // mandated by core WebGPU. The compositor scans linear values out to the
    // display in extended sRGB, so the value range above 1.0 maps to nits
    // beyond SDR white. SDR path stays on BGRA8Unorm for cheap composition.
    return r->wgpu.hdr
        ? WGPUTextureFormat_RGBA16Float
        : WGPUTextureFormat_BGRA8Unorm;
}

static void wgpu_configure_surface(renderer_t *r)
{
    WGPUSurfaceConfiguration config = {
        .device         = r->wgpu.device,
        .format         = wgpu_swapchain_format(r),
        .usage          = WGPUTextureUsage_RenderAttachment,
        .width          = r->width,
        .height         = r->height,
        .presentMode    = WGPUPresentMode_Fifo,
        .alphaMode      = WGPUCompositeAlphaMode_Auto
    };

    // SurfaceColorManagement chains an Extended tone mapping mode so the
    // compositor doesn't clamp values >1.0 to SDR white. Only attached when
    // HDR is on; otherwise default sRGB / Standard tone mapping is fine.
    WGPUSurfaceColorManagement color_mgmt = WGPU_SURFACE_COLOR_MANAGEMENT_INIT;
    if (r->wgpu.hdr) {
        color_mgmt.colorSpace      = WGPUPredefinedColorSpace_DisplayP3;
        color_mgmt.toneMappingMode = WGPUToneMappingMode_Extended;
        config.nextInChain         = &color_mgmt.chain;
    }

    wgpuSurfaceConfigure(r->wgpu.surface, &config);
}

static WGPUEmscriptenSurfaceSourceCanvasHTMLSelector html_selector = {
    .selector   = literal_to_wgpu_stringview("#canvas"),
    .chain      = { .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector }
};

static cerr wgpu_create_surface(renderer_t *r)
{
    WGPUSurfaceDescriptor desc = WGPU_SURFACE_DESCRIPTOR_INIT;
    desc.nextInChain = &html_selector.chain;
    desc.label = literal_to_wgpu_stringview("main surface");
    r->wgpu.surface = wgpuInstanceCreateSurface(r->wgpu.instance, &desc);
#ifdef __EMSCRIPTEN__
    if (!r->width || !r->height) {
        emscripten_get_canvas_element_size("canvas", &r->width, &r->height);
        dbg("### surface: %p sizes: %ux%u\n", r->wgpu.surface, r->width, r->height);
    }
#endif /* __EMSCRIPTEN__ */

    wgpu_configure_surface(r);

    return CERR_OK;
}

static cerr wgpu_create_texture_view(renderer_t *r)
{
    wgpuSurfaceGetCurrentTexture(r->wgpu.surface, &r->wgpu.surface_texture);
    if (!r->wgpu.surface_texture.texture) return CERR_INITIALIZATION_FAILED_REASON(.fmt = "surface texture == NULL");

    if (r->wgpu.surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
        dbg("suboptimal surface texture: handle me\n");

    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.format = wgpu_swapchain_format(r);
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    view_desc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    view_desc.aspect = WGPUTextureAspect_All;

    r->wgpu.texture_view = wgpuTextureCreateView(r->wgpu.surface_texture.texture, &view_desc);
    return r->wgpu.texture_view ? CERR_OK : CERR_INVALID_OPERATION_REASON(.fmt = "failed to create surface texture view");
}

static cerr wgpu_create_render_pass(renderer_t *r)
{
    if (!r->wgpu.cmd_encoder)    return CERR_INVALID_OPERATION_REASON(.fmt = "no command encoder");

    vec4 clear_color = {};

    WGPURenderPassColorAttachment color_attachments = {};
    color_attachments.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_attachments.loadOp = WGPULoadOp_Clear;
    color_attachments.storeOp = WGPUStoreOp_Store;
    color_attachments.clearValue = (WGPUColor){ clear_color[0] * clear_color[3], clear_color[1] * clear_color[3], clear_color[2] * clear_color[3], clear_color[3] };
    color_attachments.view = r->wgpu.texture_view;

    WGPURenderPassDescriptor render_pass_desc = {};
    render_pass_desc.colorAttachmentCount = 1;
    render_pass_desc.colorAttachments = &color_attachments;
    render_pass_desc.depthStencilAttachment = nullptr;

    r->wgpu.pass_encoder = wgpuCommandEncoderBeginRenderPass(r->wgpu.cmd_encoder, &render_pass_desc);

    return r->wgpu.pass_encoder ? CERR_OK : CERR_INVALID_OPERATION_REASON(.fmt = "failed to kick off render pass");
}

static cerr wgpu_create_cmd_encoder(renderer_t *r)
{
    WGPUCommandEncoderDescriptor enc_desc = {};
    r->wgpu.cmd_encoder = wgpuDeviceCreateCommandEncoder(r->wgpu.device, &enc_desc);
    return r->wgpu.cmd_encoder ? CERR_OK : CERR_INITIALIZATION_FAILED_REASON(.fmt = "failed to create command encoder");
}

static void work_done_cb(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *data1, void *data2)
{
    if (status == WGPUQueueWorkDoneStatus_Success)  return;
    err("queue work done returned '%-*s'\n", (int)message.length, message.data);
}

static cerr wgpu_get_queue(renderer_t *r)
{
    r->wgpu.queue = wgpuDeviceGetQueue(r->wgpu.device);
    if (!r->wgpu.queue) return CERR_INITIALIZATION_FAILED_REASON(.fmt = "failed to get device queue\n");

    WGPUQueueWorkDoneCallbackInfo info = {
        .callback   = work_done_cb,
        .mode       = WGPUCallbackMode_AllowSpontaneous, // XXX: check this
    };
    wgpuQueueOnSubmittedWorkDone(r->wgpu.queue, info);

    return CERR_OK;
}

cerr _renderer_init(renderer_t *renderer, const renderer_init_options *opts)
{
    atomic_init(&renderer->wgpu.error, 0);
    list_init(&renderer->wgpu.dc_cache);
    list_init(&renderer->wgpu.ubos);

    CERR_RET_CERR(wgpu_request_adapter(renderer));
    CERR_RET_CERR(wgpu_request_device(renderer));

    wgpu_query_device(renderer);

    CERR_RET_CERR(wgpu_get_queue(renderer));
    CERR_RET_CERR(wgpu_create_surface(renderer));

    renderer->ops = &wgpu_renderer_ops;

    return CERR_OK;
}

void renderer_done(renderer_t *r) {
    wgpuSurfaceUnconfigure(r->wgpu.surface);
    wgpuSurfaceRelease(r->wgpu.surface);
    wgpuQueueRelease(r->wgpu.queue);
    wgpuDeviceRelease(r->wgpu.device);
    wgpuAdapterRelease(r->wgpu.adapter);
    wgpuInstanceRelease(r->wgpu.instance);
}

static void renderer_frame_advance(renderer_t *r)
{
    uniform_buffer_t *ubo;
    list_for_each_entry(ubo, &r->wgpu.ubos, entry) {
        void *prev_data = ubo->data;
        char *base = wgpu_ubo_shadow_base(ubo);

        ubo->item = 0;
        ubo->data = base;
        ubo->dirty = false;
        ubo->advance = false;

        if (prev_data != ubo->data)
            memcpy(ubo->data, prev_data, ubo->size);
    }
}

void renderer_frame_begin(renderer_t *r)
{
    renderer_frame_advance(r);

    /* Create the command encoder for the entire frame (FBO passes + swapchain pass) */
    CERR_RET(wgpu_create_cmd_encoder(r), err_cerr(__cerr, "create command encoder failed\n"));
}

void renderer_swapchain_begin(renderer_t *r)
{
    CERR_RET(wgpu_create_texture_view(r), err_cerr(__cerr, "texture view from surface failed\n"));
    CERR_RET(wgpu_create_render_pass(r), err_cerr(__cerr, "create render pass failed\n"));
}

void renderer_swapchain_end(renderer_t *r)
{
    if (!r->wgpu.pass_encoder)
        return;

    wgpuRenderPassEncoderEnd(r->wgpu.pass_encoder);
    wgpuRenderPassEncoderRelease(r->wgpu.pass_encoder);
    r->wgpu.pass_encoder = NULL;
}

void renderer_frame_end(renderer_t *r)
{
    if (!r->wgpu.cmd_encoder) {
        err("renderer_frame_end: no command encoder\n");
        return;
    }

    WGPUCommandBufferDescriptor desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    auto cmd_buffer = wgpuCommandEncoderFinish(r->wgpu.cmd_encoder, &desc);
    wgpuQueueSubmit(r->wgpu.queue, 1, &cmd_buffer);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(r->surface);
#endif /* !__EMSCRIPTEN__ */

    if (r->wgpu.texture_view)
        wgpuTextureViewRelease(r->wgpu.texture_view);
    if (r->wgpu.surface_texture.texture)
        wgpuTextureRelease(r->wgpu.surface_texture.texture);
    wgpuCommandEncoderRelease(r->wgpu.cmd_encoder);
    wgpuCommandBufferRelease(cmd_buffer);

    r->wgpu.cmd_encoder = NULL;
    r->wgpu.texture_view = NULL;
    r->wgpu.surface_texture.texture = NULL;
}

wgpu_device_t renderer_device(renderer_t *r)
{
    return r->wgpu.device;
}

wgpu_render_pass_encoder_t renderer_pass_encoder(renderer_t *r)
{
    return r->wgpu.pass_encoder;
}

wgpu_texture_format_t renderer_swapchain_format(renderer_t *r)
{
    return wgpu_swapchain_format(r);
}

unsigned int renderer_swapchain_format_gen(renderer_t *r)
{
    return r->wgpu.swapchain_format_gen;
}

void renderer_set_version(renderer_t *renderer, int major, int minor,
                          renderer_profile profile)
{
}

void renderer_viewport(renderer_t *r, int x, int y,
                       int width, int height)
{
    r->width = width;
    r->height = height;
    wgpu_configure_surface(r);
}

void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight)
{
    if (px)
        *px = 0;
    if (py)
        *py = 0;
    if (pwidth)
        *pwidth  = r->width;
    if (pheight)
        *pheight = r->height;
}

void renderer_hdr_enable(renderer_t *r, bool enable)
{
    bool want = r->wgpu.edr_supported && enable;
    if (want == r->wgpu.hdr) return;

    r->wgpu.hdr = want;

    // Bump generation so any consumer of the swapchain format (notably ImGui's
    // WGPU backend, whose pipeline bakes the render-target format at init time)
    // can notice the change and reinitialize.
    r->wgpu.swapchain_format_gen++;
    wgpu_configure_surface(r);
}

#ifndef CONFIG_FINAL
#include "ui-debug.h"

// XXX: same in render-gl.c; move to render-common.c
// XXX^1: split max texture size: WebGPU has a different one for 1D, 2D and 3D
static const char *limit_names[RENDER_LIMIT_MAX] = {
    [RENDER_LIMIT_MAX_TEXTURE_SIZE]             = "max texture size",
    [RENDER_LIMIT_MAX_TEXTURE_UNITS]            = "max texture units",
    [RENDER_LIMIT_MAX_TEXTURE_ARRAY_LAYERS]     = "max texture array layers",
    [RENDER_LIMIT_MAX_COLOR_ATTACHMENTS]        = "max color attachments",
    [RENDER_LIMIT_MAX_COLOR_TEXTURE_SAMPLES]    = "max color texture samples",
    [RENDER_LIMIT_MAX_DEPTH_TEXTURE_SAMPLES]    = "max depth texture samples",
    [RENDER_LIMIT_MAX_SAMPLES]                  = "max samples",
    [RENDER_LIMIT_MAX_DRAW_BUFFERS]             = "max draw buffers",
    [RENDER_LIMIT_MAX_ANISOTROPY]               = "max anisotrophy",
    [RENDER_LIMIT_MAX_UBO_SIZE]                 = "max UBO size",
    [RENDER_LIMIT_MAX_UBO_BINDINGS]             = "max UBO bindings",
    [RENDER_LIMIT_MAX_VERTEX_UNIFORM_BLOCKS]    = "max vertex uniform blocks",
    [RENDER_LIMIT_MAX_GEOMETRY_UNIFORM_BLOCKS]  = "max geometry uniform blocks",
    [RENDER_LIMIT_MAX_FRAGMENT_UNIFORM_BLOCKS]  = "max fragment uniform blocks",
};

void renderer_debug(renderer_t *r)
{
    debug_module *dbgm = ui_igBegin(DEBUG_RENDERER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        // XXX: likewise, make this common
        ui_igTableHeader("renderer", (const char *[]){ "limit", "value"}, 2);
        for (size_t i = 0; i < array_size(limit_names); i++)
                ui_igTableRow(limit_names[i], "%d", r->wgpu.limits[i]);
        igEndTable();
    }

    ui_igEnd(DEBUG_RENDERER);
}
#endif /* CONFIG_FINAL */

void renderer_cull_face(renderer_t *r, cull_face cull)
{
}

void renderer_blend(renderer_t *r, bool enable, blend sfactor,
                    blend dfactor)
{
    r->blend = enable;
}

void renderer_wireframe(renderer_t *r, bool enable)
{
}

static WGPUIndexFormat wgpu_index_format(data_type type)
{
    switch (type) {
        case DT_USHORT: return WGPUIndexFormat_Uint16;
        case DT_UINT:   return WGPUIndexFormat_Uint32;
        default:        break;
    }

    clap_unreachable();
    return WGPUIndexFormat_Uint16;
}

cerr renderer_draw(renderer_t *r, draw_type draw_type,
                   unsigned int nr_faces, data_type idx_type,
                   unsigned int nr_instances)
{
    CERR_RET_CERR(wgpu_previous_errors(r));

    if (!r->wgpu.pass_encoder || !r->wgpu.dc || !r->wgpu.va || !r->wgpu.va->index)
        return CERR_INVALID_OPERATION_REASON(
            .fmt    = "pass encoder(%p)/draw_control(%p)/index buffer(%p) not bound",
            .arg0   = r->wgpu.pass_encoder,
            .arg1   = r->wgpu.dc,
            .arg2   = r->wgpu.va ? r->wgpu.va->index : nullptr
        );

    buffer_t *index = r->wgpu.va->index;
    if (!buffer_loaded(index))
        return CERR_INVALID_OPERATION_REASON(.fmt = "index buffer not loaded");

    WGPURenderPassEncoder enc = r->wgpu.pass_encoder;
    WGPURenderPipeline pipeline = r->wgpu.dc->pipeline[r->blend ? 1 : 0];

    // Re-set pipeline in case blend state changed since shader_use()
    wgpuRenderPassEncoderSetPipeline(enc, pipeline);

    // Build bind group from currently bound resources, filtered by the shader's
    // binding mask so we only include entries the pipeline layout expects
    uint64_t mask = r->wgpu.dc->shader->binding_mask;
    WGPUBindGroupEntry entries[BINDING_TEXTURE_MAX * 2 + BINDING_UBO_MAX];
    unsigned int n = 0;

    for (unsigned int i = 0; i < BINDING_TEXTURE_MAX; i++) {
        texture_t *tex = r->wgpu.bound_textures[i];
        if (!tex || !texture_loaded(tex))
            continue;

        unsigned int image_binding = i * 2;
        if (!(mask & (1ull << image_binding)))
            continue;

        entries[n++] = (WGPUBindGroupEntry) {
            .binding        = image_binding,
            .textureView    = tex->view,
        };
        entries[n++] = (WGPUBindGroupEntry) {
            .binding        = image_binding + 1,
            .sampler        = tex->sampler,
        };
    }

    for (unsigned int i = 0; i < BINDING_UBO_MAX; i++) {
        uniform_buffer_t *ubo = r->wgpu.bound_ubos[i];
        if (!ubo || !ubo->buf)
            continue;

        unsigned int binding = WGPU_BINDING_UBO_BASE + i;
        if (!(mask & (1ull << binding)))
            continue;

        entries[n++] = (WGPUBindGroupEntry){
            .binding    = binding,
            .buffer     = ubo->buf,
            .offset     = wgpu_ubo_offset(ubo),
            .size       = ubo->slot_size,
        };
    }

    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    if (!layout)
        return CERR_INVALID_OPERATION_REASON(.fmt = "wgpuRenderPipelineGetBindGroupLayout() failed");

    WGPUBindGroupDescriptor bg_desc = {
        .label      = to_wgpu_stringview(r->wgpu.dc->shader->name),
        .layout     = layout,
        .entryCount = n,
        .entries    = entries,
    };
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(r->wgpu.device, &bg_desc);

    wgpuBindGroupLayoutRelease(layout);

    if (!bg)
        return CERR_INVALID_OPERATION_REASON(.fmt = "wgpuDeviceCreateBindGroup() failed");

    wgpuRenderPassEncoderSetBindGroup(enc, 0, bg, 0, NULL);

    // Set vertex buffer (tracked from buffer_bind at loc 0)
    buffer_t *vbuf = r->wgpu.va->vbuf;
    if (!vbuf || !buffer_loaded(vbuf)) {
        wgpuBindGroupRelease(bg);
        return CERR_INVALID_OPERATION_REASON(
            .fmt    = "vertex buffer %s",
            .arg0   = !vbuf ? "not bound" : "not loaded"
        );
    }

    wgpuRenderPassEncoderSetVertexBuffer(enc, 0, vbuf->wgpu.buf, 0, vbuf->wgpu.size);

    /* Set index buffer */
    size_t idx_count = index->wgpu.size / data_comp_size(idx_type);
    wgpuRenderPassEncoderSetIndexBuffer(enc, index->wgpu.buf,
                                         wgpu_index_format(idx_type), 0, index->wgpu.size);

    wgpuRenderPassEncoderDrawIndexed(enc, idx_count, max(nr_instances, 1u), 0, 0, 0);

    wgpuBindGroupRelease(bg);

    return CERR_OK;
}
