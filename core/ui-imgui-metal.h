/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_IMGUI_METAL_H__
#define __CLAP_UI_IMGUI_METAL_H__

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

typedef struct clap_context clap_context;

void ui_imgui_metal_init(clap_context *clap_ctx);
void ui_imgui_metal_new_frame();
void ui_imgui_metal_render_draw_data(ImDrawData *draw);
void ui_imgui_metal_shutdown();

#endif /* __CLAP_UI_IMGUI_METAL_H__ */
