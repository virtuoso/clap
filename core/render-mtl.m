// SPDX-License-Identifier: Apache-2.0

#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

/****************************************************************************
 * Data sizes, component counts and strides
 ****************************************************************************/

static const MTLVertexFormat mtl_comp_type[] = {
    [DT_NONE]   = MTLVertexFormatInvalid,
    [DT_BYTE]   = MTLVertexFormatUChar,
    [DT_SHORT]  = MTLVertexFormatShort,
    [DT_USHORT] = MTLVertexFormatUShort,
    [DT_INT]    = MTLVertexFormatInt,
    [DT_UINT]   = MTLVertexFormatUInt,
    [DT_FLOAT]  = MTLVertexFormatFloat,
    [DT_IVEC2]  = MTLVertexFormatInt,
    [DT_IVEC3]  = MTLVertexFormatInt,
    [DT_IVEC4]  = MTLVertexFormatInt,
    [DT_UVEC2]  = MTLVertexFormatUInt,
    [DT_UVEC3]  = MTLVertexFormatUInt,
    [DT_UVEC4]  = MTLVertexFormatUInt,
    [DT_VEC2]   = MTLVertexFormatFloat,
    [DT_VEC3]   = MTLVertexFormatFloat,
    [DT_VEC4]   = MTLVertexFormatFloat,
    [DT_MAT2]   = MTLVertexFormatFloat,
    [DT_MAT3]   = MTLVertexFormatFloat,
    [DT_MAT4]   = MTLVertexFormatFloat,
};

static const MTLVertexFormat mtl_data_type[] = {
    [DT_NONE]   = MTLVertexFormatInvalid,
    [DT_BYTE]   = MTLVertexFormatUChar,
    [DT_SHORT]  = MTLVertexFormatShort,
    [DT_USHORT] = MTLVertexFormatUShort,
    [DT_INT]    = MTLVertexFormatInt,
    [DT_UINT]   = MTLVertexFormatUInt,
    [DT_FLOAT]  = MTLVertexFormatFloat,
    [DT_IVEC2]  = MTLVertexFormatInt2,
    [DT_IVEC3]  = MTLVertexFormatInt3,
    [DT_IVEC4]  = MTLVertexFormatInt4,
    [DT_UVEC2]  = MTLVertexFormatUInt2,
    [DT_UVEC3]  = MTLVertexFormatUInt3,
    [DT_UVEC4]  = MTLVertexFormatUInt4,
    [DT_VEC2]   = MTLVertexFormatFloat2,
    [DT_VEC3]   = MTLVertexFormatFloat3,
    [DT_VEC4]   = MTLVertexFormatFloat4,
    [DT_MAT2]   = MTLVertexFormatFloat2,
    [DT_MAT3]   = MTLVertexFormatFloat3,
    [DT_MAT4]   = MTLVertexFormatFloat4,
};

/****************************************************************************
 * Pixel formats
 ****************************************************************************/

static MTLPixelFormat mtl_texture_format(texture_format format)
{
    switch (format) {
        case TEX_FMT_R32F:      return MTLPixelFormatR32Float;
        case TEX_FMT_R16F:      return MTLPixelFormatR16Float;
        case TEX_FMT_R8:        return MTLPixelFormatR8Unorm;

        case TEX_FMT_RG32F:     return MTLPixelFormatRG32Float;
        case TEX_FMT_RG16F:     return MTLPixelFormatRG16Float;
        case TEX_FMT_RG8:       return MTLPixelFormatRG8Unorm;

        case TEX_FMT_RGB32F:    return MTLPixelFormatRGBA32Float; // no RGB format in Metal
        case TEX_FMT_RGB16F:    return MTLPixelFormatRGBA16Float;
        case TEX_FMT_RGB8:      return MTLPixelFormatRGBA8Unorm;
        case TEX_FMT_RGB8_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;

        case TEX_FMT_RGBA32F:   return MTLPixelFormatRGBA32Float;
        case TEX_FMT_RGBA16F:   return MTLPixelFormatRGBA16Float;
        case TEX_FMT_RGBA8:     return MTLPixelFormatRGBA8Unorm;
        case TEX_FMT_RGBA8_SRGB:return MTLPixelFormatRGBA8Unorm_sRGB;

        case TEX_FMT_BGRA10XR:  return MTLPixelFormatBGRA10_XR;
        case TEX_FMT_BGR10A2:   return MTLPixelFormatBGR10A2Unorm;
        case TEX_FMT_BGRA8:     return MTLPixelFormatBGRA8Unorm;

        case TEX_FMT_DEPTH16F:  return MTLPixelFormatDepth16Unorm;
        case TEX_FMT_DEPTH24F:  return MTLPixelFormatDepth24Unorm_Stencil8; // Closest option
        case TEX_FMT_DEPTH32F:  return MTLPixelFormatDepth32Float;

        case TEX_FMT_R32UI:     return MTLPixelFormatR32Uint;
        case TEX_FMT_RG32UI:    return MTLPixelFormatRG32Uint;
        case TEX_FMT_RGBA32UI:  return MTLPixelFormatRGBA32Uint;

        default:                break;
    }

    clap_unreachable();
    return MTLPixelFormatInvalid;
}

static texture_format mtl_texture_format_from_pixel_format(MTLPixelFormat pixel_format)
{
    switch (pixel_format) {
        case MTLPixelFormatR32Float:                return TEX_FMT_R32F;
        case MTLPixelFormatR16Float:                return TEX_FMT_R16F;
        case MTLPixelFormatR8Unorm:                 return TEX_FMT_R8;

        case MTLPixelFormatRG32Float:               return TEX_FMT_RG32F;
        case MTLPixelFormatRG16Float:               return TEX_FMT_RG16F;
        case MTLPixelFormatRG8Unorm:                return TEX_FMT_RG8;

        case MTLPixelFormatRGBA32Float:             return TEX_FMT_RGBA32F;
        case MTLPixelFormatRGBA16Float:             return TEX_FMT_RGBA16F;
        case MTLPixelFormatRGBA8Unorm:              return TEX_FMT_RGBA8;

        case MTLPixelFormatRGBA8Unorm_sRGB:         return TEX_FMT_RGBA8_SRGB;

        case MTLPixelFormatDepth16Unorm:            return TEX_FMT_DEPTH16F;
        case MTLPixelFormatDepth24Unorm_Stencil8:   return TEX_FMT_DEPTH24F;
        case MTLPixelFormatDepth32Float:            return TEX_FMT_DEPTH32F;

        case MTLPixelFormatBGRA8Unorm:              return TEX_FMT_BGRA8;
        case MTLPixelFormatBGR10A2Unorm:            return TEX_FMT_BGR10A2;
        case MTLPixelFormatBGRA10_XR:               return TEX_FMT_BGRA10XR;

        case MTLPixelFormatR32Uint:                 return TEX_FMT_R32UI;
        case MTLPixelFormatRG32Uint:                return TEX_FMT_RG32UI;
        case MTLPixelFormatRGBA32Uint:              return TEX_FMT_RGBA32UI;

        default:                                    break;
    }

    clap_unreachable();
    return TEX_FMT_MAX;
}

/****************************************************************************
 * Buffer
 ****************************************************************************/

static inline mtl_render_command_encoder_t cmd_encoder(renderer_t *r)
{
    auto fbo = r->fbo ? : r->screen_fbo;
    return fbo ? fbo->cmd_encoder : NULL;//r->cmd_encoder;
}

static mtl_buffer_t mtl_buffer_new(renderer_t *r, void *data, size_t size, bool shared, bool wc)
{
    if (!data && !shared)   return NULL;

    MTLResourceOptions options = shared ? MTLResourceStorageModeShared : MTLResourceStorageModePrivate;
    if (wc) options |= MTLResourceCPUCacheModeWriteCombined;

    if (data)
        return [r->device newBufferWithBytes:data length:(NSUInteger)size options:options];

    return [r->device newBufferWithLength:(NSUInteger)size options:options];
}

DEFINE_CLEANUP(buffer_t, if (*p) buffer_deinit(*p));

static cerr buffer_make(struct ref *ref, void *_opts)
{
    rc_init_opts(buffer) *opts = _opts;

    if (!opts->renderer)
        return CERR_INVALID_ARGUMENTS;

    if (opts->type == BUF_ELEMENT_ARRAY && !opts->renderer->va)
        return CERR_INVALID_OPERATION;

    LOCAL_SET(buffer_t, buf) = container_of(ref, struct buffer, ref);

    buf->renderer = opts->renderer;
    buf->size = opts->size;
    buf->off = opts->off;
    buf->stride = opts->stride;

    if (!opts->main) {
        buf->buf = mtl_buffer_new(buf->renderer, opts->data, opts->size, true, false);
        if (!buf->buf)  return CERR_NOMEM;

        if (opts->type == BUF_ELEMENT_ARRAY) {
            buf->renderer->va->index = buf;
            buf->loc = -1;
        } else {
            buf->loc = opts->loc;
        }
    }

    buf->loaded = true;

#ifndef CONFIG_FINAL
    memcpy(&buf->opts, opts, sizeof(buf->opts));
#endif /* CONFIG_FINAL */
    buf = NULL;

    return CERR_OK;
}

/* XXX: common */
static void buffer_drop(struct ref *ref)
{
    buffer_t *buf = container_of(ref, struct buffer, ref);
    buffer_deinit(buf);
}
DEFINE_REFCLASS2(buffer);
DECLARE_REFCLASS(buffer);

cerr _buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    return ref_embed_opts(buffer, buf, opts);
}

/* XXX: common */
bool buffer_loaded(buffer_t *buf)
{
    return buf->loaded;
}

void buffer_deinit(buffer_t *buf)
{
    if (!buf->loaded)
        return;

    if (buf->buf != nil)    [buf->buf release];

    buf->loaded = false;
}

void buffer_bind(buffer_t *buf, int loc)
{
    if (!buf->loaded)
        return;

    if (loc < 0) {
        if (buf->renderer->va)
            buf->renderer->va->index = buf;
        return;
    }

    if (!buf->buf)
        return;

    [cmd_encoder(buf->renderer) setVertexBuffer:buf->buf offset:0 atIndex:0];
}

void buffer_unbind(buffer_t *buf, int loc)
{
    if (!buf->loaded)
        return;

    if (loc < 0) {
        if (buf->renderer->va)
            buf->renderer->va->index = NULL;
        return;
    }
}

#ifndef CONFIG_FINAL
cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...)
{
    va_list ap;

    mem_free(buf->name);
    va_start(ap, fmt);
    cres(int) res = mem_vasprintf(&buf->name, fmt, ap);
    va_end(ap);

    [buf->buf setLabel:[NSString stringWithUTF8String:buf->name]];

    return res;
}
#endif /* CONFIG_FINAL */

/****************************************************************************
 * Vertex Array Object
 ****************************************************************************/

static cerr vertex_array_make(struct ref *ref, void *_opts)
{
    rc_init_opts(vertex_array) *opts = _opts;
    if (!opts->renderer)    return CERR_INVALID_ARGUMENTS;

    vertex_array_t *va = container_of(ref, vertex_array_t, ref);
    va->renderer = opts->renderer;
    vertex_array_bind(va);

    return CERR_OK;
}

static void vertex_array_drop(struct ref *ref)
{
    vertex_array_t *va = container_of(ref, vertex_array_t, ref);
    vertex_array_done(va);
}

DEFINE_REFCLASS2(vertex_array);
DECLARE_REFCLASS(vertex_array);

cerr vertex_array_init(vertex_array_t *va, renderer_t *r)
{
    return ref_embed(vertex_array, va, .renderer = r);
}

void vertex_array_done(vertex_array_t *va)
{
}

void vertex_array_bind(vertex_array_t *va)
{
    va->renderer->va = va;
}

void vertex_array_unbind(vertex_array_t *va)
{
    va->renderer->va = NULL;
}

/****************************************************************************
 * Draw control
 ****************************************************************************/

static size_t dc_hash(fbo_t *fbo, shader_t *shader)
{
    return fbo->id ^ (shader->id << 4);
}

cres_ret(mtl_render_pipeline_state_t);

void mtl_reflection_print(mtl_render_pipeline_reflection_t reflection)
{
    for (MTLArgument *arg in reflection.vertexArguments) {
        dbg("#   found vert arg: %s\n", [[arg.name localizedLowercaseString] UTF8String]);

        if (arg.bufferDataType == MTLDataTypeStruct) {
            for (MTLStructMember *uniform in arg.bufferStructType.members)
                dbg("##  uniform: %s type:%lu, location: %lu\n",
                    [[uniform.name localizedLowercaseString] UTF8String],
                    (unsigned long)uniform.dataType,
                    (unsigned long)uniform.offset
                );
        }
    }
    for (MTLArgument *arg in reflection.fragmentBindings) {
        dbg("#   found frag binding: %s @%lu\n", [[arg.name localizedLowercaseString] UTF8String], arg.index);

        // if (arg.bufferDataType == MTLDataTypeSampler) {
        //     for (MTLStructMember *sampler in arg.textureType)
        //         dbg("##  sampler: %s type:%lu, location: %lu\n",
        //             [[uniform.name localizedLowercaseString] UTF8String],
        //             (unsigned long)sampler.dataType,
        //             (unsigned long)sampler.offset
        //         );
        // }
    }
}

static cres(mtl_render_pipeline_state_t)
mtl_pipeline_new(renderer_t *r, shader_t *shader, texture_format *colors, texture_format depth,
                 size_t nr_colors, bool use_depth, bool use_blending, mtl_render_pipeline_reflection_t *reflection)
{
    if (!r || !shader)  return cres_error(mtl_render_pipeline_state_t, CERR_INVALID_ARGUMENTS);

    auto desc = [[MTLRenderPipelineDescriptor alloc] init];
    // desc.alphaToOneEnabled = YES;//use_blending;
    desc.rasterizationEnabled = YES;
    desc.rasterSampleCount = 1;
    desc.depthAttachmentPixelFormat = use_depth ? mtl_texture_format(depth) : MTLPixelFormatInvalid;

    for (size_t i = 0; i < nr_colors; i++) {
        desc.colorAttachments[i].pixelFormat     = mtl_texture_format(colors[i]);
        desc.colorAttachments[i].writeMask       = MTLColorWriteMaskAll;
        desc.colorAttachments[i].blendingEnabled = use_blending;
        if (use_blending) {
            desc.colorAttachments[i].alphaBlendOperation    = MTLBlendOperationAdd;//MTLBlendOperationAdd;
            desc.colorAttachments[i].rgbBlendOperation      = MTLBlendOperationAdd;//MTLBlendOperationMax;
            // desc.colorAttachments[i].sourceAlphaBlendFactor = MTLBlendFactorOne;
            // desc.colorAttachments[i].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            // desc.colorAttachments[i].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            // desc.colorAttachments[i].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            desc.colorAttachments[i].sourceAlphaBlendFactor      = MTLBlendFactorOne;
            desc.colorAttachments[i].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
            desc.colorAttachments[i].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            desc.colorAttachments[i].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        }
    }

    desc.vertexDescriptor = shader->vdesc;
    desc.vertexFunction = shader->vert;
    desc.fragmentFunction = shader->frag;
#ifndef CONFIG_FINAL
    desc.label = [NSString stringWithFormat:@"pls:%s/%s", shader->name, r->fbo ? r->fbo->name : "<invalid>"];
#endif /* !CONFIG_FINAL */

    NSError *err;
    MTLPipelineOption option = reflection ? MTLPipelineOptionBufferTypeInfo | MTLPipelineOptionArgumentInfo : 0;

    auto pipeline = [r->device
        newRenderPipelineStateWithDescriptor:desc
        options:option
        reflection:reflection
        error:&err
    ];

    [desc release];

    if (err) {
        err("pipeline error: %s\n", [[err localizedDescription] UTF8String]);
        return cres_error(mtl_render_pipeline_state_t, CERR_BUFFER_INCOMPLETE);
    }

    return cres_val(mtl_render_pipeline_state_t, pipeline);
}

static MTLCompareFunction mtl_compare_function(depth_func func)
{
    switch (func) {
        case DEPTH_FN_NEVER:            return MTLCompareFunctionNever;
        case DEPTH_FN_LESS:             return MTLCompareFunctionLess;
        case DEPTH_FN_EQUAL:            return MTLCompareFunctionEqual;
        case DEPTH_FN_LESS_OR_EQUAL:    return MTLCompareFunctionLessEqual;
        case DEPTH_FN_GREATER:          return MTLCompareFunctionGreater;
        case DEPTH_FN_NOT_EQUAL:        return MTLCompareFunctionNotEqual;
        case DEPTH_FN_GREATER_OR_EQUAL: return MTLCompareFunctionGreaterEqual;
        case DEPTH_FN_ALWAYS:           return MTLCompareFunctionAlways;
    }
}

cres_ret(mtl_depth_stencil_state_t);

static cres(mtl_depth_stencil_state_t)
mtl_depth_stencil_new(renderer_t *r)
{
    auto desc = [[MTLDepthStencilDescriptor alloc] init];
    if (!desc)  return cres_error(mtl_depth_stencil_state_t, CERR_NOMEM);

    desc.depthCompareFunction = mtl_compare_function(r->depth_func);
    desc.depthWriteEnabled = YES;

    auto ds = [r->device newDepthStencilStateWithDescriptor:desc];
    [desc release];

    if (!ds)    return cres_error(mtl_depth_stencil_state_t, CERR_NOMEM);

    return cres_val(mtl_depth_stencil_state_t, ds);
}

static cerr draw_control_make(struct ref *ref, void *_opts)
{
    rc_init_opts(draw_control) *opts = _opts;
    if (!opts->renderer || !opts->shader)   return CERR_INVALID_ARGUMENTS;

    draw_control_t *dc = container_of(ref, draw_control_t, ref);
    dc->renderer = opts->renderer;

    for (size_t pl = 0; pl < array_size(dc->pipeline); pl++) {
        // mtl_render_pipeline_reflection_t reflection;
        auto fbo = opts->fbo;
        if (fbo) {
            texture_format color_formats[FBO_COLOR_ATTACHMENTS_MAX], depth_format = fbo->depth_format;
            fbo_attachment fa;
            size_t i = 0;

            fa_for_each(fa, fbo->layout, texture) {
                color_formats[i] = fbo->color_format[fa_nr_color_texture(fa) - 1];
                i++;
            }

            bool use_depth = fbo->layout.depth_texture;
            dc->pipeline[pl] = CRES_RET_CERR(
                mtl_pipeline_new(
                    dc->renderer, opts->shader, color_formats, fbo->depth_format, i, use_depth, !!pl, NULL//&reflection
                )
            );

            if (use_depth)  dc->depth_stencil = CRES_RET_CERR(mtl_depth_stencil_new(dc->renderer));
        } else {
            texture_format color_format = mtl_texture_format_from_pixel_format(dc->renderer->layer.pixelFormat);
            dc->pipeline[pl] = CRES_RET_CERR(
                mtl_pipeline_new(
                    dc->renderer, opts->shader, &color_format, TEX_FMT_DEPTH32F, 1, false, !!pl, NULL//&reflection
                )
            );
        }
    }

    list_append(&opts->shader->dc_list, &dc->shader_entry);
    if (opts->fbo) {
        auto idx = dc_hash(opts->fbo, opts->shader);
        list_append(&dc->renderer->dc_hash[idx], &dc->hash_entry);
    }

    dc->shader  = opts->shader;
    dc->fbo     = opts->fbo;

    // if (!opts->shader->reflection) {
    //     opts->shader->reflection = reflection;
    //     mtl_reflection_print(reflection);
    // }

    return CERR_OK;
}

static void draw_control_drop(struct ref *ref)
{
    draw_control_t *dc = container_of(ref, draw_control_t, ref);
    draw_control_done(dc);
}

DEFINE_REFCLASS2(draw_control);
DECLARE_REFCLASS(draw_control);

cerr _draw_control_init(draw_control_t *dc, const draw_control_init_options *opts)
{
    return ref_embed_opts(draw_control, dc, opts);
}

void draw_control_done(draw_control_t *dc)
{
    [dc->pipeline[0] release];
    [dc->pipeline[1] release];
    if (dc->depth_stencil)  [dc->depth_stencil release];

    list_del(&dc->shader_entry);
    list_del(&dc->hash_entry);
}

void draw_control_bind(draw_control_t *dc)
{
    auto r = dc->renderer;
    r->dc = dc;

    if (dc->depth_stencil)  [cmd_encoder(r) setDepthStencilState:dc->depth_stencil];
    [cmd_encoder(r) setRenderPipelineState:dc->pipeline[r->blend ? 1 : 0]];
}

void draw_control_unbind(draw_control_t *dc)
{
    auto r = dc->renderer;
    r->dc = NULL;
    for (size_t i = 0; i < SLOTS_MAX; i++)
        r->vbuffer_cache[i] = r->fbuffer_cache[i] = NULL;
}

static draw_control_t *dc_hash_find_get(renderer_t *r, fbo_t *fbo, shader_t *shader)
{
    auto bucket = &r->dc_hash[dc_hash(fbo, shader)];
    draw_control_t *dc;
    list_for_each_entry(dc, bucket, hash_entry)
        if (dc->fbo == fbo && dc->shader == shader)  return dc;

    dc = CRES_RET(
        ref_new_checked(draw_control, .renderer = r, .fbo = fbo, .shader = shader),
        {
            err("couldn't create draw control for fbo%d + shader%d\n", fbo->id, shader->id);
            return NULL;
        }
    );

    return dc;
}

static void dc_hash_fbo_drop(renderer_t *r, fbo_t *fbo)
{
    for (size_t i = 0; i < array_size(r->dc_hash); i++) {
        auto bucket = &r->dc_hash[i];

        draw_control_t *dc, *iter;
        list_for_each_entry_iter(dc, iter, bucket, hash_entry)
            if (dc->fbo == fbo) ref_put_last(dc);
    }
}

/****************************************************************************
 * Texture
 ****************************************************************************/

static bool texture_format_needs_alpha(texture_format format)
{
    switch (format) {
        case TEX_FMT_RGB32F:
        case TEX_FMT_RGB16F:
        case TEX_FMT_RGB8:
        case TEX_FMT_RGB8_SRGB: return true;
        default:                break;
    }

    return false;
}

static MTLTextureType mtl_texture_type(texture_type type)
{
    switch (type) {
        case TEX_2D:            return MTLTextureType2D;
        case TEX_2D_ARRAY:      return MTLTextureType2DArray;
        case TEX_3D:            return MTLTextureType3D;

        default:                break;
    }

    clap_unreachable();
    return MTLTextureType2D;
}

static MTLSamplerMinMagFilter mtl_filter(texture_filter filter)
{
    switch (filter) {
        case TEX_FLT_NEAREST:   return MTLSamplerMinMagFilterNearest;
        case TEX_FLT_LINEAR:    return MTLSamplerMinMagFilterLinear;

        default:                break;
    }

    clap_unreachable();
    return MTLSamplerMinMagFilterLinear;
}

static MTLSamplerAddressMode mtl_wrap_mode(texture_wrap wrap)
{
    switch (wrap) {
        case TEX_WRAP_REPEAT:           return MTLSamplerAddressModeRepeat;
        case TEX_WRAP_MIRRORED_REPEAT:  return MTLSamplerAddressModeMirrorRepeat;
        case TEX_CLAMP_TO_EDGE:         return MTLSamplerAddressModeClampToEdge;
        case TEX_CLAMP_TO_BORDER:       return MTLSamplerAddressModeClampToBorderColor;

        default:                break;
    }

    clap_unreachable();
    return MTLSamplerAddressModeRepeat;
}

static cerr texture_make(struct ref *ref, void *_opts)
{
    struct texture *tex = container_of(ref, struct texture, ref);
    rc_init_opts(texture) *opts = _opts;

    tex->renderer       = opts->renderer;
    tex->type           = opts->type;
    tex->format         = opts->format;
    tex->layers         = opts->layers ? : 1;
    tex->target         = opts->target;
    tex->render_target  = opts->render_target;

    auto sdesc = [[MTLSamplerDescriptor alloc] init];
    sdesc.minFilter             = mtl_filter(opts->min_filter);
    sdesc.magFilter             = mtl_filter(opts->mag_filter);
    sdesc.mipFilter             = MTLSamplerMipFilterNotMipmapped;
    sdesc.borderColor           = MTLSamplerBorderColorOpaqueWhite; /* XXX */
    sdesc.sAddressMode          = mtl_wrap_mode(opts->wrap);
    sdesc.tAddressMode          = mtl_wrap_mode(opts->wrap);
    sdesc.rAddressMode          = mtl_wrap_mode(opts->wrap);
    sdesc.normalizedCoordinates = YES;
    sdesc.compareFunction       = MTLCompareFunctionAlways;

    tex->sampler = [tex->renderer->device newSamplerStateWithDescriptor:sdesc];

    [sdesc release];

    if (!tex->sampler)  return CERR_NOMEM;

    return CERR_OK;
}

/* XXX: common */
static void texture_drop(struct ref *ref)
{
    struct texture *tex = container_of(ref, struct texture, ref);
    texture_deinit(tex);
}
DEFINE_REFCLASS2(texture);
DECLARE_REFCLASS(texture);

bool texture_is_array(texture_t *tex)
{
    return tex->type == TEX_2D_ARRAY;
}

bool texture_is_multisampled(texture_t *tex)
{
    return tex->multisampled;
}

cerr _texture_init(texture_t *tex, const texture_init_options *opts)
{
    return ref_embed_opts(texture, tex, opts);
}

#ifndef CONFIG_FINAL
cres(int) texture_set_name(texture_t *tex, const char *fmt, ...)
{
    va_list ap;

    mem_free(tex->name);
    va_start(ap, fmt);
    cres(int) res = mem_vasprintf(&tex->name, fmt, ap);
    va_end(ap);

    [tex->texture setLabel:[NSString stringWithUTF8String:tex->name]];

    return res;
}
#endif /* CONFIG_FINAL */

texture_t *texture_clone(texture_t *tex)
{
    texture_t *ret = mem_alloc(sizeof(*ret), .zero = 1);//ref_new(texture);
    if (!ret)   return NULL;

    memcpy(&ret->ref, &tex->ref, sizeof(struct ref));
    ref_init(&ret->ref);

    if (ret) {
        ret->name           = tex->name;
        ret->loaded         = tex->loaded;
        ret->renderer       = tex->renderer;
        ret->texture        = tex->texture;
        ret->sampler        = tex->sampler;
        ret->type           = tex->type;
        ret->format         = tex->format;
        ret->target         = tex->target;
        ret->width          = tex->width;
        ret->height         = tex->height;
        ret->layers         = tex->layers;
        ret->multisampled   = tex->multisampled;
        tex->loaded         = false;
        tex->texture        = nil;
        tex->sampler        = nil;
        tex->name           = NULL;
    }

    return ret;
}

void texture_deinit(texture_t *tex)
{
    if (tex->sampler && !tex->loaded)   err("unloaded texture has a sampler: '%s'\n", tex->name);
    if (tex->sampler)   [tex->sampler release];

    if (!tex->loaded)
        return;

    if (tex->texture)   [tex->texture release];
    tex->loaded = false;
}

cerr texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    mem_free(tex->name);

    if (!tex->loaded)
        return CERR_TEXTURE_NOT_LOADED;

    if (tex->width == width && tex->height == height)
        return CERR_OK;

    tex->width = width;
    tex->height = height;

    return CERR_OK;
}

static NSUInteger mtl_row_bytes(texture_format format, int width)
{
    switch (format) {
        case TEX_FMT_DEPTH32F:
        case TEX_FMT_DEPTH24F:
        case TEX_FMT_R32F:      return 4 * width;
        case TEX_FMT_DEPTH16F:
        case TEX_FMT_R16F:      return 2 * width;
        case TEX_FMT_R8:        return width;

        case TEX_FMT_RG32F:     return 8 * width;
        case TEX_FMT_RG16F:     return 4 * width;
        case TEX_FMT_RG8:       return 2 * width;

        case TEX_FMT_RGBA32F:
        case TEX_FMT_RGB32F:    return 16 * width;
        case TEX_FMT_RGBA16F:
        case TEX_FMT_RGB16F:    return 8 * width;
        case TEX_FMT_RGBA8_SRGB:
        case TEX_FMT_RGB8_SRGB:
        case TEX_FMT_RGBA8:
        case TEX_FMT_RGB8:      return 4 * width;

        case TEX_FMT_R32UI:     return 4 * width;
        case TEX_FMT_RG32UI:    return 8 * width;
        case TEX_FMT_RGBA32UI:  return 16 * width;

        default:                break;
    }

    clap_unreachable();

    return 0;
}

cerr_check texture_load(texture_t *tex, texture_format format,
                        unsigned int width, unsigned int height, void *buf)
{
    tex->width  = width;
    tex->height = height;
    tex->format = format;

    auto tdesc = [[MTLTextureDescriptor alloc] init];
    tdesc.textureType       = mtl_texture_type(tex->type);
    tdesc.pixelFormat       = mtl_texture_format(tex->format);
    tdesc.sampleCount       = tex->multisampled ? 4 : 1;
    tdesc.width             = width;
    tdesc.height            = height;
    tdesc.usage             = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    tdesc.storageMode       = tex->render_target ? MTLStorageModePrivate : MTLStorageModeShared;

    MTLRegion region;
    if (tex->type == TEX_2D_ARRAY) {
        region = MTLRegionMake2D(0, 0, width, height);
        tdesc.arrayLength   = tex->layers;
    } else if (tex->type == TEX_3D) {
        region = MTLRegionMake3D(0, 0, 0, width, height, tex->layers);
        tdesc.depth         = tex->layers;
    } else {
        region = MTLRegionMake2D(0, 0, width, height);
    }

    tex->texture = [tex->renderer->device newTextureWithDescriptor:tdesc];

    [tdesc release];

    if (!tex->texture)  return CERR_NOMEM;

    if (tex->name)  [tex->texture setLabel:[NSString stringWithUTF8String:tex->name]];

    if (buf) {
        auto bytes_per_row = mtl_row_bytes(format, width);
        auto bytes_per_layer = bytes_per_row * height;

        LOCAL_SET(uchar, dest_buf) = NULL;

        if (texture_format_needs_alpha(format)) {
            size_t comp_size = mtl_row_bytes(format, 1);

            /* only RGB8 to RGBA8 supported at the moment */
            if (comp_size != 4) return CERR_NOT_SUPPORTED;

            dest_buf = mem_alloc(comp_size, .nr = width * height * tex->layers);
            if (!dest_buf) {
                [tex->texture release];
                return CERR_NOMEM;
            }

            auto bytes_per_src_row = width * 3;
            auto bytes_per_src_layer = bytes_per_src_row * height;
            for (int layer = 0; layer < tex->layers; layer++)
                for (int row = 0; row < height; row++)
                    for (int col = 0; col < width; col++) {
                        uchar *rgb_src = buf + layer * bytes_per_src_layer + row * bytes_per_src_row + col * 3;
                        uchar *rgba_dest = dest_buf + layer * bytes_per_layer + row * bytes_per_row + col * 4;
                        rgba_dest[0] = rgb_src[0];
                        rgba_dest[1] = rgb_src[1];
                        rgba_dest[2] = rgb_src[2];
                        rgba_dest[3] = 255;
                    }
        }

        for (int layer = 0; layer < tex->layers; layer++)
            [tex->texture replaceRegion:region
                mipmapLevel:0
                slice:layer
                withBytes:dest_buf ? : buf
                bytesPerRow:bytes_per_row
                bytesPerImage:bytes_per_layer];
    }

    tex->loaded = true;

    return CERR_OK;
}

void texture_bind(texture_t *tex, unsigned int target)
{
    auto r = tex->renderer;

    if (r->sampler_cache[target] != tex->sampler) {
        [cmd_encoder(tex->renderer) setFragmentSamplerState:tex->sampler atIndex:target];
        r->sampler_cache[target] = tex->sampler;
    }

    if (r->texture_cache[target] != tex->texture) {
        [cmd_encoder(tex->renderer) setFragmentTexture:tex->texture atIndex:target];
        r->texture_cache[target] = tex->texture;
    }
}

void texture_unbind(texture_t *tex, unsigned int target)
{
}

/* XXX: common */
void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight)
{
    *pwidth = tex->width;
    *pheight = tex->height;
}

/* XXX: common */
void texture_done(struct texture *tex)
{
    if (!ref_is_static(&tex->ref))
        ref_put_last(tex);
}

texid_t texture_id(struct texture *tex)
{
    if (!tex)
        return 0;

    return (texid_t)tex->texture;
}

/* XXX: common */
bool texture_loaded(struct texture *tex)
{
    return tex->loaded;
}

/* XXX: common */
cerr_check texture_pixel_init(renderer_t *renderer, texture_t *tex, float color[4])
{
    cerr err = texture_init(tex, .renderer = renderer);
    if (IS_CERR(err))
        return err;

    return texture_load(tex, TEX_FMT_RGBA8, 1, 1, color);
}

/* XXX: common */
static texture_t _white_pixel;
static texture_t _black_pixel;
static texture_t _transparent_pixel;

/* XXX: common */
texture_t *white_pixel(void) { return &_white_pixel; }
texture_t *black_pixel(void) { return &_black_pixel; }
texture_t *transparent_pixel(void) { return &_transparent_pixel; }

/* XXX: common */
void textures_init(renderer_t *renderer)
{
    cerr werr, berr, terr;

    float white[] = { 1, 1, 1, 1 };
    werr = texture_pixel_init(renderer, &_white_pixel, white);
    float black[] = { 0, 0, 0, 1 };
    berr = texture_pixel_init(renderer, &_black_pixel, black);
    float transparent[] = { 0, 0, 0, 0 };
    terr = texture_pixel_init(renderer, &_transparent_pixel, transparent);

    err_on(IS_CERR(werr) || IS_CERR(berr) || IS_CERR(terr), "failed: %d/%d/%d\n",
           CERR_CODE(werr), CERR_CODE(berr), CERR_CODE(terr));
    texture_set_name(&_white_pixel, "white pixel");
    texture_set_name(&_black_pixel, "black pixel");
    texture_set_name(&_transparent_pixel, "transparent pixel");
}

/* XXX: common */
void textures_done(void)
{
    texture_done(&_white_pixel);
    texture_done(&_black_pixel);
    texture_done(&_transparent_pixel);
}

/****************************************************************************
 * Framebuffer
 ****************************************************************************/

texture_t *fbo_texture(fbo_t *fbo, fbo_attachment attachment)
{
    if (attachment.depth_texture)
        return &fbo->depth_tex;

    int idx = fbo_attachment_color(attachment);
    if (idx >= fa_nr_color_texture(fbo->layout))
        return NULL;

    return &fbo->color_tex[idx];
}

bool fbo_texture_supported(texture_format format)
{
    if (format >= TEX_FMT_MAX)
        return false;

    return true;
}

texture_format fbo_texture_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (attachment.depth_texture)
        return fbo->depth_format;

    int target = fbo_attachment_color(attachment);

    /*
     * boundary check may not be possible yet, because this will be called early
     * in the initialization path, fbo doesn't keep the size of this array, but
     * internally correct code shouldn't violate it; otherwise it's better to
     * crash than to be unwittingly stuck with the wrong texture format
     */
    if (fbo->color_format)
        return fbo->color_format[target];

    return TEX_FMT_DEFAULT;
}

bool fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment)
{
    int tidx = fbo_attachment_color(attachment);
    if (tidx >= 0 && tidx <= fbo_attachment_color(fbo->layout) &&
        texture_loaded(&fbo->color_tex[tidx]))
        return true;

    if (attachment.depth_texture && fbo->layout.depth_texture)
        return true;

    return false;
}

texture_format fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo_attachment_valid(fbo, attachment)) {
        err("invalid attachment '%s'\n", fbo_attachment_string(attachment));
        return TEX_FMT_MAX;
    }

    if (attachment.depth_texture && fbo->layout.depth_texture)
        return fbo->depth_format;

    return fbo->color_format[fbo_attachment_color(attachment)];
}

/* XXX: common */
int fbo_width(fbo_t *fbo)
{
    return fbo->width;
}

/* XXX: common */
int fbo_height(fbo_t *fbo)
{
    return fbo->height;
}

fbo_attachment_type fbo_get_attachment(fbo_t *fbo)
{
    if (fbo_attachment_color(fbo->layout))
        return FBO_ATTACHMENT_COLOR0;

    if (fbo->layout.depth_buffer)
        return FBO_ATTACHMENT_DEPTH;

    if (fbo->layout.stencil_buffer)
        return FBO_ATTACHMENT_STENCIL;

    clap_unreachable();

    return 0;
}

static void fbo_attachments_deinit(fbo_t *fbo)
{
    fa_for_each(fa, fbo->layout, texture)
        texture_deinit(&fbo->color_tex[fbo_attachment_color(fa)]);

    texture_deinit(&fbo->depth_tex);
}

static void fbo_drop(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    fbo_attachments_deinit(fbo);

    if (fbo->cmd_encoder)   [fbo->cmd_encoder release];
    if (fbo->desc)          [fbo->desc release];

    dc_hash_fbo_drop(fbo->renderer, fbo);
    bitmap_clear(&fbo->renderer->fbo_ids, fbo->id);
    mem_free(fbo->color_format);
}
DEFINE_REFCLASS(fbo);
DECLARE_REFCLASS(fbo);

static mtl_command_buffer_t mtl_cmd_buffer(renderer_t *r)
{
    if (r->cmd_buffer)  return r->cmd_buffer;

    /* create cmd_buffer if necessary */
    dispatch_semaphore_wait(r->sem, DISPATCH_TIME_FOREVER);
    atomic_fetch_add(&r->cmdbuf_count, 1);

    r->cmd_buffer = [r->cmd_queue commandBuffer];
    [r->cmd_buffer enqueue];
    [r->cmd_buffer setLabel:[NSString stringWithFormat:@"cmd_buffer:%d", r->frame_idx]];

    [r->cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        if (cb.error)
            err("MTL error (%s): %s\n", [[cb.label localizedLowercaseString] UTF8String], [[cb.error localizedDescription] UTF8String]);
        dispatch_semaphore_signal(r->sem);
        atomic_fetch_sub(&r->cmdbuf_count, 1);
    }];

    return r->cmd_buffer;
}

void fbo_prepare(fbo_t *fbo)
{
    auto r = fbo->renderer;

    for (size_t i = 0; i < SLOTS_MAX; i++)
        /*r->vbuffer_cache[i] = r->fbuffer_cache[i] = */r->texture_cache[i] = r->sampler_cache[i] = NULL;

    mtl_cmd_buffer(r);

    fbo->cmd_encoder = [r->cmd_buffer renderCommandEncoderWithDescriptor:fbo->desc];
    if (!fbo->cmd_encoder)  return;

    [fbo->cmd_encoder setLabel:[NSString stringWithUTF8String:fbo->name]];
    r->fbo = fbo;

    MTLViewport viewport = { 0.0, 0.0, (double)fbo->width, (double)fbo->height, 0.0, 1.0 };
    [fbo->cmd_encoder setViewport:viewport];
}

void fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
    auto r = fbo->renderer;

    if (r->fbo != fbo) {
        err("wrong fbo: %d (%p)\n", fbo->id, fbo);
        return;
    }

    for (size_t i = 0; i < SLOTS_MAX; i++)
        /*r->vbuffer_cache[i] = r->fbuffer_cache[i] = */r->texture_cache[i] = r->sampler_cache[i] = NULL;

    r->fbo = NULL;
    if (!fbo->cmd_encoder)    return;

    [fbo->cmd_encoder endEncoding];
    fbo->cmd_encoder = nil;
}

void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment)
{
}

bool fbo_is_multisampled(fbo_t *fbo)
{
    return fbo->nr_samples > 1;
}

static cerr fbo_depth_texture_init(fbo_t *fbo)
{
    float border[4] = {};

    CERR_RET_CERR(
        texture_init(
            &fbo->depth_tex,
            .renderer       = fbo->renderer,
            .type           = fbo->layers > 1 ? TEX_2D_ARRAY : TEX_2D,
            .layers         = fbo->layers,
            .format         = fbo->depth_format,
            .multisampled   = fbo_is_multisampled(fbo),
            .render_target  = true,
            .wrap           = TEX_CLAMP_TO_BORDER,
            .border         = border,
            .min_filter     = TEX_FLT_NEAREST,
            .mag_filter     = TEX_FLT_NEAREST
        )
    );

    texture_set_name(&fbo->depth_tex, "%s:depth", fbo->name);

    CERR_RET_CERR(texture_load(&fbo->depth_tex, fbo->depth_format, fbo->width, fbo->height, NULL));

    fbo->desc.depthAttachment.texture = fbo->depth_tex.texture;
    fbo->desc.depthAttachment.loadAction = MTLLoadActionClear;
    fbo->desc.depthAttachment.storeAction = MTLStoreActionStore;
    fbo->desc.depthAttachment.clearDepth = fbo->renderer->clear_depth;

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
                            .multisampled  = fbo_is_multisampled(fbo),
                            .render_target = true,
                            .wrap          = TEX_CLAMP_TO_EDGE,
                            .min_filter    = TEX_FLT_LINEAR,
                            .mag_filter    = TEX_FLT_LINEAR);
    if (IS_CERR(err))
        return err;

    texture_set_name(&fbo->color_tex[idx], "%s:rt%d", fbo->name, idx);

    return texture_load(&fbo->color_tex[idx], fbo_texture_format(fbo, attachment), fbo->width, fbo->height, NULL);
}

static cerr_check fbo_textures_init(fbo_t *fbo, fbo_attachment attachment)
{
    auto r = fbo->renderer;

    fa_for_each(fa, attachment, texture) {
        CERR_RET(fbo_texture_init(fbo, fa), return __cerr);

        auto i = fa_nr_color_texture(fa) - 1;
        fbo->desc.colorAttachments[i].texture = fbo->color_tex[i].texture;
        fbo->desc.colorAttachments[i].loadAction = fbo->load_clear ? MTLLoadActionClear : MTLLoadActionDontCare;
        fbo->desc.colorAttachments[i].storeAction = MTLStoreActionStore;
        fbo->desc.colorAttachments[i].clearColor = MTLClearColorMake(
            r->clear_color[0], r->clear_color[1], r->clear_color[2], r->clear_color[3]
        );
    }

    return CERR_OK;
}

cerr_check fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo)
        return CERR_INVALID_ARGUMENTS;

    fbo_attachments_deinit(fbo);

    auto orig_width = fbo->width;
    auto orig_height = fbo->height;

    fbo->width = width;
    fbo->height = height;

    if (fbo->layout.depth_texture)
        CERR_RET(fbo_depth_texture_init(fbo), goto fail);
    if (fbo->layout.color_textures)
        CERR_RET(fbo_textures_init(fbo, fbo->layout), goto fail_depth);

    fbo->width = width;
    fbo->height = height;

    return CERR_OK;

fail_depth:
    if (fbo->layout.depth_texture)
        texture_deinit(&fbo->depth_tex);

fail:
    fbo->width = orig_width;
    fbo->height = orig_height;

    /*
     * XXX: Same problem as GL: if the below fail, fbo is in a broken state,
     * which needs to be communicated upstairs properly.
     */
    if (fbo->layout.depth_texture)
        CERR_RET_CERR(fbo_depth_texture_init(fbo));
    if (fbo->layout.color_textures)
        CERR_RET_CERR(fbo_textures_init(fbo, fbo->layout));

    return CERR_NOMEM;
}

/* XXX: common */
DEFINE_CLEANUP(fbo_t, if (*p) ref_put(*p))

must_check cresp(fbo_t) _fbo_new(const fbo_init_options *opts)
{
    if (!opts->width || !opts->height)
        return cresp_error(fbo_t, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(fbo_t, fbo) = ref_new(fbo);
    if (!fbo)
        return cresp_error(fbo_t, CERR_NOMEM);

    fbo->renderer     = opts->renderer;
    fbo->width        = opts->width;
    fbo->height       = opts->height;
    fbo->layers       = opts->layers ? : 1;
    fbo->depth_format = opts->depth_format ? : TEX_FMT_DEPTH32F;
    fbo->nr_samples   = opts->nr_samples ? : 1;
    fbo->layout       = opts->layout;
    fbo->load_clear   = opts->load_clear;
    fbo->name         = opts->name ? : "<unset>";
    /* for compatibility */
    if (!fbo->layout.mask)
        fbo->layout.color_texture0 = 1;

    int nr_color_formats = fa_nr_color_buffer(fbo->layout) ? :
                           fa_nr_color_texture(fbo->layout);

    fbo->id = CRES_RET(bitmap_find_first_unset(&fbo->renderer->fbo_ids), return cresp_error_cerr(fbo_t, __resp));
    bitmap_set(&fbo->renderer->fbo_ids, fbo->id);
    size_t size = nr_color_formats * sizeof(texture_format);
    fbo->color_format = memdup(opts->color_format ? : (texture_format[]){ TEX_FMT_DEFAULT }, size);

    fbo->desc = [[MTLRenderPassDescriptor alloc] init];
    if (!fbo->desc) return cresp_error(fbo_t, CERR_NOMEM);

    fbo->desc.defaultRasterSampleCount = fbo->nr_samples;

    if (fbo->layout.depth_texture)
        CERR_RET_T(fbo_depth_texture_init(fbo), fbo_t);
    if (fbo->layout.color_textures)
        CERR_RET_T(fbo_textures_init(fbo, fbo->layout), fbo_t);

    return cresp_val(fbo_t, NOCU(fbo));
}

/* XXX: common */
void fbo_put(fbo_t *fbo)
{
    ref_put(fbo);
}

/* XXX: common */
void fbo_put_last(fbo_t *fbo)
{
    ref_put_last(fbo);
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
    bps->binding = -1;
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
    uniform_buffer_t *ubo = container_of(ref, uniform_buffer_t, ref);

    ubo->renderer   = opts->renderer;
    ubo->binding    = opts->binding;
    ubo->dirty      = false;
    ubo->name       = opts->name ? : "<unset>";
    list_append(&ubo->renderer->ubos, &ubo->entry);

    return CERR_OK;
}

static void uniform_buffer_drop(struct ref *ref)
{
    uniform_buffer_t *ubo = container_of(ref, uniform_buffer_t, ref);
    for (int i = 0; i < MTL_INFLIGHT_FRAMES; i++)
        [ubo->buf[i] release];
    list_del(&ubo->entry);
}
DEFINE_REFCLASS2(uniform_buffer);

cerr_check uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name,
                               int binding)
{
    CERR_RET(
        ref_embed(
            uniform_buffer, ubo,
            .renderer   = r,
            .binding    = binding,
            .name       = name
        ),
        return __cerr
    );

    return CERR_OK;
}

void uniform_buffer_done(uniform_buffer_t *ubo)
{
    if (!ref_is_static(&ubo->ref))
        ref_put_last(ubo);
    else
        uniform_buffer_drop(&ubo->ref);
}

static inline size_t mtl_ubo_offset(uniform_buffer_t *ubo)
{
    return round_up(ubo->size, 256) * ubo->item;
}

static inline void *mtl_ubo_data(uniform_buffer_t *ubo)
{
    auto r = ubo->renderer;
    return [ubo->buf[r->frame_idx] contents] + mtl_ubo_offset(ubo);
}

cerr_check _uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                               unsigned int count, const void *value);

cerr_check uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                              unsigned int count, const void *value)
{
    auto r = ubo->renderer;

    if (ubo->advance)   ubo->item++;

    ubo->data = mtl_ubo_data(ubo);

    if (ubo->advance) {
        if (ubo->prev)
            memcpy(ubo->data, ubo->prev, ubo->size);
        ubo->prev = ubo->data;
        ubo->advance = false;
    }

    return _uniform_buffer_set(ubo, type, offset, size, count, value);
}

cerr uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size)
{
    /* There's no reason to ever want to reallocate a UBO */
    if (ubo->data || ubo->size)
        return CERR_INVALID_OPERATION;

    ubo->size = round_up(size, 16);
    size_t slot_size = round_up(ubo->size, 256);

    ubo->total_size = slot_size * 1024;

    auto r = ubo->renderer;

    for (int i = 0; i < MTL_INFLIGHT_FRAMES; i++) {
        ubo->buf[i] = mtl_buffer_new(ubo->renderer, NULL, ubo->total_size, true, true);
        [ubo->buf[i] setLabel:[NSString stringWithFormat:@"ubo:%s.%d", ubo->name, i]];

        ubo->data = [ubo->buf[i] contents];
        memset(ubo->data, 0, ubo->total_size);
    }

    return CERR_OK;
}

cerr uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    if (binding_points->binding < 0)
        return CERR_INVALID_ARGUMENTS;

    if (unlikely(!ubo->total_size || !ubo->size))
        return CERR_BUFFER_INCOMPLETE;

    for (int i = 0; i < MTL_INFLIGHT_FRAMES; i++)
        if (unlikely(!ubo->buf[i]))
            return CERR_BUFFER_INCOMPLETE;

    return CERR_OK;
}

void uniform_buffer_update(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    renderer_t *r = ubo->renderer;
    size_t offset = mtl_ubo_offset(ubo);
    auto ubo_buf = ubo->buf[r->frame_idx];

    if (binding_points->stages & (1 << SHADER_STAGE_VERTEX)) {
        if (r->vbuffer_cache[ubo->binding] != ubo_buf || r->voffset_cache[ubo->binding] != offset) {
            if (r->vbuffer_cache[ubo->binding] == ubo_buf)
                [cmd_encoder(r) setVertexBufferOffset:offset atIndex:ubo->binding];
            else
                [cmd_encoder(r) setVertexBuffer:ubo_buf offset:offset atIndex:ubo->binding];
            r->vbuffer_cache[ubo->binding] = ubo_buf;
            r->voffset_cache[ubo->binding] = offset;
        }
    }
    if (binding_points->stages & (1 << SHADER_STAGE_FRAGMENT)) {
        if (r->fbuffer_cache[ubo->binding] != ubo_buf || r->foffset_cache[ubo->binding] != offset) {
            if (r->fbuffer_cache[ubo->binding] == ubo_buf)
                [cmd_encoder(r) setFragmentBufferOffset:offset atIndex:ubo->binding];
            else
                [cmd_encoder(r) setFragmentBuffer:ubo_buf offset:offset atIndex:ubo->binding];
            r->fbuffer_cache[ubo->binding] = ubo_buf;
            r->foffset_cache[ubo->binding] = offset;
        }
    }

    ubo->advance = true;
    ubo->dirty = false;
}

cres(size_t) shader_uniform_offset_query(shader_t *shader, const char *ubo_name, const char *var_name)
{
    return cres_error(size_t, CERR_NOT_SUPPORTED);
}

/****************************************************************************
 * Shaders
 ****************************************************************************/

cres_ret(mtl_library_t);

static cres(mtl_library_t) mtl_library_compile(renderer_t *r, const char* source)
{
    NSError *err = NULL;

    mtl_library_t lib = [r->device
        newLibraryWithSource:[NSString stringWithUTF8String:source]
        options:nil
        error:&err
    ];

    if (err) {
        err("newLibraryWithSource error: %s\n", [[err localizedDescription] UTF8String]);
        return cres_error(mtl_library_t, CERR_SHADER_NOT_LOADED);
    }

    return cres_val(mtl_library_t, lib);
}

cres_ret(mtl_function_t);
static cres(mtl_function_t) mtl_function_create(renderer_t *r, const char *source)
{
    mtl_library_t lib = CRES_RET(
        mtl_library_compile(r, source),
        return cres_error_cerr(mtl_function_t, __resp)
    );

    mtl_function_t func = [lib newFunctionWithName:[NSString stringWithUTF8String:"main0"]];
    if (!func)
        return cres_error_cerr(mtl_function_t, CERR_SHADER_NOT_LOADED);

    return cres_val(mtl_function_t, func);
}

cerr shader_init(renderer_t *r, shader_t *shader,
                 const char *vertex, const char *geometry, const char *fragment)
{
    shader->id = CRES_RET_CERR(bitmap_find_first_unset(&r->shader_ids));
    bitmap_set(&r->shader_ids, shader->id);
    shader->vert = CRES_RET_CERR(mtl_function_create(r, vertex));
    shader->frag = CRES_RET(
        mtl_function_create(r, fragment),
        { [shader->vert release]; return cerr_error_cres(__resp); }
    );

    dbg("### vert: %p frag: %p\n", shader->vert, shader->frag);

    shader->renderer = r;
    list_init(&shader->dc_list);

    return CERR_OK;
}

void shader_set_vertex_attrs(shader_t *shader, size_t stride,
                             size_t *offs, data_type *types, size_t *comp_counts,
                             unsigned int nr_attrs)
{
    shader->vdesc = [[MTLVertexDescriptor alloc] init];
    shader->vdesc = [shader->vdesc retain];

    for (unsigned int v = 0; v < nr_attrs; v++) {
        shader->vdesc.attributes[v].format = mtl_data_type[types[v]];
        shader->vdesc.attributes[v].bufferIndex = 0;     /* main0_in: all vertex attributes */
        shader->vdesc.attributes[v].offset = offs[v];
    }

    shader->vdesc.layouts[0].stride = stride;
    shader->vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
}

void shader_done(shader_t *shader)
{
    auto r = shader->renderer;
    bitmap_clear(&r->shader_ids, shader->id);

    draw_control_t *dc, *it;
    list_for_each_entry_iter(dc, it, &shader->dc_list, shader_entry)
        ref_put_last(dc);

    [shader->vdesc release];
    [shader->vert release];
    [shader->frag release];
}

attr_t shader_attribute(shader_t *shader, const char *name, attr_t attr)
{
    /* attribute locations are shared between core and shaders */
    return attr;
}

uniform_t shader_uniform(shader_t *shader, const char *name)
{
    if (!shader->reflection)                    return UA_UNKNOWN;
    if (!shader->reflection.fragmentBindings)   return UA_UNKNOWN;

    for (MTLArgument *arg in shader->reflection.fragmentBindings) {
        auto arg_name = [[arg.name localizedLowercaseString] UTF8String];
        if (!strcmp(name, arg_name))            return arg.index;
    }

    return UA_NOT_PRESENT;
}

cerr shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name)
{
    renderer_t *r = shader->renderer;

    return CERR_OK;
}

void shader_use(shader_t *shader, bool draw)
{
    if (!draw)  return;

    auto r = shader->renderer;
    auto fbo = r->fbo ? : r->screen_fbo;
    if (!fbo)    return;
    if (!shader->vert || !shader->frag || !shader->vdesc)   return;

    auto dc = dc_hash_find_get(r, fbo, shader);
    if (!dc)    return;

    draw_control_bind(dc);
}

void shader_unuse(shader_t *shader, bool draw)
{
    if (!draw)  return;

    auto r = shader->renderer;
    if (r->dc)  draw_control_unbind(r->dc);
}

void uniform_set_ptr(uniform_t uniform, data_type type, unsigned int count, const void *value)
{
}

/****************************************************************************
 * Renderer context
 ****************************************************************************/

void _renderer_init(renderer_t *r, const renderer_init_options *opts)
{
    bitmap_init(&r->fbo_ids, FBOS_MAX);
    bitmap_init(&r->shader_ids, SHADERS_MAX);
    for (size_t i = 0; i < array_size(r->dc_hash); i++)
        list_init(&r->dc_hash[i]);

    list_init(&r->ubos);

    r->sem = dispatch_semaphore_create(MTL_INFLIGHT_FRAMES);
    r->device = opts->device;
    r->layer = opts->layer;
    [r->layer retain];
    r->layer.pixelFormat = mtl_texture_format(TEX_FMT_BGRA10XR);
    // r->layer.pixelFormat = mtl_texture_format(TEX_FMT_BGR10A2);

    r->cmd_queue = [r->device newCommandQueue];

    r->colorspace = CGColorSpaceCreateWithName(kCGColorSpaceITUR_2100_PQ);

    r->cull_mode = from_mtl_cull_mode(MTLCullModeBack);
}

void renderer_done(renderer_t *r)
{
    fbo_put_last(r->screen_fbo);

    for (int i = 0; i < MTL_INFLIGHT_FRAMES; i++)
        dispatch_semaphore_wait(r->sem, DISPATCH_TIME_FOREVER);

    for (int i = 0; i < MTL_INFLIGHT_FRAMES; i++)
        dispatch_semaphore_signal(r->sem);

    [r->sem release];

    [r->layer release];
    CGColorSpaceRelease(r->colorspace);
    [r->device release];

    bitmap_done(&r->fbo_ids);
    bitmap_done(&r->shader_ids);
    err_on(!list_empty(&r->ubos), "UBO list not empty\n");
}

void renderer_frame_begin(renderer_t *r)
{
    /* One global render pass descriptor for rendering to the screen */
    if (!r->screen_fbo)
        r->screen_fbo = CRES_RET(
            fbo_new(
                .renderer           = r,
                .name               = "swapchain",
                .color_config       = (fbo_attconfig[]) {
                    {
                        .format         = mtl_texture_format_from_pixel_format(r->layer.pixelFormat),
                        .load_action    = FBOLOAD_DONTCARE
                    }
                },
                .layout             = FBO_COLOR_TEXTURE(0),
                .width              = r->width,
                .height             = r->height,
                .layers             = 1,
            ), return
        );


    r->frame_pool = [[NSAutoreleasePool alloc] init];

    r->layer.drawableSize = CGSizeMake(r->width, r->height);

    /* HDR output */
    r->layer.wantsExtendedDynamicRangeContent = YES;
    r->layer.colorspace = r->colorspace;

    mtl_cmd_buffer(r);
}

void renderer_swapchain_begin(renderer_t *r)
{
    r->drawable = [r->layer nextDrawable];
    r->screen_fbo->desc.colorAttachments[0].texture = r->drawable.texture;

    fbo_prepare(r->screen_fbo);
}

void renderer_swapchain_end(renderer_t *r)
{
}

static void renderer_frame_advance(renderer_t *r)
{
    r->nr_draws = 0;
    r->frame_idx = (r->frame_idx + 1) % MTL_INFLIGHT_FRAMES;

    uniform_buffer_t *ubo;
    list_for_each_entry(ubo, &r->ubos, entry) {
        ubo->item = 0;
        ubo->dirty = false;
        ubo->advance = false;

        ubo->data = mtl_ubo_data(ubo);
        if (ubo->prev)
            memcpy(ubo->data, ubo->prev, ubo->size);
        ubo->prev = ubo->data;
    }
}

void renderer_frame_end(renderer_t *r)
{
    // fbo_done(r->screen_fbo, r->width, r->height);

    if (!r->fps_cap)
        [r->cmd_buffer presentDrawable:r->drawable];
    else
        [r->cmd_buffer presentDrawable:r->drawable afterMinimumDuration:1.0 / (double)r->fps_cap];
    [r->cmd_buffer commit];

    r->cmd_buffer = nil;

    renderer_frame_advance(r);

    r->drawable = nil;

    [r->frame_pool drain];
    r->frame_pool = nil;
}

/*
 * The following accessors are private between render-mtl.m and ui-imgui-metal.m
 */
void *renderer_device(renderer_t *r)
{
    return r->device;
}

void *renderer_cmd_buffer(renderer_t *r)
{
    return r->cmd_buffer;
}

void *renderer_cmd_encoder(renderer_t *r)
{
    return r->screen_fbo->cmd_encoder;
}

void *renderer_screen_desc(renderer_t *r)
{
    return r->screen_fbo->desc;
}

#ifndef CONFIG_FINAL
#include "ui-debug.h"
void renderer_debug(renderer_t *r)
{
    debug_module *dbgm = ui_igBegin(DEBUG_RENDERER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        igText("frame: %d", r->frame_idx);
        igText("draws: %zu", r->nr_draws);
        igText("cmdbufs: %d", atomic_load(&r->cmdbuf_count));
        igSliderInt("FPS cap", &r->fps_cap, 0, 320, "%d", 0);
    }

    ui_igEnd(DEBUG_RENDERER);
}
#endif /* CONFIG_FINAL */

int renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    switch (limit) {
        case RENDER_LIMIT_MAX_TEXTURE_SIZE: return 8192;
        default:                            break;
    }
    return 0;
}

/* XXX: common? */
void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile)
{
    renderer->major     = major;
    renderer->minor     = minor;
    // renderer->profile   = profile;
}

void renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    if (r->x == x && r->y == y && r->width == width && r->height == height)
        return;

    r->x = x;
    r->y = y;

    if (r->screen_fbo)
        CERR_RET(fbo_resize(r->screen_fbo, width, height), return);

    r->width = width;
    r->height = height;
}

/* XXX: common? */
void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight)
{
    if (px)
        *px      = r->x;
    if (py)
        *py      = r->y;
    if (pwidth)
        *pwidth  = r->width;
    if (pheight)
        *pheight = r->height;
}

void renderer_cull_face(renderer_t *r, cull_face cull)
{
    switch (cull) {
        case CULL_FACE_NONE:    r->cull_mode = MTLCullModeNone;
        case CULL_FACE_FRONT:   r->cull_mode = MTLCullModeFront;
        case CULL_FACE_BACK:    r->cull_mode = MTLCullModeBack;
        default:                return;
    }
}

void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor)
{
    r->blend = _blend;
}

void renderer_wireframe(renderer_t *r, bool enable)
{
}

static unsigned int mtl_draw_type(draw_type draw_type)
{
    switch (draw_type) {
        case DRAW_TYPE_POINTS:          return MTLPrimitiveTypePoint;
        case DRAW_TYPE_TRIANGLES:       return MTLPrimitiveTypeTriangle;
        case DRAW_TYPE_TRIANGLE_STRIP:  return MTLPrimitiveTypeTriangleStrip;
        case DRAW_TYPE_LINES:           return MTLPrimitiveTypeLine;
        case DRAW_TYPE_LINE_STRIP:      return MTLPrimitiveTypeLineStrip;
        default:
            break;
    }

    clap_unreachable();

    return 0;
}

static unsigned int mtl_idx_type(data_type idx_type)
{
    switch (idx_type) {
        case DT_USHORT:
            return MTLIndexTypeUInt16;
        case DT_INT: // XXX
            return MTLIndexTypeUInt32;
        default:
            break;
    }

    clap_unreachable();

    return 0;
}

void renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                   data_type idx_type, unsigned int nr_instances)
{
    if (!r->va || !r->va->index)    return;

    auto index = r->va->index;
    if (!buffer_loaded(index))      return;

    size_t _idx_count = index->size / data_comp_size(idx_type);
    unsigned int _draw_type = mtl_draw_type(draw_type);
    unsigned int _idx_type = mtl_idx_type(idx_type);

    [cmd_encoder(r) setCullMode:to_mtl_cull_mode(r->cull_mode)];
    [cmd_encoder(r) setFrontFacingWinding:MTLWindingCounterClockwise];
    [cmd_encoder(r) drawIndexedPrimitives:_draw_type
                    indexCount:_idx_count
                    indexType:_idx_type
                    indexBuffer:index->buf
                    indexBufferOffset:0
                    instanceCount:nr_instances];

    r->nr_draws++;
}

void renderer_depth_func(renderer_t *r, depth_func fn)
{
    r->depth_func = fn;
}

void renderer_cleardepth(renderer_t *r, double depth)
{
    r->clear_depth = depth;
}

void renderer_clearcolor(renderer_t *r, vec4 color)
{
    if (!memcmp(r->clear_color, color, sizeof(vec4)))
        return;

    vec4_dup(r->clear_color, color);
}

void renderer_clear(renderer_t *r, bool color, bool depth, bool stencil)
{
}
