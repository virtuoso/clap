/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCENE_H__
#define __CLAP_SCENE_H__

#include "model.h"
#include "motion.h"
#include "light.h"
#include "loading-screen.h"
#include "physics.h"
#include "pipeline.h"
#include "camera.h"
#include "sound.h"
#include "navmesh.h"
#include "json.h"

#define NR_CAMERAS_MAX 4

struct instantiator {
    const char      *name;
    struct list     entry;
    float           dx, dy, dz;
};

#ifndef CONFIG_FINAL
typedef struct entity_inspector {
    entity3d    *entity;
    bool        switch_control;
    bool        follow_control;
} entity_inspector;
#endif /* CONFIG_FINAL */

struct scene_loader;
struct worker_pool;

#define SCENE_NAME_MAX 128
struct scene {
    char                name[SCENE_NAME_MAX];
    struct mq           mq;
    struct list         characters;
    struct list         instor;
    entity3d            *control;
    struct motionctl    mctl;
    struct camera       *camera;
    struct camera       cameras[NR_CAMERAS_MAX];
    struct light        light;
    sfx_container       sfxc;
    struct clap_context *clap_ctx;
#ifndef CONFIG_FINAL
    entity_inspector    entity_inspector;
#endif /* CONFIG_FINAL */
    JsonNode            *json_root;
    char                *file_name;
    navmesh             *nm;
    navmesh_params      nm_params;
    loading_screen      *ls;
    /* the scene_load_async() under way, nullptr when there's none */
    struct scene_loader *loader;
    /* the loader thread, from the first scene_load_async() to scene_done() */
    struct worker_pool  *loader_pool;
    float               lin_speed;
    float               ang_speed;
    float               limbo_height;
    float               auto_yoffset;
    int                 nr_cameras;
    int                 fullscreen;
    int                 lut_autoswitch;
    clap_timer          *lut_timer;
    bool                initialized;
    bool                nm_wanted;
};

struct character;

int scene_get_light(struct scene *scene);
cres(int) scene_camera_add(struct scene *s);
void scene_cameras_calc(struct scene *s);
int scene_add_model(struct scene *s, model3dtx *txm);

/**
 * scene_navmesh_init() - build the scene's navigation mesh
 * @scene:  scene whose static geometry to bake
 *
 * Feeds every static collision trimesh in the scene to a fresh navmesh
 * (entities flagged ENTITY3D_NM_HAZARD become obstructions), bakes the
 * largest walkable region and stores it on the scene. Uses the params
 * parsed from the scene's "navmesh" block. Replaces any existing mesh,
 * which is freed: whatever points into it has to let go first.
 * Loading a scene file calls this after the file's entries, when the scene
 * declares a "navmesh" block; game code can call it after adding models of
 * its own.
 * Return: CERR_OK, or the error of making or baking the navmesh
 */
cerr scene_navmesh_init(struct scene *scene);

/**
 * scene_navmesh() - the scene's baked navmesh
 * @scene:  the scene
 *
 * Return: the navmesh, or nullptr if the scene has none
 */
navmesh *scene_navmesh(struct scene *scene);

cerr scene_init(struct scene *scene, struct clap_context *ctx);
void scene_done(struct scene *scene);
void scene_save(struct scene *scene, const char *name);

/**
 * scene_load() - load a scene file before returning
 * @scene:  the scene
 * @name:   the scene file, an asset name
 *
 * Puts the scene file's entries in the scene in the order the file has
 * them, and moves @scene->ls's progress after each model
 * (loading_screen_progress()).
 * Context: the main thread
 * Return: CERR_OK; CERR_ALREADY_LOADED if @scene has a scene file in or on
 *         its way; CERR_SCENE_NOT_LOADED if the file doesn't read or parse or
 *         an entry doesn't go in, with the entries before it left in place
 */
cerr scene_load(struct scene *scene, const char *name);

/**
 * typedef scene_loaded_fn - the end of a scene_load_async()
 * @scene:  the scene
 * @err:    CERR_OK with the whole scene file in; otherwise what stopped the
 *          load, with the entries before it in the scene, as scene_load()
 *          leaves them
 * @data:   scene_load_async()'s @data
 *
 * Runs from scene_update(), in the frame the load ends; on success the
 * scene's control is with its first character. scene_is_loading() is false
 * by then, and the load touches nothing after it, so after an error it may
 * start another. It runs inside a frame, so it must not draw one: no
 * scene_load() or loading_screen_progress(), and loading_screen_done() only
 * in %UI_ST_LOADING or in a browser, where it draws nothing.
 */
typedef void (*scene_loaded_fn)(struct scene *scene, cerr err, void *data);

/**
 * scene_load_async() - load a scene file while the frames go on
 * @scene:  the scene, with no scene file in or on its way
 * @name:   the scene file, an asset name
 * @done:   called once when the load is over, either way, unless scene_done()
 *          comes first; may be nullptr
 * @data:   passed to @done
 *
 * Reads and parses @name here, and has a loader thread parse the models'
 * glTF files and prepare their meshes, levels of detail and images, a model
 * or two ahead, while scene_update() puts the scene file's entries in, in
 * file order and no more than one model a frame, and moves @scene->ls's
 * progress bar without drawing a frame of its own. Without a thread to spare
 * (a browser has as many as its build's pthread pool), the frames prepare
 * the models themselves, one a frame. Until the load is over, the frames
 * don't step the scene's physics, draw it or give it input; frame_cb and
 * scripts run as ever.
 *
 * A game without a start menu brackets the load with ui_state_set_loading()
 * and, from @done after loading_screen_done(), ui_state_set_running().
 * Context: the main thread
 * Return: CERR_OK with the load under way and @done to come;
 *         CERR_ALREADY_LOADED if @scene has a scene file in or on its way;
 *         CERR_SCENE_NOT_LOADED if @name doesn't read or parse; CERR_NOMEM
 */
cerr scene_load_async(struct scene *scene, const char *name, scene_loaded_fn done, void *data);

/**
 * scene_is_loading() - is a scene_load_async() under way?
 * @scene:  the scene
 * Return: true from scene_load_async() until right before its @done runs
 */
bool scene_is_loading(struct scene *scene);
void scene_update(struct scene *scene);
bool scene_camera_follows(struct scene *s, struct character *ch);
void scene_characters_move(struct scene *s);
void scene_control_next(struct scene *s);

struct character *scene_control_character(struct scene *s);

#ifndef CONFIG_FINAL
void scene_lut_autoswitch_set(struct scene *scene);
#else
static inline void scene_lut_autoswitch_set(struct scene *scene) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_SCENE_H__ */
