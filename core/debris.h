/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_DEBRIS_H__
#define __CLAP_DEBRIS_H__

#include "error.h"
#include "fracture.h"
#include "linmath.h"
#include "model.h"
#include "prng.h"
#include "render.h"
#include "scene.h"
#include "shader.h"
#include "transform.h"
#include "util.h"

/*
 * Pre-fractured props for destructible scenery: a mesh is cut into
 * fragments up front (see fracture.h), each one an entity of its own
 * with a static collision geometry, so together they stand in for the
 * whole prop, until debris_release() sets the fragments around an
 * impact loose as dynamic bodies.
 */

typedef struct debris debris;

/**
 * struct debris_params - debris_spawn() parameters
 * @name:       name of the fragment models, numbered "%s.%u"
 * @mesh:       the prop's geometry in its own frame; fractured, not kept
 * @prog:       shader program of the fragment models
 * @tex:        texture of the fragment models
 * @xform:      the prop's placement: position and rotation of its frame
 * @density:    fragment mass per unit of volume
 * @metallic:   fragment material parameters, see model3dtx
 * @roughness:  fragment material parameters, see model3dtx
 * @fracture:   fracturing parameters, see fracture_mesh()
 * @rng:        random stream for fracture_mesh(); may be nullptr if
 *              @fracture gives the seeds
 */
typedef struct debris_params {
    const char          *name;
    struct mesh         *mesh;
    struct shader_prog  *prog;
    texture_t           *tex;
    transform_t         *xform;
    fracture_params     fracture;
    prng                *rng;
    float               density;
    float               metallic;
    float               roughness;
} debris_params;

cresp_ret(debris);
DECLARE_CLEANUP(debris);

/**
 * debris_spawn() - fracture a prop into dormant fragment entities
 * @_s:     scene to add the fragments to
 * @args:   debris_params initializers
 *
 * Fracture @mesh, make a model of every fragment and put it into the
 * scene as an entity where the fragment sits in the prop's frame, with
 * a static trimesh collision geometry: at rest the fragments together
 * are as solid as the prop was. A fragment's mass is its volume times
 * @density; it comes into play when the fragment is released.
 *
 * Return: the debris to be debris_free()d by the caller, the
 * fracture_mesh() errors, CERR_INVALID_ARGUMENTS for missing
 * parameters or a fragment without volume (an open @mesh), or
 * CERR_NOMEM if a fragment couldn't be made; on error none are left in
 * the scene.
 */
#define debris_spawn(_s, args...) _debris_spawn((_s), &(debris_params){ args })
cresp_check(debris) _debris_spawn(struct scene *scene, const debris_params *params);

/**
 * debris_release() - set the fragments around an impact loose
 * @d:      the debris
 * @at:     the impact point, world frame
 * @radius: fragments centered within @radius of @at are released
 * @speed:  speed away from @at given to a fragment right at it; it
 *          falls off linearly to zero at @radius
 *
 * Each dormant fragment within reach swaps its static geometry for a
 * dynamic body of its mass, thrown away from @at. Released fragments
 * are then the physics simulation's: they tumble, come to rest and
 * sleep like any dynamic body. Fragments released earlier are left
 * alone.
 *
 * Return: the number of fragments released by this call, or CERR_NOMEM
 * if a fragment's body couldn't be made; that fragment is left without
 * collision and counts as released.
 */
cres_check(int) debris_release(debris *d, const vec3 at, float radius, float speed);

/**
 * debris_nr_fragments() - the number of fragments
 * @d:      the debris
 *
 * Return: the number of fragment entities in the scene, released or not.
 */
size_t debris_nr_fragments(const debris *d);

/**
 * debris_nr_dormant() - the number of fragments not released yet
 * @d:      the debris
 *
 * Return: the number of fragments still standing in for the prop.
 */
size_t debris_nr_dormant(const debris *d);

/**
 * debris_free() - forget the fragments
 * @d:      the debris, may be nullptr
 *
 * The fragment entities stay in the scene as they are; only the record
 * of them goes.
 */
void debris_free(debris *d);

#endif /* __CLAP_DEBRIS_H__ */
