// SPDX-License-Identifier: Apache-2.0
#include "game.h"
#include "model.h"

extern struct game_state game_state;

struct game_options game_options_init() {
    struct game_options options;
    options.max_apple_age_ms = 20000.0;
    options.gathering_distance_squared = 2.0 * 2.0;
    options.poisson_rate_parameter = 0.01;
    options.min_spawn_time_ms = 7000.0;

    options.initial_health = 100.0;
    options.max_health = 120.0;
    options.health_loss_per_s = 1.0;
    options.raw_apple_value = 10.0;
    return options;
}

void add_health(struct game_state *g, float health) {
    g->health = fmax(0.0, fmin(g->options.max_health, g->health + health));
}

void eat_apple(struct game_state *g) {
    if (g->apple_is_carried) {
        g->apple_is_carried = false;
        add_health(g, g->options.raw_apple_value);
    } else {
        printf("No apple to eat.\n");
    }
}

int handle_game_input(struct message *m, void *data) {
    if (m->input.pad_y)
        eat_apple(&game_state);

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

void place_apple(struct entity3d *tree, struct entity3d *apple) {
    float angle = (float)drand48() * 2.0 * M_PI;
    apple->dx = tree->dx + 1.0 * cos(angle);
    apple->dy = tree->dy;
    apple->dz = tree->dz + 1.0 * sin(angle);
}

void apple_init(struct game_state *g, struct game_item *apple)
{
    apple->kind = GAME_ITEM_APPLE;
    struct entity3d *e =entity3d_new(g->apple_txmodel);
    model3dtx_add_entity(g->apple_txmodel, e);
    e->scale = 1;
    e->visible = 1;
    apple->entity = e;
}

void spawn_new_apple(struct game_state *g) {
    int number_of_free_trees = g->number_of_free_trees;
    if (number_of_free_trees == 0)
        return;
    int r = lrand48() % number_of_free_trees;
    struct free_tree *tree = get_free_tree(&g->free_trees, r);
    list_del(&tree->entry);
    g->number_of_free_trees--;
    
    struct game_item *apple;
    CHECK(apple = darray_add(&g->items.da));
    apple_init(g, apple);
    apple->apple_parent = tree;
    place_apple(tree->entity, apple->entity);
}

float calculate_squared_distance(struct entity3d *a, struct entity3d *b) {
    float dx = a->dx - b->dx;
    float dy = a->dy - b->dy;
    float dz = a->dz - b->dz;
    return dx * dx + dy * dy + dz * dz;
}

void kill_apple(struct game_state *g, struct game_item *item) {
    list_append(&g->free_trees, &item->apple_parent->entry);
    g->number_of_free_trees++;
    ref_put(item->entity);
}

void game_update(struct game_state *g, struct timespec ts) {
    // calculate time delta
    struct timespec delta_t;
    timespec_diff(&g->last_update_time, &ts, &delta_t);
    memcpy(&g->last_update_time, &ts, sizeof(ts));
    float delta_t_ms = delta_t.tv_nsec / 1000000;

    // update health
    float health_loss = g->options.health_loss_per_s * delta_t_ms / 1000.0;
    add_health(g, -health_loss);
    if (g->health == 0.0) {
        printf("DIE.\n");
    }

    //printf("health: %f\n", g->health);

    int idx = 0;
    struct entity3d *gatherer = g->scene->control->entity;
    struct game_item *item;
    while (true) {
        // loop throw apples
        if (idx >= g->items.da.nr_el)
            break;
        item = &g->items.x[idx];

        float squared_distance = calculate_squared_distance(item->entity, gatherer);
        bool gathered = false;
        if (!g->apple_is_carried && squared_distance < g->options.gathering_distance_squared) {
            // gather the apple
            printf("GATHERING APPLE\n");
            gathered = true;
            g->apple_is_carried = true;
        }
        item->age += delta_t_ms;
        if (gathered || (item->age > g->options.max_apple_age_ms)) {
            // kill apple and delete it from the item list
            kill_apple(g, item);
            
            // swap with last.
            g->items.da.nr_el--;
            int last = g->items.da.nr_el;
            if (idx != last) {
                g->items.x[idx] = g->items.x[last];
                continue;
            }
        }
        idx++;
    }
    
    float updated_next_spawn_time = g->next_spawn_time - delta_t_ms;
    if (updated_next_spawn_time < 0.0) {
        // time to spawn a new apple.
        spawn_new_apple(g);
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

void game_init(struct scene *scene)
{
    game_state.scene = scene;
    game_state.options = game_options_init();
    game_state.apple_is_carried = false;
    game_state.health = game_state.options.initial_health;
    darray_init(&game_state.items);
    list_init(&game_state.free_trees);
    mq_for_each(&scene->mq, find_trees, &game_state);
    struct model3dtx *txmodel;

    // find barrel
    list_for_each_entry(txmodel, &scene->mq.txmodels, entry) {
        if (!strcmp(txmodel->model->name, "apple"))
            game_state.apple_txmodel = txmodel;
    }
}
