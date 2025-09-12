// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "particle.h"
#include "primitives.h"
#include "scene.h"
#include "error.h"
#include "util.h"

typedef struct particle_system particle_system;

typedef struct particle {
    struct list     entry;
    vec3            pos;
    vec3            velocity;
    particle_system *ps;
} particle;

typedef struct particle_system {
    struct ref      ref;
    entity3d        *e;
    struct list     particles;
    vec3            *pos_array;
    double          radius;
    double          min_radius;
    double          radius_squared;
    double          velocity;
    particle_dist   dist;
    unsigned int    count;
} particle_system;

entity3d *particle_system_entity(particle_system *ps)
{
    return ps->e;
}

static void random_point_sphere(vec3 pos, const vec3 center, float radius, float min_radius, particle_dist dist)
{
    vec3 dir;
    dir[0] = drand48() * 2.0 - 1.0;
    dir[1] = drand48() * 2.0 - 1.0;
    dir[2] = drand48() * 2.0 - 1.0;
    vec3_norm_safe(dir, dir);

    float r;
    switch (dist) {
        case PART_DIST_POW075:
            r = radius * powf(drand48(), 0.75);
            break;
        case PART_DIST_CBRT:
            r = radius * cbrt(drand48());
            break;
        case PART_DIST_SQRT:
            r = sqrtf(drand48());
            break;
        case PART_DIST_LIN:
        default:
            r = radius * drand48();
            break;
    }
    r = min_radius + r;// / (radius - min_radius);

    vec3_add_scaled(pos, center, dir, 1.0, r);
}

static void particle_set_velocity(particle *p)
{
    p->velocity[0] = (drand48() * 2.0 - 1.0) * p->ps->velocity;
    p->velocity[1] = (drand48() * 2.0 - 1.0) * p->ps->velocity;
    p->velocity[2] = (drand48() * 2.0 - 1.0) * p->ps->velocity;
}

static void particle_spawn(particle_system *ps)
{
    particle *p = mem_alloc(sizeof(*p), .zero = 1);
    if (!p)
        return;

    p->ps = ps;
    random_point_sphere(p->pos, transform_pos(&ps->e->xform, NULL), ps->radius, ps->min_radius, ps->dist);
    list_append(&ps->particles, &p->entry);
    particle_set_velocity(p);
}

#if defined(__APPLE__) || defined(_WIN32)
static int cmp_camera_dist(void *data, const void *a, const void *b)
#else /* !__APPLE__ && !_WIN32 */
static int cmp_camera_dist(const void *a, const void *b, void *data)
#endif /* !__APPLE__ && !_WIN32 */
{
    struct scene *s = data;
    const float *pos_a = a, *pos_b = b;

    vec3 dist_a, dist_b;
    vec3_sub(dist_a, pos_a, transform_pos(&s->camera->xform, NULL));
    vec3_sub(dist_b, pos_b, transform_pos(&s->camera->xform, NULL));

    return (int)(vec3_mul_inner(dist_a, dist_a) - vec3_mul_inner(dist_b, dist_b));
}

static int particles_update(entity3d *e, void *data)
{
    struct scene *s = data;

    mat4x4_dup(e->mx, s->camera->view.main.view_mx);

    /*
     * billboard the particles: undo the view_mx's rotation so that
     * particles always face the camera
     */
    mat4x4_transpose_mat3x3(e->mx);
    transform_pos(&e->xform, e->mx[3]);

    particle_system *ps = e->particles;
    particle *p;
    int i = 0;
    list_for_each_entry(p, &ps->particles, entry) {
        vec3 dist, pos;

        transform_pos(&ps->e->xform, pos);
        vec3_sub(dist, p->pos, pos);
        if (vec3_mul_inner(dist, dist) > ps->radius_squared) {
            random_point_sphere(p->pos, pos, ps->radius, ps->min_radius, ps->dist);
            particle_set_velocity(p);
        }

        vec3_add(p->pos, p->pos, p->velocity);
        vec3_dup(ps->pos_array[i++], p->pos);
    }

    /*
     * A quick and dirty workaround for divergent signatures of qsort_r()
     * and its comparator signature.
     */
#if defined(__APPLE__)
    qsort_r(ps->pos_array, ps->count, sizeof(ps->pos_array[0]), s, cmp_camera_dist);
#elif defined(_WIN32)
    qsort_s(ps->pos_array, ps->count, sizeof(ps->pos_array[0]), cmp_camera_dist, s);
#else /* !__APPLE__ && !_WIN32 */
    qsort_r(ps->pos_array, ps->count, sizeof(ps->pos_array[0]), cmp_camera_dist, s);
#endif /* !__APPLE__ */
    return 0;
}

void particle_system_upload(particle_system *ps, struct shader_prog *prog)
{
    shader_set_var_ptr(prog, UNIFORM_PARTICLE_POS, ps->count, ps->pos_array);
}

unsigned int particle_system_count(particle_system *ps)
{
    return ps->count;
}

void particle_system_position(particle_system *ps, const vec3 center)
{
    vec3 prev;
    transform_pos(&ps->e->xform, prev);
    vec3_sub(prev, center, prev);
    if (vec3_mul_inner(prev, prev) == 0.0)
        return;

    transform_set_pos(&ps->e->xform, center);

    particle *p;
    list_for_each_entry(p, &ps->particles, entry)
        vec3_add(p->pos, p->pos, prev);
}

static cerr particle_system_make(struct ref *ref, void *_opts)
{
    rc_init_opts(particle_system) *opts = _opts;

    if (!opts->name || !opts->prog || !opts->mq || !opts->count)
        return CERR_INVALID_ARGUMENTS;

    particle_system *ps = container_of(ref, particle_system, ref);

    list_init(&ps->particles);

    LOCAL_SET(mesh_t, particle_mesh) = ref_new(mesh, .name = "particle");
    CERR_RET_CERR(mesh_attr_alloc(particle_mesh, MESH_VX, sizeof(float) * 3, 6));
    CERR_RET_CERR(mesh_attr_alloc(particle_mesh, MESH_TX, sizeof(float) * 2, 6));
    CERR_RET_CERR(mesh_attr_alloc(particle_mesh, MESH_NORM, sizeof(float) * 3, 6));
    CERR_RET_CERR(mesh_attr_alloc(particle_mesh, MESH_IDX, sizeof(unsigned short), 6));

    double scale = opts->scale ? : 0.01;
    vec3 quad[4];
    vec3_dup(quad[0], (vec3){ -scale * 2.0, -scale, 0.0 });
    vec3_dup(quad[1], (vec3){ -scale * 2.0,  scale, 0.0 });
    vec3_dup(quad[2], (vec3){  scale * 2.0,  scale, 0.0 });
    vec3_dup(quad[3], (vec3){  scale * 2.0, -scale, 0.0 });
    prim_emit_quad(quad, .mesh = particle_mesh);

    cresp(model3d) mres = ref_new_checked(model3d,
        .name   = opts->name,
        .prog   = opts->prog,
        .mesh   = particle_mesh,
    );

    if (IS_CERR(mres))
        return cerr_error_cres(mres);

    mres.val->skip_shadow = true;
    cresp(model3dtx) txres = ref_new_checked(model3dtx,
        .model  = ref_pass(mres.val),
        .tex    = opts->tex ? : white_pixel(),
    );

    if (IS_CERR(txres))
        return cerr_error_cres(txres);

    model3dtx_set_texture(txres.val, UNIFORM_EMISSION_MAP, opts->emit ? : white_pixel());

    mq_add_model(opts->mq, txres.val);

    cresp(entity3d) eres = ref_new_checked(entity3d, .txmodel = ref_pass(txres.val));

    if (IS_CERR(eres))
        return cerr_error_cres(eres);

    /* particle system should hold the reference to the entity */
    ps->e = eres.val;
    ps->e->bloom_intensity = opts->bloom_intensity ? : 1.0;
    ps->e->outline_exclude = true;
    ps->e->particles = ps;
    /* ps->e's AABB should cover a sphere with ps->radius, until then, disable culling */
    ps->e->skip_culling = true;
    ps->e->update = particles_update;
    ps->count = opts->count > PARTICLES_MAX ? PARTICLES_MAX : opts->count;
    ps->radius = opts->radius;
    ps->min_radius = opts->min_radius;
    ps->velocity = opts->velocity ? : 0.005;
    ps->radius_squared = opts->radius * opts->radius;

    ps->pos_array = mem_alloc(sizeof(vec3), .nr = ps->count);
    particle_system_position(ps, opts->center);

    for (int i = 0; i < ps->count; i++) {
        particle_spawn(ps);

        particle *p = list_last_entry(&ps->particles, particle, entry);
        vec3_dup(ps->pos_array[i], p->pos);
    }

    return CERR_OK;
}

static void particle_system_drop(struct ref *ref)
{
    particle_system *ps = container_of(ref, particle_system, ref);

    mem_free(ps->pos_array);
    ref_put_last(ps->e);

    particle *p, *it;
    list_for_each_entry_iter(p, it, &ps->particles, entry) {
        list_del(&p->entry);
        mem_free(p);
    }
}

DEFINE_REFCLASS2(particle_system);
