/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_RENDER_H__
#define __CLAP_RENDER_H__

#include <stdlib.h>
#include "display.h"
#include "error.h"
#include "linmath.h"
#include "logger.h"
#include "object.h"
#include "typedef.h"
#include "config.h"

#ifdef CONFIG_RENDERER_OPENGL
# ifdef IMPLEMENTOR
#  if defined(CONFIG_BROWSER) || defined(CONFIG_GLES)
#   include <GLES3/gl3.h>
#   include <EGL/egl.h>
#   define GL_GLEXT_PROTOTYPES
#   include <GLES2/gl2ext.h>
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
#else
# error "Unsupported renderer"
#endif

#ifdef CONFIG_BROWSER
#define GL_CLAMP_TO_BORDER GL_CLAMP_TO_EDGE
#endif /* CONFIG_BROWSER */

typedef enum data_type {
    DT_NONE = 0,
    DT_BYTE,
    DT_SHORT,
    DT_USHORT,
    DT_INT,
    DT_FLOAT,
    DT_IVEC2,
    DT_IVEC3,
    DT_IVEC4,
    DT_VEC2,
    DT_VEC3,
    DT_VEC4,
    DT_MAT2,
    DT_MAT3,
    DT_MAT4,
} data_type;

typedef enum buffer_type {
    BUF_ARRAY,
    BUF_ELEMENT_ARRAY,
} buffer_type;

typedef enum buffer_usage {
    BUF_STATIC,
    BUF_DYNAMIC
} buffer_usage;

#ifdef CONFIG_RENDERER_OPENGL
TYPE(buffer,
    struct ref  ref;
    GLenum      type;
    GLenum      usage;
    GLuint      id;
    GLuint      comp_type;
    GLuint      comp_count;
    bool        loaded;
);
#endif /* CONFIG_RENDERER_OPENGL */

typedef struct buffer_init_options {
    buffer_type         type;
    buffer_usage        usage;
    data_type           comp_type;
    unsigned int        comp_count;
    void                *data;
    size_t              size;
} buffer_init_options;

#define buffer_init(_b, args...) \
    _buffer_init((_b), &(buffer_init_options){ args })
cerr_check _buffer_init(buffer_t *buf, const buffer_init_options *opts);
void buffer_deinit(buffer_t *buf);
void buffer_bind(buffer_t *buf, int loc);
void buffer_unbind(buffer_t *buf, int loc);
void buffer_load(buffer_t *buf, void *data, size_t sz, int loc);
bool buffer_loaded(buffer_t *buf);

#ifdef CONFIG_RENDERER_OPENGL
TYPE(vertex_array,
    struct ref  ref;
    GLuint      vao;
);
#endif /* CONFIG_RENDERER_OPENGL */

cerr_check vertex_array_init(vertex_array_t *va);
void vertex_array_done(vertex_array_t *va);
void vertex_array_bind(vertex_array_t *va);
void vertex_array_unbind(vertex_array_t *va);

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
    TEX_FMT_MAX,
} texture_format;

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
    unsigned int    width;
    unsigned int    height;
    float           border[4];
);
#endif /* CONFIG_RENDERER_OPENGL */

typedef enum fbo_attachment {
    FBO_ATTACHMENT_DEPTH,
    FBO_ATTACHMENT_STENCIL,
    FBO_ATTACHMENT_COLOR0,
} fbo_attachment;

typedef struct texture_init_options {
    unsigned int        target;
    texture_type        type;
    texture_wrap        wrap;
    texture_filter      min_filter;
    texture_filter      mag_filter;
    texture_format      format;
    unsigned int        layers;
    bool                multisampled;
    float               *border;
} texture_init_options;

typedef int texid_t;

bool texture_format_supported(texture_format format);
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
cerr_check texture_pixel_init(texture_t *tex, float color[4]);
void textures_init(void);
void textures_done(void);
texture_t *white_pixel(void);
texture_t *black_pixel(void);
texture_t *transparent_pixel(void);

/* Special constants for nr_attachments */
#define FBO_DEPTH_TEXTURE (-1)
#define FBO_COLOR_TEXTURE (0)

#ifdef CONFIG_RENDERER_OPENGL
TYPE(fbo,
    struct ref      ref;
    int             width;
    int             height;
    unsigned int    fbo;
    int             depth_buf;
    GLuint          attachment;
    texture_format  *color_format;
    texture_format  depth_format;
    darray(int, color_buf);
    texture_t       tex;
    unsigned int    nr_samples;
    int             retain_tex;
);
#endif /* CONFIG_RENDERER_OPENGL */

cresp_ret(fbo_t);

/*
 * width and height correspond to the old fbo_new()'s parameters,
 * so the old users won't need to be converted.
 */
typedef struct fbo_init_options {
    unsigned int    width;
    unsigned int    height;
    int             nr_attachments;
    unsigned int    nr_samples;
    texture_format  *color_format;
    texture_format  depth_format;
    bool            multisampled;
} fbo_init_options;

bool fbo_texture_supported(texture_format format);
must_check cresp(fbo_t) _fbo_new(const fbo_init_options *opts);
#define fbo_new(args...) \
    _fbo_new(&(fbo_init_options){ args })
fbo_t *fbo_get(fbo_t *fbo);
void fbo_put(fbo_t *fbo);
void fbo_put_last(fbo_t *fbo);
void fbo_prepare(fbo_t *fbo);
void fbo_done(fbo_t *fbo, int width, int height);
void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, int attachment);
cerr_check fbo_resize(fbo_t *fbo, int width, int height);
texture_t *fbo_texture(fbo_t *fbo);
int fbo_width(fbo_t *fbo);
int fbo_height(fbo_t *fbo);
int fbo_nr_attachments(fbo_t *fbo);
bool fbo_is_multisampled(fbo_t *fbo);
fbo_attachment fbo_get_attachment(fbo_t *fbo);

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

#ifdef CONFIG_RENDERER_OPENGL
TYPE(binding_points,
    int binding;
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
#endif

DEFINE_REFCLASS_INIT_OPTIONS(uniform_buffer,
    int     binding;
);

cerr_check uniform_buffer_init(uniform_buffer_t *ubo, int binding);
cerr_check uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size);
void uniform_buffer_done(uniform_buffer_t *ubo);
void uniform_buffer_update(uniform_buffer_t *ubo);
cerr_check uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points);

/*
 * Put data into a uniform buffer in conformance with whatever data
 * layout rules the renderer imposes (i.e. GL's std140), adjust the
 * offset to point past the copied data. value==NULL is allowed, in
 * which case just adjust the offset.
 */
cerr_check uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, unsigned int count,
                              const void *value);

#ifdef CONFIG_RENDERER_OPENGL
TYPE(shader,
    GLuint  vert;
    GLuint  frag;
    GLuint  geom;
    GLuint  prog;
);
#endif /* CONFIG_RENDERER_OPENGL */

typedef int uniform_t;
typedef int attr_t;

cerr shader_init(shader_t *shader, const char *vertex, const char *geometry, const char *fragment);
void shader_done(shader_t *shader);
int shader_id(shader_t *shader);
cerr_check shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name);
attr_t shader_attribute(shader_t *shader, const char *name);
uniform_t shader_uniform(shader_t *shader, const char *name);
void shader_use(shader_t *shader);
void shader_unuse(shader_t *shader);
void uniform_set_ptr(uniform_t uniform, data_type type, unsigned int count, const void *value);

typedef enum {
    RENDERER_CORE_PROFILE,
    RENDERER_ANY_PROFILE,
} renderer_profile;

typedef enum {
    DEPTH_FN_NEVER,
    DEPTH_FN_LESS,
    DEPTH_FN_EQUAL,
    DEPTH_FN_LESS_OR_EQUAL,
    DEPTH_FN_GREATER,
    DEPTH_FN_NOT_EQUAL,
    DEPTH_FN_GREATER_OR_EQUAL,
    DEPTH_FN_ALWAYS,
} depth_func;

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
#endif /* CONFIG_RENDERER_OPENGL */

void renderer_init(renderer_t *renderer);
void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile);
void renderer_viewport(renderer_t *r, int x, int y, int width, int height);
void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight);

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
void renderer_depth_func(renderer_t *r, depth_func fn);
void renderer_cleardepth(renderer_t *r, double depth);
void renderer_depth_test(renderer_t *r, bool enable);
void renderer_wireframe(renderer_t *r, bool enable);
void renderer_clearcolor(renderer_t *r, vec4 color);
void renderer_clear(renderer_t *r, bool color, bool depth, bool stencil);

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

void renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces, data_type idx_type);

#endif /* __CLAP_RENDER_H__ */
