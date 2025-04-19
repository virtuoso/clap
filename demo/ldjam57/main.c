// SPDX-License-Identifier: Apache-2.0
#include <getopt.h>
#include <sched.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
//#include <linux/time.h> /* XXX: for intellisense */
#include <math.h>
#include <unistd.h>
#include "shader_constants.h"
#include "object.h"
#include "common.h"
#include "input.h"
#include "font.h"
#include "messagebus.h"
#include "librarian.h"
#include "loading-screen.h"
#include "model.h"
#include "shader.h"
#include "terrain.h"
#include "ui.h"
#include "scene.h"
#include "sound.h"
#include "pipeline-builder.h"
#include "physics.h"
#include "primitives.h"
#include "networking.h"
#include "profiler.h"
#include "settings.h"
#include "ui-debug.h"
#include "game.h"

static struct sound *intro_sound;
static int exit_timeout = -1;
static struct scene scene;

static texture_t platform_emission_purple;
static texture_t platform_emission_teal;
static texture_t platform_emission_peach;
static texture_t platform_emission_orange;

enum main_state {
    MS_STARTING = 0,
    MS_RUNNING,
    MS_THE_END,
};

static enum main_state main_state;

static struct ui_element *switcher, *switcher_text;

typedef struct switch_obj {
    entity3d        *e;
    struct list     platforms;
    const char      *name;
    bool            toggled;
    bool            permanent;
} switch_obj;

typedef struct platform_obj {
    entity3d        *e;
    struct list     entry;
    switch_obj      *sw;
    char            *sw_name;
    vec3            pos;
    int             (*orig_update)(entity3d *, void *);
} platform_obj;

typedef struct character_obj {
    entity3d        *e;
    int             (*orig_update)(entity3d *, void *);
    float           my_distance;
    float           distance_to_active;
    bool            connected;
} character_obj;

static darray(platform_obj, pobjs);
static darray(switch_obj, sobjs);
static darray(character_obj, cobjs);

static struct character *control;

static int platform_entity_update(entity3d *e, void *data)
{
    platform_obj *pobj = e->connect_priv;
    if (!pobj)
        return -1;

    /* here is where would animate the appearance of a platform */
    e->update = pobj->orig_update;
    entity3d_position(e, pobj->pos);
    e->visible = 1;

    return pobj->orig_update(e, data);
}

static void switch_connect(entity3d *e, entity3d *connection, void *data)
{
    struct character *c = connection->priv;
    switch_obj *sobj = data;
    if (!sobj)
        return;

    if (sobj->toggled)
        return;

    sobj->toggled = true;
    platform_obj *pobj;
    list_for_each_entry(pobj, &sobj->platforms, entry) {
        if (pobj->e->update != platform_entity_update) {
            pobj->orig_update = pobj->e->update;
            pobj->e->update = platform_entity_update;
            pobj->e->connect_priv = pobj;
        }
        pobj->e->visible = 1;
    }
}

static void switch_disconnect(entity3d *e, entity3d *connection, void *data)
{
    struct character *c = connection->priv;
    switch_obj *sobj = data;
    if (!sobj)
        return;

    if (sobj->permanent)
        return;

    if (!sobj->toggled)
        return;

    sobj->toggled = false;
    platform_obj *pobj;
    list_for_each_entry(pobj, &sobj->platforms, entry) {
        vec3 pos;
        vec3_add(pos, pobj->pos, (vec3){ 0, 100, 0 });
        entity3d_position(pobj->e, pos);
        pobj->e->visible = 0;
        pobj->e->update = pobj->orig_update;
    }
}

static void character_obj_next(struct scene *s)
{
    character_obj *cobj;
    
    do {
        struct character *ch;

        do {
            scene_control_next(s);

            ch = scene_control_character(s);
        } while (ch == s->camera->ch);

        cobj = ch->entity->connect_priv;
    } while (!cobj->connected);
}

static void switcher_update(struct scene *s)
{
    static char buf[512];
    character_obj *cobj;
    size_t len = 0;
    int i = 0;

    darray_for_each(cobj, cobjs) {
        if (!cobj->connected)
            continue;

        bool brackets = scene_camera_follows(s, cobj->e->priv);
        if (brackets)
            control = cobj->e->priv;
        len += snprintf(buf + len, sizeof(buf) - len, "%s%s%s%s", len ? "\n" : "",
                        brackets ? "> " : "", entity_name(cobj->e), brackets ? " <" : "");
    }

    if (switcher_text)
        ref_put_last(switcher_text);

    font_context *font_ctx = clap_get_font(s->clap_ctx);
    struct font *font = font_get_default(font_ctx);
    struct ui *ui = clap_get_ui(s->clap_ctx);
    switcher_text = ui_printf(ui, font, switcher, (float[]){ 1.0f, 1.0f, 1.0f, 1.0f },
                              UI_AF_BOTTOM | UI_AF_LEFT, "%s", buf);
    ref_put(font);
}

static const float game_over_start_height = -130.0;
static const float game_over_end_height = -450.0;

static int character_obj_update(entity3d *e, void *data)
{
    character_obj *cobj = e->connect_priv;
    if (!cobj)
        return -1;

    struct character *c = cobj->e->priv;
    struct scene *s = data;

    unsigned int update = 0;

    if (scene_camera_follows(s, c)) {
        if (c != control)
            update++;

        if (!cobj->connected) {
            cobj->connected = true;
            update++;
        }

        character_obj *target;
        darray_for_each(target, cobjs) {
            if (target->e->priv == s->camera->ch)
                continue;

            if (target == cobj)
                continue;

            vec3 dist;
            vec3_sub(dist, target->e->pos, cobj->e->pos);
            target->distance_to_active = vec3_mul_inner(dist, dist);
            if (target->distance_to_active < target->my_distance) {
                target->connected = true;
                update++;
            }
        }
    }

    if (update)
        switcher_update(s);

    if (e->pos[1] <= game_over_start_height) {
        static int once = 0;

        if (!once++) {
            main_state++;
            s->limbo_height = fabsf(game_over_end_height - game_over_start_height) + 10;
        }

        s->camera->target_yaw += 90 / fabsf(game_over_end_height - game_over_start_height);
        s->camera->ch->moved++;
    }

    return cobj->orig_update(e, data);
}

static void process_entity(entity3d *e, void *data)
{
    const char *name = entity_name(e);
    char *substr;

    bool permanent = !!strstr(name, ".P.");

    if ((substr = strstr(name, ".platform"))) {
        platform_obj *pobj = darray_add(pobjs);
        pobj->e = e;
        vec3_dup(pobj->pos, e->pos);

        pobj->sw_name = strndup(name, substr - name);

        /* hide the platform */
        e->visible = 0;
        entity3d_move(e, (vec3){ 0, 100, 0 });

        model3dtx *txm = e->txmodel;
        if (!permanent && texture_loaded(txm->emission) && txm->emission != &platform_emission_peach) {
            texture_deinit(txm->emission);
            model3dtx_set_texture(txm, UNIFORM_EMISSION_MAP, &platform_emission_peach);
        }
    } else if ((substr = strstr(name, ".switch"))) {
        switch_obj *sobj = darray_add(sobjs);
        sobj->e = e;
        sobj->name = name;
        e->connect = switch_connect;
        e->disconnect = switch_disconnect;

        if (permanent)
            sobj->permanent = true;
    } else if (str_endswith(name, "dude") || strstr(name, ".body")) {
        character_obj *cobj = darray_add(cobjs);
        cobj->e = e;
        cobj->orig_update = e->update;
        e->update = character_obj_update;
        cobj->my_distance = entity3d_aabb_Y(e) * 3.0;
        cobj->my_distance *= cobj->my_distance;
    } else if (!strncmp(name, "glowing spheres around", 22)) {
        texture_deinit(e->txmodel->emission);
        model3dtx_set_texture(e->txmodel, UNIFORM_EMISSION_MAP, &platform_emission_peach);
    }
}

static void process_scene(struct scene *s)
{
    model3dtx *txm;

    mq_for_each(&s->mq, process_entity, NULL);

    /*
     * These have to be done after the darrays have been filled,
     * because darray_add() may do realloc() and old pointers to
     * its elements become invalid.
     */
    switch_obj *sobj;
    darray_for_each(sobj, sobjs) {
        list_init(&sobj->platforms);
        sobj->e->connect_priv = sobj;
    }

    platform_obj *pobj;
    darray_for_each(pobj, pobjs) {
        darray_for_each(sobj, sobjs)
            if (!strcmp(pobj->sw_name, sobj->name)) {
                pobj->sw = sobj;

                list_append(&sobj->platforms, &pobj->entry);
                break;
            }
    }

    character_obj *cobj;
    darray_for_each(cobj, cobjs)
        cobj->e->connect_priv = cobj;
}

static void startup(struct scene *s)
{
    cerr err;

    darray_init(pobjs);
    darray_init(sobjs);
    darray_init(cobjs);

    /* common scene parameters */
    s->lin_speed = 2.0;
    s->ang_speed = 90.0;
    s->limbo_height = 70.0;
    s->render_options.bloom_intensity = 1.1;
    s->render_options.bloom_threshold = 0.3;
    s->render_options.bloom_exposure = 2.5;
    s->render_options.shadow_outline = false;

    /* pixel textures for everyday use */
    err = texture_pixel_init(&platform_emission_purple, (float[]){ 0.5, 0.3, 0.5, 1 });
    if (IS_CERR(err))
        err_cerr(err, "couldn't initialize pixel texture\n");
    err = texture_pixel_init(&platform_emission_teal, (float[]){ 0.3, 0.5, 0.5, 1 });
    if (IS_CERR(err))
        err_cerr(err, "couldn't initialize pixel texture\n");
    err = texture_pixel_init(&platform_emission_peach, (float[]){ 0.5, 0.375, 0.3, 1 });
    if (IS_CERR(err))
        err_cerr(err, "couldn't initialize pixel texture\n");

    struct ui *ui = clap_get_ui(s->clap_ctx);
    cresp(ui_element) res = ref_new_checked(ui_element,
        .ui         = ui,
        .txmodel    = ui_quadtx_get(),
        .affinity   = UI_AF_BOTTOM | UI_AF_RIGHT,
        .x_off      = 0.05,
        .y_off      = 5,
        .width      = 300,
        .height     = 400,
    );
    if (IS_CERR(res)) {
        err_cerr(res, "can't create UI element\n");
        return;
    }
    switcher = res.val;
}

static void cleanup(struct scene *s)
{
    if (switcher_text)
        ref_put_last(switcher_text);
    ref_put_last(switcher);

    platform_obj *pobj;
    darray_for_each(pobj, pobjs)
        mem_free(pobj->sw_name);
    darray_clearout(pobjs);
    darray_clearout(sobjs);
    darray_clearout(cobjs);
}

static bool shadow_msaa, model_msaa, edge_aa, edge_sobel;

static void build_main_pl(struct pipeline **pl)
{
    *pl = pipeline_build(&(pipeline_builder_opts) {
        .pl_opts = &(pipeline_init_options) {
                  .width            = scene.width,
                  .height           = scene.height,
                  .light            = &scene.light,
                  .camera           = &scene.cameras[0],
                  .shader_ctx       = clap_get_shaders(scene.clap_ctx),
                  .renderer         = clap_get_renderer(scene.clap_ctx),
                  .render_options   = &scene.render_options,
                  .name             = "main"
        },
        .mq     = &scene.mq,
        .pl     = scene.pl
    });
}

static const char *intro_osd[] = {
    "Arrows to move the camera",
    "WASD to move the character",
    "Enter to switch bodies",
    "Have fun"
};

static const char *outro_osd[] = { "Thank you for playing!", "The End" };

EMSCRIPTEN_KEEPALIVE void render_frame(void *data)
{
    struct scene *s = data;
    renderer_t *r = clap_get_renderer(s->clap_ctx);
    struct ui *ui = clap_get_ui(s->clap_ctx);

    if (main_state == MS_STARTING) {
        main_state++;
        ui_osd_new(ui, intro_osd, array_size(intro_osd));
    } else if (main_state == MS_THE_END) {
        main_state++;
        ui_osd_new(ui, outro_osd, array_size(outro_osd));
    }

    pipeline_render(scene.pl, ui->modal ? 1 : 0);

    if (scene.render_options.debug_draws_enabled) {
        models_render(r, &scene.debug_mq, .camera = scene.camera, .cascade = -1);
        debug_draw_clearout(s);
    }

    if (shadow_msaa != s->render_options.shadow_msaa ||
        model_msaa != s->render_options.model_msaa ||
        edge_sobel != s->render_options.edge_sobel ||
        edge_aa != s->render_options.edge_antialiasing) {
        shadow_msaa = s->render_options.shadow_msaa;
        model_msaa = s->render_options.model_msaa;
        edge_sobel = s->render_options.edge_sobel;
        edge_aa = s->render_options.edge_antialiasing;
        pipeline_clearout(scene.pl);
        build_main_pl(&scene.pl);
    }
    pipeline_debug(scene.pl);
}

/*
 * XXX: this should be a message
 * cmd.resize
 */
void resize_cb(void *data, int width, int height)
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

static int handle_input(struct message *m, void *data)
{
    struct scene *scene = data;
    bool  store = false;

    if (m->input.enter)
        character_obj_next(scene);

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

static int handle_command(struct message *m, void *data)
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
        .title          = "Towards the Light",
#ifndef CONFIG_BROWSER
        .base_url       = "demo/ldjam57/",
#endif
        .width          = 1280,
        .height         = 720,
        .frame_cb       = render_frame,
        .resize_cb      = resize_cb,
        .callback_data  = &scene,
        .default_font_name  = "ofl/Unbounded-Regular.ttf",
    };
    struct networking_config ncfg = {
        .server_ip     = CONFIG_SERVER_IP,
        .server_port   = 21044,
        .server_wsport = 21045,
        .logger        = 1,
    };
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

    imgui_render_begin(cfg.width, cfg.height);
    scene_init(&scene);
    scene.clap_ctx = clap_res.val;
    scene.ls = loading_screen_init(clap_get_ui(clap_res.val));

    cerr err;
#ifndef CONFIG_FINAL
    ncfg.clap = clap_res.val;
    err = networking_init(&ncfg, CLIENT);
    if (IS_CERR(err))
        err_cerr(err, "failed to initialize networking\n");
#endif

    phys_set_ground_contact(clap_get_phys(scene.clap_ctx), ohc_ground_contact);

    err = subscribe(MT_INPUT, handle_input, &scene);
    if (IS_CERR(err))
        goto exit_scene;

    err = subscribe(MT_COMMAND, handle_command, &scene);
    if (IS_CERR(err))
        goto exit_scene;

    // intro_sound = ref_new(sound, .ctx = clap_get_sound(scene.clap_ctx), .name = "morning.ogg");
    if (intro_sound) {
        float intro_gain = settings_get_num(clap_get_settings(scene.clap_ctx), NULL, "music_volume");
        sound_set_gain(intro_sound, intro_gain);
        sound_set_looping(intro_sound, true);
        sound_play(intro_sound);
    }

    display_get_sizes(&scene.width, &scene.height);

    build_main_pl(&scene.pl);

    fuzzer_input_init();

    if (fullscreen)
        display_enter_fullscreen();

    scene_camera_add(&scene);
    scene.camera = &scene.cameras[0];

    scene_load(&scene, "scene.json");

    startup(&scene);
    process_scene(&scene);

    loading_screen_done(scene.ls);

    scene_cameras_calc(&scene);

    imgui_render();
    display_main_loop();

    dbg("exiting peacefully\n");

#ifndef CONFIG_BROWSER
exit_scene:
    cleanup(&scene);

    scene_done(&scene);
    ref_put(scene.pl);
    clap_done(scene.clap_ctx, 0);
#else
exit_scene:
    if (IS_CERR(err))
        imgui_render();
#endif

    return EXIT_SUCCESS;
}
