/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_MENUS_H__
#define __CLAP_UI_MENUS_H__

#include "error.h"

struct ui;
struct ui_menu_item;
struct ui_widget_builder;

/**
 * struct ui_menu_config - in-game menu configuration for clap_config
 * @enable:     create the in-game menu at clap_init()
 * @menu_root:  root of the menu item tree, NULL for the built-in default
 * @uwb:        widget builder override, NULL for the default; when set, this
 *              takes precedence over @menu_root->uwb
 */
typedef struct ui_menu_config {
    bool                                enable;
    const struct ui_menu_item           *menu_root;
    const struct ui_widget_builder      *uwb;
} ui_menu_config;

/**
 * ui_menus_init() - initialize in-game menu subsystem
 * @ui:     ui context
 * @cfg:    menu configuration, may be NULL
 *
 * Subscribe to input and arm the menu open/close flow. No-op and returns
 * %CERR_OK when @cfg is NULL or @cfg->enable is false.
 * Return: %CERR_OK on success, or an error from subscribe().
 */
cerr ui_menus_init(struct ui *ui, const ui_menu_config *cfg);

/**
 * ui_menus_done() - tear down in-game menu subsystem
 * @ui:     ui context
 */
void ui_menus_done(struct ui *ui);

#endif /* __CLAP_UI_MENUS_H__ */
