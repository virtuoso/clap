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
    cresp_check(void)   (*open)(const char *path, enum fs_mode mode, bool create, bool exclusive, bool binary);
    cerr_check          (*close)(void *handle);
    cres_check(size_t)  (*read)(void *handle, void *buf, size_t size);
    cres_check(size_t)  (*write)(void *handle, const void *buf, size_t size);
    cerr_check          (*seek)(void *handle, long offset, enum fs_seek_origin origin);
};

extern const struct fs_ops fs_ops_posix;

#endif /* __CLAP_FS_OPS_H__ */
