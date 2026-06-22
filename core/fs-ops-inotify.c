// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <poll.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include "fs-ops.h"
#include "memory.h"

/*
 * IN_CLOSE_WRITE rather than IN_MODIFY: the latter fires per write(), the
 * former once, when the writer is done and the file is whole.
 */
#define FS_INOTIFY_MASK \
    (IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO)

struct fs_inotify_watch {
    size_t  len;    /* bytes valid in @buf */
    size_t  off;    /* parse offset into @buf */
    int     fd;
    /* Buffer must be aligned for struct inotify_event; hold several events */
    alignas(struct inotify_event) char buf[16 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
};

static unsigned int fs_inotify_flags(uint32_t mask)
{
    unsigned int out = 0;

    if (mask & IN_CREATE)                       out |= FS_WATCH_CREATED;
    if (mask & IN_DELETE)                       out |= FS_WATCH_DELETED;
    if (mask & IN_CLOSE_WRITE)                  out |= FS_WATCH_MODIFIED;
    if (mask & (IN_MOVED_FROM | IN_MOVED_TO))   out |= FS_WATCH_RENAMED;

    return out;
}

static cresp(void) fs_inotify_watch_open(const char *path)
{
    if (!path)      return cresp_error(void, CERR_INVALID_ARGUMENTS);

    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0)     return cresp_error_cerr(void, errno_to_cerr(errno));

    if (inotify_add_watch(fd, path, FS_INOTIFY_MASK) < 0) {
        cerr err = errno_to_cerr(errno);
        close(fd);
        return cresp_error_cerr(void, err);
    }

    struct fs_inotify_watch *watch = mem_alloc(sizeof(*watch), .zero = 1);
    if (!watch) {
        close(fd);
        return cresp_error(void, CERR_NOMEM);
    }

    watch->fd = fd;

    return cresp_val(void, watch);
}

static cerr fs_inotify_watch_read(void *handle, struct fs_watch_event *out)
{
    struct fs_inotify_watch *watch = handle;

    if (!watch || !out)     return CERR_INVALID_ARGUMENTS;

    for (;;) {
        while (watch->off < watch->len) {
            struct inotify_event *ie = (void *)(watch->buf + watch->off);
            watch->off += sizeof(*ie) + ie->len;

            unsigned int kind = fs_inotify_flags(ie->mask);
            if (!kind || !ie->len) /* skip unmapped or nameless (whole-dir) events */
                continue;

            snprintf(out->name, sizeof(out->name), "%s", ie->name);
            out->flags = kind;

            /*
             * Merge the events that follow for the same name, so that a
             * create and the close-write that finishes it come out as one
             * event, the way the watch contract has it, rather than as two
             * reasons for the receiver to act on the same file.
             */
            while (watch->off < watch->len) {
                struct inotify_event *next = (void *)(watch->buf + watch->off);

                kind = fs_inotify_flags(next->mask);
                if (!kind || !next->len || strcmp(next->name, ie->name))  break;

                out->flags |= kind;
                watch->off += sizeof(*next) + next->len;
            }
            return CERR_OK;
        }

        ssize_t n = read(watch->fd, watch->buf, sizeof(watch->buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)    return CERR_EOF;
            return errno_to_cerr(errno);
        }
        if (n == 0)     return CERR_EOF;

        watch->len = n;
        watch->off = 0;
    }
}

static cerr fs_inotify_watch_poll(void *const *handles, size_t n, int timeout_ms)
{
    if (!handles || !n)     return CERR_INVALID_ARGUMENTS;

    struct pollfd pfds[n];

    for (size_t i = 0; i < n; i++) {
        struct fs_inotify_watch *watch = handles[i];

        /* A watch with buffered, not-yet-read events is already ready */
        if (watch->off < watch->len)    return CERR_OK;

        pfds[i].fd = watch->fd;
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }

    int rc = poll(pfds, (nfds_t)n, timeout_ms);
    if (rc < 0) {
        if (errno == EINTR)             return CERR_EOF;
        return errno_to_cerr(errno);
    }
    if (rc == 0)            return CERR_EOF;

    return CERR_OK;
}

static void fs_inotify_watch_close(void *handle)
{
    struct fs_inotify_watch *watch = handle;

    if (!watch)             return;

    /* closing the fd also drops the inotify watch */
    if (watch->fd >= 0)     close(watch->fd);

    mem_free(watch);
}

const struct fs_watch_ops fs_watch_ops_inotify = {
    .open   = fs_inotify_watch_open,
    .read   = fs_inotify_watch_read,
    .poll   = fs_inotify_watch_poll,
    .close  = fs_inotify_watch_close,
};
