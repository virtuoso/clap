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
 * struct fs_ops - filesystem access vtable
 * @get_cwd:   fill @out_path with the current working directory
 * @open_dir:  open a directory stream for @path
 * @read_dir:  iterate over a directory stream, returns CERR_EOF at end
 * @close_dir: close a directory stream opened by @open_dir
 * @dirent_cmp: qsort-compatible comparison function for fs_dirent entries
 */
struct fs_ops {
    cerr_check          (*get_cwd)(char out_path[PATH_MAX]);
    cresp_check(void)   (*open_dir)(const char *path);
    cerr_check          (*read_dir)(void *dir, struct fs_dirent *out);
    void                (*close_dir)(void *dir);
    int                 (*dirent_cmp)(const void *a, const void *b);
};

extern const struct fs_ops fs_ops_posix;

#endif /* __CLAP_FS_OPS_H__ */
