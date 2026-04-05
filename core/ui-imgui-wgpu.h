/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_IMGUI_WGPU_H__
#define __CLAP_UI_IMGUI_WGPU_H__

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#include "clap.h"

#ifdef __cplusplus
#define EXTERN extern "C"
#else /* !__cplusplus*/
#define EXTERN extern
#endif /* !__cplusplus*/

EXTERN void ui_imgui_wgpu_init(clap_context *clap_ctx);
EXTERN void ui_imgui_wgpu_new_frame(void);
EXTERN void ui_imgui_wgpu_render_draw_data(ImDrawData *draw);

#endif /* __CLAP_UI_IMGUI_WGPU_H__ */
