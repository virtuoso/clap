// SPDX-License-Identifier: Apache-2.0
#include "fs-ops.h"
#include "memory.h"

struct fs_dir {
    void            *handle;
    const struct fs_ops   *ops;
};

struct fs_file {
    void            *handle;
    const struct fs_ops   *ops;
};

DEFINE_CLEANUP(fs_dir, if (*p) CERR_RET(fs_close_dir(*p),))
DEFINE_CLEANUP(fs_file, if (*p) CERR_RET(fs_close(*p),))

cerr fs_get_cwd(const struct fs_ops *ops, char out_path[PATH_MAX])
{
    if (!ops || !ops->get_cwd)
        return CERR_NOT_SUPPORTED;
    return ops->get_cwd(out_path);
}

cresp(fs_dir) fs_open_dir(const struct fs_ops *ops, const char *path)
{
    if (!ops || !ops->open_dir)
        return cresp_error(fs_dir, CERR_NOT_SUPPORTED);

    void *handle = CRES_RET_T(ops->open_dir(path), fs_dir);

    fs_dir *dir = mem_alloc(sizeof(*dir));
    if (!dir) {
        if (ops->close_dir)
            (void)ops->close_dir(handle);
        return cresp_error(fs_dir, CERR_NOMEM);
    }

    dir->handle = handle;
    dir->ops = ops;

    return cresp_val(fs_dir, dir);
}

cerr fs_read_dir(fs_dir *dir, struct fs_dirent *out)
{
    if (!dir || !dir->ops || !dir->ops->read_dir)
        return CERR_INVALID_ARGUMENTS;
    return dir->ops->read_dir(dir->handle, out);
}

cerr fs_close_dir(fs_dir *dir)
{
    if (!dir)
        return CERR_OK;

    if (dir->ops && dir->ops->close_dir)
        (void)dir->ops->close_dir(dir->handle);

    mem_free(dir);
    return CERR_OK;
}

cerr fs_make_dir(const struct fs_ops *ops, const char *path)
{
    if (!ops || !ops->make_dir)
        return CERR_NOT_SUPPORTED;
    return ops->make_dir(path);
}

cerr fs_remove_dir(const struct fs_ops *ops, const char *path)
{
    if (!ops || !ops->remove_dir)
        return CERR_NOT_SUPPORTED;
    return ops->remove_dir(path);
}

cresp(fs_file) fs_open(const struct fs_ops *ops, const char *path, enum fs_mode mode, bool create, bool exclusive, bool binary)
{
    if (exclusive && !create)
        return cresp_error(fs_file, CERR_INVALID_ARGUMENTS);

    if ((create || exclusive) && mode == FS_READ)
        return cresp_error(fs_file, CERR_INVALID_ARGUMENTS);

    if (!ops || !ops->open)
        return cresp_error(fs_file, CERR_NOT_SUPPORTED);

    void *handle = CRES_RET_T(ops->open(path, mode, create, exclusive, binary), fs_file);

    fs_file *file = mem_alloc(sizeof(*file));
    if (!file) {
        if (ops->close)
            CERR_RET(ops->close(handle),);
        return cresp_error(fs_file, CERR_NOMEM);
    }

    file->handle = handle;
    file->ops = ops;

    return cresp_val(fs_file, file);
}

cerr fs_close(fs_file *file)
{
    if (!file)
        return CERR_OK;

    cerr err = CERR_OK;
    if (file->ops && file->ops->close)
        err = file->ops->close(file->handle);

    mem_free(file);
    return err;
}

cres(size_t) fs_read(fs_file *file, void *buf, size_t size)
{
    if (!file || !file->ops || !file->ops->read)
        return cres_error(size_t, CERR_INVALID_ARGUMENTS);
    return file->ops->read(file->handle, buf, size);
}

cres(size_t) fs_write(fs_file *file, const void *buf, size_t size)
{
    if (!file || !file->ops || !file->ops->write)
        return cres_error(size_t, CERR_INVALID_ARGUMENTS);
    return file->ops->write(file->handle, buf, size);
}

cerr fs_seek(fs_file *file, long offset, enum fs_seek_origin origin)
{
    if (!file || !file->ops || !file->ops->seek)
        return CERR_INVALID_ARGUMENTS;
    return file->ops->seek(file->handle, offset, origin);
}
