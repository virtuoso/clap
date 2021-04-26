#include <errno.h>
#include "model.h"
#include "physics.h"
#include "scene.h"
#include "shader.h"
#include "terrain.h"


static float get_rand_height(struct terrain *t, int x, int z)
{
    //return 0;   
    srand48(t->seed ^ (x + z * 49152));
    //srand((x * 42 + z * 2345) + t->seed);
    //return sin(to_radians((float)rand()));
    return drand48() * 2 - 1; //sin(to_radians((float)rand()));
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
#define AMPLITUDE 40

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
    // if (vec3_len(n) != 1.000000)
    //     dbg("### normal not normal: %f,%f,%f == %f\n", n[0], n[1], n[2], vec3_len(n));
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
    // if (bsp_area(node) < 16)
    //     return false;
    if (level > 16)
        return false;
    if (node->w / node->h > 4 || node->h / node->w > 4)
        return true;
    if (bsp_area(node) > bsp_area(node->root) / 4)
        return true;
    if (level < 3)
        return true;
    return false;
    //return bsp_area(node) > bsp_area(parent) * 5 / 6;
}

static void bsp_part_one(struct bsp_part *root, int level, bsp_cb cb, void *data)
{
    bool vertical = !!(level & 1);
    double frac = drand48();
    struct bsp_part *a, *b;

    frac = clampd(frac, 0.2, 0.8);
    // if (cb)
    //     cb(root, level, data);

    // if (!level)
    //     return;

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
        struct bsp_area *x = *b;
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
    // if (level)
    //     return;

    //node->amp = max(drand48() * AMPLITUDE, 3);
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

    free(terrain->map);
    free(terrain);
}
struct terrain *terrain_init_square_landscape(struct scene *s, float x, float y, float z, float side, unsigned int nr_v)
{
    struct terrain *t;
    struct model3d *model;
    struct model3dtx *txm;
    struct shader_prog *prog = shader_prog_find(s->prog, "model"); /* XXX */
    unsigned long total = nr_v * nr_v, it, bottom;
    size_t vxsz, txsz, idxsz;
    float *vx, *norm, *tx;
    unsigned short *idx;
    struct bsp_part *bsp_root;
    struct timespec ts;
    mat4x4 idm;
    int i, j;

    CHECK(t = ref_new(struct terrain, ref, terrain_drop));
    clock_gettime(CLOCK_REALTIME, &ts);
    t->seed  = ts.tv_nsec;

    bsp_root = bsp_process(t->seed, 5, 0, 0, nr_v, nr_v, terrain_bsp_cb, NULL);

    t->nr_vert = nr_v;
    t->side = side;
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
            //float amp   = xamp / 2 + yamp / 2;//cos_interp(xamp, yamp, fabs(xfrac - yfrac));
            float amp   = cos_interp(xamp, yamp, fabs(xfrac - yfrac));

            // dbg("### (%d,%d)/(%f,%f) amp: %f bpx amp: %f bpy amp: %f xamp: %f yamp: %f\n",
            //     i, j, xfrac, yfrac, amp, bpx->amp, bpy->amp, xamp, yamp);
            //if (fabsf(xfrac) > 0.985 || fabsf(yfrac) > 0.985)
            // if (i == bp->x || j == bp->y)
            //     t->map[i * nr_v + j] = -20;
            // else
            t->map[i * nr_v + j] = get_height(t, i, j, amp, bp->oct);
        }
    free(t->map0);
    t->map0 = NULL;
    bsp_cleanup(bsp_root);

    /* add flip side */
    //total += 4;

    vxsz  = total * sizeof(*vx) * 3;
    txsz  = total * sizeof(*tx) * 2;

    idxsz = 6 * (nr_v - 1) * (nr_v - 1) * sizeof(*idx);
    /* add flip side */
    //idxsz += 30 * sizeof(*idx);

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

    bottom = it;
    /* four point flip side */
    /*vx[it * 3 + 0] = t->x;
    vx[it * 3 + 1] = -AMPLITUDE;
    vx[it * 3 + 2] = t->z;
    norm[it * 3 + 0] = 0;
    norm[it * 3 + 1] = -1;
    norm[it * 3 + 2] = 0;
    tx[it * 2 + 0] = 0;
    tx[it * 2 + 1] = 0;
    it++;
    vx[it * 3 + 0] = t->x + t->side;
    vx[it * 3 + 1] = -AMPLITUDE;
    vx[it * 3 + 2] = t->z;
    norm[it * 3 + 0] = 0;
    norm[it * 3 + 1] = -1;
    norm[it * 3 + 2] = 0;
    tx[it * 2 + 0] = 1;
    tx[it * 2 + 1] = 0;
    it++;
    vx[it * 3 + 0] = t->x + t->side;
    vx[it * 3 + 1] = -AMPLITUDE;
    vx[it * 3 + 2] = t->z + t->side;
    norm[it * 3 + 0] = 0;
    norm[it * 3 + 1] = -1;
    norm[it * 3 + 2] = 0;
    tx[it * 2 + 0] = 1;
    tx[it * 2 + 1] = 1;
    it++;
    vx[it * 3 + 0] = t->x;
    vx[it * 3 + 1] = -AMPLITUDE;
    vx[it * 3 + 2] = t->z + t->side;
    norm[it * 3 + 0] = 0;
    norm[it * 3 + 1] = -1;
    norm[it * 3 + 2] = 0;
    tx[it * 2 + 0] = 0;
    tx[it * 2 + 1] = 1;
    it++;*/

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
    
    /*idx[it++] = 0;
    idx[it++] = nr_v - 1;
    idx[it++] = bottom;

    idx[it++] = nr_v - 1;
    idx[it++] = bottom + 1;
    idx[it++] = bottom;

    idx[it++] = nr_v - 1;
    idx[it++] = (nr_v - 1) * (nr_v - 1);
    idx[it++] = bottom + 1;

    idx[it++] = (nr_v - 1) * (nr_v - 1);
    idx[it++] = bottom + 2;
    idx[it++] = bottom + 1;

    idx[it++] = (nr_v - 1) * (nr_v - 1);
    idx[it++] = (nr_v - 2) * (nr_v - 1) - 1;
    idx[it++] = bottom + 2;

    idx[it++] = (nr_v - 2) * (nr_v - 1) - 1;
    idx[it++] = bottom + 3;
    idx[it++] = bottom + 2;

    idx[it++] = (nr_v - 2) * (nr_v - 1) - 1;
    idx[it++] = 0;
    idx[it++] = bottom + 3;

    idx[it++] = 0;
    idx[it++] = bottom;
    idx[it++] = bottom + 3;

    idx[it++] = bottom;
    idx[it++] = bottom + 1;
    idx[it++] = bottom + 2;

    idx[it++] = bottom + 2;
    idx[it++] = bottom + 3;
    idx[it++] = bottom;*/

    model = model3d_new_from_vectors("terrain", prog, vx, vxsz, idx, idxsz,
                                     tx, txsz, norm, vxsz);
    free(tx);
    free(norm);

    //dbg("##### rand height(5,14): %f\n", get_avg_height(5, 14));
    //dbg("##### rand height(5,14): %f\n", get_avg_height(5, 14));
    //dbg("##### rand height(6,15): %f\n", get_avg_height(6, 15));
    txm = model3dtx_new(model, "grass20.png");
    ref_put(&model->ref);
    scene_add_model(s, txm);
    t->entity = entity3d_new(txm);
    t->entity->collision_vx = vx;
    t->entity->collision_vxsz = vxsz;
    t->entity->collision_idx = idx;
    t->entity->collision_idxsz = idxsz;
    t->entity->visible = 1;
    t->entity->update  = NULL;
    model3dtx_add_entity(txm, t->entity);
    entity3d_add_physics(t->entity, 0, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
    phys->ground = t->entity->phys_body->geom;//phys_geom_trimesh_new(phys, NULL, t->entity, dInfinity);
    dGeomSetData(phys->ground, t->entity);
    /* xyz are already baked into the mesh */
    //dGeomSetPosition(phys->ground, t->x, t->y, t->z);
    //mat4x4_identity(idm);
    //dGeomSetRotation(phys->ground, idm);
    ref_put(&prog->ref); /* matches shader_prog_find() above */
    return t;
}

enum {
    TILE_INNER = 0,
    TILE_FLOOR,
    TILE_OUTER,
};

static int tile_index(int nr_v, int level, int which, int tile)
{
    int base = 3 * nr_v;

    /* level 0 only has outer wall tiles, but needs to skip triangles */
    if (!level)
        return base + tile * 6;
    base += nr_v * 6;
    return base + 18 * nr_v * (level - 1) + 6 * nr_v * which + tile * 6;
}

static void tile_sides(int nr_v, int level, int which, int tile, bool winding,
                       unsigned short *idx, int *tl, int *tr, int *bl, int *br)
{
    int t = tile_index(nr_v, level, which, tile);

    if (winding) {
        *tl = idx[t + 0];
        *tr = idx[t + 1];
        *bl = idx[t + 2];
        *br = idx[t + 4];
    } else {
        *tl = idx[t + 0];
        *tr = idx[t + 2];
        *bl = idx[t + 1];
        *br = idx[t + 5];
    }
}

static void build_one_quad(unsigned short *idx, int pos, bool winding, int top_left, int top_right, int bottom_left, int bottom_right)
{
    if (winding) {
        idx[pos++] = top_left;
        idx[pos++] = top_right;
        idx[pos++] = bottom_left;
        idx[pos++] = top_right;
        idx[pos++] = bottom_right;
        idx[pos++] = bottom_left;
    } else {
        idx[pos++] = top_left;
        idx[pos++] = bottom_left;
        idx[pos++] = top_right;
        idx[pos++] = top_right;
        idx[pos++] = bottom_left;
        idx[pos++] = bottom_right;
    }
}

static void build_wall_idx(struct terrain *t, unsigned short *idx, int level, int which, bool winding, unsigned long top_row, unsigned long bottom_row)
{
    int i;

    for (i = 0; i < t->nr_vert; i++) {
        int top_left     = top_row + i;
        int top_right    = i == t->nr_vert - 1 ? top_row : top_left + 1;
        int bottom_left  = bottom_row + i;
        int bottom_right = i == t->nr_vert - 1 ? bottom_row : bottom_left + 1;
        int x = tile_index(t->nr_vert, level, which, i);

        build_one_quad(idx, x, winding, top_left, top_right, bottom_left, bottom_right);
        // dbg("IDX[%d] %d, %d, %d / %d, %d, %d\n", x, idx[x-6], idx[x-5], idx[x-4], idx[x-3], idx[x-2], idx[x-1]);
    }
}

/* outer: 0(inner)/1; top: 0(bottom)/1 */
static int first_vertex(int nr_v, int level, int outer, int top)
{
    int base;

    outer = !!outer;
    top = !!top;

    /* level zero only has outer wall: bottom [1..nr_v-1] top [nr_v..2*nr_v-1] */
    if (!level)
        return 1 + nr_v * top;
    base = 1 + 2 * nr_v + 4 * nr_v * (level - 1);
    return base + nr_v * (outer * 2 + top);
}

static void punch_hole(unsigned short *idx, int nr_v, int level, int tile)
{
    int top_left     = first_vertex(nr_v, level,     0, 1) + tile;
    int top_right    = first_vertex(nr_v, level - 1, 1, 1) + tile;
    int bottom_left  = first_vertex(nr_v, level,     0, 0) + tile;
    int bottom_right = first_vertex(nr_v, level - 1, 1, 0) + tile;
    int pos = tile_index(nr_v, level, TILE_INNER, tile);
    int x, xx, xy;

    build_one_quad(idx, pos, false, top_right, top_left, bottom_right, bottom_left);
    pos = tile_index(nr_v, level - 1, TILE_OUTER, tile);
    build_one_quad(idx, pos, false, top_left + 1, top_right + 1, bottom_left + 1, bottom_right + 1);

    if (level > 1) {
        pos = tile_index(nr_v, level, TILE_FLOOR, tile);
        tile_sides(nr_v, level, TILE_FLOOR, tile, false, idx, &top_left, &top_right, &xx, &xy);
        tile_sides(nr_v, level - 1, TILE_FLOOR, tile, false, idx, &bottom_left, &bottom_right, &x, &x);
        build_one_quad(idx, pos, false, top_left, top_right, bottom_left, bottom_right);
    }
}

static void erect_wall(struct terrain *t, int level, int tile)
{
    int top_left     = first_vertex(t->nr_vert, level, 1, 1) + tile;
    int top_right    = first_vertex(t->nr_vert, level - 1, 1, 1) + tile;
    int bottom_left  = first_vertex(t->nr_vert, level, 1, 0) + tile;
    int bottom_right = first_vertex(t->nr_vert, level - 1, 1, 0) + tile;
    int pos = t->nr_idx;

    t->nr_idx += 12;
    CHECK(t->idx = realloc(t->idx, terrain_idxsz(t)));
    build_one_quad(t->idx, pos, false, top_left, top_right, bottom_left, bottom_right);
    pos += 6;
    build_one_quad(t->idx, pos, false, top_right, top_left, bottom_right, bottom_left);
}

enum {
    MCL_UP = 0,
    MCL_DOWN,
    MCL_LEFT,
    MCL_RIGHT,
    MC_LINKS_MAX,
};

struct maze_cell {
    /* address */
    unsigned int         level;
    unsigned int         cell;
    /* cell can have up to 4 links (root could have nr_cells, but doesn't) */
    struct maze_cell     *links[MC_LINKS_MAX];
};

static bool cell_is_root(struct maze_cell *mc)
{
    return !mc->level && !mc->cell;
}

static bool cells_do_link(struct maze_cell *mc, struct maze_cell *to)
{
    int i;

    for (i = 0; i < MC_LINKS_MAX; i++)
        if (mc->links[i] == to || to->links[i] == mc)
            return true;

    return false;
}

static int cells_vdist(struct maze_cell *a, struct maze_cell *b)
{
    return a->level - b->level;
}

static int cells_hdist(struct maze_cell *a, struct maze_cell *b)
{
    return a->cell - b->cell;
}

static int cells_dir(struct maze_cell *a, struct maze_cell *b)
{
    int vdist = cells_vdist(a, b);
    int hdist = cells_hdist(a, b);

    if (vdist && hdist)
        return MC_LINKS_MAX;

    switch (vdist) {
    case 1:
        return MCL_DOWN;
    case -1:
        return MCL_UP;
    default:
        break;
    }
    switch (hdist) {
    case 1:
        return MCL_LEFT;
    case -1:
        return MCL_RIGHT;
    default:
        break;
    }
    return MC_LINKS_MAX;
}

static bool cells_can_link(struct maze_cell *mc, struct maze_cell *to)
{
    if (cells_do_link(mc, to))
        return false;

    if (cells_dir(mc, to) != MC_LINKS_MAX)
        return true;

    return false;
}

static int cell_link(struct maze_cell *mc, struct maze_cell *to)
{
    if (cells_can_link(mc, to))
        return -1;
    
    mc->links[cells_dir(mc, to)] = to;
    to->links[cells_dir(to, mc)] = mc;

    return 0;
}

static int cell_unlink(struct maze_cell *mc, struct maze_cell *from)
{
    if (!cells_do_link(mc, from))
        return -1;

    mc->links[cells_dir(mc, from)] = NULL;
    from->links[cells_dir(from, mc)] = NULL;

    return 0;
}

struct circ_maze {
    unsigned int     nr_levels;
    unsigned int     cells_per_level;
    unsigned int     cells_total;
    struct maze_cell *cells;
};

/* XXX: root is a silly idea, better to mark entry/exit cells */
static int maze_root_link(struct circ_maze *m, struct maze_cell *mc)
{
    int i;

    if (mc->links[MCL_DOWN])
        return -1;

    for (i = 0; i < MC_LINKS_MAX; i++)
        if (!m->cells->links[i])
            goto found;
    return -1;

found:
    m->cells->links[i] = mc;
    mc->links[MCL_DOWN] = m->cells;

    return 0;
}

static struct maze_cell *maze_get_cell(struct circ_maze *m, unsigned int level, unsigned int cell)
{
    return &m->cells[1 + (level - 1) * m->cells_per_level + cell];
}

static void maze_print(struct circ_maze *m)
{
    // struct maze_cell *mc = m->root;

    // for 
}

static struct circ_maze *maze_build(unsigned int levels, unsigned int cells)
{
    unsigned int i, level, cell;
    struct maze_cell *mc, *prev;
    struct circ_maze *m;

    CHECK(m = calloc(1, sizeof(*m)));
    m->nr_levels = levels - 1; /* level 0 has one cell */
    m->cells_per_level = cells;
    m->cells_total = m->nr_levels * cells + 1;
    CHECK(mc = calloc(m->cells_total, sizeof(*mc)));
    /* leave mc[0] with all zeroes -- it's root */
    for (i = 1, level = 1, cell = 0, prev = &mc[0]; i < m->cells_total; i++) {
        mc[i].level = level;
        mc[i].cell  = cell++;
        cell_link(&mc[i], prev);
        prev = &mc[i];
        if (cell == cells) {
            /* loop */
            prev = &mc[i - cells + 1]; /* first cell for this level */
            cell_link(&mc[i], prev);
            cell = 0;
            level++;
        }
    }
    m->cells = &mc[0];
    maze_print(m);

    return m;
}

struct terrain *terrain_init_circular_maze(struct scene *s, float x, float y, float z, float radius, unsigned int nr_v, unsigned int nr_levels)
{
    float room_side = radius / nr_levels, height = 20, wall, texmag = 1.0;
    struct shader_prog *prog = shader_prog_find(s->prog, "model"); /* XXX */
    struct bsp_part *bsp_root;
    struct model3d *model;
    struct model3dtx *txm;
    unsigned long total;
    struct timespec ts;
    struct terrain *t;
    int i, j, level;

    wall = min(0.1, sqrt(room_side));
    CHECK(t = ref_new(struct terrain, ref, terrain_drop));
    clock_gettime(CLOCK_REALTIME, &ts);
    t->seed  = ts.tv_nsec;

    //bsp_root = bsp_process(t->seed, 5, 0, 0, nr_v, nr_v, terrain_bsp_cb, NULL);

    t->nr_vert = nr_v;
    t->side    = radius;
    t->x       = x;
    t->y       = y;
    t->z       = z;
    // bsp_cleanup(bsp_root);

    /*
     * inner circle:
     * nr_v triangles;
     * vertices: nr_v + 1 (center);
     * indices:  nr_v * 3;
     * ------------------------
     * second, wall around inner circle
     *   using vertices from inner circle
     * vertices: nr_v
     * indices:  6 * nr_v
     * //second, we'll do nr_v quads in a circle
     * //total -> quads
     */
    /*
     * vertices:
     * level 0: center + 2 * nr_v outer wall edges = 1 + 2 * nr_v
     * level 1+: outer wall edges (bottom,top) + inner wall edges (bottom,top) + floor = 6 * nr_v
     */
    t->nr_vx = 1 + nr_v * (2 + 4 * (nr_levels - 1));

    /*
     * indices:
     * level 0: nr_v triangles + 2 * nr_v wall in quads = 3 * nr_v;
     * level 1+: 3 quads (outer wall, floor, inner wall) = 6 * nr_v
     */
    t->nr_idx = (3 * nr_v + 6 * nr_v * (nr_levels - 1)) * 3;

    /*
     * now, we add walls using indices of existing vertices, so
     * we need more space in the idx array;
     * a new wall is 2 quads
     * idxsz += 6 * nr_v * (nr_levels - 1)
     * or we can just realloc
     */

    CHECK(t->vx    = malloc(terrain_vxsz(t)));
    CHECK(t->norm  = malloc(terrain_vxsz(t)));
    CHECK(t->tx    = malloc(terrain_txsz(t)));
    CHECK(t->idx   = malloc(terrain_idxsz(t)));
    if (!t->vx || !t->norm || !t->tx || !t->idx)
        return NULL;

    /* center */
    t->vx[0] = x;
    t->vx[1] = y;
    t->vx[2] = z;
    t->norm[0] = 0;
    t->norm[1] = 1;
    t->norm[2] = 0;
    t->tx[0] = 0;
    t->tx[1] = 1;

    // dbg("[%d] VX [%f,%f,%f] TX [%f,%f]\n", it, vx[it*3], vx[it*3+1], vx[it*3+2],
    //     tx[it*2], tx[it*2+1]);

    /* level's vertices: [1 + 2 * nr_v * level..2 * nr_v * (level + 1)] */
    for (i = 0; i < nr_v; i++) {
        for (level = 0; level < nr_levels; level++) {
            int pos;

            texmag = max(1, level);
            if (level) {
                //pos += 2 * nr_v * (level - 1);
                /* inner wall bottom */
                pos = first_vertex(nr_v, level, 0, 0) + i;
                t->vx[pos * 3 + 0] = x + (room_side + wall / level) * cos(i * M_PI * 2 / nr_v) * level;
                t->vx[pos * 3 + 1] = y; /* no height variation yet */
                t->vx[pos * 3 + 2] = z + (room_side + wall / level) * sin(i * M_PI * 2 / nr_v) * level;
                t->norm[pos * 3 + 0] = 0;
                t->norm[pos * 3 + 1] = 1;
                t->norm[pos * 3 + 2] = 0;
                if (level & 1) {
                    t->tx[pos * 2 + 0] = i & 1 ? texmag : 0;
                } else {
                    t->tx[pos * 2 + 0] = i & 1 ? 0 : texmag;
                }
                t->tx[pos * 2 + 1] = level & 1 ? 0 : texmag;
                // dbg("[%d:%d] VX [%f,%f,%f] TX [%f,%f]\n", level, pos, vx[pos*3], vx[pos*3+1], vx[pos*3+2],
                //     tx[pos*2], tx[pos*2+1]);

                /* inner wall top */
                pos = first_vertex(nr_v, level, 0, 1) + i;
                t->vx[pos * 3 + 0] = t->vx[(pos-nr_v) * 3 + 0];
                t->vx[pos * 3 + 1] = y + height;
                t->vx[pos * 3 + 2] = t->vx[(pos-nr_v) * 3 + 2];
                t->norm[pos * 3 + 0] = 0;
                t->norm[pos * 3 + 1] = -1;
                t->norm[pos * 3 + 2] = 0;
                if (level & 1) {
                    t->tx[pos * 2 + 0] = i & 1 ? texmag : 0;
                } else {
                    t->tx[pos * 2 + 0] = i & 1 ? 0 : texmag;
                }
                t->tx[pos * 2 + 1] = level & 1 ? texmag : 0;
                // dbg("[%d:%d] VX [%f,%f,%f] TX [%f,%f]\n", level, pos, vx[pos*3], vx[pos*3+1], vx[pos*3+2],
                //     tx[pos*2], tx[pos*2+1]);
            }
            /* outer wall bottom */
            pos = first_vertex(nr_v, level, 1, 0) + i;
            t->vx[pos * 3 + 0] = x + room_side * cos(i * M_PI * 2 / nr_v) * (level + 1);
            t->vx[pos * 3 + 1] = y; /* no height variation yet */
            t->vx[pos * 3 + 2] = z + room_side * sin(i * M_PI * 2 / nr_v) * (level + 1);
            t->norm[pos * 3 + 0] = 0;
            t->norm[pos * 3 + 1] = 1;
            t->norm[pos * 3 + 2] = 0;
            if (level & 1) {
                t->tx[pos * 2 + 0] = i & 1 ? texmag : 0;
            } else {
                t->tx[pos * 2 + 0] = i & 1 ? 0 : texmag;
            }
            t->tx[pos * 2 + 1] = level & 1 ? texmag : 0;
            // dbg("[%d:%d] VX [%f,%f,%f] TX [%f,%f]\n", level, pos, vx[pos*3], vx[pos*3+1], vx[pos*3+2],
            //     tx[pos*2], tx[pos*2+1]);

            /* outer wall top */
            pos = first_vertex(nr_v, level, 1, 1) + i;
            t->vx[pos * 3 + 0] = t->vx[(pos-nr_v) * 3 + 0];
            t->vx[pos * 3 + 1] = y + height;
            t->vx[pos * 3 + 2] = t->vx[(pos-nr_v) * 3 + 2];
            t->norm[pos * 3 + 0] = 0;
            t->norm[pos * 3 + 1] = -1;
            t->norm[pos * 3 + 2] = 0;
            if (level & 1) {
                t->tx[pos * 2 + 0] = i & 1 ? texmag : 0;
            } else {
                t->tx[pos * 2 + 0] = i & 1 ? 0 : texmag;
            }
            t->tx[pos * 2 + 1] = level & 1 ? 0 : texmag;
            // dbg("[%d:%d] VX [%f,%f,%f] TX [%f,%f]\n", level, pos, vx[pos*3], vx[pos*3+1], vx[pos*3+2],
            //     tx[pos*2], tx[pos*2+1]);
        }
    }

    for (i = 0; i < nr_v; i++) {
        t->idx[i * 3 + 0] = i + 1;     /* top left */
        t->idx[i * 3 + 1] = 0; /* bottom */
        t->idx[i * 3 + 2] = i == nr_v - 1 ? 1 : i + 2; /* top right */
        // dbg("IDX %d, %d, %d\n", t->idx[it-3], idx[it-2], idx[it-1]);
    }

    for (level = 0; level < nr_levels; level++) {
        int outer_wall_bottom = first_vertex(nr_v, level, 1, 0);
        int outer_wall_top = first_vertex(nr_v, level, 1, 1);

        /* outer floor */
        if (level) {
            int floor_top = first_vertex(nr_v, level, 1, 0);
            int floor_bottom = first_vertex(nr_v, level, 0, 0);
            int inner_wall_top = first_vertex(nr_v, level, 0, 1);
            int inner_wall_bottom = first_vertex(nr_v, level, 0, 0);

            // dbg("## inner wall %d\n", level);
            build_wall_idx(t, t->idx, level, TILE_INNER, true, inner_wall_top, inner_wall_bottom);

            // dbg("## floor level %d\n", level);
            build_wall_idx(t, t->idx, level, TILE_FLOOR, false, floor_top, floor_bottom);
        }

        /* wall */
        // dbg("## outer wall level %d\n", level);
        build_wall_idx(t, t->idx, level, TILE_OUTER, false, outer_wall_top, outer_wall_bottom);
    }

    /* punch holes through the maze */
    for (level = 1; level < nr_levels; level++) {
        punch_hole(t->idx, nr_v, level, level);
        erect_wall(t, level, level);
    }

    model = model3d_new_from_vectors("terrain", prog, t->vx, terrain_vxsz(t), t->idx, terrain_idxsz(t),
                                     t->tx, terrain_txsz(t), t->norm, terrain_vxsz(t));
    free(t->tx);
    free(t->norm);

    // txm = model3dtx_new(model, "grass20.png");
    // txm = model3dtx_new(model, "stonewall.png");
    txm = model3dtx_new(model, "wall12.png");
    ref_put(&model->ref);
    scene_add_model(s, txm);
    t->entity = entity3d_new(txm);
    t->entity->collision_vx = t->vx;
    t->entity->collision_vxsz = terrain_vxsz(t);
    t->entity->collision_idx = t->idx;
    t->entity->collision_idxsz = terrain_idxsz(t);
    t->entity->visible = 1;
    t->entity->update  = NULL;
    model3dtx_add_entity(txm, t->entity);
    entity3d_add_physics(t->entity, 0, dTriMeshClass, PHYS_GEOM, 0, 0, 0);
    phys->ground = t->entity->phys_body->geom;//phys_geom_trimesh_new(phys, NULL, t->entity, dInfinity);
    dGeomSetData(phys->ground, t->entity);
    ref_put(&prog->ref); /* matches shader_prog_find() above */
    return t;
}

void terrain_done(struct terrain *t)
{
    ref_put_last(&t->ref);
}
