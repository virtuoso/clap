// SPDX-License-Identifier: Apache-2.0
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
    "WASD to move the character",
    "Space to jump",
    "Shift to dash",
    "Arrows to move the camera",
    "Have fun"
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
    ropts->fog_near = 200.0;
    ropts->fog_far = 300.0;
    ropts->contrast = 0.15;
    ropts->lighting_exposure = 1.6;
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
        .lut_presets    = (lut_preset[]){ LUT_TEAL_ORANGE },
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

    CERR_RET(clap_set_lighting_lut(clap_res.val, "teal orange"), goto exit_sound);

    fuzzer_input_init(clap_res.val);

    scene_load(scene, "scene.json");

    loading_screen_done(scene->ls);

    scene->lin_speed = 2.0;
    scene->ang_speed = 45.0;
    scene->limbo_height = 70.0;
    render_options *ropts = clap_get_render_options(clap_res.val);
    ropts->fog_near = 200.0;
    ropts->fog_far = 300.0;
    ropts->lighting_operator = 1.0;
    ropts->contrast = 0.15;
    ropts->lighting_exposure = 1.6;

    imgui_render();
    renderer_frame_end(clap_get_renderer(clap_res.val));
    display_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
exit_sound:
    if (intro_sound)
        ref_put(intro_sound);
exit_scene:
    game_ui_done(gui);
    clap_done(clap_res.val, 0);
#else
exit_sound:
exit_scene:
    if (IS_CERR(err))
        imgui_render();
#endif

    return EXIT_SUCCESS;
}
