/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_DEBUG_H__
#define __CLAP_UI_DEBUG_H__

void __ui_debug_printf(const char *mod, const char *fmt, ...);
#define ui_debug_printf(args...) __ui_debug_printf(MODNAME, ## args)

#endif /* __CLAP_UI_DEBUG_H__ */
