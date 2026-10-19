/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LUT_H__
#define __CLAP_LUT_H__

#include "object.h"
#include "render.h"

typedef enum lut_preset {
    LUT_IDENTITY = 0,
    LUT_ORANGE_BLUE_FILMIC,
    LUT_COMIC_RED,
    LUT_COMIC_GREEN,
    LUT_COMIC_BLUE,
    LUT_SUNSET_WARM,
    LUT_HYPER_SUNSET,
    LUT_GREEN_MATRIX,
    LUT_SCIFI_BLUEGREEN,
    LUT_SCIFI_NEON,
    LUT_SATURATE,
    LUT_DEEP_SEA_ABYSS,
    LUT_BLOODVEIL_CRIMSON,
    LUT_MAD_MAX_BLEACH,
    LUT_TEAL_ORANGE,
    LUT_MAX,
} lut_preset;

extern lut_preset lut_presets_all[LUT_MAX + 1];

DEFINE_REFCLASS_INIT_OPTIONS(lut,
    const char  *name;
    struct list *list;
    renderer_t  *renderer;
);
DECLARE_REFCLASS(lut);

typedef struct lut lut;
struct scene;

/*
 * Per-cell colour mapping for lut_generate_fn(): both @color_in and @color_out
 * are linear RGB (@color_in is HDR-decoded, in [0, HDR_LUT_MAX]).
 */
typedef void (*lut_gen_fn)(vec3 color_in, vec3 color_out, void *data);

cresp(lut) lut_generate(renderer_t *renderer, struct list *list, lut_preset preset, int sz);

/**
 * lut_generate_fn() - build a named LUT by evaluating a callback for every cell
 * @renderer:   renderer that owns the LUT texture
 * @list:       LUT list the new LUT joins
 * @name:       LUT name, copied
 * @fn:         per-cell colour mapping
 * @data:       opaque pointer handed to @fn
 * @exposure:   exposure the LUT is applied with
 * @contrast:   contrast the LUT is applied with
 * @sz:         LUT side; the texture is @sz cubed
 *
 * Like lut_generate(), but the colour transform comes from @fn(@data) rather
 * than a built-in preset, so a caller can drive it from a scripted in->out
 * colour function.
 *
 * Return: the new LUT, or CERR_NOMEM / an error from the texture setup.
 */
cresp(lut) lut_generate_fn(renderer_t *renderer, struct list *list, const char *name,
                           lut_gen_fn fn, void *data, float exposure, float contrast, int sz);
cresp(lut) lut_first(struct list *list);
cresp(lut) lut_next(struct list *list, lut *lut);
cresp(lut) lut_find(struct list *list, const char *name);
cresp(lut) lut_load(struct list *list, const char *name);
void lut_apply(struct scene *scene, lut *lut);
void luts_done(struct list *list);
texture_t *lut_tex(lut *lut);

struct scene;

#ifndef CONFIG_FINAL
void luts_debug(struct scene *scene);
#else
static inline void luts_debug(struct scene *scene) {}
#endif /* CONFIG_FINAL */

#endif /* __CLAP_LUT_H__ */
