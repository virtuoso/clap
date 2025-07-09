// SPDX-License-Identifier: Apache-2.0
#include "shader_constants.h"
#include "error.h"
#include "logger.h"
#include "object.h"

#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

#if defined(CONFIG_BROWSER) || !(defined(__glu_h__) || defined(GLU_H))
static inline const char *gluErrorString(int err) { return "not implemented"; }
#endif

#ifdef CLAP_DEBUG
static inline bool __gl_check_error(const char *str)
{
    GLenum err;

    err = glGetError();
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
    [DT_FLOAT]  = GL_FLOAT,
    [DT_IVEC2]  = GL_INT,
    [DT_IVEC3]  = GL_INT,
    [DT_IVEC4]  = GL_INT,
    [DT_VEC2]   = GL_FLOAT,
    [DT_VEC3]   = GL_FLOAT,
    [DT_VEC4]   = GL_FLOAT,
    [DT_MAT2]   = GL_FLOAT,
    [DT_MAT3]   = GL_FLOAT,
    [DT_MAT4]   = GL_FLOAT,
};

static const unsigned int gl_comp_count[] = {
    [DT_BYTE]   = 1,
    [DT_SHORT]  = 1,
    [DT_USHORT] = 1,
    [DT_INT]    = 1,
    [DT_FLOAT]  = 1,
    [DT_IVEC2]  = 2,
    [DT_IVEC3]  = 3,
    [DT_IVEC4]  = 4,
    [DT_VEC2]   = 2,
    [DT_VEC3]   = 3,
    [DT_VEC4]   = 4,
    [DT_MAT2]   = 4,
    [DT_MAT3]   = 9,
    [DT_MAT4]   = 16,
};

static const size_t gl_comp_size[] = {
    [DT_BYTE]   = sizeof(GLbyte),
    [DT_SHORT]  = sizeof(GLshort),
    [DT_USHORT] = sizeof(GLushort),
    [DT_INT]    = sizeof(GLint),
    [DT_FLOAT]  = sizeof(GLfloat),
    [DT_IVEC2]  = sizeof(GLint),
    [DT_IVEC3]  = sizeof(GLint),
    [DT_IVEC4]  = sizeof(GLint),
    [DT_VEC2]   = sizeof(GLfloat),
    [DT_VEC3]   = sizeof(GLfloat),
    [DT_VEC4]   = sizeof(GLfloat),
    [DT_MAT2]   = sizeof(GLfloat),
    [DT_MAT3]   = sizeof(GLfloat),
    [DT_MAT4]   = sizeof(GLfloat),
};

size_t data_type_size(data_type type)
{
    if (unlikely(type >= array_size(gl_comp_size)))
        return 0;

    return gl_comp_size[type] * gl_comp_count[type];
}

/* Get std140 storage size for a given type */
static inline size_t gl_type_storage_size(data_type type)
{
    size_t storage_size = data_type_size(type);
    if (!storage_size) {
        clap_unreachable();
        return 0;
    }

    /* Matrices are basically arrays of vec4 (technically, vecN padded to 16 bytes) */
    switch (type) {
        case DT_MAT2:
            storage_size = 2 * data_type_size(DT_VEC4); /* vec2 padded to vec4 */
            break;
        case DT_MAT3:
            storage_size = 3 * data_type_size(DT_VEC4); /* vec3 padded to vec4 */
            break;
        /* And what's left is mat4, which is perfect as it is */
        default:
            /*
             * Scalars are not padded to 16 bytes unless they are followed by
             * compound types, which we don't know about here; instead,
             * uniform_buffer_set() handles the std140 compliant offset alignment.
             */
            break;
    }

    return storage_size;
}

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

    unsigned int comp_count = opts->comp_count;
    if (!comp_count)
        comp_count = gl_comp_count[comp_type];

    buf->type = gl_buffer_type(opts->type);
    buf->usage = gl_buffer_usage(opts->usage);
    buf->comp_type = gl_comp_type[comp_type];
    buf->comp_count = comp_count;

    if (opts->data && opts->size)
        buffer_load(buf, opts->data, opts->size,
                    buf->type == GL_ELEMENT_ARRAY_BUFFER ? -1 : 0);

#ifndef CONFIG_FINAL
    memcpy(&buf->opts, opts, sizeof(*opts));
#endif /* CONFIG_FINAL */

    return CERR_OK;
}

#ifndef CONFIG_FINAL
#include "ui-debug.h"

static const char *buffer_type_str(buffer_type type)
{
    switch (type) {
        case BUF_ARRAY:         return "array";
        case BUF_ELEMENT_ARRAY: return "element array";
        default:                break;
    }

    return "<invalid type>";
};

static const char *buffer_usage_str(buffer_usage usage)
{
    switch (usage) {
        case BUF_STATIC:        return "static";
        case BUF_DYNAMIC:       return "dynamic";
        default:                break;
    }

    return "<invalid usage>";
};

static const char *data_type_str[] = {
    [DT_BYTE]   = "byte",
    [DT_SHORT]  = "short",
    [DT_USHORT] = "ushort",
    [DT_INT]    = "int",
    [DT_FLOAT]  = "float",
    [DT_IVEC2]  = "ivec2",
    [DT_IVEC3]  = "ivec3",
    [DT_IVEC4]  = "ivec4",
    [DT_VEC2]   = "vec2",
    [DT_VEC3]   = "vec3",
    [DT_VEC4]   = "vec4",
    [DT_MAT2]   = "mat2",
    [DT_MAT3]   = "mat3",
    [DT_MAT4]   = "mat4",
};

void buffer_debug_header(void)
{
    ui_igTableHeader(
        "buffers",
        (const char *[]) { "attribute", "binding", "size", "type", "usage", "comp" },
        6
    );
}

void buffer_debug(buffer_t *buf, const char *name)
{
    buffer_init_options *opts = &buf->opts;
    size_t comp_size = gl_comp_size[opts->comp_type];
    int comp_count = opts->comp_count ? : 1;

    ui_igTableCell(true, "%s", name);
    ui_igTableCell(false, "%d", buf->loc);
    ui_igTableCell(false, "%zu", opts->size);
    ui_igTooltip(
        "elements: %zu\ncomponents: %zu",
        opts->size / comp_size,
        opts->size / (comp_size * comp_count)
    );
    ui_igTableCell(false, "%s", buffer_type_str(opts->type));
    ui_igTableCell(false, "%s", buffer_usage_str(opts->usage));
    ui_igTableCell(false, "%s (%d) x %d",
                   data_type_str[opts->comp_type],
                   gl_comp_size[opts->comp_type],
                   opts->comp_count);
}
#endif /* CONFIG_FINAL */

void buffer_deinit(buffer_t *buf)
{
    if (!buf->loaded)
        return;
    GL(glDeleteBuffers(1, &buf->id));
    buf->loaded = false;
}

static inline void _buffer_bind(buffer_t *buf, uniform_t loc)
{
    GL(glVertexAttribPointer(loc, buf->comp_count, buf->comp_type, GL_FALSE, 0, (void *)0));
}

void buffer_bind(buffer_t *buf, uniform_t loc)
{
    if (!buf->loaded)
        return;

    GL(glBindBuffer(buf->type, buf->id));

#ifndef CONFIG_FINAL
    buf->loc = loc;
#endif /* CONFIG_FINAL */

    if (loc < 0)
        return;

    _buffer_bind(buf, loc);
    GL(glEnableVertexAttribArray(loc));
}

void buffer_unbind(buffer_t *buf, uniform_t loc)
{
    if (!buf->loaded)
        return;

    GL(glBindBuffer(buf->type, 0));
    if (loc < 0)
        return;

    GL(glDisableVertexAttribArray(loc));
}

void buffer_load(buffer_t *buf, void *data, size_t sz, uniform_t loc)
{
    GL(glGenBuffers(1, &buf->id));
    GL(glBindBuffer(buf->type, buf->id));
    GL(glBufferData(buf->type, sz, data, buf->usage));
#ifndef CONFIG_FINAL
    buf->opts.size = sz;
    buf->loc = loc;
#endif /* CONFIG_FINAL */

    if (loc >= 0)
        _buffer_bind(buf, loc);

    GL(glBindBuffer(buf->type, 0));
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

DEFINE_REFCLASS_INIT_OPTIONS(vertex_array);
DEFINE_REFCLASS(vertex_array);

cerr vertex_array_init(vertex_array_t *va)
{
    cerr err = ref_embed(vertex_array, va);
    if (IS_CERR(err))
        return err;

    if (!gl_does_vao())
        return CERR_OK;

    GL(glGenVertexArrays(1, &va->vao));
    GL(glBindVertexArray(va->vao));

    return CERR_OK;
}

void vertex_array_done(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glDeleteVertexArrays(1, &va->vao));
}

void vertex_array_bind(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glBindVertexArray(va->vao));
}

void vertex_array_unbind(vertex_array_t *va)
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

static __unused const char *texture_format_string[TEX_FMT_MAX] = {
        [TEX_FMT_R8]        = "R8",
        [TEX_FMT_R16F]      = "R16F",
        [TEX_FMT_R32F]      = "R32F",
        [TEX_FMT_RG8]       = "RG8",
        [TEX_FMT_RG16F]     = "RG16F",
        [TEX_FMT_RG32F]     = "RG32F",
        [TEX_FMT_RGBA8]     = "RGBA8",
        [TEX_FMT_RGB8]      = "RGB8",
        [TEX_FMT_RGBA16F]   = "RGBA16F",
        [TEX_FMT_RGB16F]    = "RGB16F",
        [TEX_FMT_RGBA32F]   = "RGBA32F",
        [TEX_FMT_RGB32F]    = "RGB32F",
        [TEX_FMT_DEPTH32F]  = "DEPTH32F",
        [TEX_FMT_DEPTH24F]  = "DEPTH24F",
        [TEX_FMT_DEPTH16F]  = "DEPTH16F",
};

static GLenum gl_texture_format(texture_format format)
{
    switch (format) {
        case TEX_FMT_R32F:
        case TEX_FMT_R16F:
        case TEX_FMT_R8:        return GL_RED;
        case TEX_FMT_RG32F:
        case TEX_FMT_RG16F:
        case TEX_FMT_RG8:       return GL_RG;
        case TEX_FMT_RGBA32F:
        case TEX_FMT_RGBA16F:
        case TEX_FMT_RGBA8:     return GL_RGBA;
        case TEX_FMT_RGB32F:
        case TEX_FMT_RGB16F:
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
        case TEX_FMT_RGBA32F:   return GL_RGBA32F;
        case TEX_FMT_RGBA16F:   return GL_RGBA16F;
        case TEX_FMT_RGB32F:    return GL_RGB32F;
        case TEX_FMT_RGB16F:    return GL_RGB16F;
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
        case TEX_FMT_RGBA8:     return GL_UNSIGNED_BYTE;
        case TEX_FMT_R16F:
        case TEX_FMT_RG16F:
        case TEX_FMT_RGB16F:
#ifdef CONFIG_GLES
        case TEX_FMT_RGBA16F:   return GL_UNSIGNED_SHORT;
#else
        case TEX_FMT_RGBA16F:   return GL_HALF_FLOAT;
#endif /* CONFIG_GLES */
        case TEX_FMT_R32F:
        case TEX_FMT_RG32F:
        case TEX_FMT_RGB32F:
        case TEX_FMT_RGBA32F:   return GL_FLOAT;
        case TEX_FMT_DEPTH32F:  return GL_FLOAT;
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

bool texture_is_array(texture_t *tex)
{
    return tex->type == GL_TEXTURE_2D_ARRAY ||
           tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
}

bool texture_is_multisampled(texture_t *tex)
{
    return tex->multisampled;
}

cerr _texture_init(texture_t *tex, const texture_init_options *opts)
{
    bool multisampled   = opts->multisampled;
#ifdef CONFIG_GLES
    multisampled  = false;
#endif /* CONFIG_GLES */

    if (!texture_format_supported(opts->format))
        return CERR_NOT_SUPPORTED;

    cerr err = ref_embed(texture, tex);
    if (IS_CERR(err))
        return err;

    tex->component_type     = gl_texture_component_type(opts->format);
    tex->wrap               = gl_texture_wrap(opts->wrap);
    tex->min_filter         = gl_texture_filter(opts->min_filter);
    tex->mag_filter         = gl_texture_filter(opts->mag_filter);
    tex->target             = GL_TEXTURE0 + opts->target;
    tex->type               = gl_texture_type(opts->type, multisampled);
    tex->format             = gl_texture_format(opts->format);
    tex->internal_format    = gl_texture_internal_format(opts->format);
    tex->layers             = opts->layers;
    tex->multisampled       = multisampled;
    if (opts->border)
        memcpy(tex->border, opts->border, sizeof(tex->border));
    GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    GL(glActiveTexture(tex->target));
    GL(glGenTextures(1, &tex->id));

#ifndef CONFIG_FINAL
    memcpy(&tex->opts, opts, sizeof(*opts));
#endif /* CONFIG_FINAL */

    return CERR_OK;
}

#ifndef CONFIG_FINAL
static const char *texture_type_str[] = {
    [TEX_2D]        = "2D",
    [TEX_2D_ARRAY]  = "2D array",
    [TEX_3D]        = "3D",
};

static const char *texture_wrap_str[] = {
    [TEX_CLAMP_TO_EDGE]         = "clamp edge",
    [TEX_CLAMP_TO_BORDER]       = "clamp border",
    [TEX_WRAP_REPEAT]           = "repeat",
    [TEX_WRAP_MIRRORED_REPEAT]  = "mirrored repeat",
};

static const char *texture_filter_str[] = {
    [TEX_FLT_LINEAR]    = "linear",
    [TEX_FLT_NEAREST]   = "nearest",
};

void texture_debug_header(void)
{
    ui_igTableHeader(
        "buffers",
        (const char *[]) { "name", "type", "format", "size", "wrap", "min", "mag", "ms" },
        8
    );
}

void texture_debug(texture_t *tex, const char *name)
{
    texture_init_options *opts = &tex->opts;

    ui_igTableCell(true, name);
    ui_igTableCell(false, texture_type_str[opts->type]);
    ui_igTableCell(false, texture_format_string[opts->format]);
    if (tex->layers)
        ui_igTableCell(false, "%d x %d x %d", tex->width, tex->height, tex->layers);
    else
        ui_igTableCell(false, "%d x %d", tex->width, tex->height);
    ui_igTableCell(false, texture_wrap_str[opts->wrap]);
    ui_igTableCell(false, texture_filter_str[opts->min_filter]);
    ui_igTableCell(false, texture_filter_str[opts->mag_filter]);
    ui_igTableCell(false, "%s", tex->multisampled ? "ms" : "");
}
#endif /* CONFIG_FINAL */

texture_t *texture_clone(texture_t *tex)
{
    texture_t *ret = ref_new(texture);

    if (ret) {
        ret->id                 = tex->id;
        ret->wrap               = tex->wrap;
        ret->component_type     = tex->component_type;
        ret->type               = tex->type;
        ret->target             = tex->target;
        ret->min_filter         = tex->min_filter;
        ret->mag_filter         = tex->mag_filter;
        ret->width              = tex->width;
        ret->height             = tex->height;
        ret->layers             = tex->layers;
        ret->format             = tex->format;
        ret->internal_format    = tex->internal_format;
        ret->loaded             = tex->loaded;
        tex->loaded             = false;
    }

    return ret;
}

void texture_deinit(texture_t *tex)
{
    if (!tex->loaded)
        return;
    GL(glDeleteTextures(1, &tex->id));
    tex->loaded = false;
}

static cerr texture_storage(texture_t *tex, void *buf)
{
    if (tex->type == GL_TEXTURE_2D)
        glTexImage2D(tex->type, 0, tex->internal_format, tex->width, tex->height,
                     0, tex->format, tex->component_type, buf);
    else if (tex->type == GL_TEXTURE_2D_ARRAY || tex->type == GL_TEXTURE_3D)
        glTexImage3D(tex->type, 0, tex->internal_format, tex->width, tex->height, tex->layers,
                     0, tex->format, tex->component_type, buf);
#ifdef CONFIG_GLES
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE ||
             tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
         return CERR_NOT_SUPPORTED;
#else
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE)
        glTexImage2DMultisample(tex->type, 4, tex->internal_format, tex->width, tex->height,
                                GL_TRUE);
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        glTexImage3DMultisample(tex->type, 4, tex->internal_format, tex->width, tex->height,
                                tex->layers, GL_TRUE);
#endif /* CONFIG_GLES */
    if (glGetError() != GL_NO_ERROR)
        return CERR_NOT_SUPPORTED;

    return CERR_OK;
}

static bool texture_size_valid(unsigned int width, unsigned int height)
{
    return width < gl_limits[RENDER_LIMIT_MAX_TEXTURE_SIZE] &&
           height < gl_limits[RENDER_LIMIT_MAX_TEXTURE_SIZE];
}

cerr texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!tex->loaded)
        return CERR_TEXTURE_NOT_LOADED;

    if (tex->width == width && tex->height == height)
        return CERR_OK;

    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE;

    tex->width = width;
    tex->height = height;
    GL(glBindTexture(tex->type, tex->id));
    cerr err = texture_storage(tex, NULL);
    GL(glBindTexture(tex->type, 0));

    return err;
}

static cerr texture_setup_begin(texture_t *tex, void *buf)
{
    GL(glActiveTexture(tex->target));
    GL(glBindTexture(tex->type, tex->id));
    if (tex->type != GL_TEXTURE_2D_MULTISAMPLE &&
        tex->type != GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        GL(glTexParameteri(tex->type, GL_TEXTURE_WRAP_S, tex->wrap));
        GL(glTexParameteri(tex->type, GL_TEXTURE_WRAP_T, tex->wrap));
        if (tex->type == TEX_3D)
            GL(glTexParameteri(tex->type, GL_TEXTURE_WRAP_R, tex->wrap));
        GL(glTexParameteri(tex->type, GL_TEXTURE_MIN_FILTER, tex->min_filter));
        GL(glTexParameteri(tex->type, GL_TEXTURE_MAG_FILTER, tex->mag_filter));
#ifndef CONFIG_GLES
        if (tex->wrap == GL_CLAMP_TO_BORDER)
            GL(glTexParameterfv(tex->type, GL_TEXTURE_BORDER_COLOR, tex->border));
#endif /* CONFIG_GLES */
    }
    return texture_storage(tex, buf);
}

static void texture_setup_end(texture_t *tex)
{
    GL(glBindTexture(tex->type, 0));
}

cerr_check texture_load(texture_t *tex, texture_format format,
                        unsigned int width, unsigned int height, void *buf)
{
    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE;

    if (!texture_format_supported(format))
        return CERR_NOT_SUPPORTED;

    GLuint gl_format = gl_texture_format(format);

    if (gl_format != tex->format) {
        tex->format             = gl_texture_format(format);
        tex->internal_format    = gl_texture_internal_format(format);
        tex->component_type     = gl_texture_component_type(format);
    }
    tex->width  = width;
    tex->height = height;

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
        return CERR_INVALID_TEXTURE_SIZE;

    if (!fbo_texture_supported(format))
        return CERR_NOT_SUPPORTED;

    GLuint gl_format = gl_texture_format(format);

    if (gl_format != tex->format) {
        tex->format             = gl_texture_format(format);
        tex->internal_format    = gl_texture_internal_format(format);
        tex->component_type     = gl_texture_component_type(format);
    }
    tex->width  = width;
    tex->height = height;

    cerr err = texture_setup_begin(tex, NULL);
    if (IS_CERR(err))
        return err;

    if (tex->type == GL_TEXTURE_2D ||
        tex->type == GL_TEXTURE_2D_MULTISAMPLE)
        GL(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, tex->type, tex->id, 0));
#ifdef CONFIG_GLES
    else
        return CERR_NOT_SUPPORTED;
#else
    else if (tex->type == GL_TEXTURE_2D_ARRAY ||
             tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        GL(glFramebufferTexture(GL_FRAMEBUFFER, attachment, tex->id, 0));
    else
        GL(glFramebufferTexture3D(GL_FRAMEBUFFER, attachment, tex->type, tex->id, 0, 0));
#endif
    texture_setup_end(tex);
    tex->loaded = true;

    return CERR_OK;
}

void texture_bind(texture_t *tex, unsigned int target)
{
    /* XXX: make tex->target useful for this instead */
    GL(glActiveTexture(GL_TEXTURE0 + target));
    GL(glBindTexture(tex->type, tex->id));
}

void texture_unbind(texture_t *tex, unsigned int target)
{
    /* XXX: make tex->target useful for this instead */
    GL(glActiveTexture(GL_TEXTURE0 + target));
    GL(glBindTexture(tex->type, 0));
}

void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight)
{
    *pwidth = tex->width;
    *pheight = tex->height;
}

void texture_done(struct texture *tex)
{
    if (!ref_is_static(&tex->ref))
        ref_put_last(tex);
}

texid_t texture_id(struct texture *tex)
{
    if (!tex)
        return 0;
    return tex->id;
}

bool texture_loaded(struct texture *tex)
{
    return tex->loaded;
}

cerr_check texture_pixel_init(texture_t *tex, float color[4])
{
    cerr err = texture_init(tex);
    if (IS_CERR(err))
        return err;

    uint8_t _color[4] = { color[0] * 255, color[1] * 255, color[2] * 255, color[3] * 255 };
    return texture_load(tex, TEX_FMT_RGBA8, 1, 1, _color);
}

static texture_t _white_pixel;
static texture_t _black_pixel;
static texture_t _transparent_pixel;

texture_t *white_pixel(void) { return &_white_pixel; }
texture_t *black_pixel(void) { return &_black_pixel; }
texture_t *transparent_pixel(void) { return &_transparent_pixel; }

void textures_init(void)
{
    cerr werr, berr, terr;

    float white[] = { 1, 1, 1, 1 };
    werr = texture_pixel_init(&_white_pixel, white);
    float black[] = { 0, 0, 0, 1 };
    berr = texture_pixel_init(&_black_pixel, black);
    float transparent[] = { 0, 0, 0, 0 };
    terr = texture_pixel_init(&_transparent_pixel, transparent);

    err_on(IS_CERR(werr) || IS_CERR(berr) || IS_CERR(terr), "failed: %d/%d/%d\n",
           CERR_CODE(werr), CERR_CODE(berr), CERR_CODE(terr));
}

void textures_done(void)
{
    texture_done(&_white_pixel);
    texture_done(&_black_pixel);
    texture_done(&_transparent_pixel);
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
                            .type          = fbo->layers ? TEX_2D_ARRAY : TEX_2D,
                            .layers        = fbo->layers,
                            .format        = fbo->depth_format,
                            .multisampled  = fbo_is_multisampled(fbo),
                            .wrap          = TEX_CLAMP_TO_BORDER,
                            .border        = border,
                            .min_filter    = TEX_FLT_NEAREST,
                            .mag_filter    = TEX_FLT_NEAREST);
    if (IS_CERR(err))
        return err;

    err = texture_fbo(&fbo->depth_tex, GL_DEPTH_ATTACHMENT, fbo->depth_format,
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
    if (idx >= fa_nr_color_texture(fbo->attachment_config))
        return NULL;

    return &fbo->color_tex[idx];
}

bool fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment)
{
    int tidx = fbo_attachment_color(attachment);
    if (tidx >= 0 && tidx <= fbo_attachment_color(fbo->attachment_config) &&
        texture_loaded(&fbo->color_tex[tidx]))
        return true;

    int bidx = fbo_attachment_color(attachment);
    if (bidx >= 0 && bidx <= fbo_attachment_color(fbo->attachment_config) &&
        fbo->color_buf[bidx])
        return true;

    if (attachment.depth_texture && fbo->attachment_config.depth_texture)
        return true;

    if (attachment.depth_buffer && fbo->attachment_config.depth_buffer)
        return true;

    return false;
}

texture_format fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo_attachment_valid(fbo, attachment)) {
        err("invalid attachment '%s'\n", fbo_attachment_string(attachment));
        return TEX_FMT_MAX;
    }

    return fbo->color_format[fbo_attachment_color(attachment)];
}

int fbo_width(fbo_t *fbo)
{
    return fbo->width;
}

int fbo_height(fbo_t *fbo)
{
    return fbo->height;
}

fbo_attachment_type fbo_get_attachment(fbo_t *fbo)
{
    if (fbo_attachment_color(fbo->attachment_config))
        return FBO_ATTACHMENT_COLOR0;

    if (fbo->attachment_config.depth_buffer)
        return FBO_ATTACHMENT_DEPTH;

    if (fbo->attachment_config.stencil_buffer)
        return FBO_ATTACHMENT_STENCIL;

    clap_unreachable();

    return GL_NONE;
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
    GLenum gl_internal_format = gl_texture_internal_format(fbo->depth_format);
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

cerr_check fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo)
        return CERR_INVALID_ARGUMENTS;

    GL(glFinish());

    cerr err = CERR_OK;
    fa_for_each(fa, fbo->attachment_config, texture) {
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

    fa_for_each(fa, fbo->attachment_config, buffer) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, fbo->color_buf[fbo_attachment_color(fa)]));
        __fbo_color_buffer_setup(fbo, fa);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    if (fbo->depth_buf >= 0) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, fbo->depth_buf));
        __fbo_depth_buffer_setup(fbo);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    return CERR_OK;
}

#define NR_TARGETS FBO_COLOR_ATTACHMENTS_MAX
void fbo_prepare(fbo_t *fbo)
{
    GLenum buffers[NR_TARGETS];
    int target = 0;

    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo));
    GL(glViewport(0, 0, fbo->width, fbo->height));

    if (fbo_attachment_is_buffers(fbo->attachment_config)) {
        fa_for_each(fa, fbo->attachment_config, buffer)
            buffers[target++] = fbo_gl_attachment(fa);
    } else {
        fa_for_each(fa, fbo->attachment_config, texture)
            buffers[target++] = fbo_gl_attachment(fa);
    }

    if (!target) {
        buffers[0] = GL_NONE;
        GL(glDrawBuffers(1, buffers));
        GL(glReadBuffer(GL_NONE));
    }

    GL(glDrawBuffers(target, buffers));
}

void fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glViewport(0, 0, width, height));
}

/*
 * attachment:
 * >= 0: color buffer
 *  < 0: depth buffer
 */
void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment)
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

    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->fbo));
    GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo->fbo));
    GL(glReadBuffer(gl_attachment));
    GL(glBlitFramebuffer(0, 0, src_fbo->width, src_fbo->height,
                         0, 0, fbo->width, fbo->height,
                         mask, gl_filter));
}

static cerr fbo_make(struct ref *ref, void *_opts)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    fbo->depth_buf = -1;

    return CERR_OK;
}

static void fbo_drop(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    GL(glDeleteFramebuffers(1, &fbo->fbo));
    /* if the texture was cloned, its ->loaded==false making this a nop */
    fa_for_each(fa, fbo->attachment_config, texture)
        texture_deinit(&fbo->color_tex[fbo_attachment_color(fa)]);

    fa_for_each(fa, fbo->attachment_config, buffer)
        glDeleteRenderbuffers(1, (const GLuint *)&fbo->color_buf[fbo_attachment_color(fa)]);

    mem_free(fbo->color_format);

   if (texture_loaded(&fbo->depth_tex))
        texture_deinit(&fbo->depth_tex);

    if (fbo->depth_buf >= 0)
        GL(glDeleteRenderbuffers(1, (GLuint *)&fbo->depth_buf));
}
DEFINE_REFCLASS2(fbo);
DECLARE_REFCLASS(fbo);

/*
 * attachment_config:
 *  FBO_DEPTH_TEXTURE: depth texture
 *  FBO_COLOR_TEXTURE: color texture
 *  FBO_COLOR_BUFFERn: n color buffer attachments
 */
static cerr_check fbo_init(fbo_t *fbo)
{
    cerr err = CERR_OK;

    fbo->fbo = fbo_create();

    if (fbo->attachment_config.depth_texture) {
        err = fbo_depth_texture_init(fbo);
    }
    if (fbo->attachment_config.color_textures) {
        err = fbo_textures_init(fbo, fbo->attachment_config);
    } else if (fbo->attachment_config.color_buffers) {
        /* "<="" meaning "up to and including attachment_config color buffer"*/
        fa_for_each(fa, fbo->attachment_config, buffer) {
            cres(int) res = fbo_color_buffer(fbo, fa);
            if (IS_CERR(res))
                goto err;

            fbo->color_buf[fbo_attachment_color(fa)] = res.val;
        }
    }

    if (IS_CERR(err))
        goto err;

    if (fbo->attachment_config.depth_buffer)
        fbo->depth_buf = fbo_depth_buffer(fbo);

    int fb_err = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_err != GL_FRAMEBUFFER_COMPLETE) {
        err("framebuffer status: 0x%04X: %s\n", fb_err, gluErrorString(fb_err));
        err = CERR_FRAMEBUFFER_INCOMPLETE;
    }
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

    return err;

err:
    fa_for_each(fa, fbo->attachment_config, buffer) {
        int color_buf = fbo->color_buf[fbo_attachment_color(fa)];
        if (color_buf)
            glDeleteRenderbuffers(1, (const GLuint *)&color_buf);
    }
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glDeleteFramebuffers(1, &fbo->fbo));

    return err;
}

bool fbo_is_multisampled(fbo_t *fbo)
{
    return !!fbo->nr_samples;
}

DEFINE_CLEANUP(fbo_t, if (*p) ref_put(*p))

must_check cresp(fbo_t) _fbo_new(const fbo_init_options *opts)
{
    if (!opts->width || !opts->height)
        return cresp_error(fbo_t, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(fbo_t, fbo) = ref_new(fbo);
    if (!fbo)
        return cresp_error(fbo_t, CERR_NOMEM);

    fbo->width        = opts->width;
    fbo->height       = opts->height;
    fbo->layers       = opts->layers;
    fbo->depth_format = opts->depth_format ? : TEX_FMT_DEPTH32F;
    fbo->attachment_config = opts->attachment_config;
    /* for compatibility */
    if (!fbo->attachment_config.mask)
        fbo->attachment_config.color_texture0 = 1;

    int nr_color_formats = fa_nr_color_buffer(fbo->attachment_config) ? :
                           fa_nr_color_texture(fbo->attachment_config);

    size_t size = nr_color_formats * sizeof(texture_format);
    fbo->color_format = memdup(opts->color_format ? : (texture_format[]){ TEX_FMT_DEFAULT }, size);

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

/****************************************************************************
 * Binding points
 ****************************************************************************/

void binding_points_init(binding_points_t *bps)
{
    bps->binding = -1;
}

void binding_points_done(binding_points_t *bps)
{
    bps->binding = -1;
}

void binding_points_add(binding_points_t *bps, shader_stage stage, int binding)
{
    bps->binding = binding;
}

/****************************************************************************
 * UBOs
 ****************************************************************************/

static cerr uniform_buffer_make(struct ref *ref, void *_opts)
{
    rc_init_opts(uniform_buffer) *opts = _opts;

    if (opts->binding < 0)
        return CERR_INVALID_ARGUMENTS;

    uniform_buffer_t *ubo = container_of(ref, uniform_buffer_t, ref);

    ubo->binding = opts->binding;
    ubo->dirty   = true;

    GL(glGenBuffers(1, &ubo->id));

    return CERR_OK;
}

static void uniform_buffer_drop(struct ref *ref)
{
    uniform_buffer_t *ubo = container_of(ref, uniform_buffer_t, ref);

    mem_free(ubo->data);
    GL(glDeleteBuffers(1, &ubo->id));
}
DEFINE_REFCLASS2(uniform_buffer);

cerr_check uniform_buffer_init(uniform_buffer_t *ubo, int binding)
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

    GL(glBindBuffer(GL_UNIFORM_BUFFER, ubo->id));
    GL(glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_DYNAMIC_DRAW));
    GL(glBindBufferBase(GL_UNIFORM_BUFFER, ubo->binding, ubo->id));
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

    GL(glBindBufferBase(GL_UNIFORM_BUFFER, binding_points->binding, ubo->id));

    return CERR_OK;
}

void uniform_buffer_update(uniform_buffer_t *ubo)
{
    if (!ubo->dirty)
        return;

    GL(glBindBuffer(GL_UNIFORM_BUFFER, ubo->id));
    GL(glBufferSubData(GL_UNIFORM_BUFFER, 0, ubo->size, ubo->data));
    GL(glBindBuffer(GL_UNIFORM_BUFFER, 0));

    ubo->dirty = false;
}

cres(size_t) shader_uniform_offset_query(shader_t *shader, const char *ubo_name, const char *var_name)
{
    char fqname[128];
    snprintf(fqname, sizeof(fqname), "%s.%s", ubo_name, var_name);

    GLuint index;
    GL(glGetUniformIndices(shader->prog, 1, (const char *[]) { fqname }, &index));
    if (index == -1u)
        return cres_error(size_t, CERR_NOT_FOUND);

    GLint offset;
    GL(glGetActiveUniformsiv(shader->prog, 1, &index, GL_UNIFORM_OFFSET, &offset));
    return cres_val(size_t, offset);
}

/*
 * Calculate uniform @offset within a UBO, its total @size and set its @value
 * if @value is not NULL
 */
cerr uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                        unsigned int count, const void *value)
{
    size_t elem_size = data_type_size(type);          /* C ABI element size */
    size_t storage_size = gl_type_storage_size(type); /* std140-aligned size */
    bool dirty = ubo->dirty;
    cerr err = CERR_OK;

    if (!elem_size || !storage_size)
        return CERR_INVALID_ARGUMENTS;

    /* These are only used if @value is set */
    const char *src = (const char *)value;
    char *dst = src ? (char *)ubo->data + *offset : NULL;

    *offset = *size;

    /*
     * Individual scalars are *not* padded to 16 bytes, unless they're in
     * an array; compound types are aligned to a 16 bype boundary even if
     * they follow non-padded scalars
     */
    if (storage_size < 16 && count > 1)
        storage_size = 16;

    /*
     * non-arrayed scalars have maximum storage size of 4, if something
     * larger is following a non-padded offset, it needs to be padded first
     */
    if (storage_size > 4 && *offset % 16)
        *offset = round_up(*offset, 16);

    *size = *offset;

    /*
     * Copy elements from a C array to std140 array. Because unlike C, std140
     * uses fun alignments for various types that don't match C at all, they
     * need to be copied one at a time.
     */
    for (unsigned int i = 0; i < count; i++) {
        if (value) {
            /* If we overshoot, the buffer is still dirty, return straight away */
            if (*size + storage_size > ubo->size) {
                err = CERR_BUFFER_OVERRUN;
                goto out;
            }

            if (type == DT_MAT2 || type == DT_MAT3) {
                /* Manually copy row-by-row with padding */
                bool is_mat3 = type == DT_MAT2;
                int rows = is_mat3 ? 3 : 2;
                /* Source row: DT_VEC2 or DT_VEC3 */
                size_t row_size = data_type_size(is_mat3 ? DT_VEC3 : DT_VEC2);
                /* Destination row: DT_VEC4 */
                size_t row_stride = gl_type_storage_size(DT_VEC4);

                for (int r = 0; r < rows; r++) {
                    if (memcmp(dst, src, row_size)) {
                        memcpy(dst, src, row_size);
                        dirty = true;
                    }
                    src += row_size;             /* Move to next row */
                    dst += row_stride;           /* Move to next aligned row */
                }
            } else {
                /* Only update the target value if it changed */
                if (memcmp(dst, src, elem_size)) {
                    /* Copy only valid bytes */
                    memcpy(dst, src, elem_size);
                    dirty = true;
                }
                src += elem_size;            /* Move to next element (C ABI aligned) */
                dst += storage_size;         /* Move to next element (std140 aligned) */
            }
        }
        *size += storage_size;
    }

out:
    ubo->dirty = dirty;

    return err;
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

cerr shader_init(shader_t *shader, const char *vertex, const char *geometry, const char *fragment)
{
    shader->vert = load_shader(GL_VERTEX_SHADER, vertex);
    shader->geom =
#ifndef CONFIG_BROWSER
        geometry ? load_shader(GL_GEOMETRY_SHADER, geometry) : 0;
#else
        0;
#endif
    shader->frag = load_shader(GL_FRAGMENT_SHADER, fragment);
    shader->prog = glCreateProgram();
    GLint linkStatus = GL_FALSE;
    cerr ret = CERR_INVALID_SHADER;

    if (!shader->vert || (geometry && !shader->geom) || !shader->frag || !shader->prog) {
        err("vshader: %d gshader: %d fshader: %d program: %d\n",
            shader->vert, shader->geom, shader->frag, shader->prog);
        return ret;
    }

    GL(glAttachShader(shader->prog, shader->vert));
    GL(glAttachShader(shader->prog, shader->frag));
    if (shader->geom)
        GL(glAttachShader(shader->prog, shader->geom));
    GL(glLinkProgram(shader->prog));
    GL(glGetProgramiv(shader->prog, GL_LINK_STATUS, &linkStatus));
    if (linkStatus != GL_TRUE) {
        GLint bufLength = 0;
        GL(glGetProgramiv(shader->prog, GL_INFO_LOG_LENGTH, &bufLength));
        if (bufLength) {
            char *buf = mem_alloc(bufLength);
            if (buf) {
                GL(glGetProgramInfoLog(shader->prog, bufLength, NULL, buf));
                err("Could not link program:\n%s\n", buf);
                mem_free(buf);
                GL(glDeleteShader(shader->vert));
                GL(glDeleteShader(shader->frag));
                if (shader->geom)
                    GL(glDeleteShader(shader->geom));
            }
        }
        GL(glDeleteProgram(shader->prog));
        shader->prog = 0;
    } else {
        ret = CERR_OK;
    }

    return ret;
}

void shader_done(shader_t *shader)
{
    GL(glDeleteProgram(shader->prog));
    GL(glDeleteShader(shader->vert));
    GL(glDeleteShader(shader->frag));
    if (shader->geom)
        GL(glDeleteShader(shader->geom));
}

int shader_id(shader_t *shader)
{
    return shader->prog;
}

static cres(int) shader_uniform_block_index(shader_t *shader, const char *name)
{
    GLuint index = glGetUniformBlockIndex(shader->prog, name);
    return index == GL_INVALID_INDEX ? cres_error(int, CERR_INVALID_INDEX) : cres_val(int, index);
}

cerr shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name)
{
    cres(int) res = shader_uniform_block_index(shader, name);
    if (IS_CERR(res))
        return cerr_error_cres(res);

    GL(glUniformBlockBinding(shader->prog, res.val, bpt->binding));

    return CERR_OK;
}

attr_t shader_attribute(shader_t *shader, const char *name)
{
    return glGetAttribLocation(shader->prog, name);
}

uniform_t shader_uniform(shader_t *shader, const char *name)
{
    return glGetUniformLocation(shader->prog, name);
}

void shader_use(shader_t *shader)
{
    GL(glUseProgram(shader->prog));
}

void shader_unuse(shader_t *shader)
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

void _renderer_init(renderer_t *renderer, const renderer_init_options *opts)
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
        renderer->mac_amd_quirk = true;
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
    renderer_depth_test(renderer, true);
    renderer_depth_func(renderer, DEPTH_FN_LESS);
    renderer_cleardepth(renderer, 1.0);
#ifndef EGL_EGL_PROTOTYPES
    GL(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
#endif /* EGL_EGL_PROTOTYPES */
    renderer_wireframe(renderer, false);

    for (i = 0; i < TEX_FMT_MAX; i++) {
        float buf[4] = {};
        texture_t tex;

        _texture_format_supported[i] = true;

        cerr err = texture_init(&tex);
        if (!IS_CERR(err)) {
            err = texture_load(&tex, i, 1, 1, &buf);
            if (IS_CERR(err))
                _texture_format_supported[i] = false;

            texture_deinit(&tex);
        }
    }

    for (i = 0; i < TEX_FMT_MAX; i++) {
        cresp(fbo_t) res;

        _fbo_texture_supported[i] = true;

        if (gl_texture_format(i) == GL_DEPTH_COMPONENT) {
            res = fbo_new(.width = 1, .height = 1, .depth_format = i,
                          .attachment_config = { .depth_texture = 1 });
            if (IS_CERR(res))
                _fbo_texture_supported[i] = false;
            else
                fbo_put(res.val);
        } else {
            res = fbo_new(.width = 1, .height = 1,
                          .color_format = (texture_format *)&i);
            if (IS_CERR(res))
                _fbo_texture_supported[i] = false;
            else
                fbo_put(res.val);
        }
    }
}

#ifndef CONFIG_FINAL
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

void renderer_debug(renderer_t *r)
{
    debug_module *dbgm = ui_igBegin(DEBUG_RENDERER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display)
        return;

    if (dbgm->unfolded) {
        igText("GL limits");
        ui_igTableHeader("renderer", (const char *[]){ "limit", "value"}, 2);

        for (int i = 0; i < array_size(gl_limits); i++)
            ui_igTableRow(gl_limit_names[i], "%d", gl_limits[i]);
        igEndTable();

        igText("FBO color formats");
        ui_igTableHeader("color formats", (const char *[]){ "format", "supported"}, 2);

        for (int i = 0; i < array_size(_texture_format_supported); i++)
            ui_igTableRow(texture_format_string[i], "%s",
                          fbo_texture_supported(i) ? "supported" : "");
        igEndTable();
    }

    ui_igEnd(DEBUG_RENDERER);
}
#endif /* CONFIG_FINAL */

int renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    if (limit >= RENDER_LIMIT_MAX)
        return 0;

    return gl_limits[limit];
}

void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile)
{
    renderer->major     = major;
    renderer->minor     = minor;
    renderer->profile   = profile;
}

void renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    if (r->x == x && r->y == y && r->width == width && r->height == height)
        return;

    r->x = x;
    r->y = y;
    r->width = width;
    r->height = height;
    GL(glViewport(x, y, width, height));
}

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

void renderer_cull_face(renderer_t *r, cull_face cull)
{
    GLenum gl_cull = gl_cull_face(cull);

    if (r->cull_face == gl_cull)
        return;

    r->cull_face = gl_cull;

    if (gl_cull != GL_NONE) {
        GL(glEnable(GL_CULL_FACE));
    } else {
        GL(glDisable(GL_CULL_FACE));
        return;
    }

    GL(glCullFace(r->cull_face));
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

void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor)
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

    if (r->blend_sfactor != _sfactor || r->blend_dfactor != _dfactor) {
        r->blend_sfactor = _sfactor;
        r->blend_dfactor = _dfactor;
    }
    GL(glBlendFunc(r->blend_sfactor, r->blend_dfactor));
}

void renderer_depth_test(renderer_t *r, bool enable)
{
    if (r->depth_test == enable)
        return;

    r->depth_test = enable;
    if (enable)
        GL(glEnable(GL_DEPTH_TEST));
    else
        GL(glDisable(GL_DEPTH_TEST));
}

void renderer_wireframe(renderer_t *r, bool enable)
{
    if (r->wireframe == enable)
        return;

    r->wireframe = enable;

#ifndef EGL_EGL_PROTOTYPES
    if (enable)
        GL(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
    else
        GL(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
#endif /* EGL_EGL_PROTOTYPES */
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

void renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces, data_type idx_type,
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
    if (r->mac_amd_quirk)
        GL(glFlush());
}

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

void renderer_depth_func(renderer_t *r, depth_func fn)
{
    GLenum _fn = gl_depth_func(fn);

    if (r->depth_func == _fn)
        return;

    r->depth_func = _fn;
    GL(glDepthFunc(r->depth_func));
}

void renderer_cleardepth(renderer_t *r, double depth)
{
    if (r->clear_depth == depth)
        return;

    r->clear_depth = depth;
#ifdef CONFIG_GLES
    GL(glClearDepthf((float)r->clear_depth));
#else
    GL(glClearDepth(r->clear_depth));
#endif /* CONFIG_GLES */
}

void renderer_clearcolor(renderer_t *r, vec4 color)
{
    if (!memcmp(r->clear_color, color, sizeof(vec4)))
        return;

    vec4_dup(r->clear_color, color);
    GL(glClearColor(color[0], color[1], color[2], color[3]));
}

void renderer_clear(renderer_t *r, bool color, bool depth, bool stencil)
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
