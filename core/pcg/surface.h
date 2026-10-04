/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_SURFACE_H__
#define __CLAP_PCG_SURFACE_H__

#include <stddef.h>
#include <stdint.h>

/**
 * enum pcg_role - what a surface is, for picking its default finish
 * @PCG_ROLE_STRUCTURE: load-bearing mass: slabs, soffits, flights, fascia
 * @PCG_ROLE_FLOOR:     walked finish: decks, aprons
 * @PCG_ROLE_CLADDING:  wall and column facing
 * @PCG_ROLE_RAIL:      balustrade members
 * @PCG_ROLE_METAL:     frames, mullions, coffers
 * @PCG_ROLE_GLASS:     panes
 * @PCG_ROLE_CARPET:    loose floor finish
 * @PCG_ROLE_FIXTURE:   light fittings
 * @PCG_ROLE_DIFFUSER:  glowing ceiling sheets
 * @PCG_ROLE_SIGN:      sign plates
 * @PCG_ROLE_FURNITURE: furniture shells
 * @PCG_ROLE_SCREEN:    computer screens
 * @PCG_ROLE_BOARD:     pinboards
 * @PCG_ROLE_NR:        number of roles, the palette's size
 *
 * A palette maps every role to a material, so remapping one role turns
 * every surface of that role over to another finish at once.
 */
typedef enum pcg_role {
    PCG_ROLE_STRUCTURE = 0,
    PCG_ROLE_FLOOR,
    PCG_ROLE_CLADDING,
    PCG_ROLE_RAIL,
    PCG_ROLE_METAL,
    PCG_ROLE_GLASS,
    PCG_ROLE_CARPET,
    PCG_ROLE_FIXTURE,
    PCG_ROLE_DIFFUSER,
    PCG_ROLE_SIGN,
    PCG_ROLE_FURNITURE,
    PCG_ROLE_SCREEN,
    PCG_ROLE_BOARD,
    PCG_ROLE_NR
} pcg_role;

/**
 * enum pcg_collide - how a surface takes part in the world
 * @PCG_COLLIDE_NONE:   render only; no physics, so the navmesh ignores it too
 * @PCG_COLLIDE_SOLID:  static trimesh, bakes into the navmesh
 * @PCG_COLLIDE_HAZARD: static trimesh that the navmesh treats as a hazard
 * @PCG_COLLIDE_NR:     number of collide classes
 */
typedef enum pcg_collide {
    PCG_COLLIDE_NONE = 0,
    PCG_COLLIDE_SOLID,
    PCG_COLLIDE_HAZARD,
    PCG_COLLIDE_NR
} pcg_collide;

/**
 * enum pcg_uv_mode - what an emitter's texture coordinates mean
 * @PCG_UV_METERS:  planar coordinates in meters; the material's UV factor
 *                  is its repeats per meter
 * @PCG_UV_UNIT:    0..1 over a decal or an atlas cell; the material's UV
 *                  factor is 1
 * @PCG_UV_NR:      number of UV modes
 */
typedef enum pcg_uv_mode {
    PCG_UV_METERS = 0,
    PCG_UV_UNIT,
    PCG_UV_NR
} pcg_uv_mode;

/* a finish that generated geometry can wear, see pcg/material.h */
typedef struct pcg_material pcg_material;

/**
 * struct pcg_surface - one routable surface group of an element
 * @mat:        explicit finish; nullptr: the palette's @role entry
 * @role:       &enum pcg_role, set by the element's _params_init()
 * @collide:    &enum pcg_collide
 * @uv_swap:    swap U and V, a quarter turn of a directional texture
 *              (plank grain)
 * @skip:       leave this group out
 *
 * Resolving a surface takes @mat, or the palette's @role entry without
 * it; with neither, emitting the surface is an error, never a fallback.
 */
typedef struct pcg_surface {
    const pcg_material  *mat;
    uint8_t             role;
    uint8_t             collide;
    bool                uv_swap;
    bool                skip;
} pcg_surface;

/**
 * struct pcg_palette - the default finish of every role
 * @mat:    material per &enum pcg_role; nullptr: none
 */
typedef struct pcg_palette {
    const pcg_material  *mat[PCG_ROLE_NR];
} pcg_palette;

/**
 * pcg_surfaces_set_role() - turn a set of surfaces over to one role
 * @surf:   surfaces, typically an element's whole surf[] array
 * @nr:     number of surfaces
 * @role:   the role they all take
 */
static inline void pcg_surfaces_set_role(pcg_surface *surf, size_t nr, pcg_role role)
{
    for (size_t i = 0; i < nr; i++)     surf[i].role = role;
}

#endif /* __CLAP_PCG_SURFACE_H__ */
