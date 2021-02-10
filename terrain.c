#include <errno.h>
#include "model.h"
#include "physics.h"
#include "scene.h"
#include "shader.h"
#include "terrain.h"


static float get_rand_height(struct terrain *t, int x, int z)
{
    //srand48((x + z * 49152) + seed);
    srand((x * 42 + z * 2345) + t->seed);
    return sin(to_radians((float)rand()));
    //return drand48() * 2 - 1; //sin(to_radians((float)rand()));
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

#define OCTAVES 3
#define ROUGHNESS 0.2f
#define AMPLITUDE 20

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

    return total;
}

static void calc_normal(struct terrain *t, vec3 n, int x, int z)
{
    /* Torus */
    int left = x == 0 ? t->nr_vert -1 : x - 1;
    int right = x == t->nr_vert-1 ? 0 : x + 1;
    int up = z == 0 ? t->nr_vert -1 : z - 1;
    int down = z == t->nr_vert-1 ? 0 : z + 1;
    float hl = t->map[left*t->nr_vert + z];
    float hr = t->map[right*t->nr_vert + z];
    float hd = t->map[x*t->nr_vert + up];
    float hu = t->map[x*t->nr_vert + down];

    n[0] = hl - hr;
    n[1] = 2.f;
    n[2] = hd - hu;

    vec3_norm(n, n);
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

static struct bsp_part *bsp_process(int depth, float x, float y, float w, float h)
{
    struct bsp_part *root;

    srand48(0xbebeabba);
    CHECK(root = calloc(1, sizeof(*root)));
    root->x = x;
    root->y = y;
    root->w = w;
    root->h = h;

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

float terrain_height(struct terrain *t, float x, float z)
{
    float square = (float)t->side / (t->nr_vert - 1);
    float tx = x + t->side / 2;
    float tz = z + t->side / 2;
    int gridx = floorf(tx / square);
    int gridz = floorf(tz / square);
    float xoff = (tx - square * gridx) / square;
    float zoff = (tz - square * gridz) / square;
    vec3 p1, p2, p3;
    vec2 pos = { xoff, zoff };
    float height;

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

    free(terrain->map);
    free(terrain);
}
struct terrain *terrain_init(struct scene *s, float x, float y, float z, float side, unsigned int nr_v)
{
    struct terrain *t;
    struct model3d *model;
    struct model3dtx *txm;
    struct shader_prog *prog = shader_prog_find(s->prog, "model"); /* XXX */
    unsigned long total = nr_v * nr_v, it;
    size_t vxsz, txsz, idxsz;
    float *vx, *norm, *tx;
    unsigned short *idx;
    struct bsp_part *bsp_root = bsp_process(4, x, z, side, side);
    struct timespec ts;
    int i, j;

    CHECK(t = ref_new(struct terrain, ref, terrain_drop));
    clock_gettime(CLOCK_REALTIME, &ts);
    t->seed  = ts.tv_nsec;

    t->nr_vert = nr_v;
    t->side = side;
    CHECK(t->map0 = calloc(nr_v * nr_v, sizeof(float)));
    for (i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++)
            t->map0[i * nr_v + j] = get_rand_height(t, i, j);
    t->map  = calloc(nr_v * nr_v, sizeof(float));
    for (i = 0; i < nr_v; i++)
        for (j = 0; j < nr_v; j++) {
            struct bsp_part *bp = bsp_find(bsp_root, i, j);

            t->map[i * nr_v + j] = get_height(t, i, j, bp->amp, bp->oct);
        }
    free(t->map0);
    t->map0 = NULL;
    bsp_cleanup(bsp_root);

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
            //vec3 v0, v1, v2, a, b;
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
    t->entity = entity3d_new(txm);
    t->entity->visible = 1;
    t->entity->update  = NULL;
    model3dtx_add_entity(txm, t->entity);
    phys->ground = phys_geom_new(phys, model->vx, vxsz, NULL/*model->norm*/, model->idx, idxsz);
    ref_put(&prog->ref); /* matches shader_prog_find() above */
    return t;
}

void terrain_done(struct terrain *t)
{
    ref_put_last(&t->ref);
}
