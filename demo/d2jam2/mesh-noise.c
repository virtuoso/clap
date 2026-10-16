// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include "clap.h"
#include "font.h"
#include "gpsm.h"
#include "interp.h"
#include "light.h"
#include "linmath.h"
#include "mesh.h"
#include "model.h"
#include "noise.h"
#include "object.h"
#include "physics.h"
#include "pipeline.h"
#include "primitives.h"
#include "prng.h"
#include "render.h"
#include "scene.h"
#include "shader.h"
#include "shader_constants.h"
#include "transform.h"
#include "ui.h"
#include "util.h"
#include "worker.h"
#include "d2jam2.h"

/* pixel textures of the generated models */
static texture_t crystal_pixel;
static texture_t cave_pixel;

/* make_cave()'s job on the loader thread, until its done() */
static job *cave_job;

/**
 * triangle_normal() - get a triangle's normal
 * @mesh:   the mesh
 * @tri:    triangle index
 * @norm:   receives the normal
 *
 * The generators emit flat triangles, so the first vertex normal is the
 * face normal.
 */
static void triangle_normal(struct mesh *mesh, size_t tri, vec3 norm)
{
    size_t vx_a = mesh_idx(mesh)[tri * 3];

    vec3_dup(norm, &mesh_norm(mesh)[vx_a * 3]);
}

/**
 * triangle_split() - split a triangle in three around a displaced center
 * @mesh:   the mesh
 * @tri:    triangle index
 * @off:    displacement of the center along the triangle's normal
 *
 * Rewrite @tri as a -> b -> center and append b -> c -> center and
 * c -> a -> center. Mesh indices are 16-bit: a mesh that can't take six
 * more vertices is left alone.
 */
static void triangle_split(struct mesh *mesh, size_t tri, float off)
{
    size_t nr_vx = mesh_nr_vx(mesh);
    size_t nr_idx = mesh_nr_idx(mesh);

    if (nr_vx + 6 > USHRT_MAX)  return;

    /* 2 more triangles: 6 vertices, 6 indices */
    CERR_RET(mesh_attr_resize(mesh, MESH_VX, nr_vx + 6), return);
    CERR_RET(mesh_attr_resize(mesh, MESH_TX, nr_vx + 6), return);
    CERR_RET(mesh_attr_resize(mesh, MESH_NORM, nr_vx + 6), return);
    CERR_RET(mesh_attr_resize(mesh, MESH_IDX, nr_idx + 6), return);

    size_t idx = tri * 3;
    size_t vx_a = mesh_idx(mesh)[idx + 0];
    size_t vx_b = mesh_idx(mesh)[idx + 1];
    size_t vx_c = mesh_idx(mesh)[idx + 2];
    vec3 a, b, c, center;
    vec3_dup(a, &mesh_vx(mesh)[vx_a * 3]);
    vec3_dup(b, &mesh_vx(mesh)[vx_b * 3]);
    vec3_dup(c, &mesh_vx(mesh)[vx_c * 3]);

    vec3 norm;
    vec3_dup(norm, &mesh_norm(mesh)[vx_a * 3]);

    /* the displaced center */
    vec3_add(center, a, b);
    vec3_add(center, center, c);
    vec3_scale(center, center, 1.0 / 3.0);
    vec3_add_scaled(center, center, norm, 1.0, off);

    /* replace c with center in the original triangle: a -> b -> center */
    vec3_dup(&mesh_vx(mesh)[vx_c * 3], center);
    prim_calc_normals(vx_a, .mesh = mesh);

    prim_emit_triangle3(b, c, center, .mesh = mesh);
    prim_emit_triangle3(c, a, center, .mesh = mesh);
}

/**
 * cave_model_new() - make a model drawn by the variant-managed "model" family
 * @scene:  the scene
 * @mesh:   the model's mesh
 * @lods:   @mesh's levels of detail, nullptr to build them here
 * @name:   model name
 *
 * Resolve the shader the way scene loading does for gltf meshes: the global
 * half of the variant mask comes from the render options and a generated
 * mesh has no tangents or joints, so no attribute-layout bits. Opt the
 * model into re-resolution, which picks up the material its txmodel gets
 * later, and give it the matching shadow prog.
 * Return: the model or a shader loading error
 */
static cresp(model3d) cave_model_new(struct scene *scene, struct mesh *mesh, struct mesh_lods *lods,
                                      const char *name)
{
    pipeline *pl = clap_get_pipeline(scene->clap_ctx);
    shader_variants_t variants = ropts_shader_variants(pipeline_render_options(pl));
    struct shader_prog *prog = CRES_RET_T(pipeline_shader_variant_get(pl, "model", variants), model3d);

    /* the constructor takes over the prog reference, on failure too */
    LOCAL_SET(model3d, model) = CRES_RET_T(
        ref_new_checked(
            model3d,
            .prog           = ref_pass(prog),
            .name           = name,
            .mesh           = mesh,
            .lods           = lods,
            .shader_base    = "model",
            .variants       = variants
        ),
        model3d
    );

    CERR_RET_T(pipeline_model_shadow_resolve(pl, model), model3d);

    return cresp_val(model3d, NOCU(model));
}

/**
 * cave_entity_new() - make an entity of a generated model and add it to the scene
 * @scene:      the scene
 * @mesh:       the model's mesh
 * @lods:       @mesh's levels of detail, nullptr to build them here
 * @name:       model name
 * @tex:        the model's texture
 * @metallic:   material metallic
 * @roughness:  material roughness
 *
 * The model's noise3d texture is the engine's global periodic noise, which
 * the noise materials and the 3D fog sample.
 * Return: the entity or an error from the constructors
 */
static cresp(entity3d) cave_entity_new(struct scene *scene, struct mesh *mesh, struct mesh_lods *lods,
                                       const char *name, texture_t *tex, float metallic, float roughness)
{
    LOCAL_SET(model3d, model) = CRES_RET_T(cave_model_new(scene, mesh, lods, name), entity3d);
    LOCAL_SET(model3dtx, txm) = CRES_RET_T(
        ref_new_checked(
            model3dtx,
            .model      = ref_pass(model),
            .tex        = tex,
            .metallic   = metallic,
            .roughness  = roughness
        ),
        entity3d
    );

    model3dtx_set_texture(txm, UNIFORM_NOISE3D_TEX, noise3d_texture(clap_get_noise3d(scene->clap_ctx)));
    scene_add_model(scene, txm);

    return ref_new_checked(entity3d, .txmodel = ref_pass(txm));
}

/**
 * make_crystal() - make a glowing crystal at a random spot on the cave floor
 * @scene:  the scene
 * @rng:    the random stream
 *
 * A crystal is a triangle split recursively with random displacements. It
 * casts a point light from above its base.
 * Return: the crystal entity or an error from the constructors
 */
static cresp(entity3d) make_crystal(struct scene *scene, prng *rng)
{
    renderer_t *renderer = clap_get_renderer(scene->clap_ctx);

    if (!texture_loaded(&crystal_pixel))
        CERR_RET_T(texture_pixel_init(renderer, &crystal_pixel, (float[]){ 0.6, 1.0, 1.0, 1 }), entity3d);

    LOCAL_SET(mesh_t, mesh) = CRES_RET_T(ref_new_checked(mesh, .name = "crystal"), entity3d);
    CERR_RET_T(mesh_attr_alloc(mesh, MESH_VX, sizeof(float) * 3, 3), entity3d);
    CERR_RET_T(mesh_attr_alloc(mesh, MESH_TX, sizeof(float) * 2, 3), entity3d);
    CERR_RET_T(mesh_attr_alloc(mesh, MESH_NORM, sizeof(float) * 3, 3), entity3d);
    CERR_RET_T(mesh_attr_alloc(mesh, MESH_IDX, sizeof(unsigned short), 3), entity3d);
    vec3 tri[] = {
        { -0.3, 0, 0 },
        {  0.3, 0, 0 },
        {  0, 0, -0.78 }
    };

    prim_emit_triangle(tri, .mesh = mesh);
    triangle_split(mesh, 0, 3);

    int total_depth = 3;
    for (int depth = 0; depth < total_depth; depth++) {
        size_t nr_tris = mesh_nr_idx(mesh) / 3;
        for (size_t i = 0; i < nr_tris; i++)    triangle_split(mesh, i, (total_depth - depth) * prng_unitd(rng));
    }

    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);

    entity3d *entity = CRES_RET_T(cave_entity_new(scene, mesh, nullptr, "crystal", &crystal_pixel, 1.0, 0.6), entity3d);

    float radius = 90 * prng_unitd(rng) + 20;
    float angle = M_PI * 2 * prng_unitd(rng);
    entity3d_position(entity, (vec3) { radius * sin(angle), 1, radius * cos(angle) });
    entity->bloom_intensity = -0.4;

    struct phys *phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 0.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);
    phys_ground_entity(phys, entity);

    /* a crystal stays where it grew: place its light above the base once */
    int light_idx = CRES_RET(light_get(&scene->light), return cresp_val(entity3d, entity));
    vec3 light_pos;

    vec3_add(light_pos, transform_pos(&entity->xform, NULL), (vec3) { 0.0, 1.0, 0.0 });
    light_set_pos(&scene->light, light_idx, light_pos);
    light_set_color(&scene->light, light_idx, (vec3){ 0.3, 0.5, 0.5 });
    light_set_attenuation(&scene->light, light_idx, (const float []){ 1.0, 0.06, 0.8 });
    light_set_directional(&scene->light, light_idx, false);

    return cresp_val(entity3d, entity);
}

/** PILLAR_BLOOM_STEP - bloom pulse phase advance per frame */
#define PILLAR_BLOOM_STEP       0.008f
/** PILLAR_BLOOM_PERIOD - bloom pulse phase period: a full cosine */
#define PILLAR_BLOOM_PERIOD     2.0f
/** PILLAR_MOTION_DURATION - seconds a pillar takes to rise or sink */
#define PILLAR_MOTION_DURATION  5.0
/** PILLAR_COOLDOWN - seconds a raised pillar stays up */
#define PILLAR_COOLDOWN         5.0
/** PILLAR_RISE - how far a pillar rises */
#define PILLAR_RISE             3.0

/**
 * enum pillar_state - a pillar's state machine states
 * @PS_DORMANT:     not woken up yet
 * @PS_FLASHING:    pulsing, waiting for the character to step on it
 * @PS_RAISING:     rising, glowing
 * @PS_COOLDOWN:    up, waiting to sink
 * @PS_LOWERING:    sinking back to its rest position
 * @PS_NR:          number of states
 */
typedef enum pillar_state {
    PS_DORMANT = 0,
    PS_FLASHING,
    PS_RAISING,
    PS_COOLDOWN,
    PS_LOWERING,
    PS_NR,
} pillar_state;

/**
 * struct pillar - a pillar the character raises by stepping on it
 * @scene:          the scene
 * @e:              the pillar entity
 * @orig_update:    the entity's own update callback, run after the pillar's
 * @timer:          the pending wake-up or cooldown timer, if any
 * @sm:             the state machine
 * @start_time:     when the current motion started
 * @rest_pos:       the pillar's position when lowered
 * @start_pos:      where the current motion started
 * @target_pos:     where the current motion ends
 * @bloom_phase:    bloom pulse phase, [0, PILLAR_BLOOM_PERIOD)
 * @final:          the last pillar of the climb, raising it wins the game
 */
typedef struct pillar {
    struct scene    *scene;
    entity3d        *e;
    int             (*orig_update)(entity3d *e, void *data);
    clap_timer      *timer;
    struct gpsm     sm;
    double          start_time;
    vec3            rest_pos;
    vec3            start_pos;
    vec3            target_pos;
    float           bloom_phase;
    bool            final;
} pillar;

static const char *final_osd[] = {
    "YOU WIN",
    "THANK YOU FOR PLAYING",
    "ENJOY"
};

/**
 * final_osd_element_cb() - schedule a line of the closing OSD
 * @uie:    the line's element
 * @i:      the line's index
 */
static void final_osd_element_cb(struct ui_element *uie, unsigned int i)
{
    ui_element_set_visibility(uie, 0);
    uia_skip_duration(uie, i * 10.0);
    uia_set_visible(uie, 1);
    uia_skip_duration(uie, 3.0);
    uia_lin_float(uie, ui_element_set_alpha, 1.0, 0.0, true, 0.5);
    uia_set_visible(uie, 0);
}

/**
 * final_osd_show() - roll the closing OSD
 * @scene:  the scene
 */
static void final_osd_show(struct scene *scene)
{
    struct ui *ui = clap_get_ui(scene->clap_ctx);
    struct font *font = ref_new(font,
                                .ctx    = clap_get_font(scene->clap_ctx),
                                .name   = "ofl/ZillaSlab-Bold.ttf",
                                .size   = 240);

    /* the OSD takes over the font reference; NULL gets it the default font */
    ui_osd_new(
        ui,
        &(const struct ui_widget_builder) {
            .el_affinity    = UI_AF_CENTER,
            .affinity       = UI_AF_CENTER,
            .el_w           = 0.9,
            .el_h           = 100,
            .el_margin      = 4,
            .w              = 0.8,
            .h              = 0.9,
            .font           = font,
            .el_cb          = final_osd_element_cb,
            .el_color       = { 0.0, 0.0, 0.0, 0.0 },
            .text_color     = { 0.8, 0.8, 0.8, 1.0 },
        },
        final_osd,
        array_size(final_osd)
    );
}

/**
 * pillar_pulse() - advance the bloom pulse by a frame
 * @p:  the pillar
 */
static void pillar_pulse(pillar *p)
{
    p->bloom_phase = fmodf(p->bloom_phase + PILLAR_BLOOM_STEP, PILLAR_BLOOM_PERIOD);
}

/**
 * pillar_motion_start() - start moving to a target position
 * @p:      the pillar
 * @target: where the motion ends
 */
static void pillar_motion_start(pillar *p, const vec3 target)
{
    p->start_time = clap_get_current_time(p->scene->clap_ctx);
    transform_pos(&p->e->xform, p->start_pos);
    vec3_dup(p->target_pos, target);
    p->bloom_phase = 0.0;
}

/**
 * pillar_move() - advance the current motion
 * @p:  the pillar
 *
 * Return: true once the pillar is at the target
 */
static bool pillar_move(pillar *p)
{
    double t = (clap_get_current_time(p->scene->clap_ctx) - p->start_time) / PILLAR_MOTION_DURATION;
    vec3 pos;

    vec3_interp(pos, p->start_pos, p->target_pos, clampf(t, 0.0, 1.0));
    entity3d_position(p->e, pos);

    return t >= 1.0;
}

/**
 * pillar_wake() - timer callback: start flashing
 * @data:   the pillar
 */
static void pillar_wake(void *data)
{
    pillar *p = data;

    p->timer = NULL;
    gpsm_set(&p->sm, PS_FLASHING);
}

/**
 * pillar_cooled() - timer callback: start sinking
 * @data:   the pillar
 */
static void pillar_cooled(void *data)
{
    pillar *p = data;

    p->timer = NULL;
    gpsm_set(&p->sm, PS_LOWERING);
}

/**
 * ps_flashing_tick() - PS_FLASHING per frame work: pulse
 * @sm:     the state machine
 * @priv:   the pillar
 */
static void ps_flashing_tick(struct gpsm *sm, void *priv)
{
    pillar_pulse(priv);
}

/**
 * ps_raising_enter() - enter PS_RAISING: start rising, roll the closing OSD for the last pillar
 * @sm:     the state machine
 * @priv:   the pillar
 * @from:   the previous state
 * Return: PS_RAISING
 */
static gpsm_state_t ps_raising_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    pillar *p = priv;
    vec3 target;

    vec3_add(target, p->rest_pos, (vec3) { 0.0, PILLAR_RISE, 0.0 });
    pillar_motion_start(p, target);
    if (p->final)   final_osd_show(p->scene);

    return PS_RAISING;
}

/**
 * ps_raising_tick() - PS_RAISING per frame work: rise, then cool down
 * @sm:     the state machine
 * @priv:   the pillar
 */
static void ps_raising_tick(struct gpsm *sm, void *priv)
{
    pillar *p = priv;

    /* the pulse ramps up to full glow and stays there */
    if (p->bloom_phase < 1.0)   p->bloom_phase = min(p->bloom_phase + PILLAR_BLOOM_STEP, 1.0);
    if (pillar_move(p))         gpsm_set(sm, PS_COOLDOWN);
}

/**
 * ps_cooldown_enter() - enter PS_COOLDOWN: arm the timer that sinks the pillar
 * @sm:     the state machine
 * @priv:   the pillar
 * @from:   the previous state
 * Return: PS_COOLDOWN, or PS_LOWERING right away if the timer can't be armed
 */
static gpsm_state_t ps_cooldown_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    pillar *p = priv;

    p->timer = CRES_RET(clap_timer_set(p->scene->clap_ctx, PILLAR_COOLDOWN, NULL, pillar_cooled, p),
                        return PS_LOWERING);

    return PS_COOLDOWN;
}

/**
 * ps_lowering_enter() - enter PS_LOWERING: start sinking
 * @sm:     the state machine
 * @priv:   the pillar
 * @from:   the previous state
 * Return: PS_LOWERING
 */
static gpsm_state_t ps_lowering_enter(struct gpsm *sm, void *priv, gpsm_state_t from)
{
    pillar *p = priv;

    pillar_motion_start(p, p->rest_pos);

    return PS_LOWERING;
}

/**
 * ps_lowering_tick() - PS_LOWERING per frame work: pulse, sink, then flash again
 * @sm:     the state machine
 * @priv:   the pillar
 */
static void ps_lowering_tick(struct gpsm *sm, void *priv)
{
    pillar *p = priv;

    pillar_pulse(p);
    if (pillar_move(p)) gpsm_set(sm, PS_FLASHING);
}

static const struct gpsm_state pillar_states[PS_NR] = {
    [PS_DORMANT]    = { .name = "dormant" },
    [PS_FLASHING]   = { .name = "flashing",     .valid_from = GPSM_FROM(PS_DORMANT, PS_LOWERING),
                        .tick = ps_flashing_tick },
    [PS_RAISING]    = { .name = "raising",      .valid_from = GPSM_FROM(PS_FLASHING),
                        .enter = ps_raising_enter, .tick = ps_raising_tick },
    [PS_COOLDOWN]   = { .name = "cooldown",     .valid_from = GPSM_FROM(PS_RAISING),
                        .enter = ps_cooldown_enter },
    /* PS_RAISING: the cooldown's fallback when its timer can't be armed */
    [PS_LOWERING]   = { .name = "lowering",     .valid_from = GPSM_FROM(PS_COOLDOWN, PS_RAISING),
                        .enter = ps_lowering_enter, .tick = ps_lowering_tick },
};

/**
 * pillar_update() - the pillar entity's update: run the state machine and the glow
 * @e:      the pillar entity
 * @data:   the scene
 * Return: the entity's own update's result
 */
static int pillar_update(entity3d *e, void *data)
{
    pillar *p = e->connect_priv;
    float glow = gpsm_get(&p->sm) == PS_FLASHING ? 0.7 : 1.0;

    gpsm_tick(&p->sm);
    e->bloom_intensity = cosf_interp(0.1, glow, p->bloom_phase);

    return p->orig_update(e, data);
}

/**
 * pillar_connect() - the character stepped on the pillar
 * @e:          the pillar entity
 * @connection: the character's entity
 * @data:       the pillar
 */
static void pillar_connect(entity3d *e, entity3d *connection, void *data)
{
    pillar *p = data;

    dbg_once("pillar connected with %s\n", entity_name(connection));
    /* only a flashing pillar reacts, the rest is busy or asleep */
    if (gpsm_get(&p->sm) == PS_FLASHING)    gpsm_set(&p->sm, PS_RAISING);
}

/**
 * pillar_disconnect() - the character stepped off the pillar
 * @e:          the pillar entity
 * @connection: the character's entity
 * @data:       the pillar
 */
static void pillar_disconnect(entity3d *e, entity3d *connection, void *data)
{
    dbg_once("pillar disconnected from %s\n", entity_name(connection));
}

/**
 * pillar_destroy() - free the pillar and drop its entity
 * @e:  the pillar entity
 *
 * mq_release() hands the entity's reference to its destroy callback.
 */
static void pillar_destroy(entity3d *e)
{
    pillar *p = e->connect_priv;

    if (p->timer)   clap_timer_cancel(p->scene->clap_ctx, p->timer);
    e->connect_priv = NULL;
    mem_free(p);
    ref_put(e);
}

/**
 * make_pillar() - make a pillar of the climb
 * @scene:  the scene
 * @idx:    pillar index along the climb, the last one wins the game
 *
 * The pillars stand in a spiral, each a step taller and a step higher
 * than the previous one; a pillar wakes up @idx seconds into the game.
 * Return: the pillar entity or an error from the constructors
 */
static cresp(entity3d) make_pillar(struct scene *scene, unsigned int idx)
{
    renderer_t *renderer = clap_get_renderer(scene->clap_ctx);

    if (!texture_loaded(&cave_pixel))
        CERR_RET_T(texture_pixel_init(renderer, &cave_pixel, (float[]){ 0.31, 0.30, 0.33, 1 }), entity3d);

    LOCAL_SET(mesh_t, mesh) = CRES_RET_T(ref_new_checked(mesh, .name = "pillar"), entity3d);
    CERR_RET_T(prim_emit_cylinder((vec3){}, 5 + idx * 2.0, 2, 6, .mesh = mesh), entity3d);
    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);

    entity3d *entity = CRES_RET_T(cave_entity_new(scene, mesh, nullptr, "pillar", &cave_pixel, 1.0, 0.6), entity3d);
    material *mat = &entity->txmodel->mat;

    mat->roughness              = 0.5;
    mat->metallic               = 1.0;
    mat->use_noise_normals      = NOISE_NORMALS_GPU;
    mat->noise_normals_amp      = 0.5;
    mat->noise_normals_scale    = 0.19;
    mat->use_noise_emission     = true;
    vec3_dup(mat->noise_emission_color, (vec3){ 0.0, 1.0, 1.0 });

    float radius = 10.0 + 1.0 * idx, x_off = -20.0, z_off = -4.0, angle_off = -M_PI / 2.0;
    /* the pillars' radius */
    float angle_segment = 2.2 * asinf(2.0 / radius);
    float x = x_off + radius * cos(angle_segment * (float)idx + angle_off);
    float z = z_off + radius * sin(angle_segment * (float)idx + angle_off);
    entity3d_position(entity, (vec3) { x, -4.9 + 0.5 * (float)idx, z });
    entity->bloom_threshold = 0.87;
    entity->bloom_intensity = 0.1;

    struct phys *phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 2.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);

    pillar *p = mem_alloc(sizeof(*p), .zero = 1);
    if (!p) return cresp_error(entity3d, CERR_NOMEM);

    p->e            = entity;
    p->orig_update  = entity->update;
    p->scene        = scene;
    p->final        = idx == 7;
    transform_pos(&entity->xform, p->rest_pos);
    p->sm.name      = "pillar";
    CERR_RET(gpsm_init(&p->sm, pillar_states, PS_NR, p, PS_DORMANT),
             { mem_free(p); return cresp_error_cerr(entity3d, __cerr); });

    entity->update          = pillar_update;
    entity->connect_priv    = p;
    entity->connect         = pillar_connect;
    entity->disconnect      = pillar_disconnect;
    entity->destroy         = pillar_destroy;
    p->timer = CRES_RET(clap_timer_set(scene->clap_ctx, (double)idx, NULL, pillar_wake, p),
                        { err_cerr(__resp, "pillar %u stays dormant\n", idx); return cresp_val(entity3d, entity); });

    return cresp_val(entity3d, entity);
}

static const room_params corridor_params = {
    .radius         = 6.0,
    .height         = 32.0,
    .nr_segments    = 4,
};

/**
 * make_corridor() - make a corridor leading out of the cave
 * @scene:  the scene
 * @params: room parameters, NULL for the default corridor
 *
 * Return: the corridor entity or an error from the constructors
 */
static cresp(entity3d) make_corridor(struct scene *scene, const room_params *_params)
{
    const room_params *params = _params ? : &corridor_params;

    LOCAL_SET(mesh_t, mesh) = CRES_RET_T(ref_new_checked(mesh, .name = "corridor"), entity3d);

    /* two capless tubes: the walls' inner and outer faces */
    CERR_RET_T(prim_emit_cylinder((vec3){}, params->height, params->radius, params->nr_segments,
                                  .mesh = mesh, .clockwise = true, .skip_mask = 3), entity3d);
    CERR_RET_T(prim_emit_cylinder((vec3){}, params->height, params->radius + 0.1, params->nr_segments,
                                  .mesh = mesh, .clockwise = false, .skip_mask = 3), entity3d);

    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);

    entity3d *entity = CRES_RET_T(cave_entity_new(scene, mesh, nullptr, "corridor", &cave_pixel, 1.0, 0.6), entity3d);
    entity3d_position(entity, (vec3) { 90, 4.6, 5 });
    entity3d_rotate(entity, M_PI_2, M_PI_4, 0.0);

    struct phys *phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 0.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);

    return cresp_val(entity3d, entity);
}

static const room_params cave_params = {
    .radius         = 120.0,
    .height         = 24.0,
    .nr_segments    = 8,
};

/**
 * faces_door() - tell if a wall triangle faces one of the doors
 * @norm:   the triangle's normal
 * @params: room parameters
 *
 * Every third wall segment has a door in the middle; the wall around it
 * stays flat so the corridor fits.
 * Return: true for a triangle facing a door
 */
static bool faces_door(const vec3 norm, const room_params *params)
{
    float seg_step = M_PI / (float)params->nr_segments;

    for (unsigned int seg = 0; seg < params->nr_segments; seg += 3) {
        float door_dot = vec3_mul_inner(norm, (vec3) { sin(seg_step * seg), 0.0, cos(seg_step * seg) });

        if (door_dot > 0.98)    return true;
    }

    return false;
}

/**
 * cave_displace() - roughen the cave walls and ceiling
 * @mesh:   the cave mesh, a cylinder
 * @params: room parameters
 * @rng:    the random stream
 *
 * Split the triangles recursively, displacing the centers: the floor
 * mostly stays flat, the walls get the largest bumps, and each level of
 * splits displaces less than the previous one.
 */
static void cave_displace(struct mesh *mesh, const room_params *params, prng *rng)
{
    int total_depth = 6;

    for (int depth = 0; depth < total_depth; depth++) {
        size_t nr_tris = mesh_nr_idx(mesh) / 3;
        for (size_t i = 0; i < nr_tris; i++) {
            vec3 norm;
            triangle_normal(mesh, i, norm);
            float up_dot = vec3_mul_inner(norm, (vec3) { 0.0, 1.0, 0.0});

            if (faces_door(norm, params))   continue;

            float disp_mul = 1.0;
            if (up_dot > 0.8) {
                if (prng_unitd(rng) < 0.8)  continue;
                disp_mul = prng_unitd(rng) * 0.7;
            } else if (up_dot < 0.3) {
                disp_mul = prng_unitd(rng) * 4.0;
            }
            float depth_mul = 2.0 * total_depth * pow(total_depth - depth, 3.0) / pow(total_depth, 3.0);
            triangle_split(mesh, i, depth_mul * disp_mul * prng_unitd(rng));
        }
    }
}

/**
 * struct cave_build - a cave on its way into the scene
 * @scene:      the scene
 * @params:     room parameters
 * @mesh:       the cave's mesh
 * @done:       make_cave()'s @done
 * @data:       make_cave()'s @data
 * @trimesh:    the collider, cooked ahead of the cave's body; nullptr: the body cooks its own
 * @lods:       the mesh's levels of detail
 */
typedef struct cave_build {
    struct scene        *scene;
    const room_params   *params;
    struct mesh         *mesh;
    cave_made_fn        done;
    void                *data;
    phys_trimesh        *trimesh;
    struct mesh_lods    lods;
} cave_build;

/**
 * cave_build_free() - free a cave build and what of the cave is still in it
 * @cb: the cave build
 */
static void cave_build_free(cave_build *cb)
{
    ref_put(cb->mesh);
    mesh_lods_done(&cb->lods);
    phys_trimesh_free(cb->trimesh);
    mem_free(cb);
}

/**
 * cave_shape() - shape the cave
 * @cb: the cave build
 *
 * Displace the walls and the ceiling, add the hull, build the levels of
 * detail and cook the collider: no engine object or reference count, so
 * the loader thread can.
 * Return: CERR_OK or the error of the stream constructor or the cylinder emits
 */
static cerr cave_shape(cave_build *cb)
{
    const room_params *params = cb->params;
    struct mesh *mesh = cb->mesh;

    /*
     * The cave shape is the same for every run: the generators' stream is
     * seeded in noisy_mesh(), the cave only needs a shape that fits the
     * pillars and the corridor
     */
    LOCAL_SET(prng, rng) = CRES_RET_CERR(prng_new(PRNG_DEFAULT, 7));

    CERR_RET_CERR(prim_emit_cylinder((vec3){}, params->height, params->radius, params->nr_segments,
                                     .mesh = mesh, .clockwise = true));
    cave_displace(mesh, params, rng);

    /*
     * Outer hull: the shadow passes cull front faces, which leaves the
     * walls' shadow casters to the hull's outward faces. Off the inner
     * walls' plane, or the walls shadow themselves at exactly their own
     * depth, and without caps: the floor and the ceiling have nothing
     * to shadow beyond them, and a cap on the floor's plane would
     * shadow the floor the same way
     */
    CERR_RET_CERR(prim_emit_cylinder((vec3){}, params->height, params->radius + 0.1, params->nr_segments,
                                     .mesh = mesh, .skip_mask = 3));

    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);
    mesh_lods_build(mesh, &cb->lods);

    /* where the physics backend can't cook ahead, the cave's body cooks its own */
    cresp(phys_trimesh) res = phys_trimesh_cook(mesh_vx(mesh), mesh_vx_sz(mesh), mesh_idx(mesh), mesh_idx_sz(mesh));
    if (!IS_CERR(res))  cb->trimesh = res.val;

    return CERR_OK;
}

/**
 * cave_add() - put the shaped cave in the scene: its model, material and body
 * @cb: the cave build
 *
 * Return: the cave entity or an error from the constructors
 */
static cresp(entity3d) cave_add(cave_build *cb)
{
    struct scene *scene = cb->scene;

    entity3d *entity = CRES_RET_T(
        cave_entity_new(scene, cb->mesh, &cb->lods, "cave", &cave_pixel, 1.0, 0.6),
        entity3d
    );
    material *mat = &entity->txmodel->mat;

    mat->roughness              = 0.0;
    mat->roughness_ceil         = 0.4;
    mat->roughness_amp          = 0.5;
    mat->roughness_scale        = 0.7;
    mat->roughness_oct          = 3;
    mat->metallic               = 0.3;
    mat->metallic_ceil          = 0.5;
    mat->metallic_amp           = 0.5;
    mat->metallic_scale         = 0.7;
    mat->metallic_oct           = 3;
    mat->metallic_mode          = MAT_METALLIC_ONE_MINUS_ROUGHNESS;
    mat->shared_scale           = true;
    mat->use_noise_normals      = NOISE_NORMALS_GPU;
    mat->noise_normals_amp      = 0.7;
    mat->noise_normals_scale    = 3.5;

    entity3d_position(entity, (vec3) { 0, 0, 5 });
    entity->outline_exclude = true;
    entity->bloom_threshold = 0.8;

    /* the model's first static trimesh body takes it */
    entity->txmodel->model->trimesh = cb->trimesh;
    cb->trimesh = nullptr;

    struct phys *phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 0.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);

    return cresp_val(entity3d, entity);
}

/**
 * cave_finish() - put the cave in the scene, unless shaping it failed, and call make_cave()'s @done
 * @cb:     the cave build, freed here
 * @err:    what shaping the cave returned
 */
static void cave_finish(cave_build *cb, cerr err)
{
    if (!IS_CERR(err))  CRES_RET(cave_add(cb), err = cerr_error_cres(__resp));

    cb->done(cb->scene, err, cb->data);
    cave_build_free(cb);
}

/**
 * cave_shape_run() - the cave job's work, on the loader thread
 * @j:  the job
 *
 * Return: what cave_shape() returns
 */
static cerr cave_shape_run(job *j)
{
    return cave_shape(job_get_data(j));
}

/**
 * cave_shape_done() - the cave job's completion, from scene_update()
 * @j:      the job
 * @err:    what cave_shape_run() returned
 *
 * Finishes the cave, unless noisy_mesh_done() cancelled the job, which
 * only frees the build.
 */
static void cave_shape_done(job *j, cerr err)
{
    cave_build *cb = job_get_data(j);
    bool cancelled = job_is_cancelled(j);

    /* make_cave()'s reference; the poll holds the pool's */
    ref_put(j);
    cave_job = nullptr;

    if (cancelled)  cave_build_free(cb);
    else            cave_finish(cb, err);
}

cerr make_cave(struct scene *scene, const room_params *params, cave_made_fn done, void *data)
{
    renderer_t *renderer = clap_get_renderer(scene->clap_ctx);

    if (!texture_loaded(&cave_pixel))
        CERR_RET_CERR(texture_pixel_init(renderer, &cave_pixel, (float[]){ 0.31, 0.30, 0.33, 1 }));

    LOCAL_SET(mesh_t, mesh) = CRES_RET_CERR(ref_new_checked(mesh, .name = "cave"));
    cave_build *cb = mem_alloc(sizeof(*cb), .zero = 1);
    if (!cb)    return CERR_NOMEM;

    cb->scene   = scene;
    cb->params  = params ? : &cave_params;
    cb->mesh    = NOCU(mesh);
    cb->done    = done;
    cb->data    = data;

    cresp(worker_pool) pool = scene_loader_pool(scene);
    if (!IS_CERR(pool)) {
        cresp(job) res = job_submit(.pool = pool.val, .run = cave_shape_run, .done = cave_shape_done, .data = cb,
                                    .name = "cave");
        if (!IS_CERR(res)) {
            dbg("shaping the cave on the loader thread\n");
            cave_job = res.val;
            return CERR_OK;
        }
    }

    /* no loader thread: the main thread shapes the cave */
    dbg("shaping the cave on the main thread\n");
    cave_finish(cb, cave_shape(cb));

    return CERR_OK;
}

cerr noisy_mesh(struct scene *scene, uint64_t seed)
{
    LOCAL_SET(prng, rng) = CRES_RET_CERR(prng_new(PRNG_DEFAULT, seed));

    for (unsigned int i = 0; i < 8; i++)    CRES_RET(make_pillar(scene, i), err_cerr(__resp, "pillar %u\n", i));
    for (int i = 0; i < 32; i++)            CRES_RET(make_crystal(scene, rng), err_cerr(__resp, "crystal %d\n", i));

    CRES_RET(make_corridor(scene, NULL), err_cerr(__resp, "corridor\n"));

    return CERR_OK;
}

void noisy_mesh_done(void)
{
    /* its done() comes when scene_done() stops the loader thread, and only frees it */
    if (cave_job)   job_cancel(cave_job);

    /*
     * model3dtx_set_texture() makes these external to the txmodels, so
     * model3dtx_drop() skips them as static refs; free the GPU side here
     */
    texture_deinit(&crystal_pixel);
    texture_deinit(&cave_pixel);
}
