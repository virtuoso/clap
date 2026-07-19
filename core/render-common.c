// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "render.h"
#include "shader_constants.h"
#undef IMPLEMENTOR

#include "object.h"
#include "linmath.h"

/****************************************************************************
 * Refclass definitions for render types
 *
 * These must be defined exactly once; each backend's _make/_drop was
 * previously its own static, causing duplicate-symbol errors in
 * multirenderer builds.
 ****************************************************************************/

#define r_op(_o, _op) (_o)->renderer->ops->_op

#define DEFINE_RENDERER_REFCLASS(_type, _init, _done) \
static cerr _type ## _make(struct ref *ref, void *_opts) \
{ \
    _type ## _t *x = container_of(ref, _type ## _t, ref); \
    rc_init_opts(_type) *opts = _opts; \
 \
    x->renderer = opts->renderer; \
    _init; \
    return CERR_OK; \
} \
 \
static void _type ## _drop(struct ref *ref) \
{ \
    _type ## _t *x = container_of(ref, _type ## _t, ref); \
    err_on(!x->renderer, "no renderer\n"); \
    _done; \
} \
DEFINE_REFCLASS2(_type)

DEFINE_RENDERER_REFCLASS(buffer,,       r_op(x, buf_deinit)(x));
DEFINE_RENDERER_REFCLASS(vertex_array,, if (r_op(x, va_done)) r_op(x, va_done)(x));
DEFINE_RENDERER_REFCLASS(texture,,      r_op(x, tex_deinit)(x));
DEFINE_RENDERER_REFCLASS(fbo,, {
    draw_control_cache_drop(x->renderer, x);
    r_op(x, fbo_destroy)(x);
});
DEFINE_RENDERER_REFCLASS(uniform_buffer,
    if (opts->binding < 0) return CERR_INVALID_ARGUMENTS,
    r_op(x, ubo_destroy)(x)
);

/****************************************************************************
 * Draw control cache
 *
 * One per renderer (renderer_t::dc_cache), holding the only reference to
 * each draw_control. Entries are keyed on (shader, render target shape)
 * and dropped with the fbo or shader they were created against, so a
 * rebuilt pipeline doesn't leave stale pipelines behind.
 ****************************************************************************/

static cerr draw_control_make(struct ref *ref, void *_opts)
{
    draw_control_t *dc = container_of(ref, draw_control_t, ref);
    rc_init_opts(draw_control) *opts = _opts;
    renderer_t *r = opts->renderer;
    fbo_t *fbo = opts->fbo;

    if (!r || !opts->shader)    return CERR_INVALID_ARGUMENTS;

    dc->renderer    = r;
    dc->shader      = opts->shader;
    dc->fbo         = fbo;
    if (fbo) {
        memcpy(dc->att_map, fbo->att_map, sizeof(dc->att_map));
        dc->no_blend_mask   = fbo->no_blend_mask;
        dc->nr_samples      = fbo->nr_samples ? : 1;
        dc->has_depth       = fbo->layout.depth_texture;
        if (dc->has_depth) {
            dc->depth_format = fbo->depth_config.format;
            dc->depth_fn     = fbo->depth_config.depth_func;
        }
    } else {
        dc->nr_samples      = 1;
        dc->swapchain_gen   = r->swapchain_gen;
    }

    CERR_RET_CERR(r->ops->dc_init(dc, opts));

    list_append(&r->dc_cache, &dc->cache_entry);
    list_append(&dc->shader->dc_list, &dc->shader_entry);

    return CERR_OK;
}

static void draw_control_drop(struct ref *ref)
{
    draw_control_t *dc = container_of(ref, draw_control_t, ref);

    err_on(!dc->renderer, "no renderer\n");
    if (!dc->renderer)  return;

    list_del(&dc->cache_entry);
    list_del(&dc->shader_entry);
    if (dc->renderer->ops->dc_done) dc->renderer->ops->dc_done(dc);
}

DEFINE_REFCLASS2(draw_control);

static bool draw_control_matches(draw_control_t *dc, fbo_t *fbo, shader_t *shader,
                                 unsigned int swapchain_gen)
{
    if (dc->shader != shader || !dc->fbo != !fbo)   return false;
    if (!fbo)                                       return dc->swapchain_gen == swapchain_gen;

    if (memcmp(dc->att_map, fbo->att_map, sizeof(dc->att_map)) ||
        dc->no_blend_mask != fbo->no_blend_mask ||
        dc->nr_samples != (fbo->nr_samples ? : 1) ||
        dc->has_depth != (bool)fbo->layout.depth_texture)
        return false;

    return !dc->has_depth || (dc->depth_format == fbo->depth_config.format &&
                              dc->depth_fn == fbo->depth_config.depth_func);
}

cresp(draw_control) draw_control_get(renderer_t *r, fbo_t *fbo, shader_t *shader)
{
    draw_control_t *dc;

    list_for_each_entry(dc, &r->dc_cache, cache_entry)
        if (draw_control_matches(dc, fbo, shader, r->swapchain_gen))    return cresp_val(draw_control, dc);

    return ref_new_checked(draw_control, .renderer = r, .fbo = fbo, .shader = shader);
}

void draw_control_cache_drop(renderer_t *r, fbo_t *fbo)
{
    draw_control_t *dc, *iter;

    list_for_each_entry_iter(dc, iter, &r->dc_cache, cache_entry)
        if (dc->fbo == fbo) ref_put_last(dc);
}

void draw_control_cache_done(renderer_t *r)
{
    draw_control_t *dc, *iter;

    list_for_each_entry_iter(dc, iter, &r->dc_cache, cache_entry)
        ref_put_last(dc);
}

static const char *render_str[RENDER_MAX] = {
    [RENDER_OPENGL] = "OpenGL",
    [RENDER_METAL]  = "Metal",
    [RENDER_WGPU]   = "WebGPU",
};

static const char *render_shader_ext[RENDER_MAX] = {
    [RENDER_OPENGL] = ".glsl",
    [RENDER_METAL]  = ".msl",
    [RENDER_WGPU]   = ".wgsl",
};

const char *renderer_shader_ext(renderer_t *r)
{
    return render_shader_ext[renderer_get_caps(r)->renderer];
}

const renderer_caps *renderer_get_caps(renderer_t *r)
{
    return r->ops->get_caps();
}

void renderer_mat4x4_ortho(renderer_t *renderer, mat4x4 M, float l, float r, float b, float t, float n, float f)
{
    renderer_get_caps(renderer)->ndc_z_zero_one
        ? mat4x4_ortho_ndc_z_1(M, l, r, b, t, n, f)
        : mat4x4_ortho_ndc_z_2(M, l, r, b, t, n, f);
}

void renderer_mat4x4_perspective(renderer_t *renderer, mat4x4 m, float y_fov, float aspect, float n, float f)
{
    renderer_get_caps(renderer)->ndc_z_zero_one
	    ? mat4x4_perspective_ndc_z_1(m, y_fov, aspect, n, f)
	    : mat4x4_perspective_ndc_z_2(m, y_fov, aspect, n, f);
}

int renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    return renderer->ops->query_limits(renderer, limit);
}

cerr _renderer_init(renderer_t *renderer, const renderer_init_options *opts)
{
    if (!renderer || !opts)
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "renderer: %p opts: %p",
            .arg0   = renderer,
            .arg1   = (void *)opts
        );

    if ((unsigned)opts->backend >= RENDER_MAX)
        return CERR_NOT_SUPPORTED_REASON(
            .fmt    = "requested rendering backend %d does not exist",
            .arg0   = (void *)(uintptr_t)opts->backend
        );

    memset(renderer, 0, sizeof(*renderer));
    list_init(&renderer->dc_cache);

    switch (opts->backend) {
        case RENDER_OPENGL:     CERR_RET_CERR(gl_renderer_setup(renderer, opts));   break;
        case RENDER_METAL:      CERR_RET_CERR(mtl_renderer_setup(renderer, opts));  break;
        case RENDER_WGPU:       CERR_RET_CERR(wgpu_renderer_setup(renderer, opts)); break;
        default:                return CERR_NOT_SUPPORTED_REASON(
                                    .fmt    = "requested rendering backend %s is not supported",
                                    .arg0   = render_str[opts->backend]
                                );
    }

    /*
     * The backend TU defines IMPLEMENTOR and sees the real backend
     * types in renderer_t where this TU sees same-size stand-ins; a
     * stand-in whose size drifted splits renderer_t into two layouts.
     */
    if (renderer->ops->renderer_size != sizeof(renderer_t))
        return CERR_INITIALIZATION_FAILED_REASON(
            .fmt    = "%s sizeof(renderer_t) %zu != %zu: IMPLEMENTOR type shim drifted",
            .arg0   = render_str[opts->backend],
            .arg1   = (void *)renderer->ops->renderer_size,
            .arg2   = (void *)sizeof(renderer_t)
        );

    return renderer->ops->init(renderer, opts);
}

void renderer_done(renderer_t *renderer)
{
    if (!renderer->ops->done)   return;
    renderer->ops->done(renderer);
}

void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile)
{
    if (!renderer->ops->set_version)    return;
    renderer->ops->set_version(renderer, major, minor, profile);
}

void renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    r->ops->viewport(r, x, y, width, height);
}

void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight)
{
    r->ops->get_viewport(r, px, py, pwidth, pheight);
}

bool renderer_supports_edr(renderer_t *r)
{
    if (!r->ops->supports_edr)  return false;
    return r->ops->supports_edr(r);
}

void renderer_hdr_enable(renderer_t *r, bool enable)
{
    if (!r->ops->hdr_enable)    return;
    r->ops->hdr_enable(r, enable);
}

void renderer_hdr_transfer_set(renderer_t *r, int transfer)
{
    if (!r->ops->hdr_transfer_set)  return;
    r->ops->hdr_transfer_set(r, transfer);
}

int renderer_hdr_transfer(renderer_t *r)
{
    if (!r->ops->hdr_transfer_get)  return HDR_TRANSFER_PQ;
    return r->ops->hdr_transfer_get(r);
}

void renderer_swapchain_begin(renderer_t *r)
{
    r->ops->swapchain_begin(r);
}

void renderer_frame_begin(renderer_t *r)
{
    if (!r->ops->frame_begin)   return;
    r->ops->frame_begin(r);
}

void renderer_frame_end(renderer_t *r)
{
    if (!r->ops->frame_end) return;
    r->ops->frame_end(r);
}

void renderer_swapchain_end(renderer_t *r)
{
    if (!r->ops->swapchain_end) return;
    r->ops->swapchain_end(r);
}

#ifndef CONFIG_FINAL
#include "ui-debug.h"

/*
 * Per-pass GPU times, for whichever backend filled renderer::pass_times
 * in. The plot history has to keep running while the window is folded
 * away, or it comes back empty.
 */
static void renderer_pass_times_debug(renderer_t *r)
{
    static debug_plot pass_plots[RENDERER_PASS_TIMERS_MAX], total_plot = {
        .fmt        = "total: %.3f ms",
        .scale_max  = 16.0,
        .size       = { 200.0, 40.0 },
    };
    static char pass_fmts[RENDERER_PASS_TIMERS_MAX][64];
    static char pass_labels[RENDERER_PASS_TIMERS_MAX][16];
    float total = 0.0;

    for (unsigned int i = 0; i < r->pass_times.count; i++) {
        debug_plot *plot = &pass_plots[i];

        snprintf(pass_fmts[i], sizeof(pass_fmts[i]), "%s: %%.3f ms",
                 r->pass_times.names[i]);
        snprintf(pass_labels[i], sizeof(pass_labels[i]), "##p%u", i);
        plot->fmt = pass_fmts[i];
        plot->scale_max = 8.0;
        plot->size[0] = 200.0;
        plot->size[1] = 40.0;
        debug_plot_push(plot, r->pass_times.ms[i]);
        total += r->pass_times.ms[i];
    }

    /* backends that can only time the whole frame report just that */
    if (!r->pass_times.count)   total = r->pass_times.total_ms;
    if (total > 0.0)            debug_plot_push(&total_plot, total);

    debug_module *dbgm = ui_igBegin(DEBUG_RENDERER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display) return;

    if (dbgm->unfolded) {
        for (unsigned int i = 0; i < r->pass_times.count; i++)  ui_igDebugPlotLines(pass_labels[i], &pass_plots[i]);
        if (total > 0.0)                                        ui_igDebugPlotLines("##ptotal", &total_plot);
    }

    ui_igEnd(DEBUG_RENDERER);
}
#endif /* !CONFIG_FINAL */

void renderer_debug(renderer_t *r)
{
#ifndef CONFIG_FINAL
    renderer_pass_times_debug(r);
#endif /* !CONFIG_FINAL */
    if (!r->ops->debug) return;
    r->ops->debug(r);
}

void renderer_cull_face(renderer_t *r, cull_face cull)
{
    r->ops->cull_face(r, cull);
}

void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor)
{
    r->ops->blend(r, _blend, sfactor, dfactor);
}

cerr renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                   data_type idx_type, unsigned int nr_instances)
{
    return r->ops->draw(r, draw_type, nr_faces, idx_type, nr_instances);
}

cerr _buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    renderer_t *r = opts->renderer;
    cerr err = r->ops->buf_init(buf, opts);
    buf->renderer = r;
    return err;
}

void buffer_deinit(buffer_t *buf)
{
    if (!buf->renderer)   return;
    buf->renderer->ops->buf_deinit(buf);
}

void buffer_bind(buffer_t *buf, uniform_t loc)
{
    if (!buf->renderer)   return;
    buf->renderer->ops->buf_bind(buf, loc);
}

void buffer_unbind(buffer_t *buf, uniform_t loc)
{
    if (!buf->renderer)   return;
    buf->renderer->ops->buf_unbind(buf, loc);
}

bool buffer_loaded(buffer_t *buf)
{
    return buf->loaded;
}

#ifndef CONFIG_FINAL
cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...)
{
    if (!buf->renderer || !buf->renderer->ops->buf_set_name)
        return cres_val(int, 0);

    va_list ap;
    va_start(ap, fmt);
    char label[128];
    vsnprintf(label, sizeof(label), fmt, ap);
    va_end(ap);

    buf->renderer->ops->buf_set_name(buf, label);

    return cres_val(int, 0);
}
#endif /* CONFIG_FINAL */

cerr vertex_array_init(vertex_array_t *va, renderer_t *r)
{
    cerr err = r->ops->va_init(va, r);
    va->renderer = r;
    return err;
}

void vertex_array_done(vertex_array_t *va)
{
    if (!va->renderer || !va->renderer->ops->va_done)    return;
    va->renderer->ops->va_done(va);
}

void vertex_array_bind(vertex_array_t *va)
{
    if (!va->renderer || !va->renderer->ops->va_bind)    return;
    va->renderer->ops->va_bind(va);
}

void vertex_array_unbind(vertex_array_t *va)
{
    if (!va->renderer || !va->renderer->ops->va_unbind)  return;
    va->renderer->ops->va_unbind(va);
}

void draw_control_bind(draw_control_t *dc)
{
    if (!dc->renderer || !dc->renderer->ops->dc_bind)    return;
    dc->renderer->ops->dc_bind(dc);
}

void draw_control_unbind(draw_control_t *dc)
{
    if (!dc->renderer || !dc->renderer->ops->dc_unbind)  return;
    dc->renderer->ops->dc_unbind(dc);
}

bool texture_loaded(texture_t *tex)
{
    return tex->loaded;
}

bool texture_is_multisampled(texture_t *tex)
{
    return tex && tex->multisampled;
}

cerr _texture_init(texture_t *tex, const texture_init_options *opts)
{
    renderer_t *r = opts->renderer;
    cerr err = r->ops->tex_init(tex, opts);
    tex->renderer = r;
    return err;
}

void texture_deinit(texture_t *tex)
{
    if (!tex->renderer)   return;
    tex->renderer->ops->tex_deinit(tex);
}

cerr texture_load(texture_t *tex, texture_format format,
                  unsigned int width, unsigned int height, void *buf)
{
    if (!tex->renderer)   return CERR_INVALID_ARGUMENTS;
    return tex->renderer->ops->tex_load(tex, format, width, height, buf);
}

cerr texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!tex->renderer)   return CERR_INVALID_ARGUMENTS;
    return tex->renderer->ops->tex_resize(tex, width, height);
}

void texture_bind(texture_t *tex, unsigned int target, uniform_t uniform)
{
    if (!tex->renderer)   return;
    tex->renderer->ops->tex_bind(tex, target, uniform);
}

void texture_unbind(texture_t *tex, unsigned int target)
{
    if (!tex->renderer || !tex->renderer->ops->tex_unbind)    return;
    tex->renderer->ops->tex_unbind(tex, target);
}

bool texture_is_array(texture_t *tex)
{
    if (!tex->renderer)   return false;
    return tex->renderer->ops->tex_is_array(tex);
}

#ifndef CONFIG_FINAL
cres(int) texture_set_name(texture_t *tex, const char *fmt, ...)
{
    if (!tex->renderer || !tex->renderer->ops->tex_set_name)
        return cres_val(int, 0);

    va_list ap;
    va_start(ap, fmt);
    char label[128];
    vsnprintf(label, sizeof(label), fmt, ap);
    va_end(ap);

    tex->renderer->ops->tex_set_name(tex, label);

    return cres_val(int, 0);
}
#endif /* CONFIG_FINAL */

texture_t *texture_clone(texture_t *tex)
{
    if (!tex || !tex->renderer)     return NULL;
    return tex->renderer->ops->tex_clone(tex);
}

texid_t texture_id(texture_t *tex)
{
    if (!tex || !tex->renderer)     return 0;
    return tex->renderer->ops->tex_id(tex);
}

bool texture_format_supported(renderer_t *r, texture_format format)
{
    return r->ops->tex_format_supported(r, format);
}

void texture_done(texture_t *tex)
{
    if (tex && !ref_is_static(&tex->ref))
        ref_put_last(tex);
}

void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight)
{
    if (pwidth)
        *pwidth = tex ? tex->width : 0;
    if (pheight)
        *pheight = tex ? tex->height : 0;
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

void fbo_prepare(fbo_t *fbo)
{
    if (!fbo->renderer)   return;
    fbo->renderer->ops->fbo_prepare(fbo);
}

void fbo_done(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo->renderer)   return;
    fbo->renderer->ops->fbo_done(fbo, width, height);
}

void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, fbo_attachment attachment)
{
    if (!fbo->renderer || !fbo->renderer->ops->fbo_blit)  return;
    fbo->renderer->ops->fbo_blit(fbo, src_fbo, attachment);
}

cerr fbo_resize(fbo_t *fbo, unsigned int width, unsigned int height)
{
    if (!fbo->renderer)   return CERR_INVALID_ARGUMENTS;
    return fbo->renderer->ops->fbo_resize(fbo, width, height);
}

bool fbo_attachment_valid(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo->renderer)   return false;
    return fbo->renderer->ops->fbo_attachment_valid(fbo, attachment);
}

texture_format fbo_attachment_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo->renderer)   return 0;
    return fbo->renderer->ops->fbo_attachment_format(fbo, attachment);
}

texture_t *fbo_texture(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo || !fbo->renderer)   return NULL;
    return fbo->renderer->ops->fbo_tex(fbo, attachment);
}

texture_format fbo_texture_format(fbo_t *fbo, fbo_attachment attachment)
{
    if (!fbo || !fbo->renderer)   return TEX_FMT_DEFAULT;
    return fbo->renderer->ops->fbo_tex_format(fbo, attachment);
}

bool fbo_texture_supported(renderer_t *r, texture_format format)
{
    return r->ops->fbo_tex_supported(r, format);
}

/* pack the attachment formats and no_blend bits into the fbo's pipeline key */
static void fbo_att_map_init(fbo_t *fbo)
{
    unsigned int nr = fa_nr_color_buffer(fbo->layout) ? : fa_nr_color_texture(fbo->layout);

    for (unsigned int idx = 0; idx < nr; idx++) {
        /* offset by one: an empty slot must not read as TEX_FMT_DEFAULT */
        fbo->att_map[idx / ATTACHMENTS_PER_LONG] |=
            (unsigned long)(fbo->color_config[idx].format + 1) <<
            ((idx % ATTACHMENTS_PER_LONG) * TEX_FMT_BITS);
        if (fbo->color_config[idx].no_blend)    fbo->no_blend_mask |= 1u << idx;
    }
}

cresp(fbo_t) _fbo_new(const fbo_init_options *opts)
{
    if (!opts || !opts->renderer)
        return cresp_error(fbo_t, CERR_INVALID_ARGUMENTS);

    fbo_t *fbo = CRES_RET_T(opts->renderer->ops->fbo_create(opts), fbo_t);
    fbo_att_map_init(fbo);

    return cresp_val(fbo_t, fbo);
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
 * Uniform buffers
 ****************************************************************************/

cerr uniform_buffer_init(renderer_t *r, uniform_buffer_t *ubo, const char *name, int binding)
{
    cerr err = r->ops->ubo_init(r, ubo, name, binding);
    ubo->renderer = r;
    return err;
}

void uniform_buffer_done(uniform_buffer_t *ubo) { if (!ubo->renderer) return; ubo->renderer->ops->ubo_done(ubo); }
cerr uniform_buffer_data_alloc(uniform_buffer_t *ubo, size_t size) { return ubo->renderer->ops->ubo_data_alloc(ubo, size); }
cerr uniform_buffer_bind(uniform_buffer_t *ubo, binding_points_t *binding_points) { return ubo->renderer->ops->ubo_bind(ubo, binding_points); }
void uniform_buffer_update(uniform_buffer_t *ubo, binding_points_t *binding_points) { ubo->renderer->ops->ubo_update(ubo, binding_points); }

cerr uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                        unsigned int count, const void *value)
{
    return ubo->renderer->ops->ubo_set(ubo, type, offset, size, count, value);
}

/****************************************************************************
 * Shaders
 ****************************************************************************/

cerr shader_init(renderer_t *r, shader_t *shader,
                 const char *vertex, size_t vert_size,
                 const char *geometry, size_t geom_size,
                 const char *fragment, size_t frag_size)
{
    list_init(&shader->dc_list);
    cerr err = r->ops->shader_init(r, shader, vertex, vert_size, geometry, geom_size,
                                   fragment, frag_size);
    shader->renderer = r;
    return err;
}

void shader_done(shader_t *shader)
{
    if (!shader->renderer)  return;

    /* the pipelines cached against this shader go with it */
    draw_control_t *dc, *iter;
    list_for_each_entry_iter(dc, iter, &shader->dc_list, shader_entry)
        ref_put_last(dc);

    shader->renderer->ops->shader_done(shader);
}

void shader_set_vertex_attrs(shader_t *shader, size_t stride,
                             size_t *offs, data_type *types, size_t *comp_counts,
                             unsigned int *locs, unsigned int nr_attrs)
{
    if (!shader->renderer)   return;
    shader->renderer->ops->shader_set_vertex_attrs(shader, stride, offs, types, comp_counts,
                                                   locs, nr_attrs);
}

int shader_id(shader_t *shader) { if (!shader->renderer) return -1; return shader->renderer->ops->shader_id(shader); }

cerr shader_uniform_buffer_bind(shader_t *shader, binding_points_t *bpt, const char *name)
{
    if (!shader->renderer)   return CERR_INVALID_ARGUMENTS;
    return shader->renderer->ops->shader_ubo_bind(shader, bpt, name);
}

attr_t shader_attribute(shader_t *shader, const char *name, attr_t attr)
{
    if (!shader->renderer)   return attr;
    return shader->renderer->ops->shader_attribute(shader, name, attr);
}

uniform_t shader_uniform(shader_t *shader, const char *name)
{
    if (!shader->renderer)   return -1;
    return shader->renderer->ops->shader_uniform(shader, name);
}

cerr shader_use(shader_t *shader, bool draw) { if (!shader->renderer) return CERR_INVALID_ARGUMENTS; return shader->renderer->ops->shader_use(shader, draw); }
void shader_unuse(shader_t *shader, bool draw) { if (!shader->renderer) return; shader->renderer->ops->shader_unuse(shader, draw); }

cres(size_t) shader_uniform_offset_query(shader_t *shader, const char *ubo_name, const char *var_name)
{
    if (!shader->renderer)   return cres_error(size_t, CERR_INVALID_ARGUMENTS);
    return shader->renderer->ops->shader_ubo_offset_query(shader, ubo_name, var_name);
}

void shader_set_name(shader_t *shader, const char *name)
{
    if (!shader->renderer || !shader->renderer->ops->shader_set_name)
        return;
    shader->renderer->ops->shader_set_name(shader, name);
}


/****************************************************************************
 * UBO packing: std140 and the like
 ****************************************************************************/

/* Get std140 storage size for a given type */
static inline size_t type_storage_size(data_type type)
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

/*
 * Calculate uniform @offset within a UBO, its total @size and set its @value
 * if @value is not NULL
 */
cerr _uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                         unsigned int count, const void *value)
{
    size_t elem_size = data_type_size(type);       /* C ABI element size */
    size_t storage_size = type_storage_size(type); /* Metal-aligned size */
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
     * A lone vec2 has 2N (8 byte) base alignment on all backends (GLSL
     * std140, MSL float2, WGSL uniform space); vec3/vec4/matrix columns
     * align to 16. Non-arrayed scalars have maximum storage size of 4
     * and pack tightly, including into a vec3's tail.
     */
    if (storage_size == 8)
        *offset = round_up(*offset, 8);
    else if (storage_size > 4 && *offset % 16)
        *offset = round_up(*offset, 16);

    *size = *offset;

    /*
     * Copy elements from a C array to a UBO array. Metal UBO layout, like
     * std140, uses fun alignments for various types that don't match C at
     * all, they need to be copied one at a time.
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
                bool is_mat3 = type == DT_MAT3;
                int rows = is_mat3 ? 3 : 2;
                /* Source row: DT_VEC2 or DT_VEC3 */
                size_t row_size = data_type_size(is_mat3 ? DT_VEC3 : DT_VEC2);
                /* Destination row: DT_VEC4 */
                size_t row_stride = type_storage_size(DT_VEC4);
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

cerr_check texture_pixel_init_type(renderer_t *renderer, texture_t *tex, texture_type type, float color[4])
{
    cerr err = texture_init(tex, .renderer = renderer, .type = type);
    if (IS_CERR(err))
        return err;

    texture_format fmt = TEX_FMT_RGBA8;
    if (texture_format_supported(renderer, TEX_FMT_RGBA32F))
        for (size_t i = 0; i < 4; i++)
            if (color[i] > 1.0f) {
                fmt = TEX_FMT_RGBA32F;
                break;
            }

    if (fmt == TEX_FMT_RGBA8) {
        uchar _color[] = { color[0] * 255.0, color[1] * 255.0, color[2] * 255.0, color[3] * 255.0 };
        return texture_load(tex, fmt, 1, 1, _color);
    }

    return texture_load(tex, fmt, 1, 1, color);
}

cerr_check texture_pixel_init(renderer_t *renderer, texture_t *tex, float color[4])
{
    return texture_pixel_init_type(renderer, tex, TEX_2D, color);
}

static texture_t _white_pixel;
static texture_t _black_pixel;
static texture_t _transparent_pixel;
static texture_t _grey_3d_pixel;
static texture_t _black_3d_pixel;
static texture_t _black_uint_pixel;

texture_t *white_pixel(void) { return &_white_pixel; }
texture_t *black_pixel(void) { return &_black_pixel; }
texture_t *transparent_pixel(void) { return &_transparent_pixel; }
texture_t *grey_3d_pixel(void) { return &_grey_3d_pixel; }
texture_t *black_3d_pixel(void) { return &_black_3d_pixel; }
texture_t *black_uint_pixel(void) { return &_black_uint_pixel; }

void textures_init(renderer_t *renderer)
{
    cerr werr, berr, terr, g3err, b3err, buerr;

    float white[] = { 1, 1, 1, 1 };
    werr = texture_pixel_init(renderer, &_white_pixel, white);
    float black[] = { 0, 0, 0, 1 };
    berr = texture_pixel_init(renderer, &_black_pixel, black);
    float transparent[] = { 0, 0, 0, 0 };
    terr = texture_pixel_init(renderer, &_transparent_pixel, transparent);
    float grey[] = { 0.5, 0.5, 0.5, 1.0 };
    g3err = texture_pixel_init_type(renderer, &_grey_3d_pixel, TEX_3D, grey);
    b3err = texture_pixel_init_type(renderer, &_black_3d_pixel, TEX_3D, black);

    buerr = texture_init(&_black_uint_pixel, .renderer = renderer, .type = TEX_2D,
                         .format = TEX_FMT_RGBA32UI,
                         .min_filter = TEX_FLT_NEAREST, .mag_filter = TEX_FLT_NEAREST);
    if (!IS_CERR(buerr)) {
        uint32_t zeros[4] = { 0, 0, 0, 0 };
        buerr = texture_load(&_black_uint_pixel, TEX_FMT_RGBA32UI, 1, 1, zeros);
    }

    err_on(IS_CERR(werr) || IS_CERR(berr) || IS_CERR(terr) || IS_CERR(g3err) || IS_CERR(b3err) || IS_CERR(buerr),
           "failed: %d/%d/%d/%d/%d/%d\n",
           CERR_CODE(werr), CERR_CODE(berr), CERR_CODE(terr), CERR_CODE(g3err), CERR_CODE(b3err), CERR_CODE(buerr));

    texture_set_name(&_white_pixel, "white pixel");
    texture_set_name(&_black_pixel, "black pixel");
    texture_set_name(&_transparent_pixel, "transparent pixel");
    texture_set_name(&_black_3d_pixel, "black 3D pixel");
    texture_set_name(&_grey_3d_pixel, "grey 3D pixel");
    texture_set_name(&_black_uint_pixel, "black uint pixel");
}

void textures_done(void)
{
    /*
     * These are embedded refs, which texture_done() deliberately skips
     * (clones share the GPU objects of embedded textures elsewhere);
     * release the GPU side explicitly, nothing clones the singletons
     */
    texture_deinit(&_white_pixel);
    texture_deinit(&_black_pixel);
    texture_deinit(&_transparent_pixel);
    texture_deinit(&_black_3d_pixel);
    texture_deinit(&_grey_3d_pixel);
    texture_deinit(&_black_uint_pixel);
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

void buffer_debug_header(void)
{
    ui_igTableHeader(
        "buffers",
        (const char *[]) { "attribute", "binding", "size", "type", "usage", "offset", "size", "comp" },
        8
    );
}

void buffer_debug(buffer_t *buf, const char *name)
{
    buffer_init_options *opts = &buf->opts;

    ui_igTableCell(true, "%s", name);
    ui_igTableCell(false, "%d", buf->loc);
    ui_igTableCell(false, "%zu", opts->size);
    ui_igTableCell(false, "%s", buffer_type_str(opts->type));
    ui_igTableCell(false, "%s", buffer_usage_str(opts->usage));
    ui_igTableCell(false, "%u", buf->off);
    ui_igTableCell(false, "%u", buf->comp_count * data_comp_size(opts->comp_type));
    ui_igTableCell(false, "%s (%d) x %d",
                   data_type_name(opts->comp_type),
                   data_comp_size(opts->comp_type),
                   opts->comp_count);
}

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
    ui_igTableCell(false, texture_format_string(opts->format));
    if (tex->layers)
        ui_igTableCell(false, "%d x %d x %d", tex->width, tex->height, tex->layers);
    else
        ui_igTableCell(false, "%d x %d", tex->width, tex->height);
    ui_igTableCell(false, texture_wrap_str[opts->wrap]);
    ui_igTableCell(false, texture_filter_str[opts->min_filter]);
    ui_igTableCell(false, texture_filter_str[opts->mag_filter]);
    ui_igTableCell(false, "%s", tex->multisampled ? "ms" : "");
}
#endif /* !CONFIG_FINAL */
