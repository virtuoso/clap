// SPDX-License-Identifier: Apache-2.0
#include "shader_constants.h"
#include "error.h"
#include "logger.h"
#include "object.h"

#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

const renderer_caps gl_renderer_caps = {
    .renderer           = RENDER_OPENGL,
};

static const renderer_caps *gl_renderer_get_caps(void)
{
    return &gl_renderer_caps;
}

static int gl_renderer_query_limits(renderer_t *renderer, render_limit limit);
static cerr gl_renderer_init(renderer_t *renderer, const renderer_init_options *opts);

static void gl_renderer_set_version(renderer_t *r, int major, int minor, renderer_profile profile);
static void gl_renderer_viewport(renderer_t *r, int x, int y, int width, int height);
static void gl_renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight);
static void gl_renderer_swapchain_begin(renderer_t *r);
#ifndef CONFIG_FINAL
static void gl_renderer_debug(renderer_t *r);
#endif
static void gl_renderer_cull_face(renderer_t *r, cull_face cull);
static void gl_renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor);
static cerr gl_renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                             data_type idx_type, unsigned int nr_instances);
static cerr gl_buffer_init(buffer_t *buf, const buffer_init_options *opts);
static void gl_buffer_deinit(buffer_t *buf);
static void gl_buffer_bind(buffer_t *buf, uniform_t loc);
static void gl_buffer_unbind(buffer_t *buf, uniform_t loc);
static cerr gl_vertex_array_init(vertex_array_t *va, renderer_t *r);
static void gl_vertex_array_done(vertex_array_t *va);
static void gl_vertex_array_bind(vertex_array_t *va);
static void gl_vertex_array_unbind(vertex_array_t *va);
static cerr gl_texture_init(texture_t *tex, const texture_init_options *opts);
static void gl_texture_deinit(texture_t *tex);
static cerr gl_texture_load(texture_t *tex, texture_format format,
                            unsigned int width, unsigned int height, void *buf);
static cerr gl_texture_resize(texture_t *tex, unsigned int width, unsigned int height);
static void gl_texture_bind(texture_t *tex, unsigned int target, uniform_t uniform);
static void gl_texture_unbind(texture_t *tex, unsigned int target);
static bool gl_texture_is_array(texture_t *tex);
static void gl_fbo_prepare(fbo_t *fbo);
static void gl_fbo_done(fbo_t *fbo, unsigned int width, unsigned int height);
static void gl_fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment);
static cerr gl_fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height);
static bool gl_fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment);
static texture_format gl_fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment);

static const renderer_ops gl_renderer_ops = {
    .get_caps       = gl_renderer_get_caps,
    .query_limits   = gl_renderer_query_limits,
    .init           = gl_renderer_init,
    .set_version    = gl_renderer_set_version,
    .viewport       = gl_renderer_viewport,
    .get_viewport   = gl_renderer_get_viewport,
    .swapchain_begin = gl_renderer_swapchain_begin,
#ifndef CONFIG_FINAL
    .debug          = gl_renderer_debug,
#endif
    .cull_face      = gl_renderer_cull_face,
    .blend          = gl_renderer_blend,
    .draw           = gl_renderer_draw,
    .buf_init   = gl_buffer_init,
    .buf_deinit = gl_buffer_deinit,
    .buf_bind   = gl_buffer_bind,
    .buf_unbind = gl_buffer_unbind,
    .va_init    = gl_vertex_array_init,
    .va_done    = gl_vertex_array_done,
    .va_bind    = gl_vertex_array_bind,
    .va_unbind  = gl_vertex_array_unbind,
    .tex_init   = gl_texture_init,
    .tex_deinit = gl_texture_deinit,
    .tex_load   = gl_texture_load,
    .tex_resize = gl_texture_resize,
    .tex_bind   = gl_texture_bind,
    .tex_unbind = gl_texture_unbind,
    .tex_is_array = gl_texture_is_array,
    .fbo_prepare  = gl_fbo_prepare,
    .fbo_done     = gl_fbo_done,
    .fbo_blit     = gl_fbo_blit_from_fbo,
    .fbo_resize   = gl_fbo_resize,
    .fbo_attachment_valid  = gl_fbo_attachment_valid,
    .fbo_attachment_format = gl_fbo_attachment_format,
};

#if defined(CONFIG_BROWSER) || !(defined(__glu_h__) || defined(GLU_H))
static inline const char *gluErrorString(int err) { return "not implemented"; }
#endif

#ifdef CLAP_DEBUG
static bool validate_gl_calls;

static inline bool __gl_check_error(const char *str)
{
    if (!validate_gl_calls) return false;

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        err("%s: GL error: 0x%04X: %s\n", str ? str : "<>", err, gluErrorString(err));
        abort();
        return true;
    }

    return false;
}

#define GL(__x) do {                    \
    __x;                                \
    __gl_check_error(__stringify(__x)); \
} while (0)
#else
#define GL(__x) __x
#endif

static int gl_limits[RENDER_LIMIT_MAX];

static const GLuint gl_comp_type[] = {
    [DT_BYTE]   = GL_UNSIGNED_BYTE,
    [DT_SHORT]  = GL_SHORT,
    [DT_USHORT] = GL_UNSIGNED_SHORT,
    [DT_INT]    = GL_INT,
    [DT_UINT]   = GL_UNSIGNED_INT,
    [DT_FLOAT]  = GL_FLOAT,
    [DT_IVEC2]  = GL_INT,
    [DT_IVEC3]  = GL_INT,
    [DT_IVEC4]  = GL_INT,
    [DT_UVEC2]  = GL_UNSIGNED_INT,
    [DT_UVEC3]  = GL_UNSIGNED_INT,
    [DT_UVEC4]  = GL_UNSIGNED_INT,
    [DT_VEC2]   = GL_FLOAT,
    [DT_VEC3]   = GL_FLOAT,
    [DT_VEC4]   = GL_FLOAT,
    [DT_MAT2]   = GL_FLOAT,
    [DT_MAT3]   = GL_FLOAT,
    [DT_MAT4]   = GL_FLOAT,
};

/****************************************************************************
 * Buffer
 ****************************************************************************/

static cerr buffer_make(struct ref *ref, void *opts)
{
    return CERR_OK;
}

static void buffer_drop(struct ref *ref)
{
    buffer_t *buf = container_of(ref, struct buffer, ref);
    buffer_deinit(buf);
}
DEFINE_REFCLASS2(buffer);

static GLenum gl_buffer_type(buffer_type type)
{
    switch (type) {
        case BUF_ARRAY:             return GL_ARRAY_BUFFER;
        case BUF_ELEMENT_ARRAY:     return GL_ELEMENT_ARRAY_BUFFER;
        default:                    break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_buffer_usage(buffer_usage usage)
{
    switch (usage) {
        case BUF_STATIC:            return GL_STATIC_DRAW;
        case BUF_DYNAMIC:           return GL_DYNAMIC_DRAW;
        default:                    break;
    }

    clap_unreachable();

    return GL_NONE;
}



static void buffer_load(buffer_t *buf, void *data, size_t sz, uniform_t loc);

static cerr gl_buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    cerr err = ref_embed(buffer, buf);
    if (IS_CERR(err))
        return err;

    data_type comp_type = opts->comp_type;
    if (comp_type == DT_NONE)
        comp_type = DT_FLOAT;

    /*
     * If comp_count is not specified, it's derived from the component type;
     * if component type is non-scalar, the comp_count should be the number
     * of scalars in that type, because GL only understands scalars. IOW, for
     * DT_VEC3, comp_count should be 3, even though the caller implies "one
     * vec3". It would become slightly more interesting if we ever need to
     * specify multiples of compound elements, but that's for later.
     */
    unsigned int comp_count = opts->comp_count;
    if (comp_count < data_comp_count(comp_type))
        comp_count = data_comp_count(comp_type);

    buf->gl.type = gl_buffer_type(opts->type);
    buf->gl.usage = gl_buffer_usage(opts->usage);
    buf->gl.comp_type = gl_comp_type[comp_type];
    buf->comp_count = comp_count;
    buf->off = opts->off;
    buf->gl.stride = opts->stride;
    buf->use_count = 1;
    /* Not doing a ref_get(opts->main) here, see the comment in buffer_deinit() */
    buf->main = opts->main;
    if (buf->main)
        buf->main->use_count++;

    if (opts->data && opts->size)
        buffer_load(buf, opts->data, opts->size,
                    buf->gl.type == GL_ELEMENT_ARRAY_BUFFER ? -1 : opts->loc);

#ifndef CONFIG_FINAL
    memcpy(&buf->opts, opts, sizeof(*opts));
#endif /* CONFIG_FINAL */

    return CERR_OK;
}

static void gl_buffer_deinit(buffer_t *buf)
{
    if (!buf->loaded)
        return;

    /*
     * This is safe, because at the moment all buffer_t objects are static;
     * normally one would do a ref_get(main)/ref_put(main) to make sure that
     * main is still around, but with static objects it doesn't work before
     * https://github.com/virtuoso/clap/issues/94 is properly addressed.
     */
    buffer_t *main = buf->main ? : buf;
    if (!--main->use_count)
        GL(glDeleteBuffers(1, &buf->gl.id));

    buf->loaded = false;
}

static inline noubsan void _buffer_bind(buffer_t *buf, uniform_t loc)
{
    switch (buf->gl.comp_type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_INT:
        case GL_UNSIGNED_INT:
            GL(glVertexAttribIPointer(loc, buf->comp_count, buf->gl.comp_type,
                                      buf->gl.stride, (void *)0 + buf->off));
            break;
        default:
            GL(glVertexAttribPointer(loc, buf->comp_count, buf->gl.comp_type, GL_FALSE,
                                     buf->gl.stride, (void *)0 + buf->off));
            break;
    }
}

static void gl_buffer_bind(buffer_t *buf, uniform_t loc)
{
    if (!buf->loaded)
        return;

#ifndef CONFIG_FINAL
    buf->loc = loc;
#endif /* CONFIG_FINAL */

    /*
     * glEnableVertexAttribArray() only needs a vertex array object to be
     * bound, which the caller should have done. Binding VBO for this is
     * redundant.
     * This may or may not implode on real GLES (e.g., RPi) where VAOs are
     * not supported at all, leave this for the future.
     */
    if (buf->gl.type == GL_ELEMENT_ARRAY_BUFFER) {
        GL(glBindBuffer(buf->gl.type, buf->gl.id));
        return;
    }

    GL(glEnableVertexAttribArray(loc));
}

static void gl_buffer_unbind(buffer_t *buf, uniform_t loc)
{
    if (!buf->loaded)
        return;

    if (buf->gl.type == GL_ELEMENT_ARRAY_BUFFER) {
        GL(glBindBuffer(buf->gl.type, 0));
        return;
    }

    GL(glDisableVertexAttribArray(loc));
}

static void buffer_load(buffer_t *buf, void *data, size_t sz, uniform_t loc)
{
    if (buf->main)
        buf->gl.id = buf->main->gl.id;
    else
        GL(glGenBuffers(1, &buf->gl.id));

    GL(glBindBuffer(buf->gl.type, buf->gl.id));

    if (!buf->main)
        GL(glBufferData(buf->gl.type, sz, data, buf->gl.usage));
#ifndef CONFIG_FINAL
    buf->opts.size = sz;
    buf->loc = loc;
#endif /* CONFIG_FINAL */

    if (loc >= 0)
        _buffer_bind(buf, loc);

    GL(glBindBuffer(buf->gl.type, 0));
    buf->loaded = true;
}

/****************************************************************************
 * Vertex Array Object
 ****************************************************************************/

static bool gl_does_vao(void)
{
#if defined(CONFIG_GLES) && !defined(CONFIG_BROWSER)
    return false;
#else
    return true;
#endif
}

static void vertex_array_drop(struct ref *ref)
{
    vertex_array_t *va = container_of(ref, vertex_array_t, ref);
    vertex_array_done(va);
}

DEFINE_REFCLASS(vertex_array);

static cerr gl_vertex_array_init(vertex_array_t *va, renderer_t *r)
{
    cerr err = ref_embed(vertex_array, va);
    if (IS_CERR(err))
        return err;

    if (!gl_does_vao())
        return CERR_OK;

    GL(glGenVertexArrays(1, &va->gl.vao));
    GL(glBindVertexArray(va->gl.vao));

    return CERR_OK;
}

static void gl_vertex_array_done(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glDeleteVertexArrays(1, &va->gl.vao));
}

static void gl_vertex_array_bind(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glBindVertexArray(va->gl.vao));
}

static void gl_vertex_array_unbind(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glBindVertexArray(0));
}

/****************************************************************************
 * Texture
 ****************************************************************************/

static cerr texture_make(struct ref *ref, void *_opts)
{
    return CERR_OK;
}

static void texture_drop(struct ref *ref)
{
    struct texture *tex = container_of(ref, struct texture, ref);
    texture_deinit(tex);
}
DEFINE_REFCLASS2(texture);

cresp_struct_ret(texture);

static GLenum gl_texture_type(texture_type type, bool multisampled)
{
    switch (type) {
#ifdef CONFIG_GLES
        case TEX_2D:        return GL_TEXTURE_2D;
        case TEX_2D_ARRAY:  return GL_TEXTURE_2D_ARRAY;
#else
        case TEX_2D:        return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        case TEX_2D_ARRAY:  return multisampled ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_ARRAY;
#endif
        case TEX_3D:        return GL_TEXTURE_3D;
        default:            break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_wrap(texture_wrap wrap)
{
    switch (wrap) {
        case TEX_WRAP_REPEAT:           return GL_REPEAT;
        case TEX_WRAP_MIRRORED_REPEAT:  return GL_MIRRORED_REPEAT;
        case TEX_CLAMP_TO_EDGE:         return GL_CLAMP_TO_EDGE;
        case TEX_CLAMP_TO_BORDER:       return GL_CLAMP_TO_BORDER;
        default:                        break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_filter(texture_filter filter)
{
    switch (filter) {
        case TEX_FLT_LINEAR:            return GL_LINEAR;
        case TEX_FLT_NEAREST:           return GL_NEAREST;
        default:                        break;
    }

    clap_unreachable();

    return GL_NONE;
}

static bool _texture_format_supported[TEX_FMT_MAX];

bool texture_format_supported(texture_format format)
{
    if (format >= TEX_FMT_MAX)
        return false;

    return _texture_format_supported[format];
}

static GLenum gl_texture_format(texture_format format)
{
    switch (format) {
        case TEX_FMT_R32UI:     return GL_RED_INTEGER;
        case TEX_FMT_R32F:
        case TEX_FMT_R16F:
        case TEX_FMT_R8:        return GL_RED;
        case TEX_FMT_RG32UI:    return GL_RG_INTEGER;
        case TEX_FMT_RG32F:
        case TEX_FMT_RG16F:
        case TEX_FMT_RG8:       return GL_RG;
        case TEX_FMT_RGBA32UI:  return GL_RGBA_INTEGER;
        case TEX_FMT_RGBA32F:
        case TEX_FMT_RGBA16F:
        case TEX_FMT_RGBA8_SRGB:
        case TEX_FMT_RGBA8:     return GL_RGBA;
        case TEX_FMT_RGB32F:
        case TEX_FMT_RGB16F:
        case TEX_FMT_RGB8_SRGB:
        case TEX_FMT_RGB8:      return GL_RGB;
        case TEX_FMT_DEPTH16F:
        case TEX_FMT_DEPTH24F:
        case TEX_FMT_DEPTH32F:  return GL_DEPTH_COMPONENT;
        default:                break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_internal_format(texture_format fmt)
{
    switch (fmt) {
        case TEX_FMT_R8:        return GL_R8;
        case TEX_FMT_R16F:      return GL_R16F;
        case TEX_FMT_R32F:      return GL_R32F;
        case TEX_FMT_RG8:       return GL_RG8;
        case TEX_FMT_RG16F:     return GL_RG16F;
        case TEX_FMT_RG32F:     return GL_RG32F;
        case TEX_FMT_RGBA8:     return GL_RGBA8;
        case TEX_FMT_RGB8:      return GL_RGB8;
        case TEX_FMT_RGBA8_SRGB:return GL_SRGB8_ALPHA8;
        case TEX_FMT_RGB8_SRGB: return GL_SRGB8;
        case TEX_FMT_RGBA32F:   return GL_RGBA32F;
        case TEX_FMT_RGBA16F:   return GL_RGBA16F;
        case TEX_FMT_RGB32F:    return GL_RGB32F;
        case TEX_FMT_RGB16F:    return GL_RGB16F;
        case TEX_FMT_R32UI:     return GL_R32UI;
        case TEX_FMT_RG32UI:    return GL_RG32UI;
        case TEX_FMT_RGBA32UI:  return GL_RGBA32UI;
        case TEX_FMT_DEPTH32F:  return GL_DEPTH_COMPONENT32F;
        case TEX_FMT_DEPTH24F:  return GL_DEPTH_COMPONENT24;
        case TEX_FMT_DEPTH16F:  return GL_DEPTH_COMPONENT16;
        default:                break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_component_type(texture_format fmt)
{
    switch (fmt) {
        case TEX_FMT_R8:
        case TEX_FMT_RG8:
        case TEX_FMT_RGB8:
        case TEX_FMT_RGBA8:
        case TEX_FMT_RGB8_SRGB:
        case TEX_FMT_RGBA8_SRGB:return GL_UNSIGNED_BYTE;
        case TEX_FMT_R16F:
        case TEX_FMT_RG16F:
        case TEX_FMT_RGB16F:
        case TEX_FMT_RGBA16F:   return GL_HALF_FLOAT;
        case TEX_FMT_R32F:
        case TEX_FMT_RG32F:
        case TEX_FMT_RGB32F:
        case TEX_FMT_RGBA32F:   return GL_FLOAT;
        case TEX_FMT_DEPTH32F:  return GL_FLOAT;
        case TEX_FMT_R32UI:
        case TEX_FMT_RG32UI:
        case TEX_FMT_RGBA32UI:
        case TEX_FMT_DEPTH24F:  return GL_UNSIGNED_INT;
#ifdef CONFIG_GLES
        case TEX_FMT_DEPTH16F:  return GL_UNSIGNED_SHORT;
#else
        case TEX_FMT_DEPTH16F:  return GL_HALF_FLOAT;
#endif /* CONFIG_GLES */
        default:                break;
    }

    clap_unreachable();

    return GL_NONE;
}

static bool gl_texture_is_array(texture_t *tex)
{
    return tex->gl.type == GL_TEXTURE_2D_ARRAY ||
           tex->gl.type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
}

static cerr gl_texture_init(texture_t *tex, const texture_init_options *opts)
{
    bool multisampled   = opts->multisampled;
#ifdef CONFIG_GLES
    multisampled  = false;
#endif /* CONFIG_GLES */

    if (!texture_format_supported(opts->format))
        return CERR_NOT_SUPPORTED_REASON(
            .fmt = "format %s (%ld) not supported",
            .arg0 = texture_format_string(opts->format),
            .arg1 = (const void *)opts->format
        );

    cerr err = ref_embed(texture, tex);
    if (IS_CERR(err))
        return err;

    tex->gl.component_type  = gl_texture_component_type(opts->format);
    tex->gl.wrap            = gl_texture_wrap(opts->wrap);
    tex->gl.min_filter      = gl_texture_filter(opts->min_filter);
    tex->gl.mag_filter      = gl_texture_filter(opts->mag_filter);
    tex->gl.target          = GL_TEXTURE0 + opts->target;
    tex->gl.type            = gl_texture_type(opts->type, multisampled);
    tex->gl.format          = gl_texture_format(opts->format);
    tex->gl.internal_format = gl_texture_internal_format(opts->format);
    tex->layers             = opts->layers;
    tex->multisampled       = multisampled;
    /*
     * we don't have dedicated samplers yet; this is to signal
     * texture_setup_begin() that sampler parameters need to be
     * configured, to avoid doing it at each texture_load()
     */
    tex->gl.sampler_updated = true;
    if (opts->border)
        memcpy(tex->gl.border, opts->border, sizeof(tex->gl.border));
    GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    GL(glActiveTexture(tex->gl.target));
    GL(glGenTextures(1, &tex->gl.id));

#ifndef CONFIG_FINAL
    memcpy(&tex->opts, opts, sizeof(*opts));
#endif /* CONFIG_FINAL */

    return CERR_OK;
}

texture_t *texture_clone(texture_t *tex)
{
    texture_t *ret = ref_new(texture);

    if (ret) {
        ret->renderer           = tex->renderer;
        ret->gl.id              = tex->gl.id;
        ret->gl.wrap            = tex->gl.wrap;
        ret->gl.component_type  = tex->gl.component_type;
        ret->gl.type            = tex->gl.type;
        ret->gl.target          = tex->gl.target;
        ret->gl.min_filter      = tex->gl.min_filter;
        ret->gl.mag_filter      = tex->gl.mag_filter;
        ret->width              = tex->width;
        ret->height             = tex->height;
        ret->layers             = tex->layers;
        ret->gl.format          = tex->gl.format;
        ret->gl.internal_format = tex->gl.internal_format;
        ret->loaded             = tex->loaded;
        ret->gl.sampler_updated = true;
        tex->loaded             = false;
    }

    return ret;
}

static void gl_texture_deinit(texture_t *tex)
{
    if (!tex->loaded)
        return;
    GL(glDeleteTextures(1, &tex->gl.id));
    tex->loaded = false;
}

static cerr texture_storage(texture_t *tex, void *buf)
{
    if (tex->gl.type == GL_TEXTURE_2D)
        glTexImage2D(tex->gl.type, 0, tex->gl.internal_format, tex->width, tex->height,
                     0, tex->gl.format, tex->gl.component_type, buf);
    else if (tex->gl.type == GL_TEXTURE_2D_ARRAY || tex->gl.type == GL_TEXTURE_3D)
        glTexImage3D(tex->gl.type, 0, tex->gl.internal_format, tex->width, tex->height, tex->layers,
                     0, tex->gl.format, tex->gl.component_type, buf);
#ifdef CONFIG_GLES
    else if (tex->gl.type == GL_TEXTURE_2D_MULTISAMPLE ||
             tex->gl.type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
         return CERR_NOT_SUPPORTED;
#else
    else if (tex->gl.type == GL_TEXTURE_2D_MULTISAMPLE)
        glTexImage2DMultisample(tex->gl.type, 4, tex->gl.internal_format, tex->width, tex->height,
                                GL_TRUE);
    else if (tex->gl.type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        glTexImage3DMultisample(tex->gl.type, 4, tex->gl.internal_format, tex->width, tex->height,
                                tex->layers, GL_TRUE);
#endif /* CONFIG_GLES */
    auto err = glGetError();
    if (err != GL_NO_ERROR)
        return CERR_NOT_SUPPORTED;

    return CERR_OK;
}

static bool texture_size_valid(unsigned int width, unsigned int height)
{
    return width < gl_limits[RENDER_LIMIT_MAX_TEXTURE_SIZE] &&
           height < gl_limits[RENDER_LIMIT_MAX_TEXTURE_SIZE];
}

static cerr gl_texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!tex->loaded)
        return CERR_TEXTURE_NOT_LOADED;

    if (tex->width == width && tex->height == height)
        return CERR_OK;

    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE;

    tex->width = width;
    tex->height = height;
    GL(glBindTexture(tex->gl.type, tex->gl.id));
    cerr err = texture_storage(tex, NULL);
    GL(glBindTexture(tex->gl.type, 0));

    return err;
}

static cerr texture_setup_begin(texture_t *tex, void *buf)
{
    GL(glActiveTexture(tex->gl.target));
    GL(glBindTexture(tex->gl.type, tex->gl.id));

    if (!tex->gl.sampler_updated)   goto out;

    if (tex->gl.type != GL_TEXTURE_2D_MULTISAMPLE &&
        tex->gl.type != GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        GL(glTexParameteri(tex->gl.type, GL_TEXTURE_WRAP_S, tex->gl.wrap));
        GL(glTexParameteri(tex->gl.type, GL_TEXTURE_WRAP_T, tex->gl.wrap));
        if (tex->gl.type == TEX_3D)
            GL(glTexParameteri(tex->gl.type, GL_TEXTURE_WRAP_R, tex->gl.wrap));
        GL(glTexParameteri(tex->gl.type, GL_TEXTURE_MIN_FILTER, tex->gl.min_filter));
        GL(glTexParameteri(tex->gl.type, GL_TEXTURE_MAG_FILTER, tex->gl.mag_filter));
#ifndef CONFIG_GLES
        if (tex->gl.wrap == GL_CLAMP_TO_BORDER)
            GL(glTexParameterfv(tex->gl.type, GL_TEXTURE_BORDER_COLOR, tex->gl.border));
#endif /* CONFIG_GLES */
    }

    tex->gl.sampler_updated = false;

out:
    return texture_storage(tex, buf);
}

static void texture_setup_end(texture_t *tex)
{
    GL(glBindTexture(tex->gl.type, 0));
}

static cerr gl_texture_load(texture_t *tex, texture_format format,
                            unsigned int width, unsigned int height, void *buf)
{
    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE_REASON(
            .fmt    = "requested size %u x %u",
            .arg0   = (void *)(uintptr_t)width,
            .arg1   = (void *)(uintptr_t)height
        );

    if (!texture_format_supported(format))
        return CERR_NOT_SUPPORTED;

    tex->gl.format          = gl_texture_format(format);
    tex->gl.internal_format = gl_texture_internal_format(format);
    tex->gl.component_type  = gl_texture_component_type(format);

    tex->width      = width;
    tex->height     = height;

    cerr err = texture_setup_begin(tex, buf);
    if (IS_CERR(err))
        return err;

    texture_setup_end(tex);
    tex->loaded = true;

    return CERR_OK;
}

static cerr_check texture_fbo(texture_t *tex, GLuint attachment, texture_format format,
                              unsigned int width, unsigned int height)
{
    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE_REASON(
            .fmt    = "requested size %u x %u",
            .arg0   = (void *)(uintptr_t)width,
            .arg1   = (void *)(uintptr_t)height
        );

    if (!fbo_texture_supported(format))
        return CERR_NOT_SUPPORTED;

    tex->gl.format             = gl_texture_format(format);
    tex->gl.internal_format    = gl_texture_internal_format(format);
    tex->gl.component_type     = gl_texture_component_type(format);

    tex->width  = width;
    tex->height = height;

    cerr err = texture_setup_begin(tex, NULL);
    if (IS_CERR(err))
        return err;

    if (tex->gl.type == GL_TEXTURE_2D ||
        tex->gl.type == GL_TEXTURE_2D_MULTISAMPLE)
        GL(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, tex->gl.type, tex->gl.id, 0));
#ifdef CONFIG_GLES
    else
        return CERR_NOT_SUPPORTED;
#else
    else if (tex->gl.type == GL_TEXTURE_2D_ARRAY ||
             tex->gl.type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        GL(glFramebufferTexture(GL_FRAMEBUFFER, attachment, tex->gl.id, 0));
    else
        GL(glFramebufferTexture3D(GL_FRAMEBUFFER, attachment, tex->gl.type, tex->gl.id, 0, 0));
#endif
    texture_setup_end(tex);
    tex->loaded = true;

    return CERR_OK;
}

static void gl_texture_bind(texture_t *tex, unsigned int target, uniform_t uniform)
{
    /* XXX: make tex->gl.target useful for this instead */
    GL(glActiveTexture(GL_TEXTURE0 + target));
    GL(glBindTexture(tex->gl.type, tex->gl.id));
    if (uniform >= 0)
        GL(glUniform1i(uniform, target));
}

static void gl_texture_unbind(texture_t *tex, unsigned int target)
{
    /* XXX: make tex->gl.target useful for this instead */
    GL(glActiveTexture(GL_TEXTURE0 + target));
    GL(glBindTexture(tex->gl.type, 0));
}


texid_t texture_id(struct texture *tex)
{
    if (!tex)
        return 0;
    return tex->gl.id;
}

/****************************************************************************
 * Framebuffer
 ****************************************************************************/

static bool _fbo_texture_supported[TEX_FMT_MAX];

bool fbo_texture_supported(texture_format format)
{
    if (format >= TEX_FMT_MAX)
        return false;

    return _fbo_texture_supported[format];
}

static int fbo_create(void)
{
    unsigned int fbo;

    GL(glGenFramebuffers(1, &fbo));
    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
    return fbo;
}

texture_format fbo_texture_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (attachment.depth_buffer || attachment.depth_texture)
        return fbo->depth_config.format;

    int target = fbo_attachment_color(attachment);

    /*
     * boundary check may not be possible yet, because this will be called early
     * in the initialization path, fbo doesn't keep the size of this array, but
     * internally correct code shouldn't violate it; otherwise it's better to
     * crash than to be unwittingly stuck with the wrong texture format
     */
    if (fbo->color_config)
        return fbo->color_config[target].format;

    return TEX_FMT_DEFAULT;
}

static GLenum fbo_gl_attachment(fbo_attachment attachment)
{
    if (attachment.depth_buffer || attachment.depth_texture)
        return GL_DEPTH_ATTACHMENT;

    if (attachment.stencil_buffer || attachment.stencil_texture)
        return GL_STENCIL_ATTACHMENT;

    return GL_COLOR_ATTACHMENT0 + fbo_attachment_color(attachment);
}

static cerr_check fbo_texture_init(fbo_t *fbo, fbo_attachment attachment)
{
    int idx = fbo_attachment_color(attachment);

    cerr err = texture_init(&fbo->color_tex[idx],
                            .renderer      = fbo->renderer,
                            .type          = fbo->layers ? TEX_2D_ARRAY : TEX_2D,
                            .layers        = fbo->layers,
                            .format        = fbo_texture_format(fbo, attachment),
                            .multisampled  = fbo_is_multisampled(fbo),
                            .wrap          = TEX_CLAMP_TO_EDGE,
                            .min_filter    = TEX_FLT_LINEAR,
                            .mag_filter    = TEX_FLT_LINEAR);
    if (IS_CERR(err))
        return err;

    err = texture_fbo(&fbo->color_tex[idx], fbo_gl_attachment(attachment),
                      fbo_texture_format(fbo, attachment), fbo->width, fbo->height);
    if (IS_CERR(err))
        return err;

    return CERR_OK;
}

static cerr_check fbo_textures_init(fbo_t *fbo, fbo_attachment attachment)
{
    fa_for_each(fa, attachment, texture) {
        cerr err = fbo_texture_init(fbo, fa);
        if (IS_CERR(err))
            return err;
    }

    return CERR_OK;
}

static cerr_check fbo_depth_texture_init(fbo_t *fbo)
{
    float border[4] = {};
    cerr err = texture_init(&fbo->depth_tex,
                            .renderer      = fbo->renderer,
                            .type          = fbo->layers ? TEX_2D_ARRAY : TEX_2D,
                            .layers        = fbo->layers,
                            .format        = fbo->depth_config.format,
                            .multisampled  = fbo_is_multisampled(fbo),
                            .wrap          = TEX_CLAMP_TO_BORDER,
                            .border        = border,
                            .min_filter    = TEX_FLT_NEAREST,
                            .mag_filter    = TEX_FLT_NEAREST);
    if (IS_CERR(err))
        return err;

    err = texture_fbo(&fbo->depth_tex, GL_DEPTH_ATTACHMENT, fbo->depth_config.format,
                      fbo->width, fbo->height);
    if (IS_CERR(err))
        return err;

    return CERR_OK;
}

texture_t *fbo_texture(fbo_t *fbo, fbo_attachment attachment)
{
    if (attachment.depth_texture)
        return &fbo->depth_tex;

    int idx = fbo_attachment_color(attachment);
    if (idx >= fa_nr_color_texture(fbo->layout))
        return NULL;

    return &fbo->color_tex[idx];
}

static bool gl_fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment)
{
    int tidx = fbo_attachment_color(attachment);
    if (tidx >= 0 && tidx <= fbo_attachment_color(fbo->layout) &&
        texture_loaded(&fbo->color_tex[tidx]))
        return true;

    int bidx = fbo_attachment_color(attachment);
    if (bidx >= 0 && bidx <= fbo_attachment_color(fbo->layout) &&
        fbo->gl.color_buf[bidx])
        return true;

    if (attachment.depth_texture && fbo->layout.depth_texture)
        return true;

    if (attachment.depth_buffer && fbo->layout.depth_buffer)
        return true;

    return false;
}

static texture_format gl_fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo_attachment_valid(fbo, attachment)) {
        err("invalid attachment '%s'\n", fbo_attachment_string(attachment));
        return TEX_FMT_MAX;
    }

    return fbo->color_config[fbo_attachment_color(attachment)].format;
}

static void __fbo_color_buffer_setup(fbo_t *fbo, fbo_attachment attachment)
{
    GLenum gl_internal_format = gl_texture_internal_format(fbo_texture_format(fbo, attachment));
    if (fbo_is_multisampled(fbo))
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, fbo->nr_samples, gl_internal_format,
                                            fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, gl_internal_format, fbo->width, fbo->height));
}

static cres(int) fbo_color_buffer(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fa_nr_color_buffer(attachment))
        return cres_error(int, CERR_INVALID_ARGUMENTS);

    GLenum gl_attachment = fbo_gl_attachment(attachment);
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    __fbo_color_buffer_setup(fbo, attachment);
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, gl_attachment, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return cres_val(int, buf);
}

static void __fbo_depth_buffer_setup(fbo_t *fbo)
{
    GLenum gl_internal_format = gl_texture_internal_format(fbo->depth_config.format);
    if (fbo_is_multisampled(fbo))
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, fbo->nr_samples,
                                            gl_internal_format, fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, gl_internal_format, fbo->width, fbo->height));
}

static int fbo_depth_buffer(fbo_t *fbo)
{
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    __fbo_depth_buffer_setup(fbo);
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return buf;
}

static cerr gl_fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo)
        return CERR_INVALID_ARGUMENTS;

    GL(glFinish());

    cerr err = CERR_OK;
    fa_for_each(fa, fbo->layout, texture) {
        texture_t *tex = &fbo->color_tex[fa_nr_color_texture(fa) - 1];
        if (texture_loaded(tex))
            err = texture_resize(tex, width, height);
        /* On error, revert back to the previous size */
        if (IS_CERR(err)) {
            if (width != fbo->width || height != fbo->height)
                return texture_resize(tex, fbo->width, fbo->height);

            return err;
        }
    }

    if (texture_loaded(&fbo->depth_tex)) {
        err = texture_resize(&fbo->depth_tex, width, height);
        if (IS_CERR(err)) {
            if (width != fbo->width || height != fbo->height)
                err = texture_resize(&fbo->depth_tex, fbo->width, fbo->height);

            return err;
        }
    }

    fbo->width = width;
    fbo->height = height;

    fa_for_each(fa, fbo->layout, buffer) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, fbo->gl.color_buf[fbo_attachment_color(fa)]));
        __fbo_color_buffer_setup(fbo, fa);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    if (fbo->gl.depth_buf >= 0) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, fbo->gl.depth_buf));
        __fbo_depth_buffer_setup(fbo);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    return CERR_OK;
}

#if defined(CONFIG_GLES) && 0
static void fbo_invalidate(fbo_t *fbo)
{
    // if (fbo->invalidate)    return;

    /* TODO: also depth (and stencil) attachments */
    GLenum attachments[FBO_COLOR_ATTACHMENTS_MAX];
    size_t nr_attachments = 0;
    fa_for_each(fa, fbo->attachment_config, texture) {
        if (fbo->color_config[nr_attachments].store_action != FBOSTORE_DONTCARE)
            continue;
        attachments[nr_attachments++] = GL_COLOR_ATTACHMENT0 + fa_nr_color_texture(fa) - 1;
    }
    GL(glInvalidateFrameBuffer(GL_DRAW_FRAMEBUFFER, nr_attachments, attachments));
}
#else
static inline void fbo_invalidate(fbo_t *fbo) {}
#endif /* CONFIG_GLES */

static GLenum gl_depth_func(depth_func fn)
{
    switch (fn) {
        case DEPTH_FN_NEVER:
            return GL_NEVER;
        case DEPTH_FN_LESS:
            return GL_LESS;
        case DEPTH_FN_EQUAL:
            return GL_EQUAL;
        case DEPTH_FN_LESS_OR_EQUAL:
            return GL_LEQUAL;
        case DEPTH_FN_GREATER:
            return GL_GREATER;
        case DEPTH_FN_NOT_EQUAL:
            return GL_NOTEQUAL;
        case DEPTH_FN_GREATER_OR_EQUAL:
            return GL_GEQUAL;
        case DEPTH_FN_ALWAYS:
            return GL_ALWAYS;
        default:    break;
    }

    clap_unreachable();

    return GL_NONE;
}

static void renderer_depth_test(renderer_t *r, bool enable)
{
    if (r->gl.depth_test == enable)
        return;

    r->gl.depth_test = enable;
    if (enable)
        GL(glEnable(GL_DEPTH_TEST));
    else
        GL(glDisable(GL_DEPTH_TEST));
}

#define NR_TARGETS FBO_COLOR_ATTACHMENTS_MAX
static void gl_fbo_prepare(fbo_t *fbo)
{
    GLenum buffers[NR_TARGETS];
    int target = 0;

    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo->gl.fbo));
    GL(glViewport(0, 0, fbo->width, fbo->height));

    if (fbo_attachment_is_buffers(fbo->layout)) {
        fa_for_each(fa, fbo->layout, buffer)
            buffers[target++] = fbo_gl_attachment(fa);
    } else {
        fa_for_each(fa, fbo->layout, texture)
            buffers[target++] = fbo_gl_attachment(fa);
    }

    if (!target) {
        buffers[0] = GL_NONE;
        GL(glDrawBuffers(1, buffers));
        GL(glReadBuffer(GL_NONE));
    }

    GL(glDrawBuffers(target, buffers));

    bool depth_test = false;
    for (int i = 0; i < target; i++)
        if (fbo->color_config[i].load_action == FBOLOAD_CLEAR)
            GL(glClearBufferfv(GL_COLOR, i, fbo->color_config[i].clear_color));

    if (fbo->layout.depth_buffers || fbo->layout.depth_textures) {
        if (fbo->depth_config.load_action == FBOLOAD_CLEAR) {
            float clear_depth = fbo->depth_config.clear_depth;
            GL(glClearBufferfv(GL_DEPTH, 0, &clear_depth));
        }

        GL(glDepthFunc(gl_depth_func(fbo->depth_config.depth_func)));
        if (fbo->depth_config.depth_func != DEPTH_FN_ALWAYS)    depth_test = true;
    }

    renderer_depth_test(fbo->renderer, depth_test);
}

static void gl_fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
    fbo_invalidate(fbo);
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glViewport(0, 0, width, height));
}

/*
 * attachment:
 * >= 0: color buffer
 *  < 0: depth buffer
 */
static void gl_fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment)
{
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    GLuint gl_attachment;
    GLenum gl_filter;

    if (attachment.depth_buffer) {
        mask = GL_DEPTH_BUFFER_BIT;
        gl_attachment = GL_DEPTH_ATTACHMENT;
        gl_filter = GL_NEAREST;
    } else if (attachment.stencil_buffer) {
        mask = GL_STENCIL_BUFFER_BIT;
        gl_attachment = GL_STENCIL_ATTACHMENT;
        gl_filter = GL_LINEAR;
    } else if (attachment.color_buffers || attachment.color_textures) {
        gl_attachment = fbo_gl_attachment(attachment);
        gl_filter = GL_LINEAR;
    } else {
        return;
    }

    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->gl.fbo));
    GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo->gl.fbo));
    GL(glReadBuffer(gl_attachment));
    GL(glBlitFramebuffer(0, 0, src_fbo->width, src_fbo->height,
                         0, 0, fbo->width, fbo->height,
                         mask, gl_filter));
}

static cerr fbo_make(struct ref *ref, void *_opts)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    fbo->gl.depth_buf = -1;

    return CERR_OK;
}

static void fbo_drop(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    GL(glDeleteFramebuffers(1, &fbo->gl.fbo));
    /* if the texture was cloned, its ->loaded==false making this a nop */
    fa_for_each(fa, fbo->layout, texture)
        texture_deinit(&fbo->color_tex[fbo_attachment_color(fa)]);

    fa_for_each(fa, fbo->layout, buffer)
        glDeleteRenderbuffers(1, (const GLuint *)&fbo->gl.color_buf[fbo_attachment_color(fa)]);

    mem_free(fbo->color_config);

   if (texture_loaded(&fbo->depth_tex))
        texture_deinit(&fbo->depth_tex);

    if (fbo->gl.depth_buf >= 0)
        GL(glDeleteRenderbuffers(1, (GLuint *)&fbo->gl.depth_buf));
}
DEFINE_REFCLASS2(fbo);
DECLARE_REFCLASS(fbo);

/*
 * layout:
 *  FBO_DEPTH_TEXTURE: depth texture
 *  FBO_COLOR_TEXTURE: color texture
 *  FBO_COLOR_BUFFERn: n color buffer attachments
 */
static cerr_check fbo_init(fbo_t *fbo)
{
    cerr err = CERR_OK;

    fbo->gl.fbo = fbo_create();

    if (fbo->layout.depth_texture) {
        err = fbo_depth_texture_init(fbo);
    }
    if (fbo->layout.color_textures) {
        err = fbo_textures_init(fbo, fbo->layout);
    } else if (fbo->layout.color_buffers) {
        /* "<="" meaning "up to and including layout color buffer" */
        fa_for_each(fa, fbo->layout, buffer) {
            cres(int) res = fbo_color_buffer(fbo, fa);
            if (IS_CERR(res))
                goto err;

            fbo->gl.color_buf[fbo_attachment_color(fa)] = res.val;
        }
    }

    if (IS_CERR(err))
        goto err;

    if (fbo->layout.depth_buffer)
        fbo->gl.depth_buf = fbo_depth_buffer(fbo);

    int fb_err = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_err != GL_FRAMEBUFFER_COMPLETE) {
        err("framebuffer status: 0x%04X: %s\n", fb_err, gluErrorString(fb_err));
        err = CERR_FRAMEBUFFER_INCOMPLETE;
    }
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

    return err;

err:
    fa_for_each(fa, fbo->layout, buffer) {
        int color_buf = fbo->gl.color_buf[fbo_attachment_color(fa)];
        if (color_buf)
            glDeleteRenderbuffers(1, (const GLuint *)&color_buf);
    }
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glDeleteFramebuffers(1, &fbo->gl.fbo));

    return err;
}

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
    fbo->layers       = opts->layers;
    fbo->depth_config = opts->depth_config;
    fbo->layout       = opts->layout;
    /* for compatibility */
    if (!fbo->layout.mask)
        fbo->layout.color_texture0 = 1;

    int nr_color_configs = fa_nr_color_buffer(fbo->layout) ? :
                           fa_nr_color_texture(fbo->layout);

    size_t size = nr_color_configs * sizeof(fbo_attconfig);
    fbo->color_config = memdup(opts->color_config ? : &(fbo_attconfig){ .format = TEX_FMT_DEFAULT }, size);

#ifdef CONFIG_GLES
    fbo->nr_samples = 0;
#else
    if (opts->nr_samples)
        fbo->nr_samples = opts->nr_samples;
    else if (opts->multisampled)
        fbo->nr_samples = MSAA_SAMPLES;
#endif

    cerr err = fbo_init(fbo);
    if (IS_CERR(err))
        return cresp_error_cerr(fbo_t, err);

    return cresp_val(fbo_t, NOCU(fbo));
}

/****************************************************************************
 * Binding points
 ****************************************************************************/


/****************************************************************************
 * UBOs
 ****************************************************************************/

static cerr uniform_buffer_make(struct ref *ref, void *_opts)
{
    rc_init_opts(uniform_buffer) *opts = _opts;

    if (opts->binding < 0)
        return CERR_INVALID_ARGUMENTS;

    uniform_buffer_t *ubo = container_of(ref, uniform_buffer_t, ref);

    ubo->gl.binding = opts->binding;
    ubo->dirty   = true;

    GL(glGenBuffers(1, &ubo->gl.id));

    return CERR_OK;
}

static void uniform_buffer_drop(struct ref *ref)
{
    uniform_buffer_t *ubo = container_of(ref, uniform_buffer_t, ref);

    mem_free(ubo->data);
    GL(glDeleteBuffers(1, &ubo->gl.id));
}
DEFINE_REFCLASS2(uniform_buffer);

cerr_check uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name,
                               int binding)
{
    return ref_embed(uniform_buffer, ubo, .binding = binding);
}

void uniform_buffer_done(uniform_buffer_t *ubo)
{
    if (!ref_is_static(&ubo->ref))
        ref_put_last(ubo);
    else
        uniform_buffer_drop(&ubo->ref);
}

cerr_check _uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                               unsigned int count, const void *value);

cerr_check
uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                   unsigned int count, const void *value)
{
    return _uniform_buffer_set(ubo, type, offset, size, count, value);
}

cerr uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size)
{
    /* There's no reason to ever want to reallocate a UBO */
    if (ubo->data || ubo->size)
        return CERR_INVALID_OPERATION;

    /* If a UBO ends on a non-padded scalar, the UBO size needs to be padded */
    size = round_up(size, 16);

    ubo->data = mem_alloc(size, .zero = 1);
    if (!ubo->data)
        return CERR_NOMEM;

    ubo->size = size;

    GL(glBindBuffer(GL_UNIFORM_BUFFER, ubo->gl.id));
    GL(glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_DYNAMIC_DRAW));
    GL(glBindBufferBase(GL_UNIFORM_BUFFER, ubo->gl.binding, ubo->gl.id));
    GL(glBindBuffer(GL_UNIFORM_BUFFER, 0));

    return CERR_OK;
}

cerr uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    if (binding_points->binding < 0)
        return CERR_INVALID_ARGUMENTS;

    if (binding_points->binding >= gl_limits[RENDER_LIMIT_MAX_UBO_BINDINGS])
        return CERR_TOO_LARGE;

    if (unlikely(!ubo->data || !ubo->size))
        return CERR_BUFFER_INCOMPLETE;

    GL(glBindBufferBase(GL_UNIFORM_BUFFER, binding_points->binding, ubo->gl.id));

    return CERR_OK;
}

void uniform_buffer_update(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    if (!ubo->dirty)
        return;

    GL(glBindBuffer(GL_UNIFORM_BUFFER, ubo->gl.id));
    GL(glBufferSubData(GL_UNIFORM_BUFFER, 0, ubo->size, ubo->data));
    GL(glBindBuffer(GL_UNIFORM_BUFFER, 0));

    ubo->dirty = false;
}

cres(size_t) shader_uniform_offset_query(shader_t *shader, const char *ubo_name, const char *var_name)
{
    char fqname[128];
    snprintf(fqname, sizeof(fqname), "%s.%s", ubo_name, var_name);

    GLuint index;
    GL(glGetUniformIndices(shader->gl.prog, 1, (const char *[]) { fqname }, &index));
    if (index == -1u)
        return cres_error(size_t, CERR_NOT_FOUND);

    GLint offset;
    GL(glGetActiveUniformsiv(shader->gl.prog, 1, &index, GL_UNIFORM_OFFSET, &offset));
    return cres_val(size_t, offset);
}

/****************************************************************************
 * Shaders
 ****************************************************************************/

static GLuint load_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader) {
        err("couldn't create shader\n");
        return -1;
    }
    GL(glShaderSource(shader, 1, &source, NULL));
    GL(glCompileShader(shader));
    GLint compiled = 0;
    GL(glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled));
    if (!compiled) {
        GLint infoLen = 0;
        GL(glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen));
        if (infoLen) {
            char *buf = mem_alloc(infoLen);
            if (buf) {
                GL(glGetShaderInfoLog(shader, infoLen, NULL, buf));
                err("Could not Compile Shader %d:\n%s\n", type, buf);
                mem_free(buf);
                err("--> %s <--\n", source);
            }
            GL(glDeleteShader(shader));
            shader = 0;
        }
    }
    return shader;
}

cerr shader_init(renderer_t *r, shader_t *shader, const char *vertex, const char *geometry, const char *fragment)
{
    shader->gl.vert = load_shader(GL_VERTEX_SHADER, vertex);
    shader->gl.geom =
#ifndef CONFIG_GLES
        geometry ? load_shader(GL_GEOMETRY_SHADER, geometry) : 0;
#else
        0;
#endif
    shader->gl.frag = load_shader(GL_FRAGMENT_SHADER, fragment);
    shader->gl.prog = glCreateProgram();
    GLint linkStatus = GL_FALSE;
    cerr ret = CERR_INVALID_SHADER;

    if (!shader->gl.vert || (geometry && !shader->gl.geom) || !shader->gl.frag || !shader->gl.prog) {
        err("vshader: %d gshader: %d fshader: %d program: %d\n",
            shader->gl.vert, shader->gl.geom, shader->gl.frag, shader->gl.prog);
        return ret;
    }

    GL(glAttachShader(shader->gl.prog, shader->gl.vert));
    GL(glAttachShader(shader->gl.prog, shader->gl.frag));
    if (shader->gl.geom)
        GL(glAttachShader(shader->gl.prog, shader->gl.geom));
    GL(glLinkProgram(shader->gl.prog));
    GL(glGetProgramiv(shader->gl.prog, GL_LINK_STATUS, &linkStatus));
    if (linkStatus != GL_TRUE) {
        GLint bufLength = 0;
        GL(glGetProgramiv(shader->gl.prog, GL_INFO_LOG_LENGTH, &bufLength));
        if (bufLength) {
            char *buf = mem_alloc(bufLength);
            if (buf) {
                GL(glGetProgramInfoLog(shader->gl.prog, bufLength, NULL, buf));
                err("Could not link program:\n%s\n", buf);
                mem_free(buf);
                GL(glDeleteShader(shader->gl.vert));
                GL(glDeleteShader(shader->gl.frag));
                if (shader->gl.geom)
                    GL(glDeleteShader(shader->gl.geom));
            }
        }
        GL(glDeleteProgram(shader->gl.prog));
        shader->gl.prog = 0;
    } else {
        ret = CERR_OK;
    }

    return ret;
}

void shader_done(shader_t *shader)
{
    GL(glDeleteProgram(shader->gl.prog));
    GL(glDeleteShader(shader->gl.vert));
    GL(glDeleteShader(shader->gl.frag));
    if (shader->gl.geom)
        GL(glDeleteShader(shader->gl.geom));
}

void shader_set_vertex_attrs(shader_t *shader, size_t stride,
                             size_t *offs, data_type *types, size_t *comp_counts,
                             unsigned int nr_attrs)
{
}

int shader_id(shader_t *shader)
{
    return shader->gl.prog;
}

static cres(int) shader_uniform_block_index(shader_t *shader, const char *name)
{
    GLuint index = glGetUniformBlockIndex(shader->gl.prog, name);
    return index == GL_INVALID_INDEX ? cres_error(int, CERR_INVALID_INDEX) : cres_val(int, index);
}

cerr shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name)
{
    cres(int) res = shader_uniform_block_index(shader, name);
    if (IS_CERR(res))
        return cerr_error_cres(res);

    GL(glUniformBlockBinding(shader->gl.prog, res.val, bpt->binding));

    return CERR_OK;
}

attr_t shader_attribute(shader_t *shader, const char *name, attr_t attr)
{
    return glGetAttribLocation(shader->gl.prog, name);
}

uniform_t shader_uniform(shader_t *shader, const char *name)
{
    return glGetUniformLocation(shader->gl.prog, name);
}

cerr shader_use(shader_t *shader, bool draw)
{
    GL(glUseProgram(shader->gl.prog));
    return CERR_OK;
}

void shader_unuse(shader_t *shader, bool draw)
{
    GL(glUseProgram(0));
}

void uniform_set_ptr(uniform_t uniform, data_type type, unsigned int count, const void *value)
{
    switch (type) {
        case DT_FLOAT: {
            GL(glUniform1fv(uniform, count, value));
            break;
        }
        case DT_INT: {
            GL(glUniform1iv(uniform, count, value));
            break;
        }
        case DT_VEC3: {
            GL(glUniform3fv(uniform, count, value));
            break;
        }
        case DT_VEC4: {
            GL(glUniform4fv(uniform, count, value));
            break;
        }
        case DT_MAT4: {
            GL(glUniformMatrix4fv(uniform, count, GL_FALSE, value));
            break;
        }
        default:
            break;
    }
}

/****************************************************************************
 * Renderer context
 ****************************************************************************/

cerr gl_renderer_setup(renderer_t *renderer, const renderer_init_options *opts)
{
    renderer->ops = &gl_renderer_ops;
    return CERR_OK;
}

static cerr gl_renderer_init(renderer_t *renderer, const renderer_init_options *opts)
{
    static_assert(sizeof(GLint) == sizeof(int), "GLint doesn't match int");
    static_assert(sizeof(GLenum) == sizeof(unsigned int), "GLenum doesn't match unsigned int");
    static_assert(sizeof(GLuint) == sizeof(unsigned int), "GLuint doesn't match unsigned int");
    static_assert(sizeof(GLsizei) == sizeof(int), "GLsizei doesn't match int");
    static_assert(sizeof(GLfloat) == sizeof(float), "GLfloat doesn't match float");
    static_assert(sizeof(GLushort) == sizeof(unsigned short), "GLushort doesn't match unsigned short");
    static_assert(sizeof(GLsizeiptr) == sizeof(ptrdiff_t), "GLsizeiptr doesn't match ptrdiff_t");

    /* Clear the any error state */
    (void)glGetError();

    int i, nr_exts;
    const unsigned char *ext;

    GL(glGetIntegerv(GL_NUM_EXTENSIONS, &nr_exts));
    for (i = 0; i < nr_exts; i++) {
        GL(ext = glGetStringi(GL_EXTENSIONS, i));
        msg("GL extension: '%s'\n", ext);
    }

#ifdef __APPLE__
    const char *renderer_str = (const char *)glGetString(GL_RENDERER);
    /* A quirk to fix frame stutter on mac OS with AMD graphics */
    if (renderer_str && !strncmp(renderer_str, "AMD", 3))
        renderer->gl.mac_amd_quirk = true;
#endif /* __APPLE__ */


    GL(glGetIntegerv(GL_MAX_SAMPLES, &gl_limits[RENDER_LIMIT_MAX_SAMPLES]));
    GL(glGetIntegerv(GL_MAX_DRAW_BUFFERS, &gl_limits[RENDER_LIMIT_MAX_DRAW_BUFFERS]));
    GL(glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_limits[RENDER_LIMIT_MAX_TEXTURE_SIZE]));
    GL(glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &gl_limits[RENDER_LIMIT_MAX_TEXTURE_UNITS]));
    GL(glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &gl_limits[RENDER_LIMIT_MAX_TEXTURE_ARRAY_LAYERS]));
    GL(glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &gl_limits[RENDER_LIMIT_MAX_COLOR_ATTACHMENTS]));
    GL(glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &gl_limits[RENDER_LIMIT_MAX_UBO_SIZE]));
    GL(glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &gl_limits[RENDER_LIMIT_MAX_UBO_BINDINGS]));
    GL(glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &gl_limits[RENDER_LIMIT_MAX_VERTEX_UNIFORM_BLOCKS]));
    GL(glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &gl_limits[RENDER_LIMIT_MAX_FRAGMENT_UNIFORM_BLOCKS]));

#ifdef GL_MAX_GEOMETRY_UNIFORM_BLOCKS
    GL(glGetIntegerv(GL_MAX_GEOMETRY_UNIFORM_BLOCKS, &gl_limits[RENDER_LIMIT_MAX_GEOMETRY_UNIFORM_BLOCKS]));
#endif /* GL_MAX_GEOMETRY_UNIFORM_BLOCKS */

#ifdef GL_MAX_COLOR_TEXTURE_SAMPLES
    GL(glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &gl_limits[RENDER_LIMIT_MAX_COLOR_TEXTURE_SAMPLES]));
#endif /* GL_MAX_COLOR_TEXTURE_SAMPLES */

#ifdef GL_MAX_DEPTH_TEXTURE_SAMPLES
    GL(glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &gl_limits[RENDER_LIMIT_MAX_DEPTH_TEXTURE_SAMPLES]));
#endif /* GL_MAX_DEPTH_TEXTURE_SAMPLES */

#ifdef GL_MAX_TEXTURE_MAX_ANISOTROPY
    GLfloat max_aniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso);
    gl_limits[RENDER_LIMIT_MAX_ANISOTROPY] = (int)max_aniso;
#else
    gl_limits[RENDER_LIMIT_MAX_ANISOTROPY] = 1;
#endif

#ifndef CONFIG_GLES
    GL(glEnable(GL_MULTISAMPLE));
#endif /* CONFIG_GLES */

    GL(glEnable(GL_CULL_FACE));
    GL(glCullFace(GL_BACK));
    renderer_cull_face(renderer, CULL_FACE_BACK);
    GL(glDisable(GL_BLEND));
    renderer_blend(renderer, false, BLEND_NONE, BLEND_NONE);
    GL(glEnable(GL_DEPTH_TEST));
#ifndef EGL_EGL_PROTOTYPES
    GL(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
#endif /* EGL_EGL_PROTOTYPES */

    for (i = 0; i < TEX_FMT_MAX; i++) {
        float buf[4] = {};
        texture_t tex;

        _texture_format_supported[i] = true;

        texture_filter filter = TEX_FLT_LINEAR;
        if (i == TEX_FMT_R32UI || i == TEX_FMT_RG32UI || i == TEX_FMT_RGBA32UI)
            filter = TEX_FLT_NEAREST;

        cerr err = texture_init(&tex, .renderer = renderer, .format = i, .wrap = TEX_CLAMP_TO_EDGE, .min_filter = filter, .mag_filter = filter);
        if (!IS_CERR(err)) {
            err = texture_load(&tex, i, 1, 1, &buf);
            if (IS_CERR(err))
                _texture_format_supported[i] = false;
        }
        texture_deinit(&tex);
    }

    for (i = 0; i < TEX_FMT_MAX; i++) {
        cresp(fbo_t) res;

        _fbo_texture_supported[i] = true;

        if (gl_texture_format(i) == GL_DEPTH_COMPONENT) {
            res = fbo_new(.renderer = renderer, .width = 1, .height = 1, .depth_config.format = i,
                          .layout = FBO_DEPTH_TEXTURE(0));
            if (IS_CERR(res))
                _fbo_texture_supported[i] = false;
            else
                fbo_put(res.val);
        } else {
            res = fbo_new(.renderer = renderer, .width = 1, .height = 1, .layout = FBO_COLOR_TEXTURE(0),
                          .color_config = (fbo_attconfig[]){ { .format = i } });
            if (IS_CERR(res))
                _fbo_texture_supported[i] = false;
            else
                fbo_put(res.val);
        }
    }

    return CERR_OK;
}

#ifndef CONFIG_FINAL
#include "ui-debug.h"

static const char *gl_limit_names[RENDER_LIMIT_MAX] = {
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

static void gl_renderer_debug(renderer_t *r)
{
    debug_module *dbgm = ui_igBegin(DEBUG_RENDERER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
#ifdef CLAP_DEBUG
        igCheckbox("validate GL calls", &validate_gl_calls);
#endif /* CLAP_DEBUG */

        if (igTreeNode_Str("GL limits")) {
            ui_igTableHeader("renderer", (const char *[]){ "limit", "value"}, 2);

            for (int i = 0; i < array_size(gl_limits); i++)
                ui_igTableRow(gl_limit_names[i], "%d", gl_limits[i]);
            igEndTable();
            igTreePop();
        }

        if (igTreeNode_Str("texture color formats")) {
            ui_igTableHeader("color formats", (const char *[]){ "format", "supported"}, 2);

            for (int i = 0; i < array_size(_texture_format_supported); i++)
                ui_igTableRow(texture_format_string(i), "%s",
                              texture_format_supported(i) ? "supported" : "");
            igEndTable();
            igTreePop();
        }

        if (igTreeNode_Str("FBO color formats")) {
            ui_igTableHeader("color formats", (const char *[]){ "format", "supported"}, 2);

            for (int i = 0; i < array_size(_texture_format_supported); i++)
                ui_igTableRow(texture_format_string(i), "%s",
                              fbo_texture_supported(i) ? "supported" : "");
            igEndTable();
            igTreePop();
        }
    }

    ui_igEnd(DEBUG_RENDERER);
}
#endif /* CONFIG_FINAL */

static int gl_renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    if (limit >= RENDER_LIMIT_MAX)
        return 0;

    return gl_limits[limit];
}

static void gl_renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile)
{
    renderer->gl.major      = major;
    renderer->gl.minor      = minor;
    renderer->gl.profile    = profile;
}

static void gl_renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    if (r->x == x && r->y == y && r->width == width && r->height == height)
        return;

    r->x = x;
    r->y = y;
    r->width = width;
    r->height = height;
    GL(glViewport(x, y, width, height));
}

static void gl_renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight)
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

static GLenum gl_cull_face(cull_face cull)
{
    switch (cull) {
        case CULL_FACE_NONE:
            return GL_NONE;
        case CULL_FACE_FRONT:
            return GL_FRONT;
        case CULL_FACE_BACK:
            return GL_BACK;
        default:    break;
    }

    clap_unreachable();

    return GL_NONE;
}

static void gl_renderer_cull_face(renderer_t *r, cull_face cull)
{
    GLenum gl_cull = gl_cull_face(cull);

    if (r->gl.cull_face == gl_cull)
        return;

    r->gl.cull_face = gl_cull;

    if (gl_cull != GL_NONE) {
        GL(glEnable(GL_CULL_FACE));
    } else {
        GL(glDisable(GL_CULL_FACE));
        return;
    }

    GL(glCullFace(r->gl.cull_face));
}

static GLenum gl_blend(blend blend)
{
    switch (blend) {
        case BLEND_NONE:
            return GL_NONE;
        case BLEND_SRC_ALPHA:
            return GL_SRC_ALPHA;
        case BLEND_ONE_MINUS_SRC_ALPHA:
            return GL_ONE_MINUS_SRC_ALPHA;
        default:    break;
    }

    clap_unreachable();

    return GL_NONE;
}

static void gl_renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor)
{
    GLenum _sfactor = gl_blend(sfactor);
    GLenum _dfactor = gl_blend(dfactor);

    if (r->blend == _blend)
        return;

    r->blend = _blend;

    if (_blend) {
        GL(glEnable(GL_BLEND));
    } else {
        GL(glDisable(GL_BLEND));
        return;
    }

    if (r->gl.blend_sfactor != _sfactor || r->gl.blend_dfactor != _dfactor) {
        r->gl.blend_sfactor = _sfactor;
        r->gl.blend_dfactor = _dfactor;
    }
    GL(glBlendFunc(r->gl.blend_sfactor, r->gl.blend_dfactor));
}

static GLenum gl_draw_type(draw_type draw_type)
{
    switch (draw_type) {
        case DRAW_TYPE_POINTS:
            return GL_POINTS;
        case DRAW_TYPE_LINE_STRIP:
            return GL_LINE_STRIP;
        case DRAW_TYPE_LINE_LOOP:
            return GL_LINE_LOOP;
        case DRAW_TYPE_LINES:
            return GL_LINES;
#ifndef CONFIG_GLES
        case DRAW_TYPE_LINE_STRIP_ADJACENCY:
            return GL_LINE_STRIP_ADJACENCY;
        case DRAW_TYPE_LINES_ADJACENCY:
            return GL_LINES_ADJACENCY;
        case DRAW_TYPE_TRIANGLES_ADJACENCY:
            return GL_TRIANGLES_ADJACENCY;
        case DRAW_TYPE_TRIANGLE_STRIP_ADJACENCY:
            return GL_TRIANGLE_STRIP_ADJACENCY;
#endif /* CONFIG_GLES */
        case DRAW_TYPE_TRIANGLE_STRIP:
            return GL_TRIANGLE_STRIP;
        case DRAW_TYPE_TRIANGLE_FAN:
            return GL_TRIANGLE_FAN;
        case DRAW_TYPE_TRIANGLES:
            return GL_TRIANGLES;
        case DRAW_TYPE_PATCHES:
            return GL_PATCHES;
        default:    break;
    }

    clap_unreachable();

    return GL_NONE;
}

static cerr gl_renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces, data_type idx_type,
                             unsigned int nr_instances)
{
    err_on(idx_type >= array_size(gl_comp_type), "invalid draw type %u\n", idx_type);

    GLenum _idx_type = gl_comp_type[idx_type];
    GLenum _draw_type = gl_draw_type(draw_type);
    if (nr_instances <= 1)
        GL(glDrawElements(_draw_type, nr_faces, _idx_type, 0));
    else
        GL(glDrawElementsInstanced(_draw_type, nr_faces, _idx_type, 0, nr_instances));

    /* Fix frame stutter on macOS + AMD (forces frame submission) */
    if (r->gl.mac_amd_quirk)
        GL(glFlush());

    return CERR_OK;
}

static void renderer_depth_func(renderer_t *r, depth_func fn)
{
    GLenum _fn = gl_depth_func(fn);

    if (r->gl.depth_func == _fn)
        return;

    r->gl.depth_func = _fn;
    GL(glDepthFunc(r->gl.depth_func));
}

static void renderer_cleardepth(renderer_t *r, double depth)
{
    if (r->gl.clear_depth == depth)
        return;

    r->gl.clear_depth = depth;
#ifdef CONFIG_GLES
    GL(glClearDepthf((float)r->gl.clear_depth));
#else
    GL(glClearDepth(r->gl.clear_depth));
#endif /* CONFIG_GLES */
}

static void renderer_clear(renderer_t *r, bool color, bool depth, bool stencil)
{
    GLbitfield flags = 0;

    if (color)
        flags |= GL_COLOR_BUFFER_BIT;
    if (depth)
        flags |= GL_DEPTH_BUFFER_BIT;
    if (stencil)
        flags |= GL_STENCIL_BUFFER_BIT;

    GL(glClear(flags));
}

static void gl_renderer_swapchain_begin(renderer_t *renderer)
{
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glViewport(0, 0, renderer->width, renderer->height));
    renderer_cleardepth(renderer, 0.0);
    renderer_clear(renderer, false, true, false);
    renderer_depth_func(renderer, DEPTH_FN_ALWAYS);
    renderer_depth_test(renderer, false);
}
