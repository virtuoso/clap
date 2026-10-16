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
 * typedef cave_made_fn - the end of a make_cave()
 * @scene:  the scene
 * @err:    CERR_OK with the cave in the scene, or what kept it out
 * @data:   make_cave()'s @data
 */
typedef void (*cave_made_fn)(struct scene *scene, cerr err, void *data);

/**
 * make_cave() - generate the cave and add it to the scene
 * @scene:  the scene
 * @params: room parameters, NULL for the default cave
 * @done:   called once when the cave is in, or isn't, unless
 *          noisy_mesh_done() comes first
 * @data:   passed to @done
 *
 * The cave is a cylinder with displaced walls and ceiling inside an outer
 * hull, which casts the walls' shadows; its shape is the same on every
 * run. The loader thread (scene_loader_pool()) shapes the mesh, builds its
 * levels of detail and cooks its collider, and scene_update() puts the cave
 * in and calls @done; without a loader thread, all of it happens here,
 * @done included. Start the scene file's load from @done, so that its
 * terrain_clamp entities ground on the cave floor. The cave allocates no
 * lights.
 * Context: the main thread
 * Return: CERR_OK with @done to come; an error from the pixel texture or
 *         the mesh constructor, with no @done
 */
cerr make_cave(struct scene *scene, const room_params *params, cave_made_fn done, void *data);

/**
 * noisy_mesh() - fill the cave with pillars, crystals and the corridor
 * @scene:  the scene, with the cave and the scene file already in it
 * @seed:   the crystals' layout seed
 *
 * The crystals draw their places and shapes from a stream seeded with
 * @seed. Each crystal takes a light: call once the scene file is in, from
 * scene_load_async()'s @done, say, so that its lights, the shadow casting
 * headlamp in slot 0 among them, come first.
 * Return: CERR_OK or the error of the stream constructor
 */
cerr noisy_mesh(struct scene *scene, uint64_t seed);

/**
 * noisy_mesh_done() - release the generators' resources
 *
 * Stops a make_cave() under way, whose @done then doesn't run, and frees
 * the GPU side of the pixel textures shared by the generated models.
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
