/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_IMGUI_GL_H__
#define __CLAP_UI_IMGUI_GL_H__

#include "common.h"

/*
 * C++ consumers get the real imgui.h
 */
#ifdef __cplusplus
# include "imgui.h"
#else /* !__cplusplus */
# define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
# include "cimgui.h"
#endif /* !__cplusplus */

#ifdef __cplusplus
# define EXTERN extern "C"
# undef  IMGUI_IMPL_API
# define IMGUI_IMPL_API extern "C"
#else /* !__cplusplus*/
# define EXTERN extern
#endif /* !__cplusplus*/

#ifdef CONFIG_RENDERER_OPENGL
EXTERN void ui_imgui_opengl_init(const char *glsl_version);
EXTERN void ui_imgui_opengl_new_frame(void);
EXTERN void ui_imgui_opengl_render_draw_data(ImDrawData *draw);
EXTERN void ui_imgui_opengl_shutdown(void);
#else /* !CONFIG_RENDERER_OPENGL */
static inline void ui_imgui_opengl_init(const char *glsl_version)       {}
static inline void ui_imgui_opengl_new_frame(void)                      {}
static inline void ui_imgui_opengl_render_draw_data(ImDrawData *draw)   {}
static inline void ui_imgui_opengl_shutdown(void)                       {}
#endif /* !CONFIG_RENDERER_OPENGL */

#endif /* __CLAP_UI_IMGUI_GL_H__ */
