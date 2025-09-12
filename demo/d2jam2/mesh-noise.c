// SPDX-License-Identifier: Apache-2.0
#include "interp.h"
#include <stdlib.h>
#include <limits.h>
#include "clap.h"
#include "light.h"
#include "linmath.h"
#include "noise.h"
#include "physics.h"
#include "render.h"
#include "shader_constants.h"
#include "error.h"
#include "mesh.h"
#include "model.h"
#include "object.h"
#include "pipeline.h"
#include "primitives.h"
#include "scene.h"
#include "shader.h"
#include "util.h"
#include "transform.h"

#include "onehandclap.h"

// XXX: -> DEFINE_REFCLASS*() + DECLARE_REFCLASS()
typedef struct shader_prog shader_prog;
DEFINE_CLEANUP(shader_prog, if (*p) ref_put(*p))
DECLARE_CLEANUP(model3d);
DECLARE_CLEANUP(model3dtx);

static void triangle_normal(struct mesh *mesh, size_t tri, vec3 norm)
{
    size_t idx = tri * 3;
    size_t vx_a = mesh_idx(mesh)[idx + 0];
    vec3_dup(norm, &mesh_norm(mesh)[vx_a * 3]);
}

static void triangle_split(struct mesh *mesh, size_t tri, float off)
{
    size_t nr_vx = mesh_nr_vx(mesh);
    size_t nr_idx = mesh_nr_idx(mesh);

#ifdef CONFIG_RENDERER_OPENGL
    /* GLES limitation: unsigned short indices only */
    if (mesh_nr_vx(mesh) + 6 > USHRT_MAX)  return;
#endif /* CONFIG_RENDERER_OPENGL */

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

    /* find and emit center vertex */
    vec3_add(center, a, b);
    vec3_add(center, center, c);
    vec3_scale(center, center, 1.0 / 3.0);
    vec3_add_scaled(center, center, norm, 1.0, off);

    /* replace c with center in the original triangle: a -> b -> center */
    vec3_dup(&mesh_vx(mesh)[vx_c * 3], center);
    /* recalculate this triangle's normals */
    prim_calc_normals(vx_a, .mesh = mesh);

    /* new triangle: b -> c -> center */
    prim_emit_triangle3(b, c, center, .mesh = mesh);
    /* new triangle: c -> a -> center */
    prim_emit_triangle3(c, a, center, .mesh = mesh);
}

static texture_t crystal_pixel;
static texture_t cave_pixel;

static cresp(entity3d) make_crystal(struct scene *scene)
{
    if (!texture_loaded(&crystal_pixel))
        CERR_RET_T(texture_pixel_init(&crystal_pixel, (float[]){ 0.6, 1.0, 1.0, 1 }), entity3d);
        // CERR_RET_T(texture_pixel_init(&crystal_pixel, (float[]){ 0.3, 0.5, 0.5, 1 }), entity3d);

        // CERR_RET_T(texture_pixel_init(&crystal_pixel, (float[]){ 0.6, 0.30, 0.5, 1 }), entity3d);

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
        for (int i = 0; i < nr_tris; i++) 
            triangle_split(mesh, i, (total_depth - depth) * drand48()/* / (double)pow(depth + 1, 1)*/);
    }

    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);

    LOCAL_SET(shader_prog, prog) = CRES_RET_T(pipeline_shader_find_get(scene->pl, "model"), entity3d);
    LOCAL_SET(model3d, model) = CRES_RET_T(ref_new_checked(model3d, .prog = prog, .mesh = mesh, .name = "crystal"), entity3d);
    LOCAL_SET(model3dtx, txm) = CRES_RET_T(
        ref_new_checked(
            model3dtx,
            .model      = ref_pass(model),
            .tex        = &crystal_pixel,
            .metallic   = 1.0,
            .roughness  = 0.6
        ),
        entity3d
    );

    scene_add_model(scene, txm);

    auto entity = CRES_RET_T(ref_new_checked(entity3d, .txmodel = ref_pass(txm)), entity3d);

    float radius = 90 * drand48() + 20;
    float angle = M_PI * 2 * drand48();
    float x = radius * sin(angle);
    float z = radius * cos(angle);
    entity3d_position(entity, (vec3) { x, 1, z });
    entity->bloom_intensity = -0.4;

    auto phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 0.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);
    phys_ground_entity(phys, entity);
    entity->light_idx = CRES_RET(light_get(&scene->light), return cresp_val(entity3d, entity));
    vec3_dup(entity->light_off, (vec3){});
    light_set_color(&scene->light, entity->light_idx, (vec3){ 0.3, 0.5, 0.5 });
    light_set_attenuation(&scene->light, entity->light_idx, (const float []){ 1.0, 0.06, 0.8 });
    // light_set_attenuation(&scene->light, entity->light_idx, (const float []){ 1.0, 0.04, 0.2 });
    // light_set_attenuation(&scene->light, entity->light_idx, (const float []){ 1.0, 0.8, 1.4 });
    light_set_directional(&scene->light, entity->light_idx, false);
    vec3_dup(entity->light_off, (vec3) { 0.0, 1.0, 0.0 });
        
    return cresp_val(entity3d, entity);
}

texture_t noise3d;

typedef enum pillar_state {
    PS_DORMANT,
    PS_FLASHING,
    PS_RAISING,
    PS_COOLDOWN,
    PS_LOWERING
} pillar_state;

typedef struct pillar_connect_data {
    struct scene    *scene;
    entity3d        *e;
    int             (*orig_update)(entity3d *e, void *data);
    double          start_time;
    double          cur_time;
    double          duration;
    vec3            start_pos;
    vec3            target_pos;
    clap_timer      *timer;
    float           bloom_delta;
    pillar_state    state;
    bool            final;
} pillar_connect_data;

static void pillar_free(pillar_connect_data *pcd)
{
    pcd->e->update = pcd->orig_update;
    pcd->e->connect = NULL;
    pcd->e->disconnect = NULL;
    pcd->e->connect_priv = NULL;
    pcd->e->destroy = NULL;
    pcd->timer = NULL;
    mem_free(pcd);
}

static void pillar_motion_start(pillar_connect_data *pcd)
{
    pcd->cur_time = pcd->start_time = clap_get_current_time(pcd->scene->clap_ctx);
    pcd->duration = 5.0;
    pcd->bloom_delta = 0.0;
    pcd->e->bloom_intensity = 2.0;
}

static bool pillar_move(pillar_connect_data *pcd)
{
    double time = clap_get_current_time(pcd->scene->clap_ctx);
    vec3 delta;
    vec3_sub(delta, pcd->target_pos, pcd->start_pos);
    vec3_scale(delta, delta, (time - pcd->cur_time) / pcd->duration);
    pcd->cur_time = time;
    entity3d_move(pcd->e, delta);
    if (time - pcd->start_time >= pcd->duration) {
        /* prepare for the opposite motion */
        vec3 tmp;
        vec3_dup(tmp, pcd->start_pos);
        vec3_dup(pcd->start_pos, pcd->target_pos);
        vec3_dup(pcd->target_pos, tmp);
        return true;
    }

    return false;
}

static void pillar_timer(void *data)
{
    pillar_connect_data *pcd = data;
    pcd->timer = NULL;
    pcd->state++;

    /* the new state is: */
    switch (pcd->state) {
        case PS_LOWERING:
            pcd->bloom_delta = -0.1;
            pillar_motion_start(pcd);
            break;
        default:            break;
    }
}

static const char *final_osd[] = {
    "YOU WIN",
    "THANK YOU FOR PLAYING",
    "ENJOY"
};

static void final_osd_element_cb(struct ui_element *uie, unsigned int i)
{
    ui_element_set_visibility(uie, 0);
    uia_skip_duration(uie, i * 10.0);
    uia_set_visible(uie, 1);
    uia_skip_duration(uie, 3.0);
    uia_lin_float(uie, ui_element_set_alpha, 1.0, 0.0, true, 0.5);
    uia_set_visible(uie, 0);
}


static int pillar_update(entity3d *e, void *data)
{
    pillar_connect_data *pcd = e->connect_priv;
    struct scene *scene = data;
    switch (pcd->state) {
        case PS_DORMANT:
            goto out;

        case PS_FLASHING:
            // if (fabsf(pcd->bloom_delta) < 1e-3)                             pcd->bloom_delta = 0.005;
            // else if (e->bloom_intensity >= 0.7 && pcd->bloom_delta > 0.0)   pcd->bloom_delta = -0.005;
            // else if (e->bloom_intensity <= 0.3 && pcd->bloom_delta < 0.0)   pcd->bloom_delta = 0.005;
            pcd->bloom_delta = fmodf(pcd->bloom_delta + 0.008, 2.0);
            goto out;

        case PS_RAISING:
            if (e->bloom_intensity < 1.0)   pcd->bloom_delta = min(fmodf(pcd->bloom_delta + 0.008, 2.0), 1.0);
            // else                            pcd->bloom_delta = 1.0;
            if (pillar_move(pcd))   pcd->state++;
            if (!pcd->final)        goto out;

            /* the end */
            auto ui = clap_get_ui(scene->clap_ctx);
            ui_osd_new(
                ui,
                &(const struct ui_widget_builder) {
                    .el_affinity  = UI_AF_CENTER,
                    .affinity   = UI_AF_CENTER,
                    .el_w       = 0.9,
                    .el_h       = 100,
                    .el_margin  = 4,
                    .x_off      = 0,
                    .y_off      = 0,
                    .w          = 0.8,
                    .h          = 0.9,
                    .font_name  = "ofl/ZillaSlab-Bold.ttf",
                    .font_size  = 240,
                    .el_cb      = final_osd_element_cb,
                    .el_color   = { 0.0, 0.0, 0.0, 0.0 },
                    .text_color = { 0.8, 0.8, 0.8, 1.0 },
                },
                final_osd,
                array_size(final_osd)
            );
            goto out;

        case PS_LOWERING:
            pcd->bloom_delta = fmodf(pcd->bloom_delta + 0.008, 2.0);
            if (pillar_move(pcd))   pcd->state = PS_FLASHING;
            goto out;

        case PS_COOLDOWN:
            if (!pcd->timer)
                pcd->timer = CRES_RET(clap_timer_set(scene->clap_ctx, 5.0, NULL, pillar_timer, pcd), goto out);
            goto out;
        
        default:            goto out;
    }

out:
    // e->bloom_intensity += pcd->bloom_delta;
    e->bloom_intensity = cosf_interp(0.1, pcd->state == PS_FLASHING ? 0.7 : 1.0, pcd->bloom_delta);
    return pcd->orig_update(e, scene);
}

static void pillar_connect(entity3d *e, entity3d *connection, void *data)
{
    dbg_once("pillar connected with %s\n", entity_name(connection));
    pillar_connect_data *pcd = e->connect_priv;
    if (pcd->state > PS_FLASHING)   return;
    pcd->state++;

    transform_pos(&e->xform, pcd->start_pos);
    vec3_add(pcd->target_pos, pcd->start_pos, (vec3) { 0.0, 3.0, 0.0 });
    pillar_motion_start(pcd);
}

static void pillar_disconnect(entity3d *e, entity3d *connection, void *data)
{
    dbg_once("pillar connected from %s\n", entity_name(connection));
}

static void pillar_destroy(entity3d *e)
{
    pillar_connect_data *pcd = e->connect_priv;
    pillar_free(pcd);
}

static cresp(entity3d) make_pillar(struct scene *scene, unsigned int idx)
{
    if (!texture_loaded(&cave_pixel))
        CERR_RET_T(texture_pixel_init(&cave_pixel, (float[]){ 0.31, 0.30, 0.33, 1 }), entity3d);

    if (!texture_loaded(&noise3d))
        CERR_RET_T(noise_grad3d_bake_rgb8_tex(&noise3d, /*side*/32, /*octaves*/4, /*lacunarity*/8.0, /*gain*/0.8, /*period*/32, /*seed*/7), entity3d);
        // CERR_RET_T(noise_grad3d_bake_rgb8_tex(&noise3d, /*side*/64, /*octaves*/2, /*lacunarity*/4.0, /*gain*/0.5, /*period*/64, /*seed*/7), entity3d);

    LOCAL_SET(shader_prog, prog) = CRES_RET_T(pipeline_shader_find_get(scene->pl, "model"), entity3d);
    LOCAL_SET(model3d, model) = CRES_RET_T(model3d_new_cylinder(prog, (vec3){}, 5 + idx * 2.0, 2, 6), entity3d);
    LOCAL_SET(model3dtx, txm) = CRES_RET_T(
        ref_new_checked(
            model3dtx,
            .model      = ref_pass(model),
            .tex        = &cave_pixel,
            .metallic   = 1.0,
            .roughness  = 0.6
        ),
        entity3d
    );

    model3dtx_set_texture(txm, UNIFORM_NOISE3D_TEX, &noise3d);

    txm->mat.roughness              = 0.5;
    txm->mat.metallic               = 1.0;
    txm->mat.use_noise_normals      = true;
    txm->mat.noise_normals_amp      = 0.5;//0.5;
    txm->mat.noise_normals_scale    = 0.19;//0.03;
    txm->mat.use_noise_emission     = true;

    scene_add_model(scene, txm);
    auto entity = CRES_RET_T(ref_new_checked(entity3d, .txmodel = ref_pass(txm)), entity3d);
    float radius = 10.0 + 1.0 * idx, x_off = -20.0, z_off = -4.0, angle_off = -M_PI / 2.0;
    float angle_segment = 2.2 * asinf(/* pillar's radius */2.0 / radius);
    float x = x_off + radius * cos(/*M_PI * (float)idx / 4.0*/angle_segment * (float)idx + angle_off);
    float z = z_off + radius * sin(/*M_PI * (float)idx / 4.0*/angle_segment * (float)idx + angle_off);
    entity3d_position(entity, (vec3) { x, -4.9 + 0.5 * (float)idx, z });
    entity->bloom_threshold = 0.87;
    entity->bloom_intensity = 0.1;

    auto phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 2.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);

    pillar_connect_data *pcd = mem_alloc(sizeof(*pcd), .zero = 1, .fatal_fail = 1); // XXX
    pcd->e                  = entity;
    pcd->orig_update        = entity->update;
    pcd->scene              = scene;
    pcd->final              = idx == 7,
    entity->update          = pillar_update;
    entity->connect_priv    = pcd;
    entity->connect         = pillar_connect;
    entity->disconnect      = pillar_disconnect;
    entity->destroy         = pillar_destroy;
    pcd->timer = CRES_RET(clap_timer_set(scene->clap_ctx, (double)idx, NULL, pillar_timer, pcd),);

    return cresp_val(entity3d, entity);
}

static const room_params corridor_params = {
    .radius         = 6.0,
    .height         = 32.0,
    .nr_segments    = 4,
};

cresp(entity3d) make_corridor(struct scene *scene, const room_params *_params)
{
    const room_params *params = _params ? : &corridor_params;

    LOCAL_SET(mesh_t, mesh) = CRES_RET_T(ref_new_checked(mesh, .name = "cave"), entity3d);

    prim_emit_cylinder((vec3){}, params->height, params->radius, params->nr_segments, .mesh = mesh, .clockwise = true, .skip_mask = 3);
    prim_emit_cylinder((vec3){}, params->height, params->radius + 0.1, params->nr_segments, .mesh = mesh, .clockwise = false, .skip_mask = 3);

    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);

    LOCAL_SET(shader_prog, prog) = CRES_RET_T(pipeline_shader_find_get(scene->pl, "model"), entity3d);
    LOCAL_SET(model3d, model) = CRES_RET_T(ref_new_checked(model3d, .prog = prog, .mesh = mesh, .name = "corridor"), entity3d);

    LOCAL_SET(model3dtx, txm) = CRES_RET_T(
        ref_new_checked(
            model3dtx,
            .model      = ref_pass(model),
            // .tex        = &cave_pixel,
            // .texture_file_name = "apple.png",
            .texture_file_name = "dnd-wall.png",
            // .normal_file_name  = "dnd-wall.png",
            .metallic   = 1.0,
            .roughness  = 0.6
        ),
        entity3d
    );

    scene_add_model(scene, txm);
    auto entity = CRES_RET_T(ref_new_checked(entity3d, .txmodel = ref_pass(txm)), entity3d);
    entity3d_position(entity, (vec3) { 90, 4.6, 5 });
    entity3d_rotate(entity, M_PI_2, M_PI_4, 0.0);

    auto phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 0.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);

    return cresp_val(entity3d, entity);
}

static const room_params cave_params = {
    .radius         = 120.0,
    .height         = 24.0,
    .nr_segments    = 8,
};

cresp(entity3d) make_cave(struct scene *scene, const room_params *_params)
{
    const room_params *params = _params ? : &cave_params;

    if (!texture_loaded(&cave_pixel))
        CERR_RET_T(texture_pixel_init(&cave_pixel, (float[]){ 0.31, 0.30, 0.33, 1 }), entity3d);

    if (!texture_loaded(&noise3d))
        CERR_RET_T(noise_grad3d_bake_rgb8_tex(&noise3d, /*side*/32, /*octaves*/4, /*lacunarity*/8.0, /*gain*/0.8, /*period*/21, /*seed*/7), entity3d);
        // CERR_RET_T(noise_grad3d_bake_rgb8_tex(&noise3d, /*side*/64, /*octaves*/2, /*lacunarity*/4.0, /*gain*/0.5, /*period*/64, /*seed*/7), entity3d);

    LOCAL_SET(mesh_t, mesh) = CRES_RET_T(ref_new_checked(mesh, .name = "cave"), entity3d);

    prim_emit_cylinder((vec3){}, params->height, params->radius, params->nr_segments, .mesh = mesh, .clockwise = true);

    int total_depth = 6;
    for (int depth = 0; depth < total_depth; depth++) {
        size_t nr_tris = mesh_nr_idx(mesh) / 3;
        for (int i = 0; i < nr_tris; i++) {
            vec3 norm;
            triangle_normal(mesh, i, norm);
            float up_dot = vec3_mul_inner(norm, (vec3) { 0.0, 1.0, 0.0});

            float seg_step = M_PI / (float)params->nr_segments;
            for (unsigned int seg = 0; seg < params->nr_segments; seg++) {
                if (seg % 3)    continue;
                float door_dot = vec3_mul_inner(norm, (vec3) { sin(seg_step * seg), 0.0, cos(seg_step * seg) });
                if (door_dot > 0.98)    goto next;
            }

            float disp_mul = 1.0;
            if (up_dot > 0.8) {
                if (drand48() < 0.8)    continue;
                disp_mul = drand48() * 0.7;
            } else if (up_dot < 0.3) {
                disp_mul = drand48() * 4.0;
            // } else if (fabsf(door_dot) > 0.96) {
            //     continue;
            }
            float depth_mul = 2.0 * total_depth * pow(total_depth - depth, 3.0) / pow(total_depth, 3.0);
            triangle_split(mesh, i, depth_mul * disp_mul * drand48()/* / (double)pow(depth + 1, 1)*/);
            next:;
        }
    }

    /* outer hull */
    prim_emit_cylinder((vec3){}, params->height, params->radius, params->nr_segments, .mesh = mesh);

    mesh_aabb_calc(mesh);
    mesh_optimize(mesh);

    LOCAL_SET(shader_prog, prog) = CRES_RET_T(pipeline_shader_find_get(scene->pl, "model"), entity3d);
    LOCAL_SET(model3d, model) = CRES_RET_T(ref_new_checked(model3d, .prog = prog, .mesh = mesh, .name = "cave"), entity3d);
    // model->skip_shadow = true;
    // model->cull_face = false;

    LOCAL_SET(model3dtx, txm) = CRES_RET_T(
        ref_new_checked(
            model3dtx,
            .model      = ref_pass(model),
            .tex        = &cave_pixel,
            .metallic   = 1.0,
            .roughness  = 0.6
        ),
        entity3d
    );

    model3dtx_set_texture(txm, UNIFORM_NOISE3D_TEX, &noise3d);
    // XXX: not needed, model3dtx_prepare() already does this
    // if (!model3dtx_loaded_texture(txm, UNIFORM_EMISSION_MAP))
    //     model3dtx_set_texture(txm, UNIFORM_EMISSION_MAP, black_pixel());

    txm->mat.roughness              = 0.0;
    txm->mat.roughness_ceil         = 0.4;
    txm->mat.roughness_amp          = 0.5;
    txm->mat.roughness_scale        = 0.7;
    txm->mat.roughness_oct          = 3;
    txm->mat.metallic               = 0.3;
    txm->mat.metallic_ceil          = 0.5;
    txm->mat.metallic_amp           = 0.5;
    txm->mat.metallic_scale         = 0.7;
    txm->mat.metallic_oct           = 3;
    txm->mat.metallic_mode          = MAT_METALLIC_ONE_MINUS_ROUGHNESS;
    txm->mat.shared_scale           = true;
    txm->mat.use_noise_normals      = NOISE_NORMALS_GPU;
    txm->mat.noise_normals_amp      = 0.7;//0.8;//0.5;
    txm->mat.noise_normals_scale    = 3.5;//3.0;//0.03;
    // txm->mat.use_noise_emission     = true;

    scene_add_model(scene, txm);
    auto entity = CRES_RET_T(ref_new_checked(entity3d, .txmodel = ref_pass(txm)), entity3d);
    entity3d_position(entity, (vec3) { 0, 0, 5 });
    entity->outline_exclude = true;
    entity->bloom_threshold = 0.8;

    auto phys = clap_get_phys(scene->clap_ctx);
    entity3d_add_physics(entity, phys, 0.0, GEOM_TRIMESH, PHYS_GEOM, 0, 0, 0);

    return cresp_val(entity3d, entity);
}

#include "settings.h"
cerr noisy_mesh(struct scene *scene)
{
    unsigned long seed = 0;

    auto rs = clap_get_settings(scene->clap_ctx);
    auto rng_group = settings_find_get(rs, NULL, "rng", JSON_OBJECT);
    if (rng_group) {
        seed = (unsigned long)settings_get_num(rs, rng_group, "seed");
        if (seed)   seed = (seed << 1) + (seed >> (sizeof(unsigned long) * 8 - 1));
    }

    if (!seed) {
        auto ts = clap_get_current_timespec(scene->clap_ctx);
        seed = ts.tv_nsec ^ ts.tv_sec;

        if (rs && rng_group) settings_set_num(rs, rng_group, "seed", (double)seed);
    }

    srand48(seed);

    // CRES_RET_CERR(make_cave(scene));

    for (unsigned int i = 0; i < 8; i++)
        make_pillar(scene, i);

    for (int i = 0; i < 32; i++)
        make_crystal(scene);
        // CRES_RET_CERR(make_crystal(scene));

    make_corridor(scene, NULL);

    // for (size_t tri = 0; tri < mesh_nr_idx(mesh))

    return CERR_OK;
}
