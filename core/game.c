// SPDX-License-Identifier: Apache-2.0
#include "game.h"
#include "model.h"
#include "terrain.h"
#include "ui-debug.h"
#include "ui.h"

extern struct game_state game_state;

struct game_options game_options_init() {
    struct game_options options;
    options.max_apple_age_ms = 20000.0;
    options.apple_maturity_age_ms = 10000.0;
    options.burrow_capacity = 9;
    
    options.gathering_distance_squared = 2.0 * 2.0;
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

void eat_apple(struct game_state *g) {
    if (g->health > g->options.max_health)
        return;
    struct entity3d *gatherer = g->scene->control->entity;
    struct entity3d *burrow = g->burrow.entity;
    float squared_distance_to_burrow = calculate_squared_distance(gatherer, burrow);
    if (squared_distance_to_burrow < g->options.burrow_distance_squared) {
        // Try to eat a mature apple from the burrow.
        struct game_item *item;
        int idx = 0;
        struct burrow *b = &g->burrow;
        while (true) {
            // loop throw apples
            if (idx >= b->items.da.nr_el)
                break;
            item = &b->items.x[idx];
            if (item->is_mature) {
                add_health(g, g->options.mature_apple_value);
                b->number_of_mature_apples--;
                
                // swap with last.
                b->items.da.nr_el--;
                int last = b->items.da.nr_el;
                if (idx != last)
                    b->items.x[idx] = b->items.x[last];
                dbg("Ate a mature apple from the burrow.");
                return;
            }
            
            idx++;
        }
    }    

    // Otherwise, try to eat a raw apple.
    if (g->apple_is_carried) {
        g->apple_is_carried = false;
        add_health(g, g->options.raw_apple_value);
        dbg("Ate a raw apple from hand.");
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

void place_apple(struct scene *s, struct entity3d *tree, struct entity3d *apple) {
    float angle = (float)drand48() * 2.0 * M_PI;
    apple->dx = tree->dx + 1.0 * cos(angle);
    apple->dz = tree->dz + 1.0 * sin(angle);
    apple->dy = terrain_height(s->terrain, apple->dx, apple->dz);
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

void apple_in_burrow_init(struct game_state *g, struct game_item *apple)
{
    apple->kind = GAME_ITEM_APPLE_IN_BURROW;
    apple->entity = NULL;
    apple->age = 0.0;
    apple->apple_parent = NULL;
    apple->is_mature = false;
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
    place_apple(g->scene, tree->entity, apple->entity);
}

void kill_apple(struct game_state *g, struct game_item *item) {
    list_append(&g->free_trees, &item->apple_parent->entry);
    g->number_of_free_trees++;
    ref_put(item->entity);
}

void put_apple_to_burrow(struct game_state *g) {
    if (g->burrow.items.da.nr_el >= g->options.burrow_capacity)
        return;
    g->apple_is_carried = false;
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
        // dbg("DIE.\n");
    }

    health_set(g->health / g->options.max_health);
    ui_debug_printf("apple in hand: %d, health: %f, apples in the burrow: %d (%d mature)\n",
                    g->apple_is_carried ? 1 : 0,
                    g->health,
                    g->burrow.items.da.nr_el,
                    g->burrow.number_of_mature_apples);

    int idx = 0;
    struct entity3d *gatherer = g->scene->control->entity;
    struct entity3d *burrow = g->burrow.entity;
    float squared_distance_to_burrow = calculate_squared_distance(gatherer, burrow);
    if (squared_distance_to_burrow < g->options.burrow_distance_squared && g->apple_is_carried)
        put_apple_to_burrow(g);
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
            dbg("GATHERING APPLE\n");
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

    burrow_update(&g->burrow, delta_t_ms, &g->options);
    
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

struct burrow burrow_init() {
    struct burrow b;
    darray_init(&b.items);
    b.number_of_mature_apples = 0;
    return b;
}

void game_init(struct scene *scene)
{
    game_state.scene = scene;
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
            game_state.apple_txmodel = txmodel;
        if (!strcmp(txmodel->model->name, "fantasy well")) {
            game_state.burrow.entity = list_first_entry(&txmodel->entities, struct entity3d, entry);
        }
    }
}
