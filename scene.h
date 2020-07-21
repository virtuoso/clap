#ifndef __CLAP_SCENE_H__
#define __CLAP_SCENE_H__

#include <time.h>
#include "common.h"
#include "matrix.h"
#include "model.h"

struct camera {
    GLfloat pos[3];
    GLfloat pitch;  /* left/right */
    GLfloat yaw;    /* sideways */
    GLfloat roll;   /* up/down */
    unsigned int moved;
    unsigned int zoom;
};

struct scene {
    char                *name;
    int                 width;
    int                 height;
    struct model3d      *_model; /* temporary */
    struct list         txmodels;
    struct entity3d     *focus;
    struct shader_prog  *prog;
    struct matrix4f     *proj_mx;
    struct matrix4f     *view_mx;
    struct matrix4f     *inv_view_mx;
    struct camera       camera;
    struct light        light;
    /* FPS calculation -- very important! */
    unsigned long       frames, frames_total, FPS;
    struct timespec     ts;
    float               aspect;
    int                 autopilot;
    int                 exit_timeout;
    int                 fullscreen;
    int                 proj_updated;
};

void scene_camera_calc(struct scene *s);
int scene_add_model(struct scene *s, struct model3dtx *txm);
int scene_init(struct scene *scene);
void scene_done(struct scene *scene);
int  scene_load(struct scene *scene, const char *name);
void scene_update(struct scene *scene);
struct shader_prog *scene_find_prog(struct scene *s, const char *name);

#endif /* __CLAP_SCENE_H__ */