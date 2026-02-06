// SPDX-License-Identifier: Apache-2.0
#include <dirent.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "fs-ops.h"

static cerr errno_to_cerr(int err)
{
    switch (err) {
    case 0:
        return CERR_OK;
    case ENOMEM:
        return CERR_NOMEM;
    case EACCES:
        return CERR_ACCESS_DENIED;
    case ENOENT:
        return CERR_NOT_FOUND;
    case ENOTDIR:
        return CERR_NOT_A_DIRECTORY;
    case EMFILE:
    case ENFILE:
        return CERR_TOO_MANY_OPEN_FILES;
    case ENAMETOOLONG:
        return CERR_NAME_TOO_LONG;
    case EINVAL:
        return CERR_INVALID_ARGUMENTS;
    default:
        return CERR_UNKNOWN_ERROR;
    }
}

static cerr fs_posix_get_cwd(char out_path[PATH_MAX])
{
    if (!getcwd(out_path, PATH_MAX))
        return errno_to_cerr(errno);

    return CERR_OK;
}

static cresp(void) fs_posix_open_dir(const char *path)
{
    DIR *dir = opendir(path);

    if (!dir)
        return cresp_error_cerr(void, errno_to_cerr(errno));

    return cresp_val(void, dir);
}

static cerr fs_posix_read_dir(void *dir, struct fs_dirent *out)
{
    errno = 0;
    struct dirent *de = readdir(dir);

    if (!de) {
        if (errno != 0)
            return errno_to_cerr(errno);
        return CERR_EOF;
    }

    snprintf(out->name, sizeof(out->name), "%s", de->d_name);

    /*
     * Emscripten's stat::st_mode is unreliable;
     * _WIN32's dirent doesn't have dirent::d_type
     */
#ifdef _WIN32
    struct stat st;
    if (stat(out->name, &st) != 0)
        return errno_to_cerr(errno);
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

static cresp(void) fs_posix_open(const char *path, enum fs_mode mode, bool create, bool exclusive, bool binary)
{
    FILE *f = NULL;

    if (!create) {
        f = fopen(path, "r");
        if (!f)
            return cresp_error_cerr(void, errno_to_cerr(errno));
    }

    char mode_buf[8];
    const char *m;

    if (binary && mode == FS_APPEND)
        return cresp_error(void, CERR_INVALID_ARGUMENTS);

    switch (mode) {
    case FS_READ:   m = binary ? FOPEN_RB : "r"; break;
    case FS_WRITE:  m = binary ? FOPEN_WB : "w"; break;
    case FS_APPEND: m = "a"; break;
    case FS_BOTH:   m = "w+"; break;
    default:        return cresp_error(void, CERR_INVALID_ARGUMENTS);
    }

    snprintf(mode_buf, sizeof(mode_buf), "%s%s%s", m,
             (binary && mode == FS_BOTH) ? "b" : "",
             (mode != FS_READ && exclusive) ? "x" : "");

    if (create) {
        f = fopen(path, mode_buf);
    } else {
        f = freopen(path, mode_buf, f);
    }

    if (!f)
        return cresp_error_cerr(void, errno_to_cerr(errno));

    if (binary)
        compat_set_binary(f);

    return cresp_val(void, f);
}

static cerr fs_posix_close(void *handle)
{
    if (!handle)
        return CERR_OK;

    if (fclose(handle) != 0)
        return errno_to_cerr(errno);

    return CERR_OK;
}

static cres(size_t) fs_posix_read(void *handle, void *buf, size_t size)
{
    size_t read = fread(buf, 1, size, handle);

    if (read < size && ferror(handle))
        return cres_error_cerr(size_t, errno_to_cerr(errno));

    return cres_val(size_t, read);
}

static cres(size_t) fs_posix_write(void *handle, const void *buf, size_t size)
{
    size_t written = fwrite(buf, 1, size, handle);

    if (written < size && ferror(handle))
        return cres_error_cerr(size_t, errno_to_cerr(errno));

    return cres_val(size_t, written);
}

static cerr fs_posix_seek(void *handle, long offset, enum fs_seek_origin origin)
{
    int whence = SEEK_SET;

    switch (origin) {
    case FS_SEEK_SET:
        whence = SEEK_SET;
        break;
    case FS_SEEK_CUR:
        whence = SEEK_CUR;
        break;
    case FS_SEEK_END:
        whence = SEEK_END;
        break;
    }

    if (fseek(handle, offset, whence) != 0)
        return errno_to_cerr(errno);

    return CERR_OK;
}

const struct fs_ops fs_ops_posix = {
    .get_cwd    = fs_posix_get_cwd,
    .open_dir   = fs_posix_open_dir,
    .read_dir   = fs_posix_read_dir,
    .close_dir  = fs_posix_close_dir,
    .dirent_cmp = fs_posix_dirent_cmp,
    .open       = fs_posix_open,
    .close      = fs_posix_close,
    .read       = fs_posix_read,
    .write      = fs_posix_write,
    .seek       = fs_posix_seek,
};
