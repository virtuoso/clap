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
static unsigned long frame;

static EMSCRIPTEN_KEEPALIVE void render_frame(clap_context *ctx, void *data)
{
    struct scene *s = clap_get_scene(ctx);
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (s->ls && ui_state_get(ui) == UI_ST_LOADING) {
        if (frame++ < 400)  { loading_screen_progress(s->ls, (float)frame / 400.0); }
        else                { loading_screen_done(s->ls); s->ls = nullptr; ui_state_set_running(ui); }
    }

    if (clap_is_paused(ctx))    return;

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

static void loading_start(clap_context *clap_ctx, void *data)
{
    auto scene = clap_get_scene(clap_ctx);
    auto ui = clap_get_ui(clap_ctx);

    scene->ls = loading_screen_init(ui);
    dbg("### loading screen: %p\n", scene->ls);
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
        .ui_menu        = { .enable = true },
        .ui_start_menu  = { .enable = true, .loading_cb = loading_start },
        .settings       = 1,
        .title          = CLAP_EXECUTABLE_TITLE,
#ifndef CONFIG_BROWSER
        .base_url       = "demo/ldjam59/",
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

    auto scene = clap_get_scene(clap_res.val);
    cerr err = subscribe(clap_res.val, MT_INPUT, handle_input, scene);
    if (IS_CERR(err))
        goto exit_scene;

    intro_sound = ref_new(sound, .ctx = clap_get_sound(clap_res.val), .name = "morning.ogg");
    if (intro_sound) {
        float intro_gain = settings_get_num(clap_get_settings(clap_res.val), NULL, "music_volume");
        sound_set_gain(intro_sound, intro_gain);
        sound_set_looping(intro_sound, true);
        sound_play(intro_sound);
    }

    fuzzer_input_init(clap_res.val);

    scene->lin_speed = 2.0;
    scene->ang_speed = 45.0;
    scene->limbo_height = 70.0;

    display_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
    if (intro_sound)
        ref_put(intro_sound);
exit_scene:
    clap_done(clap_res.val, 0);
#else
exit_scene:
#endif

    return EXIT_SUCCESS;
}
