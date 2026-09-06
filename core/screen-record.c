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

#ifdef CONFIG_BROWSER
/** BROWSER_SCREENSHOT_DIR - the browser build's capture directory, in MEMFS */
#define BROWSER_SCREENSHOT_DIR "/screenshots"

static bool screenshot_dir_ready;

/**
 * screenshot_dir_init() - make the browser build's capture directory
 *
 * MEMFS starts over with every page load, so the directory is made anew
 * each time; browser builds capture into it and nowhere else.
 */
static void screenshot_dir_init(void)
{
    if (screenshot_dir_ready)   return;
    screenshot_dir_ready = true;

    CERR_RET(fs_make_dir(&fs_ops_posix, BROWSER_SCREENSHOT_DIR),
             err_cerr(__cerr, "failed to create %s\n", BROWSER_SCREENSHOT_DIR));
    strncpy(screenshot_dir, BROWSER_SCREENSHOT_DIR, sizeof(screenshot_dir) - 1);
}

/**
 * directory_draw() - show the capture directory
 * @scene:  the scene
 *
 * Browser builds always capture into BROWSER_SCREENSHOT_DIR, there is
 * nothing to change.
 */
static void directory_draw(struct scene *scene)
{
    igText("Output directory: %s", screenshot_dir);
}

static inline void screenshot_dir_load(void) {}
#else
static inline void screenshot_dir_init(void) {}

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
#endif /* !CONFIG_BROWSER */

static atomic_ulong frame;

#ifdef CONFIG_BROWSER
/**
 * screenshot_hand_out() - offer a written capture as a download, then remove it
 * @path:   the capture's file
 *
 * Runs on a capture worker, while the filesystem and the DOM both live on
 * the browser's main thread; the call returns once the page holds the
 * file's contents in a Blob, so the file itself can go right after.
 * Return: CERR_OK, or the error removing the file
 */
static cerr screenshot_hand_out(const char *path)
{
    MAIN_THREAD_EM_ASM(offerFileAsDownload(UTF8ToString($0), 'image/x-exr');, path);

    if (remove(path))   return errno_to_cerr(errno);

    return CERR_OK;
}
#else
static inline cerr screenshot_hand_out(const char *path) { return CERR_OK; }
#endif /* !CONFIG_BROWSER */

/**
 * enum frame_role - what a captured frame is for, fixed when its readback is armed
 * @FRAME_SHOT:         a shot of its own, shot_*.exr
 * @FRAME_RECORDING:    part of a recording, frame_*.exr
 * @FRAME_DOWNLOAD:     a shot handed out as a download, then removed
 */
typedef enum frame_role {
    FRAME_SHOT,
    FRAME_RECORDING,
    FRAME_DOWNLOAD,
} frame_role;

/**
 * struct frame_job - one delivered frame on its way to an EXR file
 * @next:       the ready list's link
 * @cfg:        the readback ring's geometry, for the slot to go back to
 * @contents:   the frame's pixels, in the readback slot
 * @slot:       the readback slot
 * @index:      the number in the file's name
 * @removal:    the error removing a download's file once the page has it, for
 *              done() to report
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
    cerr                removal;
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
    CERR_RET_CERR(exr_write_file(path, &img));

    /* the frame is written either way: a file left behind is for done() to report */
    if (fj->role == FRAME_DOWNLOAD) fj->removal = screenshot_hand_out(path);

    return CERR_OK;
}

/** CAPTURE_MAX_FRAMES - frames a session captures unless configured otherwise: 10 s at 60 FPS */
#define CAPTURE_MAX_FRAMES 600

/* frames handed to the writers this session */
static unsigned int nr_captured;
/* the cap on nr_captured; recording stops there, later frames are dropped */
static int capture_max_frames = CAPTURE_MAX_FRAMES;
/* the cap has been hit and said so */
static bool capture_cap_reported;

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

    if (IS_CERR(err))           err_cerr(err, "slot %zu: frame not written\n", fj->slot);
    if (IS_CERR(fj->removal))   err_cerr(fj->removal, "offered for download, but left in %s\n", fj->dir);
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
 * capture_cap_is_reached() - has this session captured as many frames as the cap?
 *
 * Return: true once nr_captured is at the cap or past it
 */
static bool capture_cap_is_reached(void)
{
    return nr_captured >= (unsigned int)capture_max_frames;
}

/**
 * capture_cap_report() - say that the cap is reached, once per cap
 *
 * Every frame lost to the cap gets explained: the ones screenshot()
 * refuses and the ones delivered after the last frame under the cap.
 */
static void capture_cap_report(void)
{
    if (capture_cap_reported)   return;

    msg("capture: the cap of %d frames is reached; raise it in the screen recorder window "
        "or with clap::set_capture_max_frames()\n", capture_max_frames);
    capture_cap_reported = true;
}

/**
 * capture_max_frames_set() - take a new cap
 * @max_frames: the cap, 1 or more
 *
 * A cap above the frames captured so far gets a report of its own when
 * capture reaches it.
 */
static void capture_max_frames_set(int max_frames)
{
    capture_max_frames = max_frames;
    if (!capture_cap_is_reached())  capture_cap_reported = false;
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
 * ready_submit() - make every delivered frame a write job, up to the cap
 *
 * On the main thread, which numbers the frames in the order they were
 * delivered; frames delivered once the cap is reached are dropped.
 */
static void ready_submit(void)
{
    for (frame_job *fj = ready_take(), *next; fj; fj = next) {
        next = fj->next;
        fj->flip = capture_flip;
        memcpy(fj->dir, screenshot_dir, sizeof(fj->dir));

        /* armed before the cap was reached, delivered after */
        if (capture_cap_is_reached()) {
            capture_cap_report();
            frame_discard(fj);
            continue;
        }

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
        nr_captured++;
        nr_in_flight++;
        ref_put(j);
    }
}

/**
 * capture_max_frames_is_valid() - is a settings value a cap?
 * @node:   the value
 *
 * Return: true for a whole number of frames, 1 or more
 */
static bool capture_max_frames_is_valid(const JsonNode *node)
{
    return node->tag == JSON_NUMBER && node->number_ >= 1 && node->number_ <= INT_MAX &&
           node->number_ == (int)node->number_;
}

void screen_record_set_settings(struct settings *rs)
{
    settings = rs;
    debug_group = settings_find_get(rs, NULL, "debug", JSON_OBJECT);
    if (!debug_group)   return;

    screenshot_dir_load();

    JsonNode *node = settings_get(rs, debug_group, "capture_max_frames");
    if (!node)                              return;
    if (capture_max_frames_is_valid(node))  capture_max_frames_set(node->number_);
    else                                    warn("capture_max_frames setting is not a frame count, ignoring it\n");
}

/**
 * capture_max_frames_enter() - take a new cap from the screen recorder window
 * @max_frames: the cap as entered
 *
 * Capture goes on up to the new cap, which the settings remember where the
 * application keeps any; a cap below 1 is refused.
 */
static void capture_max_frames_enter(int max_frames)
{
    if (max_frames < 1) {
        warn("capture: %d is not a frame count, the cap stays at %d\n", max_frames, capture_max_frames);
        return;
    }

    capture_max_frames_set(max_frames);
    if (settings && debug_group)    settings_set_num(settings, debug_group, "capture_max_frames", capture_max_frames);
}

static bool capture_pool_failed;

/**
 * capture_pool_init() - start the capture writers with the first capture
 * @scene:  the scene, for its clap_context
 *
 * Every slot's worth of workers for recording; screenshots make do with one.
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
            .nr_threads = IS_DEFINED(CONFIG_SCREEN_RECORD) ? FBO_BLIT_FRAMES : 1
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
 *
 * Return: CERR_OK, CERR_INVALID_OPERATION while the pipeline doesn't render
 * for capture, CERR_OUT_OF_BOUNDS once the cap is reached, or the error
 * starting the writers or arming the readback
 */
static cerr screenshot(struct scene *scene, fbo_blit_complete_fn complete)
{
    fbo_t *fbo = CRES_RET(capture_fbo(scene), {
        warn_once("no pipeline pass to capture from\n");
        return CERR_INVALID_OPERATION;
    });

    if (capture_cap_is_reached()) {
        capture_cap_report();
        screen_record = false;
        return CERR_OUT_OF_BOUNDS;
    }

    CERR_RET_CERR(capture_pool_init(scene));

    capture_flip = !renderer_get_caps(clap_get_renderer(scene->clap_ctx))->origin_top_left;

    fbo_blit_config *cfg = CRES_RET(fbo_attachment_blit(fbo, FBO_COLOR_TEXTURE(1), complete), {
        cerr err = cerr_error_cres(__resp);
        err_cerr(err, "failed to arm a capture\n");
        return err;
    });

    msg_once("capture armed: %u x %u %s\n", cfg->width, cfg->height, texture_format_string(cfg->format));

    return CERR_OK;
}

#ifdef CONFIG_BROWSER
/**
 * download_frame_cb() - the readback completion of a shot to download
 * @cfg:    the readback ring's geometry, with this delivery's slot and contents
 * @fbo:    the captured fbo
 */
static void download_frame_cb(fbo_blit_config *cfg, fbo_t *fbo)
{
    queue_frame(cfg, FRAME_DOWNLOAD);
}

/**
 * download_button() - shoot, then hand the capture to the browser
 * @scene:  the scene, for the pipeline to capture from
 *
 * The written capture is offered as a download and removed from the
 * capture directory, leaving the downloaded file the only copy.
 */
static void download_button(struct scene *scene)
{
    igSameLine(0.0f, 4.0f);

    if (igButton("download", (ImVec2){}))   screenshot(scene, download_frame_cb);
}
#else
static inline void download_button(struct scene *scene) {}
#endif /* !CONFIG_BROWSER */

static void screen_recorder(struct scene *scene);

void screen_record_update(struct scene *scene)
{
    screenshot_dir_init();

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

void screen_record_arm(struct clap_context *ctx, bool arm)
{
    clap_get_render_options(ctx)->screen_capture = arm;
}

bool screen_record_is_armed(struct clap_context *ctx)
{
    struct scene *scene = clap_get_scene(ctx);

    return scene && capture_is_ready(scene);
}

cerr screen_record_shoot(struct clap_context *ctx)
{
    struct scene *scene = clap_get_scene(ctx);
    if (!scene) return CERR_NOT_SUPPORTED;

    return screenshot(scene, write_frame_cb);
}

cerr screen_record_set_dir(struct clap_context *ctx, const char *dir)
{
    struct scene *scene = clap_get_scene(ctx);

    /* browser builds capture into BROWSER_SCREENSHOT_DIR and nowhere else; no graphics, no captures */
    if (IS_DEFINED(CONFIG_BROWSER) || !scene)   return CERR_NOT_SUPPORTED;
    if (strlen(dir) >= sizeof(screenshot_dir))  return CERR_NAME_TOO_LONG;

    LOCAL_SET(fs_dir, d) = CRES_RET_CERR(fs_open_dir(&fs_ops_posix, dir));

    strncpy(screenshot_dir, dir, sizeof(screenshot_dir) - 1);

    return CERR_OK;
}

cerr screen_record_set_max_frames(int max_frames)
{
    if (max_frames < 1) return CERR_INVALID_ARGUMENTS;

    capture_max_frames_set(max_frames);

    return CERR_OK;
}

unsigned int screen_record_nr_captured(void)
{
    return nr_captured;
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
            if (IS_DEFINED(CONFIG_SCREEN_RECORD)) {
                igSameLine(0.0f, 4.0f);
                if (igButton("record", (ImVec2){})) {
                    if (!strlen(screenshot_dir))    directory_open(scene);
                    screen_record = true;
                    screenshot(scene, record_frame_cb);
                }
            }

            igSameLine(0.0f, 4.0f);

            if (igButton("shoot", (ImVec2){})) {
                if (!strlen(screenshot_dir))    directory_open(scene);
                // if (scene->width > 2560)    display_resize(2560, scene->height);
                screenshot(scene, write_frame_cb);
            }

            download_button(scene);
        }

        igText("frames captured: %u, cap:", nr_captured);
        igSameLine(0.0f, 4.0f);
        igPushItemWidth(80.0f);
        int max_frames = capture_max_frames;
        if (igInputInt("##capture_max_frames", &max_frames, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
            capture_max_frames_enter(max_frames);
        igPopItemWidth();

        if (capture_pool)
            igText("capture workers: %u, frames being written: %u",
                   worker_pool_nr_threads(capture_pool), nr_in_flight);
    }

    ui_igEnd(DEBUG_SCREEN_RECORDER);
}
