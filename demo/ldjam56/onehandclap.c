// SPDX-License-Identifier: Apache-2.0
#include "draw.h"
#include "font.h"
#include "primitives.h"
#include <float.h>
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include "object.h"
#include "common.h"
#include "display.h"
#include "input.h"
#include "messagebus.h"
#include "loading-screen.h"
#include "lut.h"
#include "model.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"
#include "pipeline-builder.h"
#include "physics.h"
#include "networking.h"
#include "settings.h"
#include "ui-debug.h"

/* XXX just note for the future */
static struct sound *intro_sound;

typedef struct game_ui game_ui;
cresp_struct_ret(game_ui);

cresp(game_ui) game_ui_init(struct ui *ui);
void game_ui_done(game_ui *game_ui);

static const char *intro_osd[] = {
    "Oh noes!",
    "I'm trapped!",
    "WHAT AM I TO DO?!?!?!"
    // "WASD to move the character",
    // "Space to jump",
    // "Shift to dash",
    // "Arrows to move the camera",
    // "Have fun"
};

enum main_state {
    MS_STARTING = 0,
    MS_RUNNING,
};

static enum main_state main_state;

static EMSCRIPTEN_KEEPALIVE void render_frame(clap_context *ctx, void *data)
{
    struct scene *s = clap_get_scene(ctx);
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (main_state == MS_STARTING) {
        main_state++;
        ui_osd_new(ui, NULL, intro_osd, array_size(intro_osd));
    }
}

static texture_t pillar_pixel, sphere_tex;
static void make_pillar(struct scene *scene)
{
    auto r = clap_get_renderer(scene->clap_ctx);
    cerr err = texture_pixel_init(r, &pillar_pixel, (float[]){ 0.31, 0.30, 0.33, 1 });
    if (IS_CERR(err))
        err_cerr(err, "couldn't initialize pixel texture\n");

    pipeline *pl = clap_get_pipeline(scene->clap_ctx);
    struct shader_prog *prog = CRES_RET(pipeline_shader_find_get(pl, "model"), return);
    constexpr float height = 2.0;
    model3d *pillar_model = CRES_RET(model3d_new_cylinder(prog, (vec3){}, height - 0.01, 1.0, 6), return);
    model3dtx *txpillar = ref_new(
        model3dtx,
        .model      = ref_pass(pillar_model),
        .tex        = &pillar_pixel,
        .metallic   = 1.0,
        .roughness  = 0.6,
    );
    scene_add_model(scene, txpillar);
    constexpr float radius = 4.0;
    constexpr int nr_segs = 16;
    constexpr int nr_levels = 2;
    for (int i = 0; i < nr_segs * nr_levels; i++) {
        entity3d *e = ref_new(entity3d, .txmodel = txpillar);
        // entity3d_position(e, (vec3) { 0.0, 0.0 + 1.0 * (float)i, -23});
        // entity3d_position(e, (vec3) { 0.0, 0.0 + 1.0 * (float)i, -23});
        // entity3d_position(e, (vec3){ 0 + 1.0*cos(drand48() * M_PI * 2), -0.5 + 1.0 * (float)i, -23 + 1.0*sin(drand48() * M_PI * 2) });
        float x = radius * cosf(((float)(i % nr_segs) / nr_segs) * M_PI * 2);
        float y = height * (float)(i / nr_segs);
        float z = radius * sinf(((float)(i % nr_segs) / nr_segs) * M_PI * 2);
        entity3d_position(e, (vec3){ x, y, -23.0 + z });
        entity3d_add_physics(e, clap_get_phys(scene->clap_ctx), 1, GEOM_TRIMESH, PHYS_BODY, 0, 0, 0);
        phys_ground_entity(clap_get_phys(scene->clap_ctx), e);
        phys_body_set_contact_params(e->phys_body,
                                     .bounce = 0.0,
                                     .bounce_vel = 0.0,
                                     .mu = 1.0,
                                     .soft_cfm = 0.05,
                                     .soft_erp = 0.05);
        phys_body_enable(e->phys_body, true);
    }

    auto font = CRES_RET(ref_new_checked(font, .ctx = clap_get_font(scene->clap_ctx), .name = "ofl/Firjar-SimiBold.ttf", .size = 32), return);
    CERR_RET(texture_init(&sphere_tex, .wrap = TEX_WRAP_MIRRORED_REPEAT, .format = TEX_FMT_RGBA16F, .renderer = clap_get_renderer(scene->clap_ctx)), return);
    tex_print(&sphere_tex, font, TEX_FMT_RGBA16F, (vec3){ 2.0, 0.0, 2.0 }, DRAW_AF_CENTER, "GET CLAP");
    font_put(font);
    LOCAL_SET(mesh_t, sphere_mesh) = CRES_RET(ref_new_checked(mesh, .name = "sphere"), return);
    constexpr float sphere_radius = 2.0f;
    prim_emit_sphere((vec3){}, sphere_radius, 20, .mesh = sphere_mesh);
    auto sphere_model = CRES_RET(ref_new_checked(model3d, .mesh = sphere_mesh, .name = "sphere", .prog = prog), return);
    auto sphere_txmodel = CRES_RET(ref_new_checked(model3dtx, .model = ref_pass(sphere_model), .tex = &sphere_tex), return);
    sphere_txmodel->mat.uv_factor = 4.0;
    scene_add_model(scene, sphere_txmodel);
    for (int i = 0; i < 16; i++) {
        auto sphere = CRES_RET(ref_new_checked(entity3d, .txmodel = sphere_txmodel), return);
        sphere->bloom_intensity = -0.02;
        entity3d_position(sphere, (vec3){ -10.0 + sphere_radius * cosf(M_PI * 2 * ((float)i / 16.0)), 2.0, 25.0 + sphere_radius * sinf(M_PI * 2 * ((float)i / 16.0)) });
        entity3d_add_physics(sphere, clap_get_phys(scene->clap_ctx), 0.5, GEOM_CAPSULE, PHYS_BODY, 0, sphere_radius, 0);
        phys_body_set_contact_params(sphere->phys_body,
                                     .bounce = 2.8,
                                     .bounce_vel = FLT_MIN,
                                     .mu = 0.0,
                                    //  .soft_cfm = 1.0,
                                    //  .soft_erp = 0.8);
        );
        phys_body_enable(sphere->phys_body, true);
    }
}

static int handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct scene *scene = data;
    bool  store = false;

    if (!intro_sound)
        return 0;

    float gain = sound_get_gain(intro_sound);

    if (m->input.volume_up) {
        gain += 0.05;
        sound_set_gain(intro_sound, gain);
        store = true;
    } else if (m->input.volume_down) {
        gain -= 0.05;
        sound_set_gain(intro_sound, gain);
        store = true;
    }

    if (store) {
        struct settings *settings = clap_get_settings(scene->clap_ctx);
        settings_set_num(settings, NULL, "music_volume", gain);
    }
    return 0;
}

static int handle_command(struct clap_context *ctx, struct message *m, void *data)
{
    return 0;
}

static cerr early_init(clap_context *ctx, void *data)
{
    struct scene *scene = data;

    return subscribe(ctx, MT_COMMAND, handle_command, scene);
}

static void graphics_init(clap_context *ctx, void *data)
{
    auto ropts = clap_get_render_options(ctx);
    ropts->fog_near = 20.0;
    ropts->fog_far = 90.0;
    vec3_dup(ropts->fog_color, (vec3){ 0.043, 0.356, 0.369 });
    ropts->lighting_operator = 1.0;
    ropts->contrast = 0.08;
    ropts->bloom_exposure = 10.0;
    ropts->bloom_intensity = 3.0;
    ropts->lighting_exposure = 1.94;
    ropts->edge_sobel = true;
    ropts->edge_antialiasing = false;
    ropts->shadow_outline = false;

    CERR_RET(
        clap_set_lighting_lut(ctx, "scifi bluegreen"),
        err_cerr(__cerr, "failed to set LUT\n")
    );
}

int main(int argc, char **argv, char **envp)
{
    struct clap_config cfg = {
        .debug          = 1,
        .input          = 1,
        .font           = 1,
        .sound          = 1,
        .phys           = 1,
        .graphics       = 1,
        .ui             = 1,
        .settings       = 1,
        .title          = CLAP_EXECUTABLE_TITLE,
#ifndef CONFIG_BROWSER
        .base_url       = "demo/ldjam56/",
#endif
        .width          = 1280,
        .height         = 720,
#ifndef CONFIG_FINAL
        .networking = &(struct networking_config) {
            .server_ip     = CONFIG_SERVER_IP,
            .server_port   = 21044,
            .server_wsport = 21045,
            .logger        = 1,
        },
#endif /* CONFIG_FINAL */
        .early_init     = early_init,
        .graphics_init  = graphics_init,
        .frame_cb       = render_frame,
        .default_font_name  = CLAP_FONT_FILE,
#ifdef CONFIG_FINAL
        .lut_presets    = (lut_preset[]){ LUT_SCIFI_BLUEGREEN },
#else
        .lut_presets    = lut_presets_all,
#endif /* CONFIG_FINAL */
    };

    cresp(clap_context) clap_res = clap_init(&cfg, argc, argv, envp);
    if (IS_CERR(clap_res)) {
        err_cerr(clap_res, "failed to initialize clap\n");
        return CERR_TO_EXIT(clap_res);
    }

    __unused auto gui = CRES_RET(game_ui_init(clap_get_ui(clap_res.val)), return EXIT_FAILURE);

    /*
     * XXX: this doesn't belong here, same as imgui_render_begin()
     * and any error paths past this point must call the corresponding
     * renderer_frame_end() etc
     */
    renderer_frame_begin(clap_get_renderer(clap_res.val));
    imgui_render_begin(cfg.width, cfg.height);

    auto scene = clap_get_scene(clap_res.val);
    cerr err = subscribe(clap_res.val, MT_INPUT, handle_input, scene);
    if (IS_CERR(err))
        goto exit_scene;

    scene->ls = loading_screen_init(clap_get_ui(clap_res.val));

    intro_sound = ref_new(sound, .ctx = clap_get_sound(clap_res.val), .name = "morning.ogg");
    if (intro_sound) {
        float intro_gain = settings_get_num(clap_get_settings(clap_res.val), NULL, "music_volume");
        sound_set_gain(intro_sound, intro_gain);
        sound_set_looping(intro_sound, true);
        sound_play(intro_sound);
    }

    fuzzer_input_init(clap_res.val);

    scene_load(scene, "scene.json");

    loading_screen_done(scene->ls);

    make_pillar(scene);
    scene->lin_speed = 2.0;
    scene->ang_speed = 45.0;
    scene->limbo_height = 70.0;

    imgui_render();
    renderer_frame_end(clap_get_renderer(clap_res.val));
    display_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    if (intro_sound)
        ref_put(intro_sound);
exit_scene:
    game_ui_done(gui);
    clap_done(clap_res.val, 0);
#else
exit_scene:
    if (IS_CERR(err))
        imgui_render();
#endif

    return EXIT_SUCCESS;
}
