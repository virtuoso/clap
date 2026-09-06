/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SCREEN_RECORD_H__
#define __CLAP_SCREEN_RECORD_H__

#include <stdbool.h>
#include "config.h"
#include "error.h"

struct clap_context;
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

/**
 * screen_record_arm() - render the screen pass for capture, or stop
 * @ctx:    clap context
 * @arm:    true to render it off-screen for capture, false to stop
 *
 * What the screen recorder window's "arm" checkbox does: the pipeline is
 * rebuilt at the end of the frame, and screen_record_is_armed() says when
 * captures can start.
 */
void screen_record_arm(struct clap_context *ctx, bool arm);

/**
 * screen_record_is_armed() - does the pipeline render for capture?
 * @ctx:    clap context
 *
 * Return: true once the rebuilt pipeline is in place, false without graphics
 */
bool screen_record_is_armed(struct clap_context *ctx);

/**
 * screen_record_shoot() - capture the frame being made
 * @ctx:    clap context
 *
 * What the window's "shoot" button does: the frame is written into the
 * capture directory once the readback delivers it, unless the session's
 * cap is reached by then.
 * Return: CERR_OK, CERR_NOT_SUPPORTED without graphics, CERR_INVALID_OPERATION
 * until screen_record_is_armed(), CERR_OUT_OF_BOUNDS once the session's cap
 * is reached, or the error starting the writers or arming the readback
 */
cerr screen_record_shoot(struct clap_context *ctx);

/**
 * screen_record_set_dir() - capture into a directory for the rest of the session
 * @ctx:    clap context
 * @dir:    an existing directory
 *
 * Frames handed to the writers from now on go there. Unlike the window's
 * directory dialog, this leaves the settings alone.
 * Return: CERR_OK, CERR_NOT_SUPPORTED without graphics and in browser
 * builds, which capture into /screenshots only, CERR_NAME_TOO_LONG, or
 * the error opening @dir
 */
cerr screen_record_set_dir(struct clap_context *ctx, const char *dir);

/**
 * screen_record_set_max_frames() - cap the frames the session captures
 * @max_frames: the cap
 *
 * Unlike the window's cap field, this leaves the settings alone.
 * Return: CERR_OK, or CERR_INVALID_ARGUMENTS for a cap below 1
 */
cerr screen_record_set_max_frames(int max_frames);

/**
 * screen_record_nr_captured() - frames handed to the writers this session
 *
 * The frames the cap counts: each one's write is under way or over.
 * Return: the number of frames
 */
unsigned int screen_record_nr_captured(void);

#else

static inline void screen_record_set_settings(struct settings *rs) {}
static inline void screen_record_update(struct scene *scene) {}
static inline void screen_record_done(void) {}
static inline void screen_record_arm(struct clap_context *ctx, bool arm) {}
static inline bool screen_record_is_armed(struct clap_context *ctx) { return false; }
static inline cerr screen_record_shoot(struct clap_context *ctx) { return CERR_NOT_SUPPORTED; }
static inline cerr screen_record_set_dir(struct clap_context *ctx, const char *dir) { return CERR_NOT_SUPPORTED; }
static inline cerr screen_record_set_max_frames(int max_frames) { return CERR_NOT_SUPPORTED; }
static inline unsigned int screen_record_nr_captured(void) { return 0; }

#endif /* CONFIG_FINAL */

#endif /* __CLAP_SCREEN_RECORD_H__ */
