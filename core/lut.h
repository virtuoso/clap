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

cresp(lut) lut_generate(renderer_t *renderer, struct list *list, lut_preset preset, int sz);
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
