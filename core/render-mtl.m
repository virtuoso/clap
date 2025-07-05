// SPDX-License-Identifier: Apache-2.0

#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

/****************************************************************************
 * Buffer
 ****************************************************************************/

/* XXX: common */
static void buffer_drop(struct ref *ref)
{
    buffer_t *buf = container_of(ref, struct buffer, ref);
    buffer_deinit(buf);
}
DEFINE_REFCLASS(buffer);
DECLARE_REFCLASS(buffer);

cerr _buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    cerr err = ref_embed(buffer, buf);
    if (IS_CERR(err))
        return err;

    return CERR_OK;
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

    buf->loaded = false;
}

void buffer_bind(buffer_t *buf, int loc)
{
    if (!buf->loaded)
        return;

    if (loc < 0)
        return;
}

void buffer_unbind(buffer_t *buf, int loc)
{
    if (!buf->loaded)
        return;

    if (loc < 0)
        return;
}

void buffer_load(buffer_t *buf, void *data, size_t sz, int loc)
{
    buf->loaded = true;
}

#ifndef CONFIG_FINAL
void buffer_debug_header(void)
{
}

void buffer_debug(buffer_t *buf, const char *name)
{
}
#endif /* CONFIG_FINAL */

/****************************************************************************
 * Vertex Array Object
 ****************************************************************************/

static void vertex_array_drop(struct ref *ref)
{
    vertex_array_t *va = container_of(ref, vertex_array_t, ref);
    vertex_array_done(va);
}

DEFINE_REFCLASS_INIT_OPTIONS(vertex_array);
DEFINE_REFCLASS(vertex_array);
DECLARE_REFCLASS(vertex_array);

cerr vertex_array_init(vertex_array_t *va)
{
    cerr err = ref_embed(vertex_array, va);
    if (IS_CERR(err))
        return err;

    return CERR_OK;
}

void vertex_array_done(vertex_array_t *va)
{
}

void vertex_array_bind(vertex_array_t *va)
{
}

void vertex_array_unbind(vertex_array_t *va)
{
}

/****************************************************************************
 * Texture
 ****************************************************************************/

/* XXX: common */
static void texture_drop(struct ref *ref)
{
    struct texture *tex = container_of(ref, struct texture, ref);
    texture_deinit(tex);
}
DEFINE_REFCLASS(texture);
DECLARE_REFCLASS(texture);

bool texture_is_array(texture_t *tex)
{
    return false;
}

bool texture_is_multisampled(texture_t *tex)
{
    return false;
}

cerr _texture_init(texture_t *tex, const texture_init_options *opts)
{
    cerr err = ref_embed(texture, tex);
    if (IS_CERR(err))
        return err;

    return CERR_OK;
}

#ifndef CONFIG_FINAL
void texture_debug_header(void)
{
}

void texture_debug(texture_t *tex, const char *name)
{
}
#endif /* CONFIG_FINAL */

texture_t *texture_clone(texture_t *tex)
{
    texture_t *ret = ref_new(texture);

    if (ret) {
        ret->loaded         = tex->loaded;
        tex->loaded         = false;
    }

    return ret;
}

void texture_deinit(texture_t *tex)
{
    if (!tex->loaded)
        return;

    tex->loaded = false;
}

cerr texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!tex->loaded)
        return CERR_TEXTURE_NOT_LOADED;

    if (tex->width == width && tex->height == height)
        return CERR_OK;

    tex->width = width;
    tex->height = height;

    return CERR_OK;
}

cerr_check texture_load(texture_t *tex, texture_format format,
                        unsigned int width, unsigned int height, void *buf)
{
    tex->width  = width;
    tex->height = height;
    tex->loaded = true;

    return CERR_OK;
}

void texture_bind(texture_t *tex, unsigned int target)
{
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
    return tex->id;
}

/* XXX: common */
bool texture_loaded(struct texture *tex)
{
    return tex->loaded;
}

/* XXX: common */
cerr_check texture_pixel_init(texture_t *tex, float color[4])
{
    cerr err = texture_init(tex);
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

/* XXX: common */
texture_t *fbo_texture(fbo_t *fbo, fbo_attachment attachment)
{
    if (attachment.depth_texture)
        return &fbo->depth_tex;

    int idx = fbo_attachment_color(attachment);
    if (idx >= fa_nr_color_texture(fbo->attachment_config))
        return NULL;

    return &fbo->color_tex[idx];
}

bool fbo_texture_supported(texture_format format)
{
    if (format >= TEX_FMT_MAX)
        return false;

    return false;
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
    if (tidx >= 0 && tidx <= fbo_attachment_color(fbo->attachment_config) &&
        texture_loaded(&fbo->color_tex[tidx]))
        return true;

    if (attachment.depth_texture && fbo->attachment_config.depth_texture)
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

int fbo_nr_attachments(fbo_t *fbo)
{
    return 0;
}

fbo_attachment_type fbo_get_attachment(fbo_t *fbo)
{
    return FBO_ATTACHMENT_COLOR0;
}

static void fbo_drop(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    fa_for_each(fa, fbo->attachment_config, texture)
        texture_deinit(&fbo->color_tex[fbo_attachment_color(fa)]);

   if (texture_loaded(&fbo->depth_tex))
        texture_deinit(&fbo->depth_tex);

    mem_free(fbo->color_format);
}
DEFINE_REFCLASS(fbo);
DECLARE_REFCLASS(fbo);

cerr_check fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo)
        return CERR_INVALID_ARGUMENTS;

    fbo->width = width;
    fbo->height = height;

    return CERR_OK;
}

void fbo_prepare(fbo_t *fbo)
{
}

void fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
}

void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment)
{
}

bool fbo_is_multisampled(fbo_t *fbo)
{
    return false;
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
    return CERR_OK;
}

static void uniform_buffer_drop(struct ref *ref)
{
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
    return CERR_OK;
}

cerr uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points)
{
    return CERR_OK;
}

void uniform_buffer_update(uniform_buffer_t *ubo)
{
    if (!ubo->dirty)
        return;

    ubo->dirty = false;
}

/*
 * Calculate uniform @offset within a UBO, its total @size and set its @value
 * if @value is not NULL
 */
cerr uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                        unsigned int count, const void *value)
{
    return CERR_OK;
}

/****************************************************************************
 * Shaders
 ****************************************************************************/

cerr shader_init(shader_t *shader, const char *vertex, const char *geometry, const char *fragment)
{
    return CERR_OK;
}

void shader_done(shader_t *shader)
{
}

attr_t shader_attribute(shader_t *shader, const char *name)
{
    return 0;
}

uniform_t shader_uniform(shader_t *shader, const char *name)
{
    return -1;
}

cerr shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name)
{
    return CERR_OK;
}

void shader_use(shader_t *shader)
{
}

void shader_unuse(shader_t *shader)
{
}

void uniform_set_ptr(uniform_t uniform, data_type type, unsigned int count, const void *value)
{
}

/****************************************************************************
 * Renderer context
 ****************************************************************************/

void _renderer_init(renderer_t *renderer, const renderer_init_options *opts)
{
}

#ifndef CONFIG_FINAL
void renderer_debug(renderer_t *r)
{
}
#endif /* CONFIG_FINAL */

int renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    return 0;
}

/* XXX: common? */
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
}

void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor)
{
}

void renderer_depth_test(renderer_t *r, bool enable)
{
    if (r->depth_test == enable)
        return;

    r->depth_test = enable;
}

void renderer_wireframe(renderer_t *r, bool enable)
{
}

void renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces, data_type idx_type,
                   unsigned int nr_instances)
{
}

void renderer_depth_func(renderer_t *r, depth_func fn)
{
}

void renderer_cleardepth(renderer_t *r, double depth)
{
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
