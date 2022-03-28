/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCENE_H__
#define __CLAP_SCENE_H__

#include <time.h>
#include "common.h"
#include "character.h"
#include "matrix.h"
#include "model.h"
#include "physics.h"

#define NR_CAMERAS_MAX 4
struct camera {
    struct character *ch;
    GLfloat pitch;  /* left/right */
    GLfloat yaw;    /* sideways */
    GLfloat roll;   /* up/down */
    unsigned int zoom;
    float   dist;
    float   yaw_turn;
    float   pitch_turn;
    struct matrix4f     *view_mx;
    struct matrix4f     *inv_view_mx;
};

struct scene {
    char                *name;
    int                 width;
    int                 height;
    struct model3d      *_model; /* temporary */
    struct mq           mq;
    struct list         characters;
    struct entity3d     *focus;
    struct character    *control;
    struct shader_prog  *prog;
    struct matrix4f     *proj_mx;
    struct terrain      *terrain;
    struct camera       *camera;
    struct camera       cameras[NR_CAMERAS_MAX];
    struct light        light;
    /* FPS calculation -- very important! */
    unsigned long       frames_total;
    struct timespec     ts;
    struct fps_data     fps;
    float               lin_speed;
    float               ang_speed;
    float               limbo_height;
    float               aspect;
    float               auto_yoffset;
    int                 nr_cameras;
    int                 autopilot;
    int                 exit_timeout;
    int                 fullscreen;
    int                 proj_updated;
};

int scene_camera_add(struct scene *s);
void scene_cameras_calc(struct scene *s);
int scene_add_model(struct scene *s, struct model3dtx *txm);
int scene_init(struct scene *scene);
void scene_done(struct scene *scene);
int  scene_load(struct scene *scene, const char *name);
void scene_update(struct scene *scene);
bool scene_camera_follows(struct scene *s, struct character *ch);
void scene_characters_move(struct scene *s);

static inline bool scene_character_is_camera(struct scene *s, struct character *ch)
{
    return s->camera->ch == ch;
}

#endif /* __CLAP_SCENE_H__ */
