/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_CLAP_H__
#define __CLAP_CLAP_H__

#include "lut.h"
#include "render.h"

/* Clap build options string */
const char *clap_build_options(void);

struct fps_data;
typedef struct clap_context clap_context;
struct phys;
struct settings;
typedef struct sound_context sound_context;
typedef struct font_context font_context;
typedef struct shader_context shader_context;
typedef struct render_options render_options;

/**
 * struct clap_os_info - static OS information
 * @name:   OS name on native builds or User-Agent on WASM builds
 * @mobile: whether running on a mobile device
 */
typedef struct clap_os_info {
    char    *name;
    bool    mobile;
} clap_os_info;

/**
 * clap_get_os() - get OS information
 * @ctx:    clap_context
 *
 * Get information about the platform/OS where this code is running.
 * Return: struct clap_os_info pointer (nonnull).
 */
clap_os_info *clap_get_os(clap_context *ctx);

/**
 * clap_get_phys() - get the clap's physics handle
 * @ctx:    clap_context
 *
 * If physics engine was initialized, get a physics handle (struct phys *).
 * Return: struct phys pointer or NULL if physics was not initialized.
 */
struct phys *clap_get_phys(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_config() - get clap's config structure
 * @ctx:    clap_context
 *
 * Get clap configuration with which clap_init() was initialized.
 * Return: struct clap_config pointer (nonnull).
 */
struct clap_config *clap_get_config(struct clap_context *ctx) __returns_nonnull __nonnull_params((1));

/**
 * clap_get_messagebus() - get clap's message bus
 * @ctx:    clap_context
 *
 * Return: messagebus pointer (nonnull).
 */
struct messagebus *clap_get_messagebus(struct clap_context *ctx) __returns_nonnull __nonnull_params((1));

/**
 * clap_get_renderer() - get clap's renderer
 * @ctx:    clap_context
 *
 * If graphics was initialized, return graphics renderer's main object.
 * Return: renderer_t pointer or NULL if graphics is not used.
 */
renderer_t *clap_get_renderer(struct clap_context *ctx) __returns_nonnull __nonnull_params((1));

/**
 * clap_get_render_options() - get clap's render options (mutable)
 * @ctx:    clap_context
 *
 * Get the pointer to a live render_options structure, which is directly used
 * in the model rendering pipeline.
 * Return: struct render_options pointer (nonnull).
 */
render_options *clap_get_render_options(struct clap_context *ctx) __returns_nonnull __nonnull_params((1));

/**
 * clap_get_shaders() - get clap's shaders context
 * @ctx:    clap_context
 *
 * If graphics was initialized, get a shader_context object, which basically
 * contains uniform block layout mappings, their binding points, uniform to
 * uniform block mappings etc, see struct shader_context and shader_vars_init()
 * in core/shader.c for details.
 * Return: struct shader_context pointer or NULL if graphics is not used.
 */
shader_context *clap_get_shaders(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_ui() - get clap's UI handle
 * @ctx:    clap_context
 *
 * Currently, returns a UI context object regardless (XXX) of whether graphics
 * or UI were initialized.
 * Return: struct ui pointer (nonnull).
 */
struct ui *clap_get_ui(clap_context *ctx) __returns_nonnull __nonnull_params((1));

/**
 * clap_get_font() - get clap's font handle
 * @ctx:    clap_context
 *
 * If font subsystem was initialized, return its handle object.
 * Return: struct font_context pointer or NULL if fonts are not used.
 */
font_context *clap_get_font(clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_sound() - get clap's sound context
 * @ctx:    clap_context
 *
 * If sound subsystem was initialized, return its context handle.
 * Return: struct sound_context pointer or NULL if sound is not used.
 */
sound_context *clap_get_sound(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_settings() - get clap's settings context
 * @ctx:    clap_context
 *
 * If settings subsystem was initialized, return its context handle.
 * Return: struct settings pointer or NULL if settings are not used.
 */
struct settings *clap_get_settings(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_current_timespec() - get current time in timespec form
 * @ctx:    clap_context
 *
 * Current time is sampled at the beginning of the current frame, this
 * function returns this timestamp as timespec.
 * TODO: fall back to a direct clock_gettime() when called outside of
 * frame code path. 
 * Context: inside the frame function path, otherwise the result is
 * undefined.
 * Return: current time in struct timespec.
 */
struct timespec clap_get_current_timespec(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_current_time() - get current time
 * @ctx:    clap_context
 *
 * Current time is sampled at the beginning of the current frame, this
 * function returns this timestamp as a floating point number of seconds.
 * Context: inside the frame function path, otherwise the result is
 * undefined.
 * Return: current time as double.
 */
double clap_get_current_time(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_is_paused() - tell if clap is paused
 * @ctx:    clap_context
 *
 * This is a more generalized version of "is UI modality on?", which signals
 * interested subsystem to act differently, suspend themselves, etc.
 *
 * Return: true if paused, false otherwise
 */
bool clap_is_paused(clap_context *ctx);

typedef void (*clap_timer_fn)(void *data);
typedef struct clap_timer clap_timer;
cresp_ret(clap_timer);

/**
 * clap_timer_set() - create or arm a timer
 * @ctx:    clap_context
 * @dt:     delta time from the beginning of the current frame
 * @timer:  timer object to arm or NULL to create a new one
 * @fn:     function to call when the timer goes off
 * @data:   callback data to pass to the timer function
 *
 * * if @timer is NULL, create a new timer and set it to call @fn after @dt
 * seconds from the beginning of the current frame
 * * if @timer is non-NULL, re-arm it
 * Context: inside the frame path.
 * Return:
 * * timer object on success
 * * CERR_NOMEM on allocation failure
 * * CERR_INVALID_ARGUMENTS if @dt is negative or @fn is NULL
 */
 cresp(clap_timer) clap_timer_set(clap_context *ctx, double dt, clap_timer *timer,
                                 clap_timer_fn fn, void *data) __nonnull_params((1));

/**
 * clap_timer_cancel() - cancel a timer
 * @ctx:    clap_context
 * @timer:  timer object to cancel
 *
 * Cancel a timer.
 * Context: inside the frame path.
 */
void clap_timer_cancel(clap_context *ctx, clap_timer *timer) __nonnull_params((1, 2));

/**
 * clap_get_fps_delta() - get time since the previous frame
 * @ctx:    clap_context
 *
 * Get time delta since the previous frame as timespec.
 * Context: inside the frame path.
 * Return: time delta since the previous frame as timespec.
 */
struct timespec clap_get_fps_delta(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_fps_fine() - get the (fine) momentary FPS value
 * @ctx:    clap_context
 *
 * Obtain current FPS value calculated from the time delta between frames.
 * Context: inside the frame path.
 * Return: FPS value as a number of frames.
 */
unsigned long clap_get_fps_fine(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_get_fps_coarse() - get the (coarse) momentary FPS value
 * @ctx:    clap_context
 *
 * Obtain current FPS value calculated from the number of frames that ran in
 * the previous wall clock second.
 * Context: inside the frame path.
 * Return: FPS value as a number of frames.
 */
unsigned long clap_get_fps_coarse(struct clap_context *ctx) __nonnull_params((1));

/**
 * clap_lut_list() - get clap's LUT list head
 * @ctx:    clap_context
 *
 * Get the clap_context's internal LUT list head. TODO: phase out.
 * Return: LUT list head pointer.
 */
struct list *clap_lut_list(clap_context *ctx) __nonnull_params((1));

/**
 * clap_lut_find() - find LUT object by name
 * @ctx:    clap_context
 * @name:   LUT name string
 *
 * Return:
 * * struct lut pointer on success
 * * CERR_NOT_FOUND on failure to find LUT
 */
cresp(lut) clap_lut_find(clap_context *ctx, const char *name) __nonnull_params((1, 2));

/**
 * clap_set_lighting_lut() - set current color grading LUT
 * @ctx:    clap_context
 * @name:   LUT name string
 *
 * Set the color grading LUT object used by the model rendering pipeline.
 * Return:
 * * CERR_OK on success
 * * CERR_NOT_FOUND on failure to find LUT
 */
cerr clap_set_lighting_lut(clap_context *ctx, const char *name) __nonnull_params((1, 2));

/**
 * struct clap_config - engine configuration for clap_init()
 * @debug:              enable debug functionality; currently used in the logger
 *                      initialization
 * @quiet:              limit logger's verbosity level to normal, warning and
 *                      error messages
 * @input:              initialize input subsystem
 * @font:               initialize font rendering via FreeType
 * @sound:              initialize sound
 * @phys:               initialize physics engine for collision and dynamics simulation
 * @graphics:           initialize graphics: display, window, rendering backend, default
 *                      1x1 pixel textures, debug UI via ImGui (if @input is also enabled),
 *                      generate color grading LUT textures
 * @ui:                 initialize clap's own UI subsystem
 * @settings:           initialize settings subsystem and load existing settings
 * @title:              application/window title
 * @base_url:           base URL/path where librarian will look for files
 * @default_font_name:  default font for clap's UI
 * @width:              initial window width
 * @height:             initial window height
 * @early_init:         optional early init callback, called after messagebus_init(); allowed to faila
 * @frame_cb:           callback to run every frame after model updates, but before rendering
 * @resize_cb:          window resize callback
 * @callback_data:      data to be passed into @frame_cb and @resize_cb
 * @settings_cb:        callback to run on settings load
 * @settings_cb_data:   data to be passed into the @settings_cb
 * @lut_presets:        a LUT_MAX-terminated array of lut_preset constants to generate on
 *                      initialization
 */
struct clap_config {
    unsigned long   debug       : 1,
                    quiet       : 1,
                    input       : 1,
                    font        : 1,
                    sound       : 1,
                    phys        : 1,
                    graphics    : 1,
                    ui          : 1,
                    settings    : 1;
    const char      *title;
    const char      *base_url;
    const char      *default_font_name;
    unsigned int    width;
    unsigned int    height;
    cerr            (*early_init)(clap_context *ctx, void *data);
    void            (*frame_cb)(void *data);
    void            (*resize_cb)(void *data, int width, int height);
    void            *callback_data;
    void            (*settings_cb)(struct settings *rs, void *data);
    void            *settings_cb_data;
    lut_preset      *lut_presets;
};

cresp_struct_ret(clap_context);

/**
 * clap_init() - initialize clap engine
 * @cfg:    struct clap_config with initialization parameters
 * @argc:   command line argument count
 * @argv:   command line argument array
 * @envp:   environment variable array
 *
 * Initialize the engine with subsystems enabled in @cfg, see struct clap_config.
 * Return: clap_context pointer on success or a CERR code on error.
 */
cresp_check(clap_context) clap_init(struct clap_config *cfg, int argc, char **argv, char **envp);

/**
 * clap_done() - deinitialize clap engine
 * @ctx:    clap_context pointer returned from clap_init()
 * @status: exit code
 *
 * Deinitialize subsystems enabled by clap_init().
 */
void clap_done(struct clap_context *ctx, int status) __nonnull_params((1));

/**
 * clap_restart() - restart the running clap process
 * @ctx:    clap_context pointer returned from clap_init()
 *
 * Perform a clap shutdown and exec*() the current process.
 * Return:
 * * CERR_INVALID_ARGUMENTS if clap_init() was not provided argc/argv arguments
 * * an integer exec*() syscall return code, if it fails
 * * on success, does not return
 */
cres(int) clap_restart(struct clap_context *ctx) __nonnull_params((1));

#endif /* __CLAP_CLAP_H__ */
