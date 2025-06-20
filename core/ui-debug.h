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

bool __ui_mouse_event_propagate(void);
void imgui_init(struct clap_context *ctx, void *data, int width, int height);
void imgui_set_settings(struct settings *rs);
void imgui_done(void);
void imgui_render_begin(int width, int height);
void imgui_render(void);

enum debug_modules {
    DEBUG_ENTITY_INSPECTOR,
    DEBUG_PIPELINE_PASSES,
    DEBUG_PIPELINE_SELECTOR,
    DEBUG_SCENE_PARAMETERS,
    DEBUG_FRUSTUM_VIEW,
    DEBUG_LIGHT,
    DEBUG_CHARACTERS,
    DEBUG_CHARACTER_MOTION,
    DEBUG_INPUT,
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

bool ui_igTableHeader(const char *str_id, const char **labels, int n);
void ui_igTableRow(const char *key, const char *fmt, ...);
bool ui_igVecTableHeader(const char *str_id, int n);
void ui_igVecRow(const float *v, int n, const char *fmt, ...);
bool ui_igMat4x4(const mat4x4 m, const char *name);
bool ui_igControlTableHeader(const char *str_id_fmt, const char *longest_label, ...);
bool ui_igCheckbox(const char *label, bool *v);
void ui_igLabel(const char *label);
bool ui_igSliderInt(const char *label, int *v, int min, int max, const char *fmt,
                    ImGuiSliderFlags flags);
bool ui_igSliderFloat(const char *label, float *v, float min, float max, const char *fmt,
                      ImGuiSliderFlags flags);
bool ui_igSliderFloat3(const char *label, float *v, float min, float max, const char *fmt,
                       ImGuiSliderFlags flags);
bool ui_igBeginCombo(const char *label, const char *preview_value, ImGuiComboFlags flags);
void ui_igEndCombo(void);
bool ui_igColorEdit3(const char *label, float *color, ImGuiColorEditFlags flags);

#else

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
