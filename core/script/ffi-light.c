// SPDX-License-Identifier: Apache-2.0
//
// FFI over the scene's lights: enumerate them, move/recolour them and toggle
// their type between frames while an agent debugs lighting or shadows. Unlike
// entities, all lights live in one struct light as parallel arrays indexed by
// slot, so a Light handle carries the slot index (encoded as idx + 1, so a
// zeroed/undefined handle reads back as invalid rather than as light 0). The
// uniform array pokes are generated from the FFI_LIGHT_* markers below; the ones
// with a twist (direction is stored negated, cutoff is radians, radius is
// derived) are written out.

#include "clap.h"
#include "light.h"
#include "scene.h"
#include "script/ffi.h"

CLAP_SCRIPT_TYPE("type Light* = struct { _h: uint }")

/* The scene's light array, and the slot a Light handle refers to (idx + 1 packed). */
static struct light *ffi_light(UmkaStackSlot *result)
{
    struct scene *scene = clap_get_scene(ffi_ctx(result));
    return scene ? &scene->light : NULL;
}

static int ffi_light_idx(UmkaStackSlot *params, int slot)
{
    return (int)(intptr_t)ffi_ptr(params, slot) - 1;
}

/*
 * Arrayed field accessors: like the FFI_*_VEC3/BOOL macros in ffi.h but indexing
 * one slot of the scene's shared struct light. The @field is the parallel array
 * (pos/color/attenuation are float[3*LIGHTS_MAX], is_dir is int[LIGHTS_MAX]); an
 * invalid slot reads zero / ignores the write. The generator reads the proto for
 * the Umka binding, exactly as for the pointer-handle macros.
 */
#define FFI_LIGHT_GET_VEC3(cfunc, proto, field)                                     \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        struct light *l = ffi_light(result);                                        \
        int idx = ffi_light_idx(params, 0);                                         \
        if (l && light_is_valid(l, idx))                                            \
            ffi_ret_vec3(params, result, l->field[idx * 3],                         \
                         l->field[idx * 3 + 1], l->field[idx * 3 + 2]);             \
        else                                                                        \
            ffi_ret_vec3(params, result, 0, 0, 0);                                  \
    }

#define FFI_LIGHT_SET_VEC3(cfunc, proto, field)                                     \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        (void)result;                                                               \
        struct light *l = ffi_light(result);                                        \
        int idx = ffi_light_idx(params, 0);                                         \
        if (l && light_is_valid(l, idx)) {                                          \
            l->field[idx * 3]     = ffi_real(params, 1);                            \
            l->field[idx * 3 + 1] = ffi_real(params, 2);                            \
            l->field[idx * 3 + 2] = ffi_real(params, 3);                            \
        }                                                                           \
    }

#define FFI_LIGHT_GET_BOOL(cfunc, proto, field)                                     \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        struct light *l = ffi_light(result);                                        \
        int idx = ffi_light_idx(params, 0);                                         \
        ffi_ret_bool(params, result,                                                \
                     (l && light_is_valid(l, idx)) ? !!l->field[idx] : false);      \
    }

#define FFI_LIGHT_SET_BOOL(cfunc, proto, field)                                     \
    void cfunc(UmkaStackSlot *params, UmkaStackSlot *result)                        \
    {                                                                               \
        (void)result;                                                               \
        struct light *l = ffi_light(result);                                        \
        int idx = ffi_light_idx(params, 0);                                         \
        if (l && light_is_valid(l, idx))                                            \
            l->field[idx] = ffi_bool(params, 1);                                    \
    }

/* Enumeration and allocation */

/* Highest allocated slot + 1; iterate 0..count-1 and skip !valid() slots. */
CLAP_SCRIPT_FN(ffi_light_count, "fn light_count*(): int")
void ffi_light_count(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    ffi_ret_int(params, result, l ? l->nr_lights : 0);
}

CLAP_SCRIPT_FN(ffi_get_light, "fn get_light*(idx: int): Light")
void ffi_get_light(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    int idx = ffi_int(params, 0);
    ffi_ret_ptr(params, result,
                (l && light_is_valid(l, idx)) ? (void *)(intptr_t)(idx + 1) : NULL);
}

/* Allocate a fresh light slot (directional, black, at the origin) to set up. */
CLAP_SCRIPT_FN(ffi_add_light, "fn add_light*(): Light")
void ffi_add_light(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    if (!l) {
        ffi_ret_ptr(params, result, NULL);
        return;
    }

    cres(int) r = light_get(l);
    ffi_ret_ptr(params, result, IS_CERR(r) ? NULL : (void *)(intptr_t)(r.val + 1));
}

/* Per-light properties */

CLAP_SCRIPT_METHOD(ffi_light_valid, "fn (l: Light) valid*(): bool")
void ffi_light_valid(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    ffi_ret_bool(params, result, l && light_is_valid(l, ffi_light_idx(params, 0)));
}

FFI_LIGHT_GET_VEC3(ffi_light_position, "fn (l: Light) position*(): Vec3", pos)
FFI_LIGHT_SET_VEC3(ffi_light_set_position, "fn (l: Light) set_position*(x, y, z: real)", pos)
FFI_LIGHT_GET_VEC3(ffi_light_color, "fn (l: Light) color*(): Vec3", color)
FFI_LIGHT_SET_VEC3(ffi_light_set_color, "fn (l: Light) set_color*(r, g, b: real)", color)
FFI_LIGHT_GET_VEC3(ffi_light_attenuation, "fn (l: Light) attenuation*(): Vec3", attenuation)
FFI_LIGHT_SET_VEC3(ffi_light_set_attenuation, "fn (l: Light) set_attenuation*(a, b, c: real)", attenuation)
FFI_LIGHT_GET_BOOL(ffi_light_is_directional, "fn (l: Light) is_directional*(): bool", is_dir)
FFI_LIGHT_SET_BOOL(ffi_light_set_directional, "fn (l: Light) set_directional*(v: bool)", is_dir)

/* Direction is stored negated (tip points where the light shines); hide that. */
CLAP_SCRIPT_METHOD(ffi_light_direction, "fn (l: Light) direction*(): Vec3")
void ffi_light_direction(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    int idx = ffi_light_idx(params, 0);
    if (l && light_is_valid(l, idx))
        ffi_ret_vec3(params, result, -l->dir[idx * 3], -l->dir[idx * 3 + 1], -l->dir[idx * 3 + 2]);
    else    ffi_ret_vec3(params, result, 0, 0, 0);
}

CLAP_SCRIPT_METHOD(ffi_light_set_direction, "fn (l: Light) set_direction*(x, y, z: real)")
void ffi_light_set_direction(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    struct light *l = ffi_light(result);
    if (l)
        light_set_direction(l, ffi_light_idx(params, 0),
                            (vec3){ ffi_real(params, 1), ffi_real(params, 2), ffi_real(params, 3) });
}

/* Spotlight cone half-angle, in degrees for the script (radians in the array). */
CLAP_SCRIPT_METHOD(ffi_light_cutoff, "fn (l: Light) cutoff*(): real")
void ffi_light_cutoff(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    int idx = ffi_light_idx(params, 0);
    ffi_ret_real(params, result, (l && light_is_valid(l, idx)) ? to_degrees(l->cutoff[idx]) : 0.0);
}

CLAP_SCRIPT_METHOD(ffi_light_set_cutoff, "fn (l: Light) set_cutoff*(deg: real)")
void ffi_light_set_cutoff(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    struct light *l = ffi_light(result);
    if (l)  light_set_cutoff(l, ffi_light_idx(params, 0), to_radians(ffi_real(params, 1)));
}

CLAP_SCRIPT_METHOD(ffi_light_is_spotlight, "fn (l: Light) is_spotlight*(): bool")
void ffi_light_is_spotlight(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    ffi_ret_bool(params, result, l && light_is_spotlight(l, ffi_light_idx(params, 0)));
}

/* Effective illumination radius, derived from colour and attenuation. */
CLAP_SCRIPT_METHOD(ffi_light_radius, "fn (l: Light) radius*(): real")
void ffi_light_radius(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    int idx = ffi_light_idx(params, 0);
    ffi_ret_real(params, result, (l && light_is_valid(l, idx)) ? light_get_radius(l, idx) : 0.0);
}

/* Scene-wide lighting (not per-light) */

CLAP_SCRIPT_FN(ffi_ambient, "fn ambient*(): Vec3")
void ffi_ambient(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    if (l)  ffi_ret_vec3(params, result, l->ambient[0], l->ambient[1], l->ambient[2]);
    else    ffi_ret_vec3(params, result, 0, 0, 0);
}

CLAP_SCRIPT_FN(ffi_set_ambient, "fn set_ambient*(r, g, b: real)")
void ffi_set_ambient(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    struct light *l = ffi_light(result);
    if (l)  light_set_ambient(l, (vec3){ ffi_real(params, 0), ffi_real(params, 1), ffi_real(params, 2) });
}

CLAP_SCRIPT_FN(ffi_shadow_tint, "fn shadow_tint*(): Vec3")
void ffi_shadow_tint(UmkaStackSlot *params, UmkaStackSlot *result)
{
    struct light *l = ffi_light(result);
    if (l)  ffi_ret_vec3(params, result, l->shadow_tint[0], l->shadow_tint[1], l->shadow_tint[2]);
    else    ffi_ret_vec3(params, result, 0, 0, 0);
}

CLAP_SCRIPT_FN(ffi_set_shadow_tint, "fn set_shadow_tint*(r, g, b: real)")
void ffi_set_shadow_tint(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)result;
    struct light *l = ffi_light(result);
    if (l)  light_set_shadow_tint(l, (vec3){ ffi_real(params, 0), ffi_real(params, 1), ffi_real(params, 2) });
}
