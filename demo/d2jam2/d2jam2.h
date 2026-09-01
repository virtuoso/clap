/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __DEMO_D2JAM2_H__
#define __DEMO_D2JAM2_H__

#include "error.h"
#include "scene.h"

/**
 * struct room_params - a cylindrical room
 * @radius:         room radius
 * @height:         room height
 * @nr_segments:    number of wall segments
 */
typedef struct room_params {
    float           radius;
    float           height;
    unsigned int    nr_segments;
} room_params;

/**
 * make_cave() - generate the cave and add it to the scene
 * @scene:  the scene
 * @params: room parameters, NULL for the default cave
 *
 * The cave is a cylinder with displaced walls and ceiling inside an outer
 * hull, which casts the walls' shadows; its shape is the same on every
 * run. Call before scene_load(), so that the scene file's terrain_clamp
 * entities ground on the cave floor. The cave allocates no lights.
 * Return: the cave entity or an error from the mesh and model constructors
 */
cresp(entity3d) make_cave(struct scene *scene, const room_params *params);

/**
 * noisy_mesh() - fill the cave with pillars, crystals and the corridor
 * @scene:  the scene, with the cave and the scene file already in it
 * @seed:   the crystals' layout seed
 *
 * The crystals draw their places and shapes from a stream seeded with
 * @seed. Each crystal takes a light: call after scene_load(), so that the
 * scene file's lights, the shadow casting headlamp in slot 0 among them,
 * come first.
 * Return: CERR_OK or the error of the stream constructor
 */
cerr noisy_mesh(struct scene *scene, uint64_t seed);

/**
 * noisy_mesh_done() - release the generators' resources
 *
 * Frees the GPU side of the pixel textures shared by the generated models.
 */
void noisy_mesh_done(void);

#ifdef D2JAM2_FOG_PARTICLES
/**
 * fog_init() - fill the cave with fog particles
 * @scene:  the scene, with the cave already in it
 *
 * A particle system of large transparent quads shaded with the material
 * level 3D fog, sampling the engine's global noise.
 * Return: CERR_OK or an error from the particle system constructor
 */
cerr fog_init(struct scene *scene);

/**
 * fog_done() - drop the fog particle system
 */
void fog_done(void);
#else
static inline cerr fog_init(struct scene *scene) { return CERR_OK; }
static inline void fog_done(void) {}
#endif /* !D2JAM2_FOG_PARTICLES */

#endif /* __DEMO_D2JAM2_H__ */
