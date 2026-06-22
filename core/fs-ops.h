/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_FS_OPS_H__
#define __CLAP_FS_OPS_H__

#include <limits.h>
#include <stdbool.h>
#include "error.h"
#include "util.h"

/**
 * struct fs_dirent - minimal directory entry information
 * @name:    entry name (no path)
 * @is_dir:  true when entry is a directory
 */
struct fs_dirent {
    char name[PATH_MAX];
    bool is_dir;
};

/**
 * enum fs_mode - file open mode
 * @FS_READ:   read only
 * @FS_WRITE:  write only
 * @FS_APPEND: append only
 * @FS_BOTH:   read and write
 */
enum fs_mode {
    FS_READ,
    FS_WRITE,
    FS_APPEND,
    FS_BOTH,
};

/**
 * enum fs_seek_origin - file seek origin
 * @FS_SEEK_SET: beginning of file
 * @FS_SEEK_CUR: current position
 * @FS_SEEK_END: end of file
 */
enum fs_seek_origin {
    FS_SEEK_SET,
    FS_SEEK_CUR,
    FS_SEEK_END,
};

/**
 * enum fs_watch_flags - kinds of filesystem change, combinable as a bitmask
 * @FS_WATCH_CREATED:  an entry appeared in the directory
 * @FS_WATCH_DELETED:  an entry was removed from the directory
 * @FS_WATCH_MODIFIED: an entry's contents changed
 * @FS_WATCH_RENAMED:  an entry was renamed into or out of the directory
 *
 * Backends coalesce changes that happen in quick succession, so a single
 * event may carry more than one flag.
 */
enum fs_watch_flags {
    FS_WATCH_CREATED    = 1 << 0,
    FS_WATCH_DELETED    = 1 << 1,
    FS_WATCH_MODIFIED   = 1 << 2,
    FS_WATCH_RENAMED    = 1 << 3,
};

/**
 * struct fs_watch_event - a single directory change notification
 * @name:  affected entry name, relative to the watched directory
 * @flags: bitmask of enum fs_watch_flags describing what happened
 */
struct fs_watch_event {
    char            name[PATH_MAX];
    unsigned int    flags;
};

/**
 * struct fs_ops - filesystem access vtable
 * @get_cwd:   fill @out_path with the current working directory
 * @open_dir:  open a directory stream for @path
 * @read_dir:  iterate over a directory stream, returns CERR_EOF at end
 * @close_dir: close a directory stream opened by @open_dir
 * @dirent_cmp: qsort-compatible comparison function for fs_dirent entries
 * @make_dir:  create a directory
 * @remove_dir: remove a directory
 * @open:      open a file
 * @close:     close a file
 * @read:      read from file
 * @write:     write to file
 * @seek:      seek in file
 */
struct fs_ops {
    cerr_check          (*get_cwd)(char out_path[PATH_MAX]);
    cresp_check(void)   (*open_dir)(const char *path);
    cerr_check          (*read_dir)(void *dir, struct fs_dirent *out);
    void                (*close_dir)(void *dir);
    int                 (*dirent_cmp)(const void *a, const void *b);
    cerr_check          (*make_dir)(const char *path);
    cerr_check          (*remove_dir)(const char *path);
    cresp_check(void)   (*open)(const char *path, enum fs_mode mode, bool create, bool exclusive, bool binary);
    cerr_check          (*close)(void *handle);
    cres_check(size_t)  (*read)(void *handle, void *buf, size_t size);
    cres_check(size_t)  (*write)(void *handle, const void *buf, size_t size);
    cerr_check          (*seek)(void *handle, long offset, enum fs_seek_origin origin);
};

extern const struct fs_ops fs_ops_posix;

/**
 * struct fs_watch_ops - directory change notification vtable
 * @open:  begin watching @path, returns an opaque watch handle
 * @read:  dequeue the next pending event, returns CERR_EOF when none are pending
 * @poll:  block until any of @n watches may have events, or @timeout_ms elapses
 * @close: stop watching and release a handle returned by @open
 *
 * A backend maps the platform's native mechanism (FSEvents on macOS, inotify
 * on Linux) onto a non-blocking, pollable event queue. All handles passed to
 * @poll belong to the same backend.
 */
struct fs_watch_ops {
    cresp_check(void)   (*open)(const char *path);
    cerr_check          (*read)(void *watch, struct fs_watch_event *out);
    cerr_check          (*poll)(void *const *watches, size_t n, int timeout_ms);
    void                (*close)(void *watch);
};

/*
 * The native directory watch backend for this platform, exposed as
 * fs_watch_ops_native with HAVE_FSWATCH defined when one exists.
 */
#if defined(__APPLE__)
extern const struct fs_watch_ops fs_watch_ops_fsevents;
#define fs_watch_ops_native fs_watch_ops_fsevents
#define HAVE_FSWATCH 1
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
extern const struct fs_watch_ops fs_watch_ops_inotify;
#define fs_watch_ops_native fs_watch_ops_inotify
#define HAVE_FSWATCH 1
#endif /* __linux__ && !__EMSCRIPTEN__ */

typedef struct fs_dir fs_dir;
typedef struct fs_file fs_file;
typedef struct fs_watch fs_watch;

DECLARE_CLEANUP(fs_dir);
DECLARE_CLEANUP(fs_file);
DECLARE_CLEANUP(fs_watch);

cresp_struct_ret(fs_dir);
cresp_struct_ret(fs_file);
cresp_struct_ret(fs_watch);

/**
 * fs_get_cwd() - get current working directory
 * @ops:      filesystem operations vtable
 * @out_path: buffer to write path to
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check          fs_get_cwd(const struct fs_ops *ops, char out_path[PATH_MAX]);

/**
 * fs_open_dir() - open a directory
 * @ops:  filesystem operations vtable
 * @path: path to directory
 *
 * Return: directory handle on success, error code otherwise.
 */
cresp_check(fs_dir) fs_open_dir(const struct fs_ops *ops, const char *path);

/**
 * fs_read_dir() - read next directory entry
 * @dir: directory handle
 * @out: buffer to write entry info to
 *
 * Return: CERR_OK on success, CERR_EOF on end of directory, error code otherwise.
 */
cerr_check          fs_read_dir(fs_dir *dir, struct fs_dirent *out);

/**
 * fs_close_dir() - close directory
 * @dir: directory handle
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check          fs_close_dir(fs_dir *dir);

/**
 * fs_make_dir() - create a directory
 * @ops:  filesystem operations vtable
 * @path: path to directory to create
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check          fs_make_dir(const struct fs_ops *ops, const char *path);

/**
 * fs_remove_dir() - remove a directory
 * @ops:  filesystem operations vtable
 * @path: path to directory to remove
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check          fs_remove_dir(const struct fs_ops *ops, const char *path);

/**
 * fs_open() - open a file
 * @ops:    filesystem operations vtable
 * @path:   path to file
 * @mode:   open mode
 * @create: create file if it doesn't exist
 * @exclusive: fail if file exists
 * @binary: open in binary mode
 *
 * Return: file handle on success, error code otherwise.
 */
cresp_check(fs_file) fs_open(const struct fs_ops *ops, const char *path, enum fs_mode mode, bool create, bool exclusive, bool binary);

/**
 * fs_close() - close file
 * @file: file handle
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check           fs_close(fs_file *file);

/**
 * fs_read() - read from file
 * @file: file handle
 * @buf:  buffer to read into
 * @size: number of bytes to read
 *
 * Return: number of bytes read on success, error code otherwise.
 */
cres_check(size_t)   fs_read(fs_file *file, void *buf, size_t size);

/**
 * fs_write() - write to file
 * @file: file handle
 * @buf:  buffer to write from
 * @size: number of bytes to write
 *
 * Return: number of bytes written on success, error code otherwise.
 */
cres_check(size_t)   fs_write(fs_file *file, const void *buf, size_t size);

/**
 * fs_seek() - seek in file
 * @file:   file handle
 * @offset: offset to seek to
 * @origin: seek origin
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check           fs_seek(fs_file *file, long offset, enum fs_seek_origin origin);

/**
 * fs_watch_open() - start watching a directory for changes
 * @ops:  directory change notification vtable
 * @path: path to the directory to watch
 *
 * Return: watch handle on success, error code otherwise.
 */
cresp_check(fs_watch) fs_watch_open(const struct fs_watch_ops *ops, const char *path);

/**
 * fs_watch_read() - dequeue the next pending change event
 * @watch: watch handle
 * @out:   buffer to write the event to
 *
 * Non-blocking: reports changes accumulated since the previous call.
 *
 * Return: CERR_OK on success, CERR_EOF when no events are pending, error code otherwise.
 */
cerr_check           fs_watch_read(fs_watch *watch, struct fs_watch_event *out);

/**
 * fs_watch_poll() - wait for pending change events across one or more watches
 * @watches:    array of @n watch handles, all from the same backend
 * @n:          number of handles in @watches
 * @timeout_ms: maximum wait in milliseconds; 0 returns at once, negative waits forever
 *
 * Blocks so callers need not sleep-poll, waiting on all @watches at once and
 * returning as soon as any of them may have events. Drain them with
 * fs_watch_read() to find which fired. Must not race with fs_watch_read() or
 * fs_watch_close() on the same handle.
 *
 * Return: CERR_OK when some watch may have events, CERR_EOF on timeout, error
 * code otherwise.
 */
cerr_check           fs_watch_poll(fs_watch **watches, size_t n, int timeout_ms);

/**
 * fs_watch_close() - stop watching a directory
 * @watch: watch handle
 *
 * Return: CERR_OK on success, error code otherwise.
 */
cerr_check           fs_watch_close(fs_watch *watch);

#endif /* __CLAP_FS_OPS_H__ */
