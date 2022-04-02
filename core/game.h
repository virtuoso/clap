/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GAME_H__
#define __CLAP_GAME_H__

#include "scene.h"
#include "util.h"

struct free_tree {
    struct entity3d *entity;
    struct list entry;
};

struct game_options {
    float max_apple_age_ms;
    float gathering_distance_squared;
    float poisson_rate_parameter;
    float min_spawn_time_ms;

    float initial_health;
    float max_health;
    float health_loss_per_s;
    float raw_apple_value;
};

struct game_state {
    struct scene *scene;
    darray(struct game_item, items);
    
    struct timespec last_update_time;
    float next_spawn_time;

    float health;
    bool apple_is_carried;
    
    struct list free_trees;
    int number_of_free_trees;
    struct model3dtx *apple_txmodel;
    struct game_options options;
};

enum game_item_kind {
    GAME_ITEM_UNDEFINED = 0,
    GAME_ITEM_APPLE,
};

struct game_item {
    enum game_item_kind kind;
    struct entity3d *entity;
    float distance_to_character;
    float age;
    struct free_tree *apple_parent;
    struct list entry;
};

void game_init(struct scene *scene);
void game_update(struct game_state *g, struct timespec ts);
int handle_game_input(struct message *m, void *data);

#endif
