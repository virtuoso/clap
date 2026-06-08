/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_SETTINGS_H__
#define __CLAP_UI_SETTINGS_H__

struct ui;
struct ui_menu_item;

/**
 * ui_settings_open_controls() - open the "Controls" settings screen
 * @ui:     ui context
 * @item:   menu item that triggered the open (unused)
 *
 * Intended to be plugged in as the &ui_menu_item.fn of the Settings ->
 * Controls leaf. Replaces the current menu widget with a dynamically
 * rebuilt list of the present gamepads, "None"/"Any" policies, and a
 * "Use mouse for camera" toggle. Clicks rebuild the widget with updated
 * labels and highlight colors.
 */
void ui_settings_open_controls(struct ui *ui, const struct ui_menu_item *item);

#endif /* __CLAP_UI_SETTINGS_H__ */
