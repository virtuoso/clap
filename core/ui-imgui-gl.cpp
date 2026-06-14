// SPDX-License-Identifier: Apache-2.0

// imgui_impl_opengl3.h has C++ in it, so it can't be included directly into
// ui-imgui.c; wrap its backend calls in C-callable functions instead.

#include "ui-imgui-gl.h"
#include "imgui_impl_opengl3.h"

void ui_imgui_opengl_init(const char *glsl_version)     { ImGui_ImplOpenGL3_Init(glsl_version); }
void ui_imgui_opengl_new_frame(void)                    { ImGui_ImplOpenGL3_NewFrame(); }
void ui_imgui_opengl_render_draw_data(ImDrawData *draw) { ImGui_ImplOpenGL3_RenderDrawData(draw); }
void ui_imgui_opengl_shutdown(void)                     { ImGui_ImplOpenGL3_Shutdown(); }
