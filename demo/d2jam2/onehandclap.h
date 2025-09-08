/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __DEMO_ONEHANDCLAP_H__
#define __DEMO_ONEHANDCLAP_H__

#include "error.h"
#include "ui.h"

typedef struct game_ui game_ui;
cresp_struct_ret(game_ui);

cresp(game_ui) game_ui_init(struct ui *ui);

cerr noisy_mesh(struct scene *scene);

typedef struct room_params {
    float           radius;
    float           height;
    unsigned int    nr_segments;
} room_params;

cresp(entity3d) make_cave(struct scene *scene, const room_params *_params);

#endif /* __DEMO_ONEHANDCLAP_H__ */
