// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "display.h"
#include "render.h"
#undef IMPLEMENTOR

#include "common.h"
#include "logger.h"
#include "memory.h"
#include "util.h"

/*
 * Vulkan renderer backend
 *
 * Every op below is a benign stub. Modeled on render-wgpu.c
 * (pass/pipeline/binding flow) and render-mtl.m (frames in flight,
 * swapchain pacing).
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

/* static guesses: there is no device to ask */
static int vk_renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    switch (limit) {
        case RENDER_LIMIT_MAX_TEXTURE_SIZE:             return 16384;
        case RENDER_LIMIT_MAX_TEXTURE_UNITS:            return 32;
        case RENDER_LIMIT_MAX_TEXTURE_ARRAY_LAYERS:     return 2048;
        case RENDER_LIMIT_MAX_COLOR_ATTACHMENTS:        return FBO_COLOR_ATTACHMENTS_MAX;
        case RENDER_LIMIT_MAX_COLOR_TEXTURE_SAMPLES:    return 4;
        case RENDER_LIMIT_MAX_DEPTH_TEXTURE_SAMPLES:    return 4;
        case RENDER_LIMIT_MAX_SAMPLES:                  return 4;
        case RENDER_LIMIT_MAX_DRAW_BUFFERS:             return FBO_COLOR_ATTACHMENTS_MAX;
        case RENDER_LIMIT_MAX_ANISOTROPY:               return 16;
        case RENDER_LIMIT_MAX_UBO_SIZE:                 return 65536;
        case RENDER_LIMIT_MAX_UBO_BINDINGS:             return BINDING_UBO_MAX;
        case RENDER_LIMIT_MAX_VERTEX_UNIFORM_BLOCKS:    return BINDING_UBO_MAX;
        case RENDER_LIMIT_MAX_GEOMETRY_UNIFORM_BLOCKS:  return BINDING_UBO_MAX;
        case RENDER_LIMIT_MAX_FRAGMENT_UNIFORM_BLOCKS:  return BINDING_UBO_MAX;
        default:                                        return 0;
    }
}

static cerr vk_renderer_init(renderer_t *renderer, const renderer_init_options *opts)
{
    msg("vulkan renderer: stub initialized\n");
    return CERR_OK;
}

static void vk_renderer_done(renderer_t *r)
{
}

static void vk_renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    r->x = x;
    r->y = y;
    r->width = width;
    r->height = height;
    r->vk.swapchain_dirty = true;
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

static void vk_renderer_swapchain_begin(renderer_t *r)
{
}

static void vk_renderer_swapchain_end(renderer_t *r)
{
}

static void vk_renderer_frame_begin(renderer_t *r)
{
}

static void vk_renderer_frame_end(renderer_t *r)
{
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

static cerr vk_texture_init(texture_t *tex, const texture_init_options *opts)
{
    CERR_RET_CERR(ref_embed(texture, tex));

    tex->vk.type = opts->type;
    tex->vk.format = opts->format;
    tex->vk.render_target = opts->render_target;
    tex->layers = opts->layers;

    return CERR_OK;
}

static void vk_texture_deinit(texture_t *tex)
{
    tex->loaded = false;
}

static cerr vk_texture_load(texture_t *tex, texture_format format,
                            unsigned int width, unsigned int height, void *buf)
{
    tex->vk.format = format;
    tex->width = width;
    tex->height = height;
    tex->loaded = true;

    return CERR_OK;
}

static cerr vk_texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    tex->width = width;
    tex->height = height;

    return CERR_OK;
}

static void vk_texture_bind(texture_t *tex, unsigned int target, uniform_t uniform)
{
}

static void vk_texture_unbind(texture_t *tex, unsigned int target)
{
}

static bool vk_texture_is_array(texture_t *tex)                     { return false; }
static void vk_texture_set_name(texture_t *tex, const char *name)   {}

static texture_t *vk_texture_clone(texture_t *tex)
{
    return NULL;
}

static texid_t vk_texture_id(texture_t *tex)
{
    return 0;
}

static bool vk_texture_format_supported(renderer_t *r, texture_format format)
{
    return format < TEX_FMT_MAX;
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

static cerr vk_shader_init(renderer_t *r, shader_t *shader,
                           const char *vertex, size_t vert_size,
                           const char *geometry, size_t geom_size,
                           const char *fragment, size_t frag_size)
{
    return CERR_NOT_SUPPORTED_REASON(.fmt = "vulkan: shaders not implemented yet");
}

static void vk_shader_done(shader_t *shader)
{
}

static void vk_shader_set_vertex_attrs(shader_t *shader, size_t stride,
                                       size_t *offs, data_type *types,
                                       size_t *comp_counts, unsigned int *locs,
                                       unsigned int nr_attrs)
{
}

static int vk_shader_id(shader_t *shader)   { return 0; }

static cerr vk_shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name)
{
    return CERR_OK;
}

static attr_t vk_shader_attribute(shader_t *shader, const char *name, attr_t attr)
{
    return attr;
}

static uniform_t vk_shader_uniform(shader_t *shader, const char *name)
{
    return UA_NOT_PRESENT;
}

static cerr vk_shader_use(shader_t *shader, bool draw)
{
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

cerr vk_renderer_setup(renderer_t *renderer, const renderer_init_options *opts)
{
    list_init(&renderer->vk.ubos);

    renderer->ops = &vk_renderer_ops;

    return CERR_OK;
}
