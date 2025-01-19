// SPDX-License-Identifier: Apache-2.0
#include "game.h"
#include "model.h"
#include "terrain.h"
#include "ui-debug.h"
#include "ui.h"

extern struct game_state game_state;

struct game_options game_options_init() {
    struct game_options options;
    options.max_age_ms[GAME_ITEM_APPLE] = 20000.0;
    options.apple_maturity_age_ms = 10000.0;
    options.burrow_capacity = 9;
    
    options.gathering_distance_squared = 2.0;
    options.burrow_distance_squared = 3.0 * 3.0;
    
    options.poisson_rate_parameter = 0.01;
    options.min_spawn_time_ms = 7000.0;

    options.initial_health = 100.0;
    options.max_health = 120.0;
    options.health_loss_per_s = 1.0;
    options.raw_apple_value = 10.0;
    options.mature_apple_value = 60.0;
    return options;
}

void add_health(struct game_state *g, float health) {
    g->health = fmax(0.0, fmin(g->options.max_health, g->health + health));
}

float calculate_squared_distance(struct entity3d *a, struct entity3d *b) {
    float dx = a->dx - b->dx;
    float dy = a->dy - b->dy;
    float dz = a->dz - b->dz;
    return dx * dx + dy * dy + dz * dz;
}

void put_apple_into_pocket(struct game_state *g) {
    g->apple_is_carried = true;
    show_apple_in_pocket();
}

void get_apple_out_of_pocket(struct game_state *g) {
    g->apple_is_carried = false;
    show_empty_pocket();
}

bool is_near_burrow(struct game_state *g) {
    struct entity3d *gatherer = g->scene->control;
    struct entity3d *burrow = g->burrow.entity;
    float squared_distance_to_burrow = calculate_squared_distance(gatherer, burrow);
    return squared_distance_to_burrow < g->options.burrow_distance_squared;
}

void eat_apple_from_inventory(struct game_state *g, int apple_index) {
    // Try to eat a mature apple from the burrow.
    struct game_item *item;
    struct burrow *b = &g->burrow;
    item = &b->items.x[apple_index];
    if (item->is_mature) {
        add_health(g, g->options.mature_apple_value);
        b->number_of_mature_apples--;
        item->is_deleted = true;
        dbg("Ate a mature apple from the burrow.\n");
    } else {
        dbg("Can only eat mature apples.\n");
    }
}

void eat_apple(struct game_state *g) {
    if (g->health > g->options.max_health)
        return;

    // Try to eat a raw apple.
    if (g->apple_is_carried) {
        get_apple_out_of_pocket(g);
        add_health(g, g->options.raw_apple_value);
        dbg("Ate a raw apple from hand.\n");
    }
}


void handle_inventory_click(struct ui_element *uie, float x, float y) {
    int apple_index = (long)uie->priv;
    eat_apple_from_inventory(&game_state, apple_index);
}

void show_inventory(struct game_state *g)
{
    int idx;
    struct game_item *item;
    struct burrow *b = &g->burrow;
    float apple_ages[b->items.da.nr_el];

    idx = 0;
    while (true) {
        // loop throw apples
        if (idx >= b->items.da.nr_el)
            break;
        item = &b->items.x[idx];
        if (item->is_mature)
            apple_ages[idx] = 1.0;
        else
            apple_ages[idx] = item->age / g->options.apple_maturity_age_ms;
        
        idx++;
    }

    ui_inventory_init(g->ui, b->items.da.nr_el, apple_ages, handle_inventory_click);
}

int handle_game_input(struct message *m, void *data) {
    if (m->input.pad_y) {
        eat_apple(&game_state);
    } else if (m->input.inv_toggle) {
        if (game_state.ui->inventory)
            ui_inventory_done(game_state.ui);
        else if (is_near_burrow(&game_state))
            show_inventory(&game_state);
    }
    
    return 0;
}

float get_next_spawn_time(struct game_options *options) {
    float u = (float)drand48();
    return options->min_spawn_time_ms + (-logf(u) / options->poisson_rate_parameter);
}

struct free_tree *get_free_tree(struct list *trees, int index) {
    struct free_tree * tree = list_first_entry(trees, struct free_tree, entry);
    for (int i = 0; i < index; i++)
        tree = list_next_entry(tree, entry);
    return tree;
}

void place_apple(struct scene *s, struct entity3d *tree, struct entity3d *apple) {
    float angle = (float)drand48() * 2.0 * M_PI;
    apple->dx = tree->dx + 1.0 * cos(angle);
    apple->dz = tree->dz + 1.0 * sin(angle);
    apple->dy = terrain_height(s->terrain, apple->dx, apple->dz);
}

static const char *game_item_str(struct game_item *item)
{
    static const char *kind_str[] = {
        [GAME_ITEM_APPLE]           = "apple",
        [GAME_ITEM_APPLE_IN_BURROW] = "apple-in-burrow",
        [GAME_ITEM_MUSHROOM]        = "mushroom",
    };

    if (item->kind >= GAME_ITEM_MAX)
        return "<undefined>";

    return kind_str[item->kind];
}

void game_item_init(struct game_item *item, struct game_state *g,
                    enum game_item_kind kind, struct model3dtx *txm)
{
    struct entity3d *e = entity3d_new(txm);

    item->kind = kind;
    model3dtx_add_entity(txm, e);
    e->scale = 1;
    e->visible = 1;
    item->entity = e;
}

struct game_item *game_item_new(struct game_state *g, enum game_item_kind kind,
                                struct model3dtx *txm)
{
    struct game_item *item;

    CHECK(item = darray_add(&g->items.da));
    game_item_init(item, g, kind, txm);

    return item;
}

int game_item_find_idx(struct game_state *g, struct game_item *item)
{
    int i;

    for (i = 0; i < g->items.da.nr_el; i++)
        if (&g->items.x[i] == item)
            return i;

    return -1;
}

void game_item_delete_idx(struct game_state *g, int idx)
{
    struct game_item *item = &g->items.x[idx];
    int last = g->items.da.nr_el - 1;

    if (item->kill)
        item->kill(g, item);

    /* swap with the last, then pop */
    if (idx != last)
        g->items.x[idx] = g->items.x[last];

    darray_resize(&g->items.da, g->items.da.nr_el - 1);
}

void game_item_delete(struct game_state *g, struct game_item *item)
{
    int idx = game_item_find_idx(g, item);

    if (idx < 0)
        return;

    game_item_delete_idx(g, idx);
}

void game_item_collect(struct game_state *g, struct game_item *item, struct entity3d *actor)
{
    dbg("%s collects %s\n", entity_name(actor), game_item_str(item));
    g->carried[item->kind]++;
    put_apple_into_pocket(g);
    pocket_count_set(g->ui, item->kind == GAME_ITEM_APPLE ? 0 : 1, g->carried[item->kind]);

    game_item_delete(g, item);
    item->interact = NULL;
}

void apple_in_burrow_init(struct game_state *g, struct game_item *apple)
{
    apple->kind = GAME_ITEM_APPLE_IN_BURROW;
    apple->entity = NULL;
    apple->age = 0.0;
    apple->apple_parent = NULL;
    apple->is_mature = false;
    apple->is_deleted = false;
}

static void kill_apple(struct game_state *g, struct game_item *item)
{
    list_append(&g->free_trees, &item->apple_parent->entry);
    g->number_of_free_trees++;
    ref_put(item->entity);
}

struct game_item *game_item_spawn(struct game_state *g, enum game_item_kind kind)
{
    int number_of_free_trees = g->number_of_free_trees;
    if (number_of_free_trees == 0)
        return NULL;
    int r = lrand48() % number_of_free_trees;
    struct free_tree *tree = get_free_tree(&g->free_trees, r);
    list_del(&tree->entry);
    g->number_of_free_trees--;
    
    struct game_item *item = game_item_new(g, kind, g->txmodel[kind]);
    item->apple_parent = tree;
    item->age_limit = g->options.max_age_ms[kind];
    item->kill = kill_apple;
    item->interact = game_item_collect;
    place_apple(g->scene, tree->entity, item->entity);

    return item;
}

void put_apple_to_burrow(struct game_state *g) {
    if (g->burrow.items.da.nr_el >= g->options.burrow_capacity)
        return;
    get_apple_out_of_pocket(g);
    struct game_item *apple;
    CHECK(apple = darray_add(&g->burrow.items.da));
    apple_in_burrow_init(g, apple);
}

void burrow_update(struct burrow *b, float delta_t_ms, struct game_options *options) {
    struct game_item *item;
    int idx = 0;
    while (true) {
        // loop throw apples
        if (idx >= b->items.da.nr_el)
            break;
        item = &b->items.x[idx];
        if (item->is_deleted) {
            b->items.da.nr_el--;
            int last = b->items.da.nr_el;
            if (idx != last)
                b->items.x[idx] = b->items.x[last];
            continue;
        }
        if (!item->is_mature) {
            item->age += delta_t_ms;
            if (item->age > options->apple_maturity_age_ms) {
                item->is_mature = true;
                b->number_of_mature_apples++;
            }
        }

        idx++;
    }    
}

void game_update(struct game_state *g, struct timespec ts, bool paused)
{
    float delta_t_ms, health_loss;
    struct timespec delta_t;

    if (!paused) {
        // calculate time delta
        if (timespec_nonzero(&g->paused_time)) {
            memcpy(&g->last_update_time, &g->paused_time, sizeof(g->last_update_time));
            g->paused_time.tv_sec = g->paused_time.tv_nsec = 0;
        }
        timespec_diff(&g->last_update_time, &ts, &delta_t);
        memcpy(&g->last_update_time, &ts, sizeof(ts));
    } else {
        memcpy(&g->paused_time, &ts, sizeof(ts));
        return;
    }
    delta_t_ms = delta_t.tv_nsec / 1000000;

    // update health
    health_loss = g->options.health_loss_per_s * delta_t_ms / 1000.0;
    add_health(g, -health_loss);
    if (g->health == 0.0) {
        // dbg("DIE.\n");
    }

    health_set(g->health / g->options.max_health);
    ui_debug_printf("apple in hand: %d, health: %f, apples in the burrow: %zu (%d mature)\n",
                    g->apple_is_carried ? 1 : 0,
                    g->health,
                    g->burrow.items.da.nr_el,
                    g->burrow.number_of_mature_apples);

    int idx = 0;
    struct entity3d *gatherer = g->scene->control;
    if (is_near_burrow(g) && g->apple_is_carried)
        put_apple_to_burrow(g);
    struct game_item *item;
    while (true) {
        // loop through apples
        if (idx >= g->items.da.nr_el)
            break;
        item = &g->items.x[idx];

        item->age += delta_t_ms;
        if (item->age > item->age_limit) {
            game_item_delete_idx(g, idx);
            continue;
        }

        float squared_distance = calculate_squared_distance(item->entity, gatherer);
        if (squared_distance < g->options.gathering_distance_squared) {
            if (item->interact)
                item->interact(g, item, gatherer);
        }
        idx++;
    }

    burrow_update(&g->burrow, delta_t_ms, &g->options);
    
    float updated_next_spawn_time = g->next_spawn_time - delta_t_ms;
    if (updated_next_spawn_time < 0.0) {
        int i, nr_items = lrand48() % 30;

        // time to spawn a new item
        for (i = 0; i < nr_items; i++)
            game_item_spawn(g, GAME_ITEM_APPLE);
        g->next_spawn_time = get_next_spawn_time(&g->options);
    } else {
        g->next_spawn_time = updated_next_spawn_time;
    }
}

void find_trees(struct entity3d *e, void *data)
{
    struct game_state *g = data;
    const char* name = entity_name(e);
    if (!strcmp(name, "tree") || !strcmp(name, "cool tree") || !strcmp(name, "spruce tree")) {
        // insert tree into the list of free trees.
        struct free_tree *new_tree = malloc(sizeof(struct free_tree));
        new_tree->entity = e;
        list_append(&g->free_trees, &new_tree->entry);
        g->number_of_free_trees++;
    }
}

struct burrow burrow_init() {
    struct burrow b;
    darray_init(&b.items);
    b.number_of_mature_apples = 0;
    return b;
}

void game_init(struct scene *scene, struct ui *ui)
{
    game_state.scene = scene;
    game_state.ui = ui;
    game_state.options = game_options_init();
    game_state.apple_is_carried = false;
    game_state.health = game_state.options.initial_health;
    game_state.burrow = burrow_init();
    darray_init(&game_state.items);
    list_init(&game_state.free_trees);
    mq_for_each(&scene->mq, find_trees, &game_state);
    struct model3dtx *txmodel;

    // find apple
    list_for_each_entry(txmodel, &scene->mq.txmodels, entry) {
        if (!strcmp(txmodel->model->name, "apple"))
            game_state.txmodel[GAME_ITEM_APPLE] = txmodel;
        if (!strcmp(txmodel->model->name, "mushroom"))
            game_state.txmodel[GAME_ITEM_MUSHROOM] = txmodel;
        if (!strcmp(txmodel->model->name, "fantasy well")) {
            game_state.burrow.entity = list_first_entry(&txmodel->entities, struct entity3d, entry);
        }
    }
}
