#include <errno.h>
#include "model.h"
#include "physics.h"
#include "scene.h"
#include "shader.h"

#define SIZE 1000
//#define FRAC 4.f
static long seed;
static float *map, *map0;
static unsigned int side;

static float get_rand_height(int x, int z)
{
    /*if (x < - SIZE/2 || x > SIZE/2)
        return 0;
    if (z < - SIZE/2 || z > SIZE/2)
        return 0;*/

    x += SIZE / 2;
    z += SIZE / 2;
    //srand48((x + z * 49152) + seed);
    srand((x * 42 + z * 2345) + seed);
    return sin(to_radians((float)rand()));
    //return drand48() * 2 - 1; //sin(to_radians((float)rand()));
}

static float get_mapped_rand_height(int x, int z)
{
    /* Torus */
    if (x < 0)
        x = side - 1;
    else if (x >= side)
        x = 0;
    if (z < 0)
        z = side - 1;
    else if (z >= side)
        z = 0;
    return map0[x * side + z];
}

static float get_avg_height(int x, int z)
{
    float corners, sides, self;

    corners  = get_mapped_rand_height(x - 1, z - 1);
    corners += get_mapped_rand_height(x + 1, z - 1);
    corners += get_mapped_rand_height(x - 1, z + 1);
    corners += get_mapped_rand_height(x + 1, z + 1);
    corners /= 16.f;

    sides  = get_mapped_rand_height(x - 1, z);
    sides += get_mapped_rand_height(x + 1, z);
    sides += get_mapped_rand_height(x, z - 1);
    sides += get_mapped_rand_height(x, z + 1);
    sides /= 8.f;

    self = get_mapped_rand_height(x, z) / 4.f;

    return corners + sides + self;
}

static float get_interp_height(float x, float z)
{
    int intx = floor(x);
    int intz = floor(z);
    float fracx = x - intx;
    float fracz = z - intz;
    float v1    = get_avg_height(intx, intz);
    float v2    = get_avg_height(intx + 1, intz);
    float v3    = get_avg_height(intx, intz + 1);
    float v4    = get_avg_height(intx + 1, intz + 1);
    float i1    = cos_interp(v1, v2, fracx);
    float i2    = cos_interp(v3, v4, fracx);
    //dbg("#### %f,%f => %f => %f; %f,%f => %f => %f\n", v1, v2, fracx, i1, v3, v4, fracx, i2);

    return cos_interp(i1, i2, fracz);
}

#define OCTAVES 3
#define ROUGHNESS 0.2f
#define AMPLITUDE 20

static float get_height(int x, int z, float _amp, int _oct)
{
    float total = 0;
    float d = pow(2, _oct - 1);
    int i;

    for (i = 0; i < _oct; i++) {
        float freq = pow(2, i) / d;
        float amp  = pow(ROUGHNESS, i) * _amp;

        total += get_interp_height(x * freq, z * freq) * amp;
    }

    return total;
}

static void calc_normal(vec3 n, int x, int z)
{
    /* Torus */
    int left = x == 0 ? side -1 : x - 1;
    int right = x == side-1 ? 0 : x + 1;
    int up = z == 0 ? side -1 : z - 1;
    int down = z == side-1 ? 0 : z + 1;
    float hl = map[left*side + z];//get_height(x - 1, z);
    float hr = map[right*side + z];//get_height(x + 1, z);
    float hd = map[x*side + up];//get_height(x, z - 1);
    float hu = map[x*side + down];//get_height(x, z + 1);

    n[0] = hl - hr;
    n[1] = 2.f;
    n[2] = hd - hu;

    vec3_norm(n, n);
    //if (vec3_len(n) != 1.000000)
    //    dbg("### %f,%f,%f  -> %f\n", n[0], n[1], n[2], vec3_len(n));
}

struct bsp_part {
    int x, y, w, h;
    float amp;
    int oct;
    struct bsp_part *a, *b;
};

static void bsp_part_one(struct bsp_part *root, int level)
{
    bool vertical = !!(rand() & 1);
    double frac = drand48();
    struct bsp_part *a, *b;

    if (!level) {
        root->amp = max(drand48() * 70, 3);
        root->oct = (rand() & 3) + 2;
        dbg("### BSP [%d,%d,%d,%d]: %f, %d\n",
            root->x, root->y, root->x + root->w, root->y + root->h, root->amp, root->oct);
        return;
    }

    CHECK(a = calloc(1, sizeof(*a)));
    CHECK(b = calloc(1, sizeof(*b)));
    a->x = b->x = root->x;
    a->y = b->y = root->y;
    a->w = b->w = root->w;
    a->h = b->h = root->h;

    if (vertical) {
        a->w = frac * a->w;
        b->x += a->w;
        b->w -= a->w;
        err_on(a->w + b->w != root->w, "widths don't match %d+%d!=%d\n",
               a->w, b->w, root->w);
    } else {
        a->h = frac * a->h;
        b->y += a->h;
        b->h -= a->h;
        err_on(a->h + b->h != root->h, "heights don't match %d+%d!=%d\n",
               a->h, b->h, root->h);
    }

    root->a = a;
    root->b = b;
    bsp_part_one(root->a, level - 1);
    bsp_part_one(root->b, level - 1);
}

static struct bsp_part *bsp_process(int depth)
{
    struct bsp_part *root;

    srand48(0xbebeabbad000d5);
    CHECK(root = calloc(1, sizeof(*root)));
    root->x = -SIZE / 2;
    root->y = -SIZE / 2;
    root->w = SIZE;
    root->h = SIZE;

    bsp_part_one(root, depth);
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

static bool bsp_within(struct bsp_part *bp, int x, int y)
{
    return x >= bp->x && x < bp->x + bp->w && y >= bp->y && y < bp->y + bp->h;
}

static struct bsp_part *bsp_find(struct bsp_part *root, int x, int y)
{
    struct bsp_part *it = root;

    while (it->a && it->b)
        it = bsp_within(it->a, x, y) ? it->a : it->b;

    return it;
}

int terrain_init(struct scene *s, float vpos, unsigned int nr_v)
{
    struct model3d *model;
    struct model3dtx *txm;
    struct entity3d *e;
    struct shader_prog *prog = shader_prog_find(s->prog, "model"); /* XXX */
    unsigned long total = nr_v * nr_v, it;
    size_t vxsz, txsz, idxsz;
    float *vx, *norm, *tx;
    unsigned short *idx;
    struct bsp_part *bsp_root = bsp_process(4);
    struct timespec ts;
    int i, j;

    dbg("TERRAIN\n");
    clock_gettime(CLOCK_REALTIME, &ts);
    seed  = ts.tv_nsec;

    side = nr_v;
    map0 = calloc(nr_v * nr_v, sizeof(float));
    for (i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++)
            map0[i * nr_v + j] = get_rand_height(i, j);
    map  = calloc(nr_v * nr_v, sizeof(float));
    for (i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++) {
            struct bsp_part *bp = bsp_find(bsp_root, i, j);

            map[i * nr_v + j] = get_height(i, j, bp->amp, bp->oct);
        }
    free(map0);
    bsp_cleanup(bsp_root);

    vxsz  = total * sizeof(*vx) * 3;
    txsz  = total * sizeof(*tx) * 2;
    idxsz = 6 * (nr_v - 1) * (nr_v - 1) * sizeof(*idx);
    vx    = malloc(vxsz);
    norm  = malloc(vxsz);
    tx    = malloc(txsz);
    idx   = malloc(idxsz);
    if (!vx || !norm || !tx || !idx)
        return -ENOMEM;

    for (it = 0, i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++) {
            //vec3 v0, v1, v2, a, b;
            vec3 normal;

            vx[it * 3 + 0] = (float)j / ((float)nr_v - 1) * SIZE - SIZE/2;
            vx[it * 3 + 1] = vpos + map[i * nr_v + j];
            //vx[it * 3 + 1] = vpos + get_height(j, i);
            //vx[it * 3 + 1] = vpos + get_avg_height(j, i) * AMPLITUDE;
            vx[it * 3 + 2] = (float)i / ((float)nr_v - 1) * SIZE - SIZE/2;
            calc_normal(normal, j, i);
            norm[it * 3 + 0] = normal[0];
            norm[it * 3 + 1] = normal[1];
            norm[it * 3 + 2] = normal[2];
            tx[it * 2 + 0] = (float)j*32 / ((float)nr_v - 1);
            tx[it * 2 + 1] = (float)i*32 / ((float)nr_v - 1);
            it++;
        }
    //dbg("it %lu nr_v %lu total %lu vxsz %lu txsz %lu idxsz %lu\n", it, nr_v, total,
    //    vxsz, txsz, idxsz);

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
    free(map);
    //dbg("it %lu\n", it);
    model = model3d_new_from_vectors("terrain", prog, vx, vxsz, idx, idxsz,
                                     tx, txsz, norm, vxsz);
    free(vx);
    free(tx);
    free(norm);
    free(idx);

    //dbg("##### rand height(5,14): %f\n", get_avg_height(5, 14));
    //dbg("##### rand height(5,14): %f\n", get_avg_height(5, 14));
    //dbg("##### rand height(6,15): %f\n", get_avg_height(6, 15));
    txm = model3dtx_new(model, "grass20.png");
    ref_put(&model->ref);
    scene_add_model(s, txm);
    e = entity3d_new(txm);
    e->visible = 1;
    e->update  = NULL;
    model3dtx_add_entity(txm, e);
    phys->ground = phys_geom_new(phys, model->vx, vxsz, NULL/*model->norm*/, model->idx, idxsz);
    ref_put(&prog->ref); /* matches shader_prog_find() above */
    return 0;
}
