// SPDX-License-Identifier: Apache-2.0
#include "model.h"
#include "particle.h"
#include "primitives.h"
#include "scene.h"
#include "error.h"

typedef struct particle {
    struct list entry;
    vec3        pos;
    vec3        velocity;
} particle;

typedef struct particle_system {
    struct ref      ref;
    entity3d        *e;
    struct list     particles;
    vec3            *pos_array;
    double          radius;
    double          radius_squared;
    particle_dist   dist;
    unsigned int    count;
} particle_system;

static void random_point_sphere(vec3 pos, const vec3 center, float radius, particle_dist dist)
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

    vec3_add_scaled(pos, center, dir, 1.0, r);
}

static void particle_set_velocity(particle *p)
{
    p->velocity[0] = (drand48() * 2.0 - 1.0) * 0.005;
    p->velocity[1] = (drand48() * 2.0 - 1.0) * 0.005;
    p->velocity[2] = (drand48() * 2.0 - 1.0) * 0.005;
}

static void particle_spawn(particle_system *ps)
{
    particle *p = mem_alloc(sizeof(*p), .zero = 1);
    if (!p)
        return;

    random_point_sphere(p->pos, transform_pos(&ps->e->xform, NULL), ps->radius, ps->dist);
    list_append(&ps->particles, &p->entry);
    particle_set_velocity(p);
}

static int particles_update(entity3d *e, void *data)
{
    struct scene *s = data;

    mat4x4 mx;
    mat4x4_dup(mx, s->camera->view.main.view_mx);

    mat4x4_identity(e->mx);
    /*
     * billboard the particles: undo the view_mx's rotation so that
     * particles always face the camera
     */
    vec3_dup(e->mx[0], (vec3){ mx[0][0], mx[1][0], mx[2][0] });
    vec3_dup(e->mx[1], (vec3){ mx[0][1], mx[1][1], mx[2][1] });
    vec3_dup(e->mx[2], (vec3){ mx[0][2], mx[1][2], mx[2][2] });
    transform_pos(&e->xform, e->mx[3]);
    // entity3d_aabb_update(e);

    particle_system *ps = e->particles;
    particle *p;
    int i = 0;
    list_for_each_entry(p, &ps->particles, entry) {
        vec3 dist, pos;

        transform_pos(&ps->e->xform, pos);
        vec3_sub(dist, p->pos, pos);
        if (vec3_mul_inner(dist, dist) > ps->radius_squared) {
            random_point_sphere(p->pos, pos, ps->radius, ps->dist);
            particle_set_velocity(p);
        }

        vec3_add(p->pos, p->pos, p->velocity);
        vec3_dup(ps->pos_array[i++], p->pos);
    }

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
    transform_set_pos(&ps->e->xform, center);
}

static cerr particle_system_make(struct ref *ref, void *_opts)
{
    rc_init_opts(particle_system) *opts = _opts;

    if (!opts->name || !opts->prog || !opts->mq || !opts->count)
        return CERR_INVALID_ARGUMENTS;

    particle_system *ps = container_of(ref, particle_system, ref);

    list_init(&ps->particles);

    LOCAL_SET(mesh_t, particle_mesh) = ref_new(mesh, .name = "particle");
    mesh_attr_alloc(particle_mesh, MESH_VX, sizeof(float) * 3, 6);
    mesh_attr_alloc(particle_mesh, MESH_TX, sizeof(float) * 2, 6);
    mesh_attr_alloc(particle_mesh, MESH_NORM, sizeof(float) * 3, 6);
    mesh_attr_alloc(particle_mesh, MESH_IDX, sizeof(unsigned short), 6);

    double scale = opts->scale ? : 0.01;
    vec3 quad[4];
    vec3_dup(quad[0], (vec3){ -scale, -scale, 0.0 });
    vec3_dup(quad[1], (vec3){ -scale,  scale, 0.0 });
    vec3_dup(quad[2], (vec3){  scale,  scale, 0.0 });
    vec3_dup(quad[3], (vec3){  scale, -scale, 0.0 });
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

    mq_add_model_tail(opts->mq, txres.val);

    cresp(entity3d) eres = ref_new_checked(entity3d, .txmodel = ref_pass(txres.val));

    if (IS_CERR(eres))
        return cerr_error_cres(eres);

    /* particle system should hold the reference to the entity */
    ps->e = eres.val;
    ps->e->particles = ps;
    /* ps->e's AABB should cover a sphere with ps->radius, until then, disable culling */
    ps->e->skip_culling = true;
    ps->e->update = particles_update;
    ps->count = opts->count > PARTICLES_MAX ? PARTICLES_MAX : opts->count;
    ps->radius = opts->radius;
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
