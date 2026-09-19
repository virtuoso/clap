// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "debris.h"
#include "memory.h"
#include "physics.h"

struct debris_fragment {
    entity3d    *e;
    float       mass;
    bool        released;
};

struct debris {
    struct scene            *scene;
    struct debris_fragment  *fragments;
    size_t                  nr;
};

DEFINE_CLEANUP(debris, if (*p) debris_free(*p))

/* one fragment: a model, an entity in place, a static collision geometry */
static cerr debris_fragment_spawn(debris *d, const debris_params *params, const fracture_fragment *f,
                                  unsigned int i)
{
    LOCAL(char, name);

    /* a dynamic body has to have a mass */
    if (!(f->volume > 0.0f))    return CERR_INVALID_ARGUMENTS;

    CRES_RET_CERR(mem_asprintf(&name, "%s.%u", params->name, i));

    /* the soup welded: a connected surface for the collision mesh */
    mesh_optimize(f->mesh);

    LOCAL_SET(model3d, m) = CRES_RET_CERR(
        ref_new_checked(model3d, .name = name, .prog = params->prog, .mesh = f->mesh)
    );
    LOCAL_SET(model3dtx, txm) = CRES_RET_CERR(
        ref_new_checked(model3dtx, .model = ref_pass(m), .tex = params->tex,
                        .metallic = params->metallic, .roughness = params->roughness)
    );

    scene_add_model(d->scene, txm);

    entity3d *e = CRES_RET_CERR(ref_new_checked(entity3d, .txmodel = ref_pass(txm)));

    /* where the fragment's centroid sits once the prop is placed */
    quat rot;
    vec3 center, pos;

    vec4_dup(rot, transform_rotation_quat(params->xform));
    vec3_dup(center, f->center);
    quat_mul_vec3(pos, rot, center);
    vec3_add(pos, pos, transform_pos(params->xform, nullptr));
    entity3d_position(e, pos);
    transform_set_quat(&e->xform, rot);

    entity3d_add_physics(e, clap_get_phys(d->scene->clap_ctx), 0.0, GEOM_TRIMESH, PHYS_GEOM,
                         0.0, 0.0, 0.0);
    if (!entity3d_matches(e, ENTITY3D_HAS_PHYSICS)) {
        entity3d_delete(e);
        return CERR_NOMEM;
    }

    d->fragments[i].e = e;
    d->fragments[i].mass = params->density * f->volume;
    d->nr = i + 1;

    return CERR_OK;
}

cresp(debris) _debris_spawn(struct scene *scene, const debris_params *params)
{
    if (!scene || !params || !params->name || !params->mesh || !params->prog || !params->tex ||
        !params->xform || params->density <= 0.0f)
        return cresp_error(debris, CERR_INVALID_ARGUMENTS);

    fracture_fragments ff = CRES_RET_T(fracture_mesh(params->mesh, &params->fracture, params->rng), debris);
    LOCAL_SET(debris, d) = mem_alloc(sizeof(*d), .zero = 1);
    cerr err = CERR_NOMEM;

    if (!d)     goto out;

    d->scene = scene;
    d->fragments = mem_alloc(sizeof(*d->fragments), .nr = ff.nr, .zero = 1);
    if (!d->fragments)  goto out;

    err = CERR_OK;
    for (size_t i = 0; i < ff.nr && !IS_CERR(err); i++) err = debris_fragment_spawn(d, params, &ff.fragments[i], i);

    /* all or nothing: a half-spawned prop is neither */
    if (IS_CERR(err))
        for (size_t i = 0; i < d->nr; i++)  entity3d_delete(d->fragments[i].e);

out:
    fracture_fragments_release(&ff);

    if (IS_CERR(err))   return cresp_error_cerr(debris, err);

    return cresp_val(debris, NOCU(d));
}

cres(int) debris_release(debris *d, const vec3 at, float radius, float speed)
{
    if (!d || radius <= 0.0f)   return cres_error(int, CERR_INVALID_ARGUMENTS);

    struct phys *phys = clap_get_phys(d->scene->clap_ctx);
    int nr = 0;

    for (size_t i = 0; i < d->nr; i++) {
        struct debris_fragment *fr = &d->fragments[i];
        entity3d *e = fr->e;
        vec3 pos, dir, vel;

        if (fr->released)   continue;

        transform_pos(&e->xform, pos);
        vec3_sub(dir, pos, at);

        float dist = vec3_len(dir);
        if (dist > radius)  continue;

        /* the static geometry makes way for a dynamic body of the fragment's mass */
        if (entity3d_matches(e, ENTITY3D_HAS_PHYSICS)) {
            phys_body_done(e->phys_body);
            e->phys_body = nullptr;
            entity3d_clear(e, ENTITY3D_HAS_PHYSICS | ENTITY3D_PHYS_IS_BODY);
        }
        entity3d_add_physics(e, phys, fr->mass, GEOM_TRIMESH, PHYS_BODY, 0.0, 0.0, 0.0);
        if (!entity3d_matches(e, ENTITY3D_PHYS_IS_BODY)) {
            /* it has no geometry left to tear down: don't come back to it */
            fr->released = true;
            return cres_error(int, CERR_NOMEM);
        }

        /* the body starts out in the entity's pose, then it's the simulation's */
        phys_body_rotate_xform(e->phys_body, &e->xform);
        /*
         * ODE's own contact hardness: the engine's defaults are tuned soft
         * for the character controller, and a slab of rubble on them sinks
         * through the floor before the contacts hold it.
         */
        phys_body_set_contact_params(e->phys_body, .mu = 1.0f, .soft_erp = 0.2f, .soft_cfm = 1e-5f);

        /* a fragment right at the impact has nowhere to go but up */
        if (dist > 0.0f)
            vec3_scale(vel, dir, speed * (1.0f - dist / radius) / dist);
        else
            vec3_setup(vel, 0.0f, speed, 0.0f);
        phys_body_set_velocity(e->phys_body, vel);

        fr->released = true;
        nr++;
    }

    return cres_val(int, nr);
}

size_t debris_nr_fragments(const debris *d)
{
    return d ? d->nr : 0;
}

size_t debris_nr_dormant(const debris *d)
{
    size_t nr = 0;

    if (!d)     return 0;

    for (size_t i = 0; i < d->nr; i++)
        if (!d->fragments[i].released)  nr++;

    return nr;
}

void debris_free(debris *d)
{
    if (!d)     return;

    mem_free(d->fragments);
    mem_free(d);
}
