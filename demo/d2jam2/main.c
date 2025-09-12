// SPDX-License-Identifier: Apache-2.0
#include <getopt.h>
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
#include "noise.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"
#include "pipeline-builder.h"
#include "physics.h"
#include "networking.h"
#include "settings.h"
#include "ui-debug.h"

#include "onehandclap.h"

/* XXX just note for the future */
static struct sound *intro_sound;
static int exit_timeout = -1;
static struct scene scene; /* XXX */

static bool shadow_msaa, model_msaa, edge_aa = true, edge_sobel, ssao = true, vsm = true;

static texture_t fog_noise3d;

static void build_main_pl(struct pipeline **pl)
{
    *pl = CRES_RET(
        pipeline_build(&(pipeline_builder_opts) {
            .pl_opts    = &(pipeline_init_options) {
                .width            = scene.width,
                .height           = scene.height,
                .clap_ctx         = scene.clap_ctx,
                .light            = &scene.light,
                .camera           = &scene.cameras[0],
                .noise3d          = &fog_noise3d,
                .nr_cascades      = 1,
                .name             = "main"
            },
            .mq         = &scene.mq,
            .pl         = scene.pl
        }),
        return
    );
}

static particle_system *swarm;
static particle_system *fog;
static int (*orig_update)(entity3d *, void *);
static int swarm_joint = -1;

static int particles_update(entity3d *e, void *data)
{
    if (!swarm) goto out;

    vec3 pos;
    auto joint = &e->joints[swarm_joint];
    vec3_dup(pos, joint->pos);

    if (swarm)  particle_system_position(swarm, pos);
    /* fog doesn't really move with the character */
    // if (fog)    particle_system_position(fog, pos);

out:
    return orig_update(e, data);
}

static __unused void swarm_init(struct scene *scene)
{
    auto e = scene->control;
    auto m = e->txmodel->model;

    swarm_joint = CRES_RET(model3d_get_joint(m, JOINT_HEAD), return);

    auto prog = CRES_RET(pipeline_shader_find_get(scene->pl, "particle"), return);
    swarm = CRES_RET(
        ref_new_checked(particle_system,
            .name       = "swarm",
            .prog       = ref_pass(prog),
            .mq         = &scene->mq,
            .dist       = PART_DIST_CBRT,
            .emit       = white_pixel(),
            .tex        = transparent_pixel(),
            .count      = 500,
            .radius     = 0.75,
            .min_radius = 0.2,
            .scale      = 0.005,
            .velocity   = 0.005,
            .bloom_intensity    = 0.2,
        ),
    );

    orig_update = e->update;
    e->update = particles_update;
}

static __unused void swarm_done(void)
{
    ref_put(swarm);
}

static cerr fog_noise3d_init(texture_t *tex)
{
    if (!tex)   return CERR_INVALID_ARGUMENTS;
    if (!texture_loaded(tex))
        CERR_RET(
            noise_grad3d_bake_rgb8_tex(
                tex, /*side*/32, /*octaves*/1, /*lacunarity*/2.0, /*gain*/0.25, /*period*/32, /*seed*/7
            ),
            return __cerr
        );

    return CERR_OK;
}

static __unused void fog_init(struct scene *scene)
{
    // CERR_RET(noise_grad3d_bake_rgb8_tex(&fog_noise3d, /*side*/32, /*octaves*/1, /*lacunarity*/2.0, /*gain*/0.25, /*period*/32, /*seed*/7), return);

    auto prog = CRES_RET(pipeline_shader_find_get(scene->pl, "particle"), return);
    fog = CRES_RET(
        ref_new_checked(particle_system,
            .name       = "fog",
            .prog       = ref_pass(prog),
            .mq         = &scene->mq,
            .dist       = PART_DIST_CBRT,
            .emit       = transparent_pixel(),
            .tex        = transparent_pixel(),
            .count      = 64,
            .radius     = 50.0,
            .min_radius = 10.0,
            .scale      = 2.0,
            .velocity   = 0.03,
            .bloom_intensity    = 0.0,
        ),
    );

    auto e = particle_system_entity(fog);
    e->txmodel->mat.use_3d_fog = true;
    e->txmodel->mat.fog_3d_amp   = 0.7;//4.0;//0.8;//0.38;//1.6;
    e->txmodel->mat.fog_3d_scale = 0.05;//0.06;//0.09;//0.9;
    e->txmodel->mat.use_noise_normals = NOISE_NORMALS_NONE;
    e->txmodel->mat.noise_normals_amp   = 0.36;//4.0;//0.8;//0.38;//1.6;
    e->txmodel->mat.noise_normals_scale = 1.24;//0.06;//0.09;//0.9;
    e->txmodel->mat.metallic = 0.0;
    e->txmodel->mat.roughness = 1.0;
    model3dtx_set_texture(e->txmodel, UNIFORM_NOISE3D_TEX, &fog_noise3d);
    e->txmodel->model->alpha_blend = true;
}

static __unused void fog_done(void)
{
    ref_put(fog);
}

static const char *intro_osd[] = {
    "WASD to move the character",
    "Space to jump / Shift to dash",
    "Arrows to move the camera",
};

static const char *title_osd[] = {
    "GATHERING",
    "SKILLS",
    "IN A CAVE"
};

static sound *title_sound;

static void title_osd_element_cb(struct ui_element *uie, unsigned int i)
{
    uia_skip_duration(uie, i * 4.0);
    uia_set_visible(uie, 1);
    uia_skip_duration(uie, 3.0);
    uia_lin_float(uie, ui_element_set_alpha, 1.0, 0.0, true, 0.5);
    uia_set_visible(uie, 0);
}

static void __title_kickoff(void *data)
{
    struct scene *scene = data;

    if (title_sound)    sound_play(title_sound);
    
    auto ui = clap_get_ui(scene->clap_ctx);
    ui_osd_new(
        ui,
        &(const struct ui_widget_builder) {
            .el_affinity  = UI_AF_CENTER | UI_SZ_WIDTH_FRAC,
            .affinity   = UI_AF_CENTER | UI_SZ_FRAC,
            .el_w       = 0.9,
            .el_h       = 100,
            .el_margin  = 4,
            .x_off      = 0,
            .y_off      = 0,
            .w          = 0.8,
            .h          = 0.9,
            // .font_name  = "ofl/DMSerifDisplay-Regular.ttf",
            .font_name  = "ofl/ZillaSlab-Bold.ttf",
            .font_size  = 240,
            .el_cb      = title_osd_element_cb,
            .el_color   = { 0.0, 0.0, 0.0, 0.0 },
            .text_color = { 0.8, 0.8, 0.8, 1.0 },
        },
        title_osd,
        array_size(title_osd)
    );
}

static clap_timer *title_timer;

static void title_kickoff(struct scene *scene)
{
    if (title_timer) return;

    title_timer = CRES_RET(clap_timer_set(scene->clap_ctx, 10.0, NULL, __title_kickoff, scene),);
}

enum main_state {
    MS_STARTING = 0,
    MS_RUNNING,
};

static enum main_state main_state;

static EMSCRIPTEN_KEEPALIVE void render_frame(void *data)
{
    struct scene *s = data;
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (main_state == MS_STARTING) {
        main_state++;
        ui_osd_new(ui, NULL, intro_osd, array_size(intro_osd));
    }

    pipeline_render(scene.pl, clap_is_paused(s->clap_ctx) ? 1 : 0);

    render_options *ropts = clap_get_render_options(s->clap_ctx);
    if (shadow_msaa != ropts->shadow_msaa ||
        model_msaa != ropts->model_msaa ||
        edge_sobel != ropts->edge_sobel ||
        ssao != ropts->ssao ||
        vsm != ropts->shadow_vsm ||
        edge_aa != ropts->edge_antialiasing) {
        shadow_msaa = ropts->shadow_msaa;
        model_msaa = ropts->model_msaa;
        edge_sobel = ropts->edge_sobel;
        edge_aa = ropts->edge_antialiasing;
        ssao = ropts->ssao;
        vsm = ropts->shadow_vsm;
        pipeline_clearout(scene.pl);
        build_main_pl(&scene.pl);
    }
    pipeline_debug(scene.pl);
}

/*
 * XXX: this should be a message
 * cmd.resize
 */
static void resize_cb(void *data, int width, int height)
{
    struct scene *scene = data;

    if (!scene->initialized)
        return;

    if (scene->pl)
        pipeline_resize(scene->pl, width, height);
}

static void ohc_ground_contact(void *priv, float x, float y, float z)
{
    if (scene.auto_yoffset < y)
        scene.auto_yoffset = y;
}

static int handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct scene *scene = data;
    bool  store = false;

    if (fabsf(m->input.delta_lx) > 1e-3 || m->input.up || m->input.down || m->input.left || m->input.right)
        title_kickoff(scene);

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
    if (m->cmd.status && exit_timeout >= 0) {
        if (!exit_timeout--)
            display_request_exit();
    }

    return 0;
}

static struct option long_options[] = {
    { "fullscreen", no_argument,        0, 'F' },
    { "exitafter",  required_argument,  0, 'e' },
    { "aoe",        no_argument,        0, 'E' },
    { "server",     required_argument,  0, 'S'},
    {}
};

static const char short_options[] = "e:EFS:";

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
        .title          = "Cave Gathering",
#ifndef CONFIG_BROWSER
        .base_url       = "demo/d2jam2/",
#endif
        .width          = 1280,
        .height         = 720,
        .frame_cb       = render_frame,
        .resize_cb      = resize_cb,
        .callback_data  = &scene,
        .default_font_name  = "ofl/Chivo[wght].ttf",
#ifdef CONFIG_FINAL
        .lut_presets    = (lut_preset[]){ LUT_SCIFI_BLUEGREEN, LUT_DEEP_SEA_ABYSS, LUT_BLOODVEIL_CRIMSON },
#else
        .lut_presets    = lut_presets_all,
#endif /* CONFIG_FINAL */
    };
#ifndef CONFIG_FINAL
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = 21044,
        .server_wsport = 21045,
        .logger        = 1,
    };
#endif /* CONFIG_FINAL */
    int c, option_index;
    unsigned int fullscreen = 0;

    for (;;) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'F':
            fullscreen++;
            break;
#ifndef CONFIG_FINAL
        case 'e':
            exit_timeout = atoi(optarg);
            break;
        case 'E':
            abort_on_error++;
            break;
        case 'S':
            ncfg.server_ip = optarg;
            break;
#endif /* CONFIG_FINAL */
        default:
            fprintf(stderr, "invalid option %x\n", c);
            exit(EXIT_FAILURE);
        }
    }

    cresp(clap_context) clap_res = clap_init(&cfg, argc, argv, envp);
    if (IS_CERR(clap_res)) {
        err_cerr(clap_res, "failed to initialize clap\n");
        return EXIT_FAILURE;
    }

    CRES_RET(game_ui_init(clap_get_ui(clap_res.val)), return EXIT_FAILURE);

    /*
     * XXX: this doesn't belong here, same as imgui_render_begin()
     * and any error paths past this point must call the corresponding
     * renderer_frame_end() etc
     */
    renderer_frame_begin(clap_get_renderer(clap_res.val));
    imgui_render_begin(cfg.width, cfg.height);
    scene_init(&scene, clap_res.val);

    cerr err;
#ifndef CONFIG_FINAL
    ncfg.clap = clap_res.val;
    err = networking_init(scene.clap_ctx, &ncfg, CLIENT);
    if (IS_CERR(err))
        err_cerr(err, "failed to initialize networking\n");
#endif

    phys_set_ground_contact(clap_get_phys(scene.clap_ctx), ohc_ground_contact);

    err = subscribe(scene.clap_ctx, MT_INPUT, handle_input, &scene);
    if (IS_CERR(err))
        goto exit_scene;

    err = subscribe(scene.clap_ctx, MT_COMMAND, handle_command, &scene);
    if (IS_CERR(err))
        goto exit_scene;

    display_get_sizes(&scene.width, &scene.height);
    scene.ls = loading_screen_init(clap_get_ui(clap_res.val));

    // intro_sound = ref_new(sound, .ctx = clap_get_sound(scene.clap_ctx), .name = "morning.ogg");
    // if (intro_sound) {
    //     float intro_gain = settings_get_num(clap_get_settings(scene.clap_ctx), NULL, "music_volume");
    //     sound_set_gain(intro_sound, intro_gain);
    //     sound_set_looping(intro_sound, true);
    //     sound_play(intro_sound);
    // }

    title_sound = ref_new(sound, .ctx = clap_get_sound(scene.clap_ctx), .name = "brass attack.ogg");
    if (title_sound)    sound_set_gain(title_sound, 0.1);

    // CERR_RET(clap_set_lighting_lut(scene.clap_ctx, "bloodveil crimson"), goto exit_sound);
    CERR_RET(clap_set_lighting_lut(scene.clap_ctx, "deep sea abyss"), goto exit_sound);

    scene_camera_add(&scene);
    scene.camera = &scene.cameras[0];
    scene.camera->view.main.far_plane = 700.0;
    scene_cameras_calc(&scene);

    fog_noise3d_init(&fog_noise3d);

    build_main_pl(&scene.pl);

    fuzzer_input_init(scene.clap_ctx);

    if (fullscreen)
        display_enter_fullscreen();

    make_cave(&scene, NULL);
    scene_load(&scene, "cave.json");
    noisy_mesh(&scene);

    // swarm_init(&scene);
    fog_init(&scene);

    loading_screen_done(scene.ls);

    scene.lin_speed = 2.0;
    scene.ang_speed = 45.0;
    scene.limbo_height = 70.0;
    render_options *ropts = clap_get_render_options(scene.clap_ctx);
    ropts->fog_near = 10.0;
    ropts->fog_far = 200.0;
    vec3_dup(ropts->fog_color, (vec3) { 0.0, 0.35, 0.605});
    ropts->lighting_operator = 1.0;
    ropts->contrast = 0.1;
    ropts->lighting_exposure = 2.6;
    ropts->bloom_threshold = 0.7;
    ropts->bloom_exposure = 2.6;
    ropts->bloom_intensity = 2.4;
    // ropts->light_drqaws_enabled = true;
    ropts->film_grain = true;

    imgui_render();
    renderer_frame_end(clap_get_renderer(clap_res.val));
    display_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    //swarm_done();
    fog_done();
exit_sound:
    // if (intro_sound)
    //     ref_put(intro_sound);
exit_scene:
    scene_done(&scene);
    ref_put(scene.pl); /* XXX: scene_init()/scene_done() */
    clap_done(scene.clap_ctx, 0);
#else
exit_sound:
exit_scene:
    if (IS_CERR(err))
        imgui_render();
#endif

    return EXIT_SUCCESS;
}
