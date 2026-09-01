#include "light.h"
#include "display.h"
#include "messagebus.h"
#include "scene.h"
#include "ui-debug.h"
#include "util.h"
#include "view.h"

void light_set_ambient(struct light *light, const float *color)
{
    vec3_dup(light->ambient, color);
}

void light_set_shadow_tint(struct light *light, const float *color)
{
    vec3_dup(light->shadow_tint, color);
}

bool light_is_valid(struct light *light, int idx)
{
    if (idx < 0 || idx >= light->nr_lights)
        return false;

    return bitmap_is_set(&light->active, idx);
}

bool light_is_directional(struct light *light, int idx)
{
    if (!light_is_valid(light, idx))
        return false;

    return light->is_dir[idx];
}

/*
 * A very basic implementation of clustered lighting
 *
 * light::grid is a grid of tiles made of RGBA32UI (128-bit) that is used
 * as a bitmask for up to 128 light sources. This texture communicates to
 * the fragment shader which light sources to apply for a fragment, thereby
 * allowing for more light sources in the scene without blowing out the GPU.
 *
 * The tile count comes from the window size and the nominal cell size,
 * but the tiles then split the screen evenly, in fractions rather than
 * pixels, so a pass rendering at a fraction of the window (render_scale)
 * lands on the same tiles through its own framebuffer fraction.
 */
#define LIGHT_MASK_SIZE sizeof(ui32vec4)

static void light_grid_update(struct light *light)
{
    if (!light->grid.width || !light->grid.height || !light->grid.cell) return;

    auto grid = &light->grid;
    auto twidth = (unsigned int)ceilf((float)grid->width / grid->cell);
    auto theight = (unsigned int)ceilf((float)grid->height / grid->cell);

    if (twidth * theight == grid->twidth * grid->theight)   return;
    if (!twidth || !theight)    return;

    if (texture_loaded(&grid->tex))
        CERR_RET(
            texture_resize(&grid->tex, twidth, theight),
            { err_cerr(__cerr, "grid texture resize failed\n"); return; }
        );

    mem_free(grid->tiles);
    grid->twidth = grid->theight = 0;
    grid->tiles = NULL;

    auto new_tiles = mem_alloc(LIGHT_MASK_SIZE, .nr = twidth * theight);
    if (!new_tiles)  return;

    grid->twidth = twidth;
    grid->theight = theight;
    grid->tiles = new_tiles;
}

static ui32vec4 *grid_get(struct light_grid *grid, unsigned int x, unsigned int y)
{
    if (x >= grid->twidth || y >= grid->theight)    return NULL;
    return &grid->tiles[y * grid->twidth + x];
}

static void ui32vec4_set(ui32vec4 *v, unsigned int idx)
{
    if (idx >= sizeof(v->v) * 8)    return;

    static const size_t bits_per_comp = sizeof(*v->v) * 8;
    size_t comp = idx / bits_per_comp;
    v->v[comp] |= 1u << (idx - comp * bits_per_comp);
}

/**
 * cone_outside_plane() - test a spotlight cone against a plane
 * @n:      the plane's unit normal, pointing to the inside
 * @d:      the plane's offset: a point x is inside when dot(n, x) + d >= 0
 * @apex:   the cone's apex, the light's position
 * @cap:    the center of the cone's cap disk
 * @axis:   the cone's unit axis, apex to cap
 * @rim:    the cap disk's radius
 *
 * A solid spotlight cone is the convex hull of its apex and its cap
 * disk (center apex + range * axis, radius range * tan(angle)), so it
 * lies entirely behind a plane iff both the apex and the disk's
 * closest-to-the-plane rim point do: the rim extreme along a unit
 * normal n is dot(n, cap_center) + rim * sqrt(1 - dot(n, axis)^2).
 * The hull slightly overshoots the attenuation sphere's spherical cap
 * with the flat disk, which errs on the conservative side.
 * Return: true when the whole cone lies outside the plane
 */
static bool cone_outside_plane(const vec3 n, float d, const vec3 apex,
                               const vec3 cap, const vec3 axis, float rim)
{
    if (vec3_mul_inner(n, apex) + d >= 0.0f)    return false;

    float na = vec3_mul_inner(n, axis);
    float cap_d = vec3_mul_inner(n, cap) + d +
                  rim * sqrtf(fmaxf(1.0f - na * na, 0.0f));

    return cap_d < 0.0f;
}

/**
 * light_grid_spot_cone() - mark the tiles whose frusta a spotlight's cone intersects
 * @light:      lights
 * @idx:        the spotlight's index
 * @subview:    the camera's main subview, for the view and projection matrices
 * @grid:       the light grid to mark
 *
 * The tiles' side planes all pass through the camera, which makes view
 * space the cheap place to test: a screen-x tile boundary at ndc
 * x = nx is the plane fx*x + nx*z = 0 (from the projection
 * fx * x / -z = nx), and rows work the same through fy. The cone is
 * culled from a tile if it lies entirely outside any of the four
 * side planes (with inward-facing normals) or entirely in front of
 * the near plane; plane-wise separation can only over-include, never
 * over-cull. There is no depth bound per tile, matching the point
 * lights' screen-space circle.
 * Return: true when the cone was handled; false to fall back to the
 * circle path: wide cones (tan blows up), an unset direction, or a
 * divergent attenuation radius
 */
static bool light_grid_spot_cone(struct light *light, unsigned int idx,
                                 struct subview *subview, struct light_grid *grid)
{
    /* the soft rim spotlight_intensity() feathers over */
    float angle = light->cutoff[idx] + to_radians(5.0f);
    if (angle >= to_radians(85.0f))         return false;

    float range = light_get_radius(light, idx);
    if (!isfinite(range) || range <= 0.0f)  return false;

    /* view-space cone: stored dir points at the light, the beam away from it */
    vec4 apex_w, apex, axis_w, axis;
    vec3_dup(apex_w, &light->pos[idx * 3]);
    apex_w[3] = 1.0f;
    mat4x4_mul_vec4_post(apex, subview->view_mx, apex_w);

    vec3_scale(axis_w, &light->dir[idx * 3], -1.0f);
    axis_w[3] = 0.0f;
    mat4x4_mul_vec4_post(axis, subview->view_mx, axis_w);

    float axis_len = vec3_len(axis);
    if (axis_len < 1e-6f)                   return false;
    vec3_scale(axis, axis, 1.0f / axis_len);

    vec3 cap;
    vec3_scale(cap, axis, range);
    vec3_add(cap, cap, apex);
    float rim = range * tanf(angle);

    /* the camera looks down -z: skip a cone entirely closer than the near plane */
    vec3 near_n = { 0.0f, 0.0f, -1.0f };
    if (cone_outside_plane(near_n, -subview->near_plane, apex, cap, axis, rim)) return true;

    float fx = subview->proj_mx[0][0];
    float fy = subview->proj_mx[1][1];

    for (unsigned int gy = 0; gy < grid->theight; gy++) {
        float ny0 = 1.0f - 2.0f * gy / grid->theight;
        float ny1 = 1.0f - 2.0f * (gy + 1) / grid->theight;
        /* inward normals: top boundary faces down, bottom faces up */
        vec3 top = { 0.0f, -fy, -ny0 };
        vec3 bottom = { 0.0f, fy, ny1 };
        vec3_norm(top, top);
        vec3_norm(bottom, bottom);

        if (cone_outside_plane(top, 0.0f, apex, cap, axis, rim))    continue;
        if (cone_outside_plane(bottom, 0.0f, apex, cap, axis, rim)) continue;

        for (unsigned int gx = 0; gx < grid->twidth; gx++) {
            float nx0 = 2.0f * gx / grid->twidth - 1.0f;
            float nx1 = 2.0f * (gx + 1) / grid->twidth - 1.0f;
            vec3 left = { fx, 0.0f, nx0 };
            vec3 right = { -fx, 0.0f, -nx1 };
            vec3_norm(left, left);
            vec3_norm(right, right);

            if (cone_outside_plane(left, 0.0f, apex, cap, axis, rim))   continue;
            if (cone_outside_plane(right, 0.0f, apex, cap, axis, rim))  continue;

            auto v = grid_get(grid, gx, gy);
            if (v)  ui32vec4_set(v, idx);
        }
    }

    return true;
}

void light_grid_compute(struct light *light, struct view *view)
{
    light_grid_update(light);

    auto grid = &light->grid;
    if (!grid->twidth || !grid->theight || !grid->tiles)    return;

    memset(grid->tiles, 0, grid->twidth * grid->theight * sizeof(ui32vec4));
    mat4x4 mvp;
    auto subview = &view->main;
    float cell_w = (float)grid->width / grid->twidth;
    float cell_h = (float)grid->height / grid->theight;

    mat4x4_mul(mvp, subview->proj_mx, subview->view_mx);

    for (unsigned int idx = 0; idx < (unsigned int)light->nr_lights; idx++) {
        if (!bitmap_is_set(&light->active, idx)) continue;

        bool spot = light_is_spotlight(light, idx);

        /* plain directional light(s) always apply */
        if (light->is_dir[idx] && !spot) {
            for (unsigned int gy = 0; gy < grid->theight; gy++)
                for (unsigned int gx = 0; gx < grid->twidth; gx++) {
                    auto v = grid_get(grid, gx, gy);
                    if (v)  ui32vec4_set(v, idx);
                }
            continue;
        }

        /* spotlights only reach tiles their cone intersects */
        if (spot && light_grid_spot_cone(light, idx, subview, grid))    continue;

        vec4 light_pos;
        vec3_dup(light_pos, &light->pos[idx * 3]);
        light_pos[3] = 1.0f;

        /* light position in ndc and view space */
        vec4 light_pos_ndc, light_pos_view;
        mat4x4_mul_vec4_post(light_pos_view, subview->view_mx, light_pos);
        mat4x4_mul_vec4_post(light_pos_ndc, mvp, light_pos);
        vec3_scale(light_pos_ndc, light_pos_ndc, 1.0f / light_pos_ndc[3]);

        if (fabsf(light_pos_ndc[3]) < 1e-3) continue;

        if (light_pos_ndc[2] > 1.0)         continue;

        float fx = subview->proj_mx[0][0];
        float radius = light_get_radius(light, idx) * fx / -light_pos_view[2] * (grid->width / 2.0f);
        float rsq = radius * radius;

        vec2 light_pos_screen = {
            (light_pos_ndc[0] + 1.0f) / 2.0f * grid->width,
            (1.0f - light_pos_ndc[1]) / 2.0f * grid->height
        };

        for (unsigned int gy = 0; gy < grid->theight; gy++)
            for (unsigned int gx = 0; gx < grid->twidth; gx++) {
                auto v = grid_get(grid, gx, gy);
                if (!v) continue;

                /* test 4 corners of the tile */
                for (unsigned int corner = 0; corner < 4; corner++) {
                    vec2 dist;
                    vec2_sub(dist, light_pos_screen, (vec2) {
                        (gx + (corner & 1)) * cell_w,
                        (gy + !!(corner & 2)) * cell_h
                    });
                    float distsq = vec2_mul_inner(dist, dist);

                    if (distsq < rsq)   { ui32vec4_set(v, idx); break; }
                }
            }
        }
    CERR_RET(
        texture_load(&grid->tex, TEX_FMT_RGBA32UI, grid->twidth, grid->theight, grid->tiles),
        err_cerr(__cerr, "grid texture (%u x %u) load failed\n", grid->twidth, grid->theight);
    );
}

static int light_handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct light *light = data;

    if (m->type == MT_INPUT && m->input.resize) {
        light->grid.width  = m->input.x;
        light->grid.height = m->input.y;
    }

    return MSG_HANDLED;
}

#ifndef CONFIG_FINAL
static void light_hover(float x, float y, void *data)
{
    struct light *light = data;
    struct scene *scene = container_of(light, struct scene, light);

    if (!clap_get_render_options(scene->clap_ctx)->light_draws_enabled) return;

    auto grid = &light->grid;
    if (!grid->width || !grid->height)  return;

    auto gx = (unsigned int)(x / grid->width * grid->twidth);
    auto gy = (unsigned int)(y / grid->height * grid->theight);
    auto v  = grid_get(grid, gx, gy);

    if (v && igBeginTooltip()) {
        igText(
            "TILE %u, %u\n(%f, %f)\n%08x%08x%08x%08x",
            gx, gy, x, y, v->v[3], v->v[2], v->v[1], v->v[0]
        );
        igEndTooltip();
    }
}
#endif /* CONFIG_FINAL */

void light_init(struct clap_context *ctx, struct light *light)
{
    bitmap_init(&light->active, LIGHTS_MAX);

    clap_get_viewport(ctx, nullptr, nullptr, (int *)&light->grid.width, (int *)&light->grid.height);
    light_grid_update(light);

    CERR_RET(
        texture_init(
            &light->grid.tex,
            .renderer   = clap_get_renderer(ctx),
            .type       = TEX_2D,
            .format     = TEX_FMT_RGBA32UI,
            .min_filter = TEX_FLT_NEAREST,
            .mag_filter = TEX_FLT_NEAREST,
            .wrap       = TEX_CLAMP_TO_EDGE
        ),
        { err_cerr(__cerr, "grid texture init failed\n"); return; }
    );
    light->grid.cell = TILE_WIDTH;
    subscribe(ctx, MT_INPUT, light_handle_input, light);
#ifndef CONFIG_FINAL
    ui_debug_set_hover(light_hover, light);
#endif /* CONFIG_FINAL */
}

void light_done(struct clap_context *ctx, struct light *light)
{
    for (int idx = 0; idx < light->nr_lights; idx++) {
        if (!bitmap_is_set(&light->active, idx)) continue;
        if (light->update_cleanup[idx])
            light->update_cleanup[idx](light->update_data[idx]);
    }

    unsubscribe(ctx, MT_INPUT, light);
    mem_free(light->grid.tiles);
    texture_deinit(&light->grid.tex);
    bitmap_done(&light->active);
}

#ifndef CONFIG_FINAL
void light_draw_directions(struct clap_context *ctx, struct light *light)
{
    for (unsigned int idx = 0; idx < (unsigned int)light->nr_lights; idx++) {
        if (!bitmap_is_set(&light->active, idx)) continue;
        if (!light->draw_direction[idx]) continue;

        float *pos = &light->pos[idx * 3];
        float *dir = &light->dir[idx * 3];
        /* light->dir is stored negated; tip points the way the light shines */
        struct message dm_dir = {
            .type   = MT_DEBUG_DRAW,
            .debug_draw = (struct message_debug_draw) {
                .color      = { 1.0, 1.0, 0.0, 1.0 },
                .shape      = DEBUG_DRAW_LINE,
                .thickness  = 0.2,
                .v0         = { pos[0], pos[1], pos[2] },
                .v1         = { pos[0] - dir[0], pos[1] - dir[1], pos[2] - dir[2] },
            }
        };
        message_send(ctx, &dm_dir);
    }
}

void light_draw(struct clap_context *ctx, struct light *light)
{
    /* Here we go */
    for (unsigned int idx = 0; idx < (unsigned int)light->nr_lights; idx++) {
        if (!bitmap_is_set(&light->active, idx)) continue;
        if (light->is_dir[idx]) continue;
        float radius = light_get_radius(light, idx);
        float *center = &light->pos[idx * 3];

        struct message dm_light_center = {
            .type   = MT_DEBUG_DRAW,
            .debug_draw     = (struct message_debug_draw) {
                .color      = { 1.0, 0.0, 0.0, 1.0 },
                .radius     = 10.0,
                .shape      = DEBUG_DRAW_DISC,
                .v0         = { center[0], center[1], center[2] },
            }
        };
        message_send(ctx, &dm_light_center);

        struct message dm_light_disc = {
            .type   = MT_DEBUG_DRAW,
            .debug_draw     = (struct message_debug_draw) {
                .color      = { 1.0, 0.0, 0.0, 1.0 },
                .radius     = radius,
                .shape      = DEBUG_DRAW_CIRCLE,
                .thickness  = 0.2,
                .v0         = { center[0], center[1], center[2] },
            }
        };
        message_send(ctx, &dm_light_disc);
    }

    struct message dm_grid = {
        .type   = MT_DEBUG_DRAW,
        .debug_draw     = (struct message_debug_draw) {
            .color      = { 0.3, 0.3, 0.3, 0.5 },
            .shape      = DEBUG_DRAW_GRID,
            .cell       = 32,
            .thickness  = 0.2,
        }
    };
    message_send(ctx, &dm_grid);
}
#endif /* CONFIG_FINAL */

float light_get_radius(struct light *light, unsigned int idx)
{
    /* spotlights attenuate like point lights; only sun-likes are unbounded */
    if (light->is_dir[idx] && !light_is_spotlight(light, idx))  return 0.0f;

    float *color = &light->color[idx * 3];
    float comp_max = max3(color[0], color[1], color[2]);
    float *att = &light->attenuation[idx * 3];
    return (-att[1] + sqrtf(att[1] * att[1] - 4.0f * att[2] * (att[0] - comp_max / LIGHT_CUTOFF))) / (2.0f * att[2]);
}

cres(int) light_get(struct light *light)
{
    int idx = CRES_RET(bitmap_set_lowest(&light->active),
                       return cres_error(int, CERR_TOO_LARGE));

    if (idx >= light->nr_lights)
        light->nr_lights = idx + 1;

    light->pos[idx * 3]         = 0;
    light->pos[idx * 3 + 1]     = 0;
    light->pos[idx * 3 + 2]     = 0;
    light->color[idx * 3]       = 0;
    light->color[idx * 3 + 1]   = 0;
    light->color[idx * 3 + 2]   = 0;
    light->attenuation[idx * 3]     = 1;
    light->attenuation[idx * 3 + 1] = 0;
    light->attenuation[idx * 3 + 2] = 0;
    light->dir[idx * 3]         = 0;
    light->dir[idx * 3 + 1]     = 0;
    light->dir[idx * 3 + 2]     = 0;
    light->cutoff[idx]          = 0;
    light->volumetric[idx]      = 0;
    light->is_dir[idx]          = true;
    light->update[idx]          = NULL;
    light->update_cleanup[idx]  = NULL;
    light->update_data[idx]     = NULL;
    light->update_track[idx]    = NULL;

    return cres_val(int, idx);
}

void light_put(struct light *light, int idx)
{
    if (!light_is_valid(light, idx))
        return;

    if (light->update_cleanup[idx])
        light->update_cleanup[idx](light->update_data[idx]);
    light->update[idx]          = NULL;
    light->update_cleanup[idx]  = NULL;
    light->update_data[idx]     = NULL;
    light->update_track[idx]    = NULL;

    /*
     * Zero the slot so the shader sees no contribution from it on subsequent
     * uploads. nr_lights stays as a high-water mark; only shrink it if the
     * released slot was the topmost one, to keep upload range tight.
     */
    light->color[idx * 3]       = 0;
    light->color[idx * 3 + 1]   = 0;
    light->color[idx * 3 + 2]   = 0;
    light->is_dir[idx]          = false;
    light->cutoff[idx]          = 0;

    bitmap_clear(&light->active, idx);

    while (light->nr_lights > 0 && !bitmap_is_set(&light->active, light->nr_lights - 1))
        light->nr_lights--;
}

void light_set_update(struct light *light, int idx, light_update_fn fn,
                      void *data, transform_t *track, light_cleanup_fn cleanup)
{
    if (!light_is_valid(light, idx))
        return;

    if (light->update_cleanup[idx])
        light->update_cleanup[idx](light->update_data[idx]);

    light->update[idx]          = fn;
    light->update_cleanup[idx]  = cleanup;
    light->update_data[idx]     = data;
    light->update_track[idx]    = track;
}

/**
 * light_spot_view_update() - (re)build a spotlight's shadow view
 * @ctx:    clap context
 * @light:  lights
 * @idx:    the spotlight's index
 *
 * A square perspective projection covering the full beam -- twice the
 * cutoff half-angle plus the soft rim spotlight_intensity() feathers
 * over -- reaching as far as the light's attenuation radius, looking
 * from the light's position along its beam. Call it whenever either
 * changes.
 */
void light_spot_view_update(struct clap_context *ctx, struct light *light, int idx)
{
    struct view *lv = &light->view[idx];
    float range = light_get_radius(light, idx);

    lv->nr_cascades       = 1;
    lv->fov               = 2.0f * (light->cutoff[idx] + to_radians(5.0f));
    lv->main.near_plane   = 0.1f;
    lv->main.far_plane    = isfinite(range) && range > 0.0f ? range : 200.0f;
    lv->proj_update       = true;
    /* aspect of 1.0 -> square shadow map */
    view_update_perspective_projection(ctx, lv, 1, 1, 1.0f);

    /* stored dir points at the light; the beam looks away from it */
    vec3 beam;
    vec3_sub(beam, (vec3){}, &light->dir[idx * 3]);
    view_update_from_direction(ctx, lv, &light->pos[idx * 3], beam);
    view_calc_frustum(ctx, lv);

    /*
     * The pipeline's nr_cascades comes from the camera (typically 4 for CSM),
     * but the spotlight only needs one.  Fill the remaining cascade slots with
     * copies of subview[0] so the shader's cascade selection always finds a
     * valid shadow MVP instead of stale/uninitialized data.
     */
    for (int i = 1; i < CASCADES_MAX; i++) {
        lv->subview[i] = lv->subview[0];
        lv->divider[i] = lv->divider[0];
    }
}

void light_update_from_entity(struct light *light, int idx, void *data)
{
    struct scene *scene = container_of(light, struct scene, light);
    entity3d *e = data;
    vec3 pos;

    transform_pos(&e->xform, pos);
    if (!light_is_spotlight(light, idx)) {
        vec3_add(pos, pos, e->light_off);
        light_set_pos(light, idx, pos);
        return;
    }

    vec3 dir = { 0.0f, 0.0f, 1.0f }, off;
    vec3_dup(off, e->light_off);

    transform_rotate_vec3(&e->xform, dir);
    transform_rotate_vec3(&e->xform, off);
    light_set_direction(light, idx, dir);
    vec3_add(pos, pos, off);
    light_set_pos(light, idx, pos);

    light_spot_view_update(scene->clap_ctx, light, idx);
}

/*
 * Track a camera's pose: copy position, derive the forward direction from
 * the camera transform, and (for spotlights) update the light's shadow view
 * to match. Intended for game-side hookups like a flashlight attached to the
 * active camera; no user in core today.
 */
void light_update_from_camera(struct light *light, int idx, void *data)
{
    struct scene *scene = container_of(light, struct scene, light);
    struct camera *cam = data;
    vec3 pos, dir = { 0.0f, 0.0f, -1.0f };

    transform_pos(&cam->xform, pos);
    light_set_pos(light, idx, pos);

    transform_rotate_vec3(&cam->xform, dir);
    light_set_direction(light, idx, dir);

    if (!light_is_spotlight(light, idx))
        return;

    light_spot_view_update(scene->clap_ctx, light, idx);
}

void light_update(struct light *light)
{
    for (int idx = 0; idx < light->nr_lights; idx++) {
        if (!bitmap_is_set(&light->active, idx)) continue;
        if (!light->update[idx] || !light->update_track[idx]) continue;
        if (!transform_is_updated(light->update_track[idx])) continue;

        light->update[idx](light, idx, light->update_data[idx]);
    }
}

void light_set_pos(struct light *light, int idx, const float pos[3])
{
    if (!light_is_valid(light, idx))
        return;

    for (int i = 0; i < 3; i++)
        light->pos[idx * 3 + i] = pos[i];
}

void light_set_color(struct light *light, int idx, const float color[3])
{
    if (!light_is_valid(light, idx))
        return;

    for (int i = 0; i < 3; i++)
        light->color[idx * 3 + i] = color[i];
}

void light_set_attenuation(struct light *light, int idx, const float attenuation[3])
{
    if (!light_is_valid(light, idx))
        return;

    for (int i = 0; i < 3; i++)
        light->attenuation[idx * 3 + i] = attenuation[i];
}

void light_set_directional(struct light *light, int idx, bool is_directional)
{
    if (!light_is_valid(light, idx))
        return;

    light->is_dir[idx] = is_directional;
}

void light_set_direction(struct light *light, int idx, vec3 dir)
{
    if (!light_is_valid(light, idx))
        return;

    vec3_sub(&light->dir[idx * 3], (vec3){}, dir);
}

bool light_is_spotlight(struct light *light, int idx)
{
    if (!light_is_valid(light, idx))
        return false;

    return light->is_dir[idx] && light->cutoff[idx] > 0.0;
}

void light_set_cutoff(struct light *light, int idx, float cutoff)
{
    if (!light_is_valid(light, idx))
        return;

    light->cutoff[idx] = cutoff;
}

/**
 * light_set_volumetric() - set a light's volumetric scattering factor
 * @light:      lights
 * @idx:        light index from light_get()
 * @volumetric: in-scattering contribution factor
 *
 * Scales this light's contribution to the volumetric scattering pass.
 * Volumetric scattering is opt-in per light: every allocated light
 * defaults to 0.0 (excluded). Has no effect on surface lighting.
 */
void light_set_volumetric(struct light *light, int idx, float volumetric)
{
    if (!light_is_valid(light, idx))    return;

    light->volumetric[idx] = volumetric;
}
