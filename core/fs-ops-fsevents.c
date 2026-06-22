// SPDX-License-Identifier: Apache-2.0
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <os/lock.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs-ops.h"
#include "memory.h"

/* FSEvents coalescing window, in seconds */
#define FS_FSEVENTS_LATENCY 0.1

/*
 * The event queue is filled from an FSEvents dispatch-queue callback and drained
 * by the caller under @lock. core/thread.h can't be used here: its semaphore_t
 * and thread_t typedefs collide with the Mach names CoreServices pulls in, so
 * this Apple-only file uses os_unfair_lock. Waiting uses a dispatch semaphore,
 * since thread.h has no timed wait and dispatch is the natural fit for a backend
 * already running on a dispatch queue. @notify is armed for the duration of a
 * poll and may be shared by the several watches a single poll waits on.
 */
struct fs_fsevents_watch {
    FSEventStreamRef        stream;
    dispatch_queue_t        queue;
    dispatch_semaphore_t    notify;     /* when armed by a poll, signalled as events arrive */
    darray(struct fs_watch_event, events);
    char                    root[PATH_MAX]; /* canonical watched dir, no trailing slash */
    size_t                  root_len;
    os_unfair_lock          lock;       /* guards @events and @notify, held across the callback */
};

static unsigned int fs_fsevents_flags(FSEventStreamEventFlags flags)
{
    unsigned int out = 0;

    if (flags & kFSEventStreamEventFlagItemCreated)     out |= FS_WATCH_CREATED;
    if (flags & kFSEventStreamEventFlagItemRemoved)     out |= FS_WATCH_DELETED;
    if (flags & kFSEventStreamEventFlagItemModified)    out |= FS_WATCH_MODIFIED;
    if (flags & kFSEventStreamEventFlagItemRenamed)     out |= FS_WATCH_RENAMED;

    return out;
}

/*
 * FSEvents reports canonical absolute paths and is recursive. Reduce a path to
 * a name relative to the watched directory, keeping only its direct children so
 * the semantics match inotify watching a single directory.
 */
static const char *fs_fsevents_relpath(struct fs_fsevents_watch *watch, const char *path)
{
    if (strncmp(path, watch->root, watch->root_len))    return NULL;
    if (path[watch->root_len] != '/')                   return NULL;

    const char *rel = path + watch->root_len + 1;
    if (!*rel || strchr(rel, '/'))                      return NULL;

    return rel;
}

static void fs_fsevents_cb(ConstFSEventStreamRef stream, void *info, size_t nr,
                           void *paths_in, const FSEventStreamEventFlags flags[],
                           const FSEventStreamEventId ids[])
{
    struct fs_fsevents_watch *watch = info;
    const char *const *paths = paths_in;
    bool added = false;

    os_unfair_lock_lock(&watch->lock);
    for (size_t i = 0; i < nr; i++) {
        unsigned int kind = fs_fsevents_flags(flags[i]);
        if (!kind)  continue;

        const char *rel = fs_fsevents_relpath(watch, paths[i]);
        if (!rel)   continue;

        struct fs_watch_event *ev = darray_add(watch->events);
        if (!ev) /* out of memory: drop the event, nothing we can do from here */
            break;

        snprintf(ev->name, sizeof(ev->name), "%s", rel);
        ev->flags = kind;
        added = true;
    }
    if (added && watch->notify)     dispatch_semaphore_signal(watch->notify);
    os_unfair_lock_unlock(&watch->lock);
}

/* No-op used to drain the serial queue during teardown */
static void fs_fsevents_noop(void *ctx)
{
}

static cresp(void) fs_fsevents_watch_open(const char *path)
{
    if (!path)                                  return cresp_error(void, CERR_INVALID_ARGUMENTS);

    struct fs_fsevents_watch *watch = mem_alloc(sizeof(*watch), .zero = 1);
    if (!watch)                                 return cresp_error(void, CERR_NOMEM);

    /*
     * FSEvents delivers canonical, symlink-resolved paths, so watch the
     * canonical directory too; otherwise the prefix check in the callback
     * would never match (e.g. /tmp vs /private/tmp).
     */
    if (!realpath(path, watch->root)) {
        cerr err = errno_to_cerr(errno);
        mem_free(watch);
        return cresp_error_cerr(void, err);
    }
    watch->root_len = strlen(watch->root);

    /* A zeroed os_unfair_lock is OS_UNFAIR_LOCK_INIT, so mem_alloc(.zero) suffices */
    darray_init(watch->events);

    CFStringRef cfroot = CFStringCreateWithCString(NULL, watch->root, kCFStringEncodingUTF8);
    if (!cfroot)                                goto err_sync;

    CFArrayRef cfpaths = CFArrayCreate(NULL, (const void **)&cfroot, 1, &kCFTypeArrayCallBacks);
    CFRelease(cfroot);
    if (!cfpaths)                               goto err_sync;

    FSEventStreamContext ctx = { .info = watch };
    watch->stream = FSEventStreamCreate(NULL, fs_fsevents_cb, &ctx, cfpaths,
                                        kFSEventStreamEventIdSinceNow, FS_FSEVENTS_LATENCY,
                                        kFSEventStreamCreateFlagFileEvents |
                                        kFSEventStreamCreateFlagNoDefer);
    CFRelease(cfpaths);
    if (!watch->stream)                         goto err_sync;

    watch->queue = dispatch_queue_create("org.clap.fsevents", DISPATCH_QUEUE_SERIAL);
    if (!watch->queue)                          goto err_stream;

    FSEventStreamSetDispatchQueue(watch->stream, watch->queue);
    if (!FSEventStreamStart(watch->stream))     goto err_queue;

    return cresp_val(void, watch);

err_queue:
    FSEventStreamInvalidate(watch->stream);
    dispatch_release(watch->queue);
err_stream:
    FSEventStreamRelease(watch->stream);
err_sync:
    darray_clearout(watch->events);
    mem_free(watch);
    return cresp_error(void, CERR_INITIALIZATION_FAILED);
}

static cerr fs_fsevents_watch_read(void *handle, struct fs_watch_event *out)
{
    struct fs_fsevents_watch *watch = handle;

    if (!watch || !out)     return CERR_INVALID_ARGUMENTS;

    os_unfair_lock_lock(&watch->lock);
    struct fs_watch_event *ev = darray_get(watch->events, 0);
    if (!ev) {
        os_unfair_lock_unlock(&watch->lock);
        return CERR_EOF;
    }

    *out = *ev;
    darray_delete(watch->events, 0);
    os_unfair_lock_unlock(&watch->lock);

    return CERR_OK;
}

static bool fs_fsevents_any_ready(void *const *handles, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        struct fs_fsevents_watch *watch = handles[i];
        bool ready;

        os_unfair_lock_lock(&watch->lock);
        ready = darray_count(watch->events) > 0;
        os_unfair_lock_unlock(&watch->lock);

        if (ready)  return true;
    }

    return false;
}

/* Point every watch's notify semaphore at @sem (NULL to disarm) */
static void fs_fsevents_arm(void *const *handles, size_t n, dispatch_semaphore_t sem)
{
    for (size_t i = 0; i < n; i++) {
        struct fs_fsevents_watch *watch = handles[i];

        os_unfair_lock_lock(&watch->lock);
        watch->notify = sem;
        os_unfair_lock_unlock(&watch->lock);
    }
}

static cerr fs_fsevents_watch_poll(void *const *handles, size_t n, int timeout_ms)
{
    if (!handles || !n)                     return CERR_INVALID_ARGUMENTS;

    if (fs_fsevents_any_ready(handles, n))  return CERR_OK;
    if (timeout_ms == 0)                    return CERR_EOF;

    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    if (!sem)                               return CERR_NOMEM;

    fs_fsevents_arm(handles, n, sem);

    /*
     * Re-check after arming: an event enqueued between the fast path above and
     * the arm would otherwise go unnoticed until the timeout.
     */
    bool ready = fs_fsevents_any_ready(handles, n);
    if (!ready) {
        dispatch_time_t dt = timeout_ms < 0
            ? DISPATCH_TIME_FOREVER
            : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_ms * 1000000);
        dispatch_semaphore_wait(sem, dt);
        ready = fs_fsevents_any_ready(handles, n);
    }

    fs_fsevents_arm(handles, n, NULL);
    dispatch_release(sem);

    return ready ? CERR_OK : CERR_EOF;
}

static void fs_fsevents_watch_close(void *handle)
{
    struct fs_fsevents_watch *watch = handle;

    if (!watch)     return;

    if (watch->stream) {
        FSEventStreamStop(watch->stream);
        FSEventStreamInvalidate(watch->stream);
        FSEventStreamRelease(watch->stream);
    }
    if (watch->queue) {
        /* Flush any callback still in flight before freeing shared state */
        dispatch_sync_f(watch->queue, NULL, fs_fsevents_noop);
        dispatch_release(watch->queue);
    }

    darray_clearout(watch->events);
    mem_free(watch);
}

const struct fs_watch_ops fs_watch_ops_fsevents = {
    .open   = fs_fsevents_watch_open,
    .read   = fs_fsevents_watch_read,
    .poll   = fs_fsevents_watch_poll,
    .close  = fs_fsevents_watch_close,
};
