/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCENE_H__
#define __CLAP_SCENE_H__

#include <time.h>
#include "common.h"
#include "character.h"
#include "model.h"
#include "motion.h"
#include "light.h"
#include "loading-screen.h"
#include "physics.h"
#include "pipeline.h"
#include "camera.h"
#include "sound.h"


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

#define SCENE_NAME_MAX 128
struct scene {
    char                name[SCENE_NAME_MAX];
    int                 width;
    int                 height;
    struct mq           mq;
    pipeline            *pl;
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
    loading_screen      *ls;
    float               lin_speed;
    float               ang_speed;
    float               limbo_height;
    float               auto_yoffset;
    int                 nr_cameras;
    int                 fullscreen;
    int                 lut_autoswitch;
    clap_timer          *lut_timer;
    bool                initialized;
};

struct character;

int scene_get_light(struct scene *scene);
cres(int) scene_camera_add(struct scene *s);
void scene_cameras_calc(struct scene *s);
int scene_add_model(struct scene *s, model3dtx *txm);
cerr scene_init(struct scene *scene);
void scene_done(struct scene *scene);
void scene_save(struct scene *scene, const char *name);
cerr scene_load(struct scene *scene, const char *name);
void scene_update(struct scene *scene);
bool scene_camera_follows(struct scene *s, struct character *ch);
void scene_characters_move(struct scene *s);
void scene_control_next(struct scene *s);

static inline struct character *scene_control_character(struct scene *s)
{
    return s->control ? s->control->priv : NULL;
}

#ifndef CONFIG_FINAL
void scene_lut_autoswitch_set(struct scene *scene);
#else
static inline void scene_lut_autoswitch_set(struct scene *scene) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_SCENE_H__ */
