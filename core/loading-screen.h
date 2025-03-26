/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_LOADING_SCREEN_H__
#define __CLAP_LOADING_SCREEN_H__

#include "ui.h"

typedef struct loading_screen {
    struct ui           *ui;
    struct ui_element   *background;
    struct ui_widget    *progress;
    struct ui_element   *uie;
    struct ui_element   *uit;
} loading_screen;

#ifndef CONFIG_BROWSER

loading_screen *loading_screen_init(struct ui *ui);
void loading_screen_done(loading_screen *ls);
void loading_screen_progress(loading_screen *ls, float progress);

#else
static inline loading_screen *loading_screen_init(struct ui *ui) { return NULL; }
static inline void loading_screen_done(loading_screen *ls) {}
static inline void loading_screen_progress(loading_screen *ls, float progress) {}
#endif /* CONFIG_BROWSER */

#endif /* __CLAP_LOADING_SCREEN_H__ */
