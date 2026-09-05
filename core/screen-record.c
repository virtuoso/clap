// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "display.h"
#include "scene.h"
#include "settings.h"
#include "exr.h"
#include "screen-record.h"
#include "ui-debug.h"
#include "ui-debug-fs.h"
#include "worker.h"

/*
 * BT.709 primaries and D65 white: the capture target holds the scene's
 * linear color, which only the OETF encode converts to other primaries
 */
static const float rec709_chromaticities[] = {
    0.640f, 0.330f,
    0.300f, 0.600f,
    0.150f, 0.060f,
    0.3127f, 0.3290f
};

static ui_debug_fs_dialog screenshot_dialog;
static char screenshot_dir[PATH_MAX];
static struct settings *settings;
static struct JsonNode *debug_group;
static bool screen_record;
/* GL delivers rows bottom-up; EXR scanlines run top-down */
static bool capture_flip;

/**
 * directory_selected() - take the directory picked in the dialog
 * @cwd:    the dialog's directory
 * @sel:    the entry picked in it
 * @is_dir: whether the entry is a directory
 * @data:   the dialog's data, the scene
 *
 * The settings remember the directory for the next run.
 */
static void directory_selected(const char *cwd, const char *sel, bool is_dir, void *data)
{
    if (!is_dir) {
        err("%s/%s is not a directory\n", cwd, sel);
        return;
    }

    dbg("selected directory: %s/%s\n", cwd, sel);
    path_join(screenshot_dir, sizeof(screenshot_dir), cwd, sel);
    if (settings && debug_group)    settings_set_string(settings, debug_group, "screenshot_dir", screenshot_dir);
}

/**
 * directory_open() - open the dialog that picks the capture directory
 * @s:      the scene
 *
 * The dialog starts in the capture directory, or while there is none, in
 * the working directory, $HOME or a temporary one.
 * Return: CERR_OK, or the error opening the dialog
 */
static cerr directory_open(struct scene *s)
{
    if (!*screenshot_dir) {
        char cwd[PATH_MAX], *src = nullptr;
        auto err = fs_get_cwd(&fs_ops_posix, cwd);
        if (!IS_CERR(err))  src = cwd;
        if (!src || !*src)  src = getenv("HOME");
        if (!src || !*src)  src = getenv("TMPPREFIX");
        if (!src || !*src)  src = getenv("TMPDIR");
        if (!src || !*src)  src = "";
        path_join(screenshot_dir, sizeof(screenshot_dir), src, "/");
    }

    struct ui_debug_fs_config cfg = {
        .title          = "Select screenshot directory",
        .modal          = true,
        .input_field    = "Directory:",
        .action_label   = "Select",
        .select_mode    = UI_DEBUG_FS_SELECT_DIR,
        .on_accept      = directory_selected,
        .start_dir      = screenshot_dir,
        .data           = s,
    };

    return ui_debug_fs_open(&screenshot_dialog, &cfg, NULL);
}

/**
 * screenshot_dir_load() - take the capture directory from the settings
 *
 * Unless there is one by then: a directory picked or set before the
 * settings load stays.
 */
static void screenshot_dir_load(void)
{
    const char *dir = settings_get_str(settings, debug_group, "screenshot_dir");

    if (dir && !*screenshot_dir)    strncpy(screenshot_dir, dir, sizeof(screenshot_dir) - 1);
}

/**
 * directory_draw() - show the capture directory, with a button to change it
 * @scene:  the scene, for the directory dialog
 *
 * Leaves the cursor on the button's line, for the capture buttons to follow.
 */
static void directory_draw(struct scene *scene)
{
    igText("Output directory: %s", screenshot_dir);

    if (igButton("Change", (ImVec2){})) directory_open(scene);

    ui_debug_fs_draw(&screenshot_dialog);

    igSameLine(0.0f, 4.0f);
}

static atomic_ulong frame;

/**
 * enum frame_role - what a captured frame is for, fixed when its readback is armed
 * @FRAME_SHOT:         a shot of its own, shot_*.exr
 * @FRAME_RECORDING:    part of a recording, frame_*.exr
 */
typedef enum frame_role {
    FRAME_SHOT,
    FRAME_RECORDING,
} frame_role;

/**
 * struct frame_job - one delivered frame on its way to an EXR file
 * @next:       the ready list's link
 * @cfg:        the readback ring's geometry, for the slot to go back to
 * @contents:   the frame's pixels, in the readback slot
 * @slot:       the readback slot
 * @index:      the number in the file's name
 * @role:       what the frame is for
 * @flip:       rows run bottom-up
 * @dir:        the capture directory
 *
 * Its pixels stay in the readback slot, and the slot stays busy, until
 * the write's done() returns it on the main thread. Made by the readback
 * completion on whichever thread the backend completes on, handed to the
 * main thread through the ready list, freed once the write is over. The
 * main thread fills in @index, @flip and @dir before it submits the
 * write, so the workers read nothing that it changes.
 */
typedef struct frame_job {
    struct frame_job    *next;
    fbo_blit_config     *cfg;
    void                *contents;
    size_t              slot;
    unsigned long       index;
    frame_role          role;
    bool                flip;
    char                dir[PATH_MAX];
} frame_job;

/**
 * write_frame() - write a frame to an EXR file
 * @fj:     the frame
 *
 * On a capture worker.
 * Return: CERR_OK, or the error writing the file
 */
static cerr write_frame(frame_job *fj)
{
    char name[PATH_MAX], path[PATH_MAX];

    if (fj->role == FRAME_RECORDING)    snprintf(name, sizeof(name), "frame_%06lu.exr", fj->index);
    else                                snprintf(name, sizeof(name), "shot_%06lu.exr", fj->index);

    CERR_RET_CERR(path_join(path, sizeof(path), fj->dir, name));

    exr_image img = {
        .pixels         = fj->contents,
        .chromaticities = rec709_chromaticities,
        .row_pitch      = fj->cfg->bytes_per_row_aligned,
        .width          = fj->cfg->width,
        .height         = fj->cfg->height,
        .nr_comps       = texture_format_nr_comps(fj->cfg->format),
        .comp_size      = texture_format_comp_size(fj->cfg->format),
        .flip           = fj->flip,
    };

    return exr_write_file(path, &img);
}

static worker_pool *capture_pool;
/* delivered frames waiting for the main thread to submit them, newest first */
static _Atomic(frame_job *) ready;
/* completions between checking the recorder is open and listing their frame */
static atomic_uint ready_pushes;
static atomic_bool capture_closed;
/* submitted and not yet done(), for the debug view */
static unsigned int nr_in_flight;

/**
 * frame_discard() - give a frame's readback slot back and free the frame
 * @fj:     the frame, written or not
 */
static void frame_discard(frame_job *fj)
{
    fbo_blit_slot_put(fj->cfg, fj->slot);
    mem_free(fj);
}

/**
 * frame_run() - the write job's run(): write the frame
 * @j:      the job
 *
 * Return: the result of write_frame()
 */
static cerr frame_run(job *j)
{
    return write_frame(job_get_data(j));
}

/**
 * frame_done() - the write job's done(): report what failed, then let go
 * @j:      the job
 * @err:    the result of frame_run()
 *
 * On the main thread.
 */
static void frame_done(job *j, cerr err)
{
    frame_job *fj = job_get_data(j);

    if (IS_CERR(err))   err_cerr(err, "slot %zu: frame not written\n", fj->slot);
    nr_in_flight--;
    frame_discard(fj);
}

/**
 * queue_frame() - list a delivered frame for the main thread to submit
 * @cfg:    the readback ring's geometry, with this delivery's slot and contents
 * @role:   what the frame is for
 *
 * The readback completions' body: on the render thread, or Metal's
 * completion thread.
 */
static void queue_frame(fbo_blit_config *cfg, frame_role role)
{
    frame_job *fj = mem_alloc(sizeof(*fj));

    if (!fj) {
        dbg("dropping frame at %lu\n", atomic_load_explicit(&frame, memory_order_relaxed));
        fbo_blit_slot_put(cfg, cfg->frame);
        return;
    }

    *fj = (frame_job){
        .cfg        = cfg,
        /* the ring's cfg carries this delivery's slot only until the next one */
        .contents   = cfg->contents,
        .slot       = cfg->frame,
        .role       = role,
    };

    /*
     * Metal completes on a thread of its own, possibly while
     * screen_record_done() is closing the recorder on the main thread:
     * the latter waits for completions that found it open before it
     * drains the list for the last time. Both sides store first and
     * load second, which only sequential consistency orders across
     * threads.
     */
    atomic_fetch_add(&ready_pushes, 1);
    if (atomic_load(&capture_closed)) {
        frame_discard(fj);
    } else {
        frame_job *head = atomic_load_explicit(&ready, memory_order_relaxed);

        do {
            fj->next = head;
        } while (!atomic_compare_exchange_weak_explicit(&ready, &head, fj,
                                                        memory_order_release, memory_order_relaxed));
    }
    atomic_fetch_sub(&ready_pushes, 1);
}

/**
 * write_frame_cb() - the readback completion of a shot
 * @cfg:    the readback ring's geometry, with this delivery's slot and contents
 * @fbo:    the captured fbo
 */
static void write_frame_cb(fbo_blit_config *cfg, fbo_t *fbo)
{
    queue_frame(cfg, FRAME_SHOT);
}

/**
 * record_frame_cb() - the readback completion of a recorded frame
 * @cfg:    the readback ring's geometry, with this delivery's slot and contents
 * @fbo:    the captured fbo
 */
static void record_frame_cb(fbo_blit_config *cfg, fbo_t *fbo)
{
    queue_frame(cfg, FRAME_RECORDING);
}

/**
 * ready_take() - take the ready list
 *
 * Return: the frames delivered since the last take, oldest first
 */
static frame_job *ready_take(void)
{
    frame_job *fj = atomic_exchange_explicit(&ready, NULL, memory_order_acquire);
    frame_job *fifo = NULL;

    while (fj) {
        frame_job *next = fj->next;

        fj->next = fifo;
        fifo = fj;
        fj = next;
    }

    return fifo;
}

/**
 * capture_number() - the number in a capture's file name
 * @name:   a directory entry's name
 * @prefix: the capture kind's prefix, "frame_" or "shot_"
 * @nr:     set to the number, if there is one
 *
 * Return: true if @name is @prefix, decimal digits below ULONG_MAX and ".exr"
 */
static bool capture_number(const char *name, const char *prefix, unsigned long *nr)
{
    size_t len = strlen(prefix);
    char *end;

    if (strncmp(name, prefix, len) || !isdigit((unsigned char)name[len]))   return false;

    errno = 0;
    *nr = strtoul(name + len, &end, 10);

    return !errno && *nr < ULONG_MAX && !strcmp(end, ".exr");
}

/* the directory the frame numbers were last seeded from, once there was one */
static char frame_seed_dir[PATH_MAX];
static bool frame_seeded;

/**
 * frame_seed() - number the captures past the ones already in a directory
 * @dir:    the capture directory
 *
 * The numbering starts over with every run, while the settings keep the
 * directory: without looking, a run would write its captures over the
 * last one's under the same names. Once per directory, on the main thread.
 */
static void frame_seed(const char *dir)
{
    if (frame_seeded && !strcmp(frame_seed_dir, dir))   return;
    frame_seeded = true;
    strncpy(frame_seed_dir, dir, sizeof(frame_seed_dir) - 1);

    LOCAL_SET(fs_dir, d) = CRES_RET(fs_open_dir(&fs_ops_posix, *dir ? dir : "."), return);

    unsigned long next = atomic_load_explicit(&frame, memory_order_relaxed);
    struct fs_dirent ent;

    for (cerr err = fs_read_dir(d, &ent); !IS_CERR(err); err = fs_read_dir(d, &ent)) {
        unsigned long nr;
        bool numbered = capture_number(ent.name, "frame_", &nr) || capture_number(ent.name, "shot_", &nr);

        if (numbered && nr >= next) next = nr + 1;
    }

    atomic_store_explicit(&frame, next, memory_order_relaxed);
}

/**
 * ready_submit() - make every delivered frame a write job
 *
 * On the main thread, which numbers the frames in the order they were
 * delivered.
 */
static void ready_submit(void)
{
    for (frame_job *fj = ready_take(), *next; fj; fj = next) {
        next = fj->next;
        fj->flip = capture_flip;
        memcpy(fj->dir, screenshot_dir, sizeof(fj->dir));
        frame_seed(fj->dir);
        fj->index = atomic_fetch_add_explicit(&frame, 1, memory_order_relaxed);

        job *j = CRES_RET(
            job_submit(.pool = capture_pool, .run = frame_run, .done = frame_done, .data = fj, .name = "capture"),
            {
                dbg("dropping frame at %lu\n", atomic_load_explicit(&frame, memory_order_relaxed));
                frame_discard(fj);
                continue;
            }
        );

        /* the pool holds the job until done(); nothing here needs the handle */
        nr_in_flight++;
        ref_put(j);
    }
}

void screen_record_set_settings(struct settings *rs)
{
    settings = rs;
    debug_group = settings_find_get(rs, NULL, "debug", JSON_OBJECT);
    if (debug_group)    screenshot_dir_load();
}

static bool capture_pool_failed;

/**
 * capture_pool_init() - start the capture writers with the first capture
 * @scene:  the scene, for its clap_context
 *
 * A worker per readback slot: recording keeps them all busy.
 * Return: CERR_OK, or the error starting the pool, which is tried once
 */
static cerr capture_pool_init(struct scene *scene)
{
    if (capture_pool)           return CERR_OK;
    /* a pool that couldn't start won't next frame either: one try, one error */
    if (capture_pool_failed)    return CERR_INVALID_OPERATION;

    capture_pool = CRES_RET(
        worker_pool_new(
            .ctx        = scene->clap_ctx,
            .name       = "capture",
            .nr_threads = FBO_BLIT_FRAMES
        ),
        {
            cerr err = cerr_error_cres(__resp);

            capture_pool_failed = true;
            err_cerr(err, "capture worker pool failed to start\n");
            return err;
        }
    );

    return CERR_OK;
}

/**
 * capture_fbo() - the screen pass' fbo, if it renders for capture
 * @scene:  the scene, for its pipeline
 *
 * The screen pass is "present", or "combine" where the pipeline merges
 * the two; render_options::screen_capture rebuilds the pipeline to render
 * it off-screen, with the linear frame in color attachment 1.
 * Return: the fbo; CERR_NOT_FOUND without a pipeline or a screen pass,
 *         CERR_INVALID_OPERATION until the pipeline renders for capture
 */
static cresp(fbo_t) capture_fbo(struct scene *scene)
{
    pipeline *pl = clap_get_pipeline(scene->clap_ctx);
    if (!pl)    return cresp_error(fbo_t, CERR_NOT_FOUND);

    auto res = pipeline_find_pass(pl, "present");
    if (IS_CERR(res))   res = pipeline_find_pass(pl, "combine");

    fbo_t *fbo = render_pass_get_fbo(CRES_RET_T(res, fbo_t));
    if (!fbo_attachment_valid(fbo, FBO_COLOR_TEXTURE(1)))   return cresp_error(fbo_t, CERR_INVALID_OPERATION);

    return cresp_val(fbo_t, fbo);
}

/**
 * capture_is_ready() - does the pipeline render for capture?
 * @scene:  the scene, for its pipeline
 *
 * Return: true once capture_fbo() finds the capture target
 */
static bool capture_is_ready(struct scene *scene)
{
    auto res = capture_fbo(scene);

    return !IS_CERR(res);
}

/**
 * screenshot() - capture the frame being made
 * @scene:      the scene, for the pipeline to capture from
 * @complete:   the readback completion, which lists the frame for writing
 */
static void screenshot(struct scene *scene, fbo_blit_complete_fn complete)
{
    fbo_t *fbo = CRES_RET(capture_fbo(scene), {
        warn_once("no pipeline pass to capture from\n");
        return;
    });

    CERR_RET(capture_pool_init(scene), return);

    capture_flip = !renderer_get_caps(clap_get_renderer(scene->clap_ctx))->origin_top_left;

    fbo_blit_config *cfg = CRES_RET(fbo_attachment_blit(fbo, FBO_COLOR_TEXTURE(1), complete), {
        cerr err = cerr_error_cres(__resp);
        err_cerr(err, "failed to arm a capture\n");
        return;
    });

    msg_once("capture armed: %u x %u %s\n", cfg->width, cfg->height, texture_format_string(cfg->format));
}

static void screen_recorder(struct scene *scene);

void screen_record_update(struct scene *scene)
{
    /* the writes finished since last frame return their slots before new arms */
    if (capture_pool) {
        worker_pool_poll(capture_pool, 0);
        ready_submit();
    }

    screen_recorder(scene);

    /*
     * One-shot arms are consumed every frame; keep a recording armed, for
     * as long as the pipeline renders for capture
     */
    if (screen_record && !capture_is_ready(scene))  screen_record = false;
    if (screen_record)                              screenshot(scene, record_frame_cb);
}

void screen_record_done(void)
{
    if (!capture_pool)  return;

    /* frames delivered since the last update go out with the rest */
    ready_submit();
    screen_record = false;

    /* the workers write out what's queued, then every slot goes back */
    ref_put_last(capture_pool);
    capture_pool = NULL;

    /* a completion that found the recorder open gets to list its frame first */
    atomic_store(&capture_closed, true);
    while (atomic_load(&ready_pushes))
        ;

    for (frame_job *fj = ready_take(), *next; fj; fj = next) {
        next = fj->next;
        frame_discard(fj);
    }
}

/**
 * screen_recorder() - draw the screen recorder debug window
 * @scene:  the scene, for the pipeline to capture from
 */
static void screen_recorder(struct scene *scene)
{
    debug_module *dbgm = ui_igBegin(DEBUG_SCREEN_RECORDER, ImGuiWindowFlags_AlwaysAutoResize);

    if (!dbgm->display) return;

    if (dbgm->unfolded) {
        int width, height;
        clap_get_viewport(scene->clap_ctx, NULL, NULL, &width, &height);

        igText("viewport: %u x %u", width, height);
        igText("last frame: %06lu", atomic_load_explicit(&frame, memory_order_relaxed));
        directory_draw(scene);

        /* rebuilds the pipeline with the screen pass rendering for capture */
        igCheckbox("arm", &clap_get_render_options(scene->clap_ctx)->screen_capture);

        /* fails until the rebuild has happened */
        auto capture = capture_fbo(scene);
        bool armed = !IS_CERR(capture);

        if (armed && screen_record) {
            igSameLine(0.0f, 4.0f);
            if (igButton("stop", (ImVec2){})) {
                screen_record = false;
                fbo_blit_disarm(capture.val);
            }
        } else if (armed) {
            igSameLine(0.0f, 4.0f);
            if (igButton("record", (ImVec2){})) {
                if (!strlen(screenshot_dir))    directory_open(scene);
                screen_record = true;
                screenshot(scene, record_frame_cb);
            }

            igSameLine(0.0f, 4.0f);

            if (igButton("shoot", (ImVec2){})) {
                if (!strlen(screenshot_dir))    directory_open(scene);
                // if (scene->width > 2560)    display_resize(2560, scene->height);
                screenshot(scene, write_frame_cb);
            }
        }

        if (capture_pool)
            igText("capture workers: %u, frames being written: %u",
                   worker_pool_nr_threads(capture_pool), nr_in_flight);
    }

    ui_igEnd(DEBUG_SCREEN_RECORDER);
}
