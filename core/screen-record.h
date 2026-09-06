/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCREEN_RECORD_H__
#define __CLAP_SCREEN_RECORD_H__

#include "config.h"

struct scene;
struct settings;

#ifndef CONFIG_FINAL

/**
 * screen_record_set_settings() - load the recorder's settings
 * @rs:     the settings, once they are ready
 *
 * Reads the capture directory, "screenshot_dir" in the "debug" group,
 * unless there is one by then, and the cap on the frames a session
 * captures, "capture_max_frames" in the same group; the window's
 * directory dialog keeps a picked directory there.
 */
void screen_record_set_settings(struct settings *rs);

/**
 * screen_record_update() - per-frame screen capture housekeeping
 * @scene:  the scene, for the pipeline to capture from
 *
 * Draws the screen recorder debug window and re-arms an ongoing
 * recording; must run every frame regardless of the debug UI state.
 */
void screen_record_update(struct scene *scene);

/**
 * screen_record_done() - flush and join the capture worker pool
 *
 * Call before render targets are destroyed: workers write out what
 * they hold and release their slots.
 */
void screen_record_done(void);

#else

static inline void screen_record_set_settings(struct settings *rs) {}
static inline void screen_record_update(struct scene *scene) {}
static inline void screen_record_done(void) {}

#endif /* CONFIG_FINAL */

#endif /* __CLAP_SCREEN_RECORD_H__ */
