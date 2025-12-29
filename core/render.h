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
#   endif /* __APPLE__ */
#   include <GL/glew.h>
#  endif
# else
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned short GLushort;
typedef ptrdiff_t GLsizeiptr;
# endif /* IMPLEMENTOR */
#elif defined(CONFIG_RENDERER_METAL)
# if defined(IMPLEMENTOR) && defined(__OBJC__)
/*
 * XXX: "weak" clashes with something in the guts of Metal, rename it in
 * the tree
 */
#  undef weak
#  import <Metal/Metal.h>
#  import <QuartzCore/CoreAnimation.h>
#  import <CoreGraphics/CGColorSpace.h>
// #  import <QuartzCore/QuartzCore.h>
typedef id<MTLDevice> mtl_device_t;
typedef id<MTLLibrary> mtl_library_t;
typedef id<MTLFunction> mtl_function_t;
typedef id<MTLCommandQueue> mtl_command_queue_t;
typedef id<MTLCommandBuffer> mtl_command_buffer_t;
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
# endif /* !IMPLEMENTOR || !__OBJC__ */
// Metal's integral types are landmines between C and ObjC
typedef uint64_t mtl_cull_mode_t;
#else
# error "Unsupported renderer"
#endif

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

TYPE_FORWARD(renderer);
TYPE_FORWARD(vertex_array);

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

#ifdef CONFIG_RENDERER_OPENGL
TYPE(buffer,
    struct ref  ref;
    buffer_t    *main;
    GLenum      type;
    GLenum      usage;
    GLuint      id;
    GLuint      comp_type;
    GLuint      comp_count;
    GLuint      off;
    GLsizei     stride;
    int         use_count;
    bool        loaded;
#ifndef CONFIG_FINAL
    buffer_init_options opts;
    uniform_t           loc;
#endif /* CONFIG_FINAL */
);
#elif defined(CONFIG_RENDERER_METAL)
TYPE(buffer,
    struct ref      ref;
    renderer_t      *renderer;
    mtl_buffer_t    buf;
    size_t          off;
    size_t          stride;
    size_t          size;
    data_type       comp_type;
    unsigned int    comp_count;
    char            *name;
    bool            loaded;
    uniform_t       loc;
#ifndef CONFIG_FINAL
    buffer_init_options opts;
#endif /* CONFIG_FINAL */
);
#endif /* CONFIG_RENDERER_OPENGL */

#define buffer_init(_b, args...) \
    _buffer_init((_b), &(buffer_init_options){ args })
cerr_check _buffer_init(buffer_t *buf, const buffer_init_options *opts);
void buffer_deinit(buffer_t *buf);
void buffer_bind(buffer_t *buf, uniform_t loc);
void buffer_unbind(buffer_t *buf, uniform_t loc);
bool buffer_loaded(buffer_t *buf);
#ifdef CONFIG_RENDERER_OPENGL
static inline cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...) { return cres_error(int, CERR_NOT_SUPPORTED); }
#else
cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...);
#endif /* !CONFIG_FINAL */

#ifdef CONFIG_RENDERER_OPENGL
TYPE(vertex_array,
    struct ref  ref;
    GLuint      vao;
);
#elif defined(CONFIG_RENDERER_METAL)
TYPE(vertex_array,
    struct ref  ref;
    renderer_t  *renderer;
    buffer_t    *index;
);
#endif /* CONFIG_RENDERER_OPENGL */

DEFINE_REFCLASS_INIT_OPTIONS(vertex_array,
    renderer_t  *renderer;
);

cerr_check vertex_array_init(vertex_array_t *va, renderer_t *r);
void vertex_array_done(vertex_array_t *va);
void vertex_array_bind(vertex_array_t *va);
void vertex_array_unbind(vertex_array_t *va);

TYPE_FORWARD(shader);
TYPE_FORWARD(fbo);

DEFINE_REFCLASS_INIT_OPTIONS(draw_control,
    renderer_t  *renderer;
    shader_t    *shader;
    fbo_t       *fbo;
    const char  *name;
);

#ifdef CONFIG_RENDERER_OPENGL
TYPE(draw_control,
    struct ref  ref;
);
#elif defined(CONFIG_RENDERER_METAL)
TYPE(draw_control,
    struct ref                          ref;
    renderer_t                          *renderer;
    struct list                         shader_entry;
    struct list                         hash_entry;
    shader_t                            *shader;
    fbo_t                               *fbo;
    mtl_depth_stencil_state_t           depth_stencil;
    mtl_render_pipeline_state_t         pipeline[2];
    mtl_render_pipeline_reflection_t    reflection;
);
#endif /* CONFIG_RENDERER_OPENGL */

#define draw_control_init(_dc, args...) \
    _draw_control_init((_dc), &(draw_control_init_options){ args })
cerr _draw_control_init(draw_control_t *dc, const draw_control_init_options *opts);
void draw_control_done(draw_control_t *dc);
void draw_control_bind(draw_control_t *dc);
void draw_control_unbind(draw_control_t *dc);

typedef enum texture_type {
    TEX_2D,
    TEX_2D_ARRAY,
    TEX_3D,
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

#ifdef CONFIG_RENDERER_OPENGL
TYPE(texture,
    struct ref      ref;
    GLuint          id;
    GLenum          format;
    GLenum          internal_format;
    GLenum          component_type;
    GLenum          type;
    GLint           wrap;
    GLint           min_filter;
    GLint           mag_filter;
    GLint           target;
    GLsizei         layers;
    bool            loaded;
    bool            multisampled;
    bool            sampler_updated;
    unsigned int    width;
    unsigned int    height;
    float           border[4];
#ifndef CONFIG_FINAL
    texture_init_options    opts;
#endif /* CONFIG_FINAL */
);
#elif defined(CONFIG_RENDERER_METAL)
TYPE(texture,
    struct ref          ref;
    renderer_t          *renderer;
    mtl_texture_t       texture;
    mtl_sampler_state_t sampler;
    texture_type        type;
    texture_format      format;
    int                 target;
    int                 width;
    int                 height;
    unsigned int        layers;
    bool                loaded;
    bool                multisampled;
    bool                render_target;
    char                *name;
#ifndef CONFIG_FINAL
    texture_init_options    opts;
#endif /* CONFIG_FINAL */
);
#endif /* CONFIG_RENDERER_OPENGL */

typedef enum fbo_attachment_type {
    FBO_ATTACHMENT_DEPTH,
    FBO_ATTACHMENT_STENCIL,
    FBO_ATTACHMENT_COLOR0,
} fbo_attachment_type;

typedef uint64_t texid_t;

bool texture_format_supported(texture_format format);
const char *texture_format_string(texture_format fmt);
cerr_check _texture_init(texture_t *tex, const texture_init_options *opts);
#define texture_init(_t, args...) \
    _texture_init((_t), &(texture_init_options){ args })
void texture_deinit(texture_t *tex);
void texture_done(texture_t *tex);
cerr_check texture_load(texture_t *tex, texture_format format,
                        unsigned int width, unsigned int height, void *buf);
cerr_check texture_resize(texture_t *tex, unsigned int width, unsigned int height);
texid_t texture_id(texture_t *tex);
void texture_bind(texture_t *tex, unsigned int target);
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
#ifdef CONFIG_RENDERER_OPENGL
static inline cres(int) texture_set_name(texture_t *tex, const char *fmt, ...) { return cres_error(int, CERR_NOT_SUPPORTED); }
#else
cres(int) texture_set_name(texture_t *tex, const char *fmt, ...);
#endif /* !CONFIG_FINAL */


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

typedef struct fbo_attconfig {
    texture_format      format;
    fbo_load_action     load_action;
    fbo_store_action    store_action;
    depth_func          depth_func;
    vec4                clear_color;
    double              clear_depth;
    uint32_t            clear_stencil;
} fbo_attconfig;

#ifdef CONFIG_RENDERER_OPENGL
TYPE(fbo,
    struct ref      ref;
    renderer_t      *renderer;
    unsigned int    width;
    unsigned int    height;
    unsigned int    layers;
    unsigned int    fbo;
    fbo_attachment  layout;
    fbo_attconfig   *color_config;
    fbo_attconfig   depth_config;
    texture_t       depth_tex;
    int             color_buf[FBO_COLOR_ATTACHMENTS_MAX];
    texture_t       color_tex[FBO_COLOR_ATTACHMENTS_MAX];
    int             depth_buf;
    unsigned int    nr_samples;
);
#elif defined(CONFIG_RENDERER_METAL)
TYPE(fbo,
    struct ref                      ref;
    renderer_t                      *renderer;
    texture_t                       color_tex[FBO_COLOR_ATTACHMENTS_MAX];
    texture_t                       depth_tex;
    mtl_render_pass_descriptor_t    desc;
    mtl_render_command_encoder_t    cmd_encoder;
    unsigned int                    width;
    unsigned int                    height;
    unsigned int                    layers;
    unsigned int                    nr_samples;
    fbo_attachment                  layout;
    fbo_attconfig                   *color_config;
    fbo_attconfig                   depth_config;
    unsigned int                    id;
    const char                      *name;
);
#endif /* CONFIG_RENDERER_OPENGL */

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

bool fbo_texture_supported(texture_format format);
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
fbo_attachment_type fbo_get_attachment(fbo_t *fbo);
texture_format fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment);

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

#if defined(CONFIG_RENDERER_OPENGL) || defined(CONFIG_RENDERER_METAL)
TYPE(binding_points,
    int binding;
    int stages;
);
#endif

void binding_points_init(binding_points_t *bps);
void binding_points_done(binding_points_t *bps);
void binding_points_add(binding_points_t *bps, shader_stage stage, int binding);

#ifdef CONFIG_RENDERER_OPENGL
TYPE(uniform_buffer,
    struct ref  ref;
    GLsizeiptr  size;     /* Size in bytes */
    GLuint      id;       /* GL buffer object */
    GLuint      binding;  /* UBO binding point */
    void        *data;    /* CPU-side shadow buffer */
    bool        dirty;    /* Flag for updates */
);
#elif defined(CONFIG_RENDERER_METAL)
TYPE(uniform_buffer,
    struct ref      ref;
    renderer_t      *renderer;
    mtl_buffer_t    buf;
    const char      *name;
    size_t          size;     /* Size in bytes */
    void            *data;    /* CPU-side shadow buffer */
    int             binding;  /* UBO binding point */
    bool            dirty;    /* Flag for updates */
);
#endif

DEFINE_REFCLASS_INIT_OPTIONS(uniform_buffer,
    renderer_t  *renderer;
    const char  *name;
    int         binding;
);

cerr_check uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name, int binding);
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

#ifdef CONFIG_RENDERER_OPENGL
TYPE(shader,
    GLuint  vert;
    GLuint  frag;
    GLuint  geom;
    GLuint  prog;
);
#elif defined(CONFIG_RENDERER_METAL)
TYPE(shader,
    // struct ref      ref;
    renderer_t                          *renderer;
    struct list                         dc_list;
    mtl_function_t                      vert;
    mtl_function_t                      frag;
    mtl_vertex_descriptor_t             vdesc;
    mtl_render_pipeline_reflection_t    reflection;
    const char                          *name;
    unsigned int                        id;
);
#endif /* CONFIG_RENDERER_OPENGL */

cerr shader_init(renderer_t *r, shader_t *shader,
                 const char *vertex, const char *geometry, const char *fragment);
void shader_set_vertex_attrs(shader_t *shader, size_t stride,
                             size_t *offs, data_type *types, size_t *comp_counts,
                             unsigned int nr_attrs);
void shader_done(shader_t *shader);
int shader_id(shader_t *shader);
cerr_check shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name);
attr_t shader_attribute(shader_t *shader, const char *name, attr_t attr);
uniform_t shader_uniform(shader_t *shader, const char *name);

void shader_use(shader_t *shader, bool draw);
void shader_unuse(shader_t *shader, bool draw);
/* Query a uniform offset within a uniform block as expected by the shader */
cres(size_t) shader_uniform_offset_query(shader_t *shader, const char *ubo_name, const char *var_name);
void uniform_set_ptr(uniform_t uniform, data_type type, unsigned int count, const void *value);

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

#ifdef CONFIG_RENDERER_OPENGL
TYPE(renderer,
    GLenum              cull_face;
    GLenum              blend_sfactor;
    GLenum              blend_dfactor;
    GLenum              depth_func;
    vec4                clear_color;
    double              clear_depth;
    int                 major;
    int                 minor;
    renderer_profile    profile;
    int                 x;
    int                 y;
    int                 width;
    int                 height;
    bool                blend;
    bool                depth_test;
    bool                wireframe;
    bool                mac_amd_quirk;
);
#elif defined(CONFIG_RENDERER_METAL)
#define FBOS_MAX 1024
#define SHADERS_MAX 1024
TYPE(renderer,
    struct ref                          ref;
    ns_autorelease_pool_t               frame_pool;
    mtl_device_t                        device;
    mtl_command_queue_t                 cmd_queue;
    mtl_command_buffer_t                cmd_buffer;
    // mtl_render_command_encoder_t        cmd_encoder;
    // mtl_render_pass_descriptor_t        screen_desc;
    mtl_ca_layer_t                      layer;
    mtl_ca_drawable_t                   drawable;
    vertex_array_t                      *va;
    // shader_t                            *shader;
    draw_control_t                      *dc;
    fbo_t                               *screen_fbo;
    fbo_t                               *fbo;
    void                                *vbuffer_cache[SLOTS_MAX];
    void                                *fbuffer_cache[SLOTS_MAX];
    void                                *texture_cache[SLOTS_MAX];
    void                                *sampler_cache[SLOTS_MAX];
    struct bitmap                       fbo_ids;
    struct bitmap                       shader_ids;
    struct list                         dc_hash[256];
    // mtl_buffer_t                        uniform_buffers;
    cg_colorspace_ref_t                 colorspace;
    int                                 major;
    int                                 minor;
    renderer_profile                    profile;
    int                                 x;
    int                                 y;
    int                                 width;
    int                                 height;
    mtl_cull_mode_t                     cull_mode;
    bool                                blend;
);
#endif /* CONFIG_RENDERER_OPENGL */

int renderer_query_limits(renderer_t *renderer, render_limit limit);

typedef struct renderer_init_options {
#ifdef CONFIG_RENDERER_METAL
    void    *device;
    void    *layer;
#endif /* CONFIG_RENDERER_METAL */
} renderer_init_options;

#define renderer_init(_r, args...) \
    _renderer_init((_r), &(renderer_init_options){ args })
void _renderer_init(renderer_t *renderer, const renderer_init_options *opts);
void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile);
void renderer_viewport(renderer_t *r, int x, int y, int width, int height);
void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight);

#ifdef CONFIG_RENDERER_METAL
void renderer_done(renderer_t *r);
void renderer_frame_begin(renderer_t *renderer);
void renderer_swapchain_begin(renderer_t *renderer);
void renderer_swapchain_end(renderer_t *r);
void renderer_frame_end(renderer_t *renderer);
#else
static inline void renderer_frame_begin(renderer_t *renderer) {}
static inline void renderer_frame_end(renderer_t *renderer) {}
void renderer_swapchain_begin(renderer_t *renderer);
// static inline void renderer_swapchain_begin(renderer_t *renderer) {}
static inline void renderer_swapchain_end(renderer_t *renderer) {}
static inline void renderer_done(renderer_t *renderer) {}
#endif /* CONFIG_RENDERER_METAL */

#ifndef CONFIG_FINAL
void buffer_debug_header(void);
void buffer_debug(buffer_t *buf, const char *name);
void texture_debug_header(void);
void texture_debug(texture_t *tex, const char *name);
void renderer_debug(renderer_t *r);
#else
static inline void buffer_debug_header(void) {}
static inline void buffer_debug(buffer_t *buf) {}
static inline void renderer_debug(renderer_t *r) {}
#endif /* CONFIG_FINAL */

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

void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor);
void renderer_wireframe(renderer_t *r, bool enable);

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

void renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                   data_type idx_type, unsigned int nr_instances);

#endif /* __CLAP_RENDER_H__ */
