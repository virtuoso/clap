/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_MATERIAL_H__
#define __CLAP_PCG_MATERIAL_H__

#include "pcg/surface.h"

/**
 * struct pcg_material - a finish generated geometry can wear
 * @name:           names the meshes that wear it; owned by whoever made
 *                  the material
 * @uv_mode:        what the texture coordinates of the surfaces wearing
 *                  it must mean
 * @skip_shadow:    casts no shadow
 *
 * The emitting layer only compares material pointers, to batch the
 * surfaces that wear the same one, and checks the UV contract. Materials
 * outlive every sink that refers to them.
 */
typedef struct pcg_material {
    const char      *name;
    pcg_uv_mode     uv_mode;
    bool            skip_shadow;
} pcg_material;

#endif /* __CLAP_PCG_MATERIAL_H__ */
