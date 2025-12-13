// SPDX-License-Identifier: Apache-2.0
#include <dirent.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fs-ops.h"

static cerr fs_posix_get_cwd(char out_path[PATH_MAX])
{
    if (!getcwd(out_path, PATH_MAX))
        return CERR_INVALID_OPERATION;

    return CERR_OK;
}

static cresp(void) fs_posix_open_dir(const char *path)
{
    DIR *dir = opendir(path);

    if (!dir)
        return cresp_error(void, CERR_INVALID_ARGUMENTS);

    return cresp_val(void, dir);
}

static cerr fs_posix_read_dir(void *dir, struct fs_dirent *out)
{
    struct dirent *de = readdir(dir);

    if (!de)
        return CERR_EOF;

    snprintf(out->name, sizeof(out->name), "%s", de->d_name);

    /*
     * Emscripten's stat::st_mode is unreliable;
     * _WIN32's dirent doesn't have dirent::d_type
     */
#ifdef _WIN32
    struct stat st;
    stat(out->name, &st);
    out->is_dir = S_ISDIR(st.st_mode);
#else /* !_WIN32 */
    out->is_dir = de->d_type == DT_DIR;
#endif /* !_WIN32 */

    return CERR_OK;
}

static void fs_posix_close_dir(void *dir)
{
    if (!dir)
        return;

    closedir(dir);
}

static int fs_posix_dirent_cmp(const void *a, const void *b)
{
    const struct fs_dirent *ea = a;
    const struct fs_dirent *eb = b;

    return strcasecmp(ea->name, eb->name);
}

const struct fs_ops fs_ops_posix = {
    .get_cwd    = fs_posix_get_cwd,
    .open_dir   = fs_posix_open_dir,
    .read_dir   = fs_posix_read_dir,
    .close_dir  = fs_posix_close_dir,
    .dirent_cmp = fs_posix_dirent_cmp,
};
