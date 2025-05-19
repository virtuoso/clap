/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_DEBUG_H__
#define __CLAP_UI_DEBUG_H__

#include <stdbool.h>
#include "clap.h"
#include "error.h"
#include "util.h"
#include "linmath.h"

struct settings;
struct ui;

#ifndef CONFIG_FINAL

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

/* cimgui doesn't export IM_COL32() */
#ifdef IMGUI_USE_BGRA_PACKED_COLOR
#define IM_COL32(R,G,B,A) (((A)<<24) | ((R)<<16) | ((G)<<8) | (B))
#else
#define IM_COL32(R,G,B,A) (((A)<<24) | ((B)<<16) | ((G)<<8) | (R))
#endif

void ui_debug_update(struct ui *ui);
void __ui_debug_printf(const char *mod, const char *fmt, ...) __printf(2, 3);
#define ui_debug_printf(args...) __ui_debug_printf(MODNAME, ## args)
void ui_show_debug(const char *debug_name);
struct ui_widget *ui_debug_menu(struct ui *ui);
cerr ui_debug_init(struct ui *ui);
void ui_debug_done(struct ui *ui);

static inline void ui_show_debug_once(const char *debug_name)
{
    static int done = 0;

    if (!done++)
        ui_show_debug(str_basename(debug_name));
}

bool __ui_mouse_event_propagate(void);
void imgui_init(struct clap_context *ctx, void *data, int width, int height);
void imgui_set_settings(struct settings *rs);
void imgui_done(void);
void imgui_render_begin(int width, int height);
void imgui_render(void);

enum debug_modules {
    DEBUG_CAMERA_SELECTOR,
    DEBUG_PIPELINE_PASSES,
    DEBUG_PIPELINE_SELECTOR,
    DEBUG_SCENE_PARAMETERS,
    DEBUG_FRUSTUM_VIEW,
    DEBUG_LIGHT,
    DEBUG_CHARACTERS,
    DEBUG_CHARACTER_MOTION,
    DEBUG_FRAME_PROFILER,
    DEBUG_RENDERER,
    DEBUG_MODULES_MAX,
};

typedef struct debug_module {
    const char  *name;
    bool        display;    /* display debug UI */
    bool        unfolded;   /* UI collapsed */
    bool        open;       /* should UI stay open*/
    bool        prev;       /* previous display value */
} debug_module;

void ui_toggle_debug_selector(void);
void ui_debug_selector(void);
void ui_debug_set_settings(struct settings *rs);
debug_module *ui_debug_module(enum debug_modules mod);
debug_module *ui_igBegin_name(enum debug_modules mod, ImGuiWindowFlags flags,
                              const char *fmt, ...) __printf(3, 4);

static inline debug_module *ui_igBegin(enum debug_modules mod, ImGuiWindowFlags flags)
{
    return ui_igBegin_name(mod, flags, NULL);
}

void ui_igEnd(enum debug_modules mod);

void ui_igCheckbox(bool *v, const char *label, ...) __printf(2, 3);
void ui_igSliderFloat(float *v, float min, float max, const char *fmt, ImGuiSliderFlags flags,
                      const char *label, ...) __printf(6, 7);

bool ui_igTableHeader(const char *str_id, const char **labels, int n);
void ui_igTableRow(const char *key, const char *fmt, ...);
bool ui_igVecTableHeader(const char *str_id, int n);
void ui_igVecRow(float *v, int n, const char *fmt, ...);
bool ui_igMat4x4(mat4x4 m, const char *name);

#else

static inline void ui_debug_update(struct ui *ui) {}
static inline void __ui_debug_printf(const char *mod, const char *fmt, ...) {}
#define ui_debug_printf(args...) __ui_debug_printf(MODNAME, ## args)
static inline void ui_show_debug(const char *debug_name) {}
static inline void ui_show_debug_once(const char *debug_name) {}
static inline struct ui_widget *ui_debug_menu(struct ui *ui) { return NULL; }
static inline cerr ui_debug_init(struct ui *ui) { return CERR_OK; }
static inline void ui_debug_done(struct ui *ui) {}

static inline void ui_debug_selector(void) {}
static inline void ui_toggle_debug_selector(void) {}
static inline void ui_debug_set_settings(struct settings *rs) {}

static inline bool __ui_mouse_event_propagate(void) { return true; }
static inline void imgui_init(struct clap_context *ctx, void *data, int width, int height) {}
static inline void imgui_set_settings(struct settings *rs) {}
static inline void imgui_done(void) {}
static inline void imgui_render_begin(int width, int height) {}
static inline void imgui_render(void) {}


#endif /* CONFIG_FINAL */

#endif /* __CLAP_UI_DEBUG_H__ */
