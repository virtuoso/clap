/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_DEBUG_H__
#define __CLAP_UI_DEBUG_H__

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include <stdbool.h>
#include "util.h"
#include "linmath.h"

void __ui_debug_printf(const char *mod, const char *fmt, ...);
#define ui_debug_printf(args...) __ui_debug_printf(MODNAME, ## args)
void ui_show_debug(const char *debug_name);

static inline void ui_show_debug_once(const char *debug_name)
{
    static int done = 0;

    if (!done++)
        ui_show_debug(str_basename(debug_name));
}

struct settings;
void imgui_init(struct clap_context *ctx, void *data, int width, int height);
void imgui_set_settings(struct settings *rs);
void imgui_done(void);
void imgui_render_begin(int width, int height);
void imgui_render(void);

bool ui_igVecTableHeader(const char *str_id, int n);
void ui_igVecRow(float *v, int n, const char *fmt, ...);
bool ui_igMat4x4(mat4x4 m, const char *name);

#endif /* __CLAP_UI_DEBUG_H__ */
