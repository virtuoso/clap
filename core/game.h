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
    float apple_maturity_age_ms;
    int   burrow_capacity;
    
    float gathering_distance_squared;
    float burrow_distance_squared;
    
    float poisson_rate_parameter;
    float min_spawn_time_ms;

    float initial_health;
    float max_health;
    float health_loss_per_s;
    float raw_apple_value;
    float mature_apple_value;
};

struct burrow {
    struct entity3d *entity;
    darray(struct game_item, items);
    int number_of_mature_apples;
};

struct game_state {
    struct scene *scene;
    struct ui *ui;
    darray(struct game_item, items);
    
    struct timespec last_update_time;
    struct timespec paused_time;
    float next_spawn_time;

    float health;
    bool apple_is_carried;
    
    struct list free_trees;
    int number_of_free_trees;
    struct model3dtx *apple_txmodel;
    struct game_options options;
    struct burrow burrow;
};

enum game_item_kind {
    GAME_ITEM_UNDEFINED = 0,
    GAME_ITEM_APPLE,
    GAME_ITEM_APPLE_IN_BURROW,
};

struct game_item {
    enum game_item_kind kind;
    struct entity3d *entity;
    float distance_to_character;
    float age;
    struct free_tree *apple_parent;
    bool is_mature;
};

void game_init(struct scene *scene, struct ui *ui);
void game_update(struct game_state *g, struct timespec ts, bool paused);
int handle_game_input(struct message *m, void *data);

/* from ui.c, but doesn't really fit in ui.h */
void health_set(float perc);
void show_apple_in_pocket();
void show_empty_pocket();
void ui_inventory_done(struct ui *ui);
void ui_inventory_init(struct ui *ui, int number_of_apples, float apple_ages[]);

#endif
