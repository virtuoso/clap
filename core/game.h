/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_GAME_H__
#define __CLAP_GAME_H__

#include "scene.h"
#include "util.h"
#include "ui.h"

struct free_tree {
    entity3d *entity;
    struct list entry;
};

enum game_item_kind {
    GAME_ITEM_UNDEFINED = 0,
    GAME_ITEM_APPLE,
    GAME_ITEM_APPLE_IN_BURROW,
    GAME_ITEM_MUSHROOM,
    GAME_ITEM_MAX,
};

struct game_options {
    float max_age_ms[GAME_ITEM_MAX];
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
    entity3d *entity;
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
    int carried[GAME_ITEM_MAX];
    
    struct list free_trees;
    int number_of_free_trees;
    model3dtx *txmodel[GAME_ITEM_MAX];
    struct game_options options;
    struct burrow burrow;
};

struct game_item {
    enum game_item_kind kind;
    entity3d *entity;
    float age;
    float age_limit;
    struct free_tree *apple_parent;
    bool is_mature;
    bool is_deleted;
    void (*interact)(struct game_state *g, struct game_item *item, entity3d *actor);
    void (*kill)(struct game_state *g, struct game_item *item);
    void *priv;
};

struct game_item *game_item_new(struct game_state *g, enum game_item_kind kind,
                                model3dtx *txm);
void game_item_delete(struct game_state *g, struct game_item *item);
int game_item_find_idx(struct game_state *g, struct game_item *item);
void game_item_delete_idx(struct game_state *g, int idx);
void game_item_collect(struct game_state *g, struct game_item *item, entity3d *actor);
struct game_item *game_item_spawn(struct game_state *g, enum game_item_kind kind);

void game_init(struct scene *scene, struct ui *ui);
void game_update(struct game_state *g, struct timespec ts, bool paused);
int handle_game_input(struct message *m, void *data);

/* from ui.c, but doesn't really fit in ui.h */
void health_set(float perc);
void show_apple_in_pocket();
void show_empty_pocket();
void pocket_count_set(struct ui *ui, int kind, int count);
void pocket_total_set(struct ui *ui, int kind, int count);
void ui_inventory_done(struct ui *ui);
void ui_inventory_init(struct ui *ui, int number_of_apples, float apple_ages[],
                       void (*on_click)(struct ui_element *uie, float x, float y));

#endif
