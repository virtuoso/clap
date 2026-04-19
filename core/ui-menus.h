/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_MENUS_H__
#define __CLAP_UI_MENUS_H__

#include "error.h"

struct ui;
struct ui_menu_item;
struct ui_widget_builder;
struct clap_context;

/**
 * enum ui_state - high-level UI state machine
 * @UI_ST_START_MENU:   start menu is active, clap is paused
 * @UI_ST_LOADING:      "Start Game" selected; loading callback is in flight
 * @UI_ST_RUNNING:      gameplay; in-game menu is toggleable
 */
typedef enum ui_state {
    UI_ST_START_MENU,
    UI_ST_LOADING,
    UI_ST_RUNNING,
} ui_state;

/**
 * struct ui_menu_config - menu configuration for clap_config
 * @enable:             create the menu at clap_init()
 * @menu_root:          root of the menu item tree, NULL for the built-in default
 * @uwb:                widget builder override, NULL for the default; when set,
 *                      takes precedence over @menu_root->uwb
 * @loading_cb:         start menu only: callback fired on "Start Game" click
 * @loading_cb_data:    data pointer passed to @loading_cb
 */
typedef struct ui_menu_config {
    bool                                enable;
    const struct ui_menu_item           *menu_root;
    const struct ui_widget_builder      *uwb;
    void                                (*loading_cb)(struct clap_context *ctx, void *data);
    void                                *loading_cb_data;
} ui_menu_config;

/**
 * ui_menus_init() - initialize menu subsystem
 * @ui:             ui context
 * @in_game_cfg:    in-game menu configuration, may be NULL
 * @start_cfg:      start menu configuration, may be NULL
 *
 * Subscribe to input and arm the menu open/close flow. When @start_cfg is
 * enabled, the initial state is %UI_ST_START_MENU, the start menu opens, and
 * clap is paused via a modality toggle. Otherwise the initial state is
 * %UI_ST_RUNNING. No-op and returns %CERR_OK if neither config is enabled.
 * Return: %CERR_OK on success, or an error from subscribe().
 */
cerr ui_menus_init(struct ui *ui,
                   const ui_menu_config *in_game_cfg,
                   const ui_menu_config *start_cfg);

/**
 * ui_menus_done() - tear down menu subsystem
 * @ui:     ui context
 */
void ui_menus_done(struct ui *ui);

/**
 * ui_state_get() - read current UI state
 * @ui:     ui context
 */
ui_state ui_state_get(const struct ui *ui);

/**
 * ui_state_set_running() - transition UI state to %UI_ST_RUNNING
 * @ui:     ui context
 *
 * Intended to be called by a user loading_cb once loading is finished.
 * Unpauses clap (via modality toggle) and allows the in-game menu to
 * respond to input. Idempotent when already running.
 */
void ui_state_set_running(struct ui *ui);

#endif /* __CLAP_UI_MENUS_H__ */
