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

typedef struct fs_dir fs_dir;
typedef struct fs_file fs_file;

DECLARE_CLEANUP(fs_dir);
DECLARE_CLEANUP(fs_file);

cresp_struct_ret(fs_dir);
cresp_struct_ret(fs_file);

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

#endif /* __CLAP_FS_OPS_H__ */
