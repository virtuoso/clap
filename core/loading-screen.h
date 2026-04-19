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

typedef struct loading_screen_options {
    bool    skip_background;
} loading_screen_options;

#define loading_screen_init(_ui, ...) \
    _loading_screen_init((_ui), &(const loading_screen_options) { __VA_ARGS__ })
loading_screen *_loading_screen_init(struct ui *ui, const loading_screen_options *opts);
void loading_screen_done(loading_screen *ls);
void loading_screen_progress(loading_screen *ls, float progress);

#endif /* __CLAP_LOADING_SCREEN_H__ */
