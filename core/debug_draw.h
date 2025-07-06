/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_DEBUG_DRAW_H__
#define __CLAP_DEBUG_DRAW_H__

#include "error.h"

#ifndef CONFIG_FINAL
cerr debug_draw_install(struct camera *cam);
#else
static inline cerr debug_draw_install(struct camera *cam) { return CERR_OK; }
#endif /* CONFIG_FINAL */

#endif /* __CLAP_DEBUG_DRAW_H__ */
