#ifndef __CLAP_UI_H__
#define __CLAP_UI_H__

#include "object.h"
#include "model.h"
#include "scene.h"

#define UI_AF_TOP    0x1
#define UI_AF_BOTTOM 0x2
#define UI_AF_LEFT   0x4
#define UI_AF_RIGHT  0x8
#define UI_AF_HCENTER (UI_AF_LEFT | UI_AF_RIGHT)
#define UI_AF_VCENTER (UI_AF_TOP | UI_AF_BOTTOM)
#define UI_AF_CENTER (UI_AF_VCENTER | UI_AF_HCENTER)

struct ui_element {
    struct ref       ref;
    struct entity3d *entity;
    unsigned long    affinity;
    float            x_off, y_off;
    float            width, height;
};

//int ui_element_init(struct scene *s, float x, float y, float w, float h);
struct ui {
    struct model3d     *_model;
    struct model3dtx   *txmodel;
    struct shader_prog *prog;
    int width, height;
};

int ui_init(struct ui *ui, int width, int height);
void ui_update(struct ui *ui);

#endif /* __CLAP_UI_H__ */