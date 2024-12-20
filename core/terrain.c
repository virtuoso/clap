// SPDX-License-Identifier: Apache-2.0
#include <sys/time.h>
#include <errno.h>
#include "ca2d.h"
#include "model.h"
#include "physics.h"
#include "scene.h"
#include "shader.h"
#include "terrain.h"
#include "xyarray.h"
#include "util.h"

static float get_rand_height(struct terrain *t, int x, int z)
{
    srand48(t->seed ^ (x + z * 43210));
    return drand48() * 2 - 1;
}

static float get_mapped_rand_height(struct terrain *t, int x, int z)
{
    /* Torus */
    if (x < 0)
        x = t->nr_vert - 1;
    else if (x >= t->nr_vert)
        x = 0;
    if (z < 0)
        z = t->nr_vert - 1;
    else if (z >= t->nr_vert)
        z = 0;
    return t->map0[x * t->nr_vert + z];
}

static float get_avg_height(struct terrain *t, int x, int z)
{
    float corners, sides, self;

    corners  = get_mapped_rand_height(t, x - 1, z - 1);
    corners += get_mapped_rand_height(t, x + 1, z - 1);
    corners += get_mapped_rand_height(t, x - 1, z + 1);
    corners += get_mapped_rand_height(t, x + 1, z + 1);
    corners /= 16.f;

    sides  = get_mapped_rand_height(t, x - 1, z);
    sides += get_mapped_rand_height(t, x + 1, z);
    sides += get_mapped_rand_height(t, x, z - 1);
    sides += get_mapped_rand_height(t, x, z + 1);
    sides /= 8.f;

    self = get_mapped_rand_height(t, x, z) / 4.f;

    return corners + sides + self;
}

static float get_interp_height(struct terrain *t, float x, float z)
{
    int intx = floor(x);
    int intz = floor(z);
    float fracx = x - intx;
    float fracz = z - intz;
    float v1    = get_avg_height(t, intx, intz);
    float v2    = get_avg_height(t, intx + 1, intz);
    float v3    = get_avg_height(t, intx, intz + 1);
    float v4    = get_avg_height(t, intx + 1, intz + 1);
    float i1    = cos_interp(v1, v2, fracx);
    float i2    = cos_interp(v3, v4, fracx);
    //dbg("#### %f,%f => %f => %f; %f,%f => %f => %f\n", v1, v2, fracx, i1, v3, v4, fracx, i2);

    return cos_interp(i1, i2, fracz);
}

#define OCTAVES 4
#define ROUGHNESS 0.5f
#define AMPLITUDE 8

static float get_height(struct terrain *t, int x, int z, float _amp, int _oct)
{
    float total = 0;
    float d = pow(2, _oct - 1);
    int i;

    for (i = 0; i < _oct; i++) {
        float freq = pow(2, i) / d;
        float amp  = pow(ROUGHNESS, i) * _amp;

        total += get_interp_height(t, x * freq, z * freq) * amp;
    }

    return t->y + total;
}

static void calc_normal(struct terrain *t, vec3 n, int x, int z)
{
    /* Torus */
    int left = x == 0 ? t->nr_vert -1 : x - 1;
    int right = x == t->nr_vert-1 ? 0 : x + 1;
    int up = z == 0 ? t->nr_vert -1 : z - 1;
    int down = z == t->nr_vert-1 ? 0 : z + 1;
    float hl = x == 0 ? 0 : t->map[left*t->nr_vert + z];
    float hr = x == t->nr_vert - 1 ? 0 : t->map[right*t->nr_vert + z];
    float hd = z == 0 ? 0 : t->map[x*t->nr_vert + up];
    float hu = z == t->nr_vert - 1 ? 0 : t->map[x*t->nr_vert + down];

    n[0] = hl - hr;
    n[1] = 2.f;
    n[2] = hd - hu;

    vec3_norm(n, n);
}

struct bsp_part {
    int x, y, w, h;
    float amp;
    int oct;
    struct bsp_part *a, *b, *root;
};

typedef void (*bsp_cb)(struct bsp_part *node, int level, void *data);

static int bsp_area(struct bsp_part *node)
{
    return node->w * node->h;
}

#define BSP_MIN_WIDTH 1
static bool bsp_needs_split(struct bsp_part *node, struct bsp_part *parent, int level)
{
    if (node->w == BSP_MIN_WIDTH * 2 || node->h == BSP_MIN_WIDTH * 2)
        return false;
    if (level > 16)
        return false;
    if (node->w / node->h > 4 || node->h / node->w > 4)
        return true;
    if (bsp_area(node) > bsp_area(node->root) / 4)
        return true;
    if (level < 3)
        return true;
    return false;
}

static void bsp_part_one(struct bsp_part *root, int level, bsp_cb cb, void *data)
{
    bool vertical = !!(level & 1);
    double frac = drand48();
    struct bsp_part *a, *b;

    frac = clampd(frac, 0.2, 0.8);

    if (root->w / root->h > 4)
        vertical = true;
    else if (root->h / root->w > 4)
        vertical = false;

    CHECK(a = calloc(1, sizeof(*a)));
    CHECK(b = calloc(1, sizeof(*b)));
    a->x = b->x = root->x;
    a->y = b->y = root->y;
    a->w = b->w = root->w;
    a->h = b->h = root->h;
    for (a->root = root; a->root->root != a->root; a->root = a->root->root)
        ;
    for (b->root = root; b->root->root != b->root; b->root = b->root->root)
        ;

    if (vertical) {
        a->w = min(max(frac * a->w, BSP_MIN_WIDTH), b->w - BSP_MIN_WIDTH);
        b->x += a->w;
        b->w -= a->w;
        err_on(a->w + b->w != root->w, "widths don't match %d+%d!=%d\n",
               a->w, b->w, root->w);
    } else {
        a->h = min(max(frac * a->h, BSP_MIN_WIDTH), b->h - BSP_MIN_WIDTH);
        b->y += a->h;
        b->h -= a->h;
        err_on(a->h + b->h != root->h, "heights don't match %d+%d!=%d\n",
               a->h, b->h, root->h);
    }

    root->a = a;
    root->b = b;
    if (bsp_needs_split(root->a, root, level))
        bsp_part_one(root->a, level + 1, cb, data);
    else
        cb(root->a, level, data);

    if (bsp_needs_split(root->b, root, level))
        bsp_part_one(root->b, level + 1, cb, data);
    else
        cb(root->b, level, data);
}

static struct bsp_part *
bsp_process(unsigned long seed, int depth, int x, int y, int w, int h, bsp_cb cb, void *data)
{
    struct bsp_part *root;

    srand48(seed);
    CHECK(root = calloc(1, sizeof(*root)));
    root->x = x;
    root->y = y;
    root->w = w;
    root->h = h;
    root->root = root;

    bsp_part_one(root, 0, cb, data);
    return root;
}

static void bsp_cleanup(struct bsp_part *root)
{
    if (root->a && root->b) {
        bsp_cleanup(root->a);
        bsp_cleanup(root->b);
    }

    free(root);
}

static bool bsp_within_rect(struct bsp_part *bp, int x, int y)
{
    return x >= bp->x && x < bp->x + bp->w && y >= bp->y && y < bp->y + bp->h;
}

static void bsp_larger_smaller(struct bsp_part **a, struct bsp_part **b)
{
    if (bsp_area(*a) < bsp_area(*b)) {
        struct bsp_part *x = *b;
        *b = *a;
        *a = x;
    }
}

static bool bsp_within_ellipse(struct bsp_part *bp, int x, int y)
{
    float xax = bp->w / 2;
    float yax = bp->h / 2;
    float dx = x;
    float dy = y;

    /*
     * ellipse: x^2/a^2 + y^2/b^2 = 1
     * Where axis || x: 2a, axis || y: 2b
     */
    if (!bsp_within_rect(bp, x, y))
        return false;
    
    dx -= bp->x + bp->w/2;
    dy -= bp->y + bp->h/2;
    if (powf(dx, 2) / powf(xax, 2) + powf(dy, 2) / powf(yax, 2) <= 1)
        return true;
    return false;
}

static bool bsp_within(struct bsp_part *bp, int x, int y)
{
    if (bp->a && bp->a->a)
       return bsp_within_rect(bp, x, y);
    return bsp_within_ellipse(bp, x, y);
}
static struct bsp_part *bsp_find(struct bsp_part *root, int x, int y)
{
    struct bsp_part *it = root;
    struct bsp_part *a, *b;

    while (it->a && it->b) {
        a = it->a;
        b = it->b;
        bsp_larger_smaller(&a, &b);
        it = bsp_within(a, x, y) ? a : b;
    }

    if (it->a || it->b)
        err("BSP node (%d,%d,%d,%d) has children\n", it->x, it->y, it->w, it->h);
    return it;
}

static float bsp_xfrac(struct bsp_part *node, int x)
{
    return ((float)(x - node->x - node->w/2)) / ((float)node->w / 2);
}

static float bsp_yfrac(struct bsp_part *node, int y)
{
    return ((float)(y - node->y - node->h/2)) / ((float)node->h / 2);
}

static struct bsp_part *bsp_xneigh(struct bsp_part *node, int x, int y)
{
    int dir = bsp_xfrac(node, x) >= 0 ? 1 : -1;

    if (dir > 0) {
        if (x >= node->root->x + node->root->w)
            return node;
        return bsp_find(node->root, node->x + node->w, y);
    }

    if (x <= node->root->x)
        return node;
    return bsp_find(node->root, node->x - 1, y);
}

static struct bsp_part *bsp_yneigh(struct bsp_part *node, int x, int y)
{
    int dir = bsp_yfrac(node, y) >= 0 ? 1 : -1;

    if (dir > 0) {
        if (y >= node->root->y + node->root->h)
            return node;
        return bsp_find(node->root, x, node->y + node->h);
    }

    if (y <= node->root->y)
        return node;
    return bsp_find(node->root, x, node->y - 1);
}

static void terrain_bsp_cb(struct bsp_part *node, int level, void *data)
{
    node->amp = min(drand48() * AMPLITUDE, (16 - level) * 3.f);
    node->oct = (rand() & 3) + 3;
    dbg("### BSP [%d,%d,%d,%d] level %d area %d: %f, %d\n", node->x, node->y, node->x + node->w, node->y + node->h,
        level, node->w * node->h, node->amp, node->oct);
}

void terrain_normal(struct terrain *t, float x, float z, vec3 n)
{
    float square = (float)t->side / (t->nr_vert - 1);
    float tx     = x - t->x;
    float tz     = z - t->z;
    int   gridx  = floorf(tx / square);
    int   gridz  = floorf(tz / square);
    calc_normal(t, n, gridx, gridz);
}

float terrain_height(struct terrain *t, float x, float z)
{
    float square = (float)t->side / (t->nr_vert - 1);
    float tx = x - t->x;
    float tz = z - t->z;
    int gridx = floorf(tx / square);
    int gridz = floorf(tz / square);
    float xoff = (tx - square * gridx) / square;
    float zoff = (tz - square * gridz) / square;
    vec3 p1, p2, p3;
    vec2 pos = { xoff, zoff };
    float height;

    if (!t->map)
        return 0;
    if (x < t->x || x > t->x + t->side || z < t->z || z > t->z + t->side)
        return 0;

    if (xoff <= 1 - zoff) {
        p1[0] = 0;
        p1[1] = t->map[gridx * t->nr_vert + gridz];
        p1[2] = 0;
        p2[0] = 1;
        p2[1] = t->map[(gridx + 1) * t->nr_vert + gridz];
        p2[2] = 0;
        p3[0] = 0;
        p3[1] = t->map[gridx * t->nr_vert + gridz + 1];
        p3[2] = 1;
        height = barrycentric(p1, p2, p3, pos);
    } else {
        p1[0] = 1;
        p1[1] = t->map[(gridx + 1) * t->nr_vert + gridz];
        p1[2] = 0;
        p2[0] = 1;
        p2[1] = t->map[(gridx + 1) * t->nr_vert + gridz + 1];
        p2[2] = 1;
        p3[0] = 0;
        p3[1] = t->map[gridx * t->nr_vert + gridz + 1];
        p3[2] = 1;
        height = barrycentric(p1, p2, p3, pos);
    }

    return height;
}

static void terrain_drop(struct ref *ref)
{
    struct terrain *terrain = container_of(ref, struct terrain, ref);

    // ref_put_last(terrain->entity);
    free(terrain->map);
}

DECLARE_REFCLASS(terrain);

const struct cell_automaton ca_test = {
    .name = "test",
    .born = 3 << 2,
    .surv = 3 << 7,
    .nr_states = 4,
    .decay = true,
    .neigh = ca2d_neigh_m1,
};

const struct cell_automaton ca_instors[] = {
    {
        .name = "cool tree",
        .born = 0x1e,
        .surv = 0xff,
        .nr_states = 20,
        .neigh = ca2d_neigh_mv,
    },
    {
        .name = "ash pinus",
        .born = 0xffffff,
        .surv = 0xffffff,
        .nr_states = 21,
        .neigh = ca2d_neigh_mv,
    },
};

#define MAZE_FAC 8
struct terrain *terrain_init_square_landscape(struct scene *s, float x, float y, float z, float side, unsigned int nr_v)
{
    struct terrain *t;
    struct model3d *model;
    struct model3dtx *txm;
    struct shader_prog *prog = shader_prog_find(&s->shaders, "terrain"); /* XXX */
    unsigned long total = nr_v * nr_v, it, bottom;
    size_t vxsz, txsz, idxsz;
    float *vx, *norm, *tx;
    unsigned short *idx;
    struct bsp_part *bsp_root;
    struct timespec ts;
    unsigned char *maze;
    int i, j, mside = nr_v / MAZE_FAC;

    maze = ca2d_generate(&ca_test, mside, 4);

    CHECK(t = ref_new(terrain));
    clock_gettime(CLOCK_REALTIME, &ts);
    t->seed  = ts.tv_nsec ^ ts.tv_sec;

    bsp_root = bsp_process(t->seed, 3, 0, 0, nr_v, nr_v, terrain_bsp_cb, NULL);

    t->nr_vert = nr_v;
    t->side    = side;
    t->x       = x;
    t->y       = y;
    t->z       = z;
    CHECK(t->map0 = calloc(nr_v * nr_v, sizeof(float)));
    for (i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++)
            t->map0[i * nr_v + j] = get_rand_height(t, i, j);
    t->map  = calloc(nr_v * nr_v, sizeof(float));
    for (i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++) {
            struct bsp_part *bp = bsp_find(bsp_root, i, j);
            struct bsp_part *bpx = bsp_xneigh(bsp_root, i, j);
            struct bsp_part *bpy = bsp_yneigh(bsp_root, i, j);
            float xfrac = bsp_xfrac(bp, i);
            float yfrac = bsp_yfrac(bp, j);
            float xamp  = cos_interp(bp->amp, bpx->amp, fabsf(xfrac));
            float yamp  = cos_interp(bp->amp, bpy->amp, fabsf(yfrac));
            xfrac = fmodf(i, MAZE_FAC) / MAZE_FAC;
            yfrac = fmodf(j, MAZE_FAC) / MAZE_FAC;
            int xpos = i / MAZE_FAC, ypos = j / MAZE_FAC;
            unsigned char cn = xyarray_get(maze, mside, xpos, ypos);
            unsigned char xn = xyarray_get(maze, mside, xfrac >= 0.5 ? xpos + 1 : xpos - 1, ypos);
            unsigned char yn = xyarray_get(maze, mside, xpos, yfrac >= 0.5 ? ypos + 1 : ypos - 1);
            float xavg  = cn > xn ? cn : cos_interp(cn, xn, 2 * xfrac - 1);
            float yavg  = cn > yn ? cn : cos_interp(cn, yn, 2 * yfrac - 1);
            float avg   = cos_interp(xavg, yavg, fabsf(xfrac - yfrac));
            t->map[i * nr_v + j] = get_height(t, i, j, powf(1.5, avg), OCTAVES) + avg;
        }
    free(t->map0);
    t->map0 = NULL;
    bsp_cleanup(bsp_root);

    /* place instantiators */
    for (i = 0; i < array_size(ca_instors); i++) {
        ca2d_step(&ca_instors[i], maze, mside);
        // dbg("trees (%s):\n", ca_instors[i].name);
        // xyarray_print(maze, mside, mside);
    }

    vxsz  = total * sizeof(*vx) * 3;
    txsz  = total * sizeof(*tx) * 2;

    idxsz = 6 * (nr_v - 1) * (nr_v - 1) * sizeof(*idx);

    CHECK(vx    = malloc(vxsz));
    CHECK(norm  = malloc(vxsz));
    CHECK(tx    = malloc(txsz));
    CHECK(idx   = malloc(idxsz));
    if (!vx || !norm || !tx || !idx)
        return NULL;

    for (it = 0, i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++) {
            vec3 normal;

            vx[it * 3 + 0] = x + (float)j / ((float)nr_v - 1) * side;
            vx[it * 3 + 1] = y + t->map[j * nr_v + i];
            vx[it * 3 + 2] = z + (float)i / ((float)nr_v - 1) * side;
            calc_normal(t, normal, j, i);
            norm[it * 3 + 0] = normal[0];
            norm[it * 3 + 1] = normal[1];
            norm[it * 3 + 2] = normal[2];
            tx[it * 2 + 0] = (float)j*32 / ((float)nr_v - 1);
            tx[it * 2 + 1] = (float)i*32 / ((float)nr_v - 1);
            it++;
        }

    bottom = it;

    for (it = 0, i = 0; i < nr_v - 1; i++)
        for (j = 0; j < nr_v - 1; j++) {
            int top_left = i * nr_v + j;
            int top_right = top_left + 1;
            int bottom_left = (i + 1) * nr_v + j;
            int bottom_right = bottom_left + 1;
            idx[it++] = top_left;
            idx[it++] = bottom_left;
            idx[it++] = top_right;
            idx[it++] = top_right;
            idx[it++] = bottom_left;
            idx[it++] = bottom_right;
        }
    
    model = model3d_new_from_vectors("terrain", prog, vx, vxsz, idx, idxsz,
                                     tx, txsz, norm, vxsz);
    free(tx);
    free(norm);

    txm = model3dtx_new(ref_pass(model), "terrain.png");
    scene_add_model(s, txm);
    t->entity = entity3d_new(txm);
    t->entity->txmodel->model->collision_vx = vx;
    t->entity->txmodel->model->collision_vxsz = vxsz;
    t->entity->txmodel->model->collision_idx = idx;
    t->entity->txmodel->model->collision_idxsz = idxsz;
    t->entity->visible = 1;
    t->entity->update  = NULL;
    t->entity->scale = 1;
    t->entity->skip_culling = true;
    entity3d_reset(t->entity);
    model3dtx_add_entity(txm, t->entity);
    entity3d_add_physics(t->entity, 0, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
    ref_put(prog); /* matches shader_prog_find() above */

    for (i = 0; i < mside; i++)
        for (j = 0; j < mside; j++) {
            int ca;

            for (ca = 0; ca < array_size(ca_instors); ca++)
                if (xyarray_get(maze, mside, i, j) == ca_instors[ca].nr_states) {
                    struct instantiator *instor;

                    CHECK(instor = calloc(1, sizeof(*instor)));
                    instor->name = ca_instors[ca].name;
                    instor->dx = x + (float)(i + 0.5) * MAZE_FAC * side / (nr_v - 1);
                    instor->dz = z + (float)(j + 0.5) * MAZE_FAC * side / (nr_v - 1);
                    instor->dy = terrain_height(t, instor->dx, instor->dz);
                    list_append(&s->instor, &instor->entry);
                }
        }


    return t;
}

void terrain_done(struct terrain *t)
{
    ref_put_last(t);
}
