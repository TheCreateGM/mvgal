/*
 * mvgal_uapi_wrapper.c
 *
 * Simple libmvgal wrapper implementation around the MVGAL kernel UAPI.
 *
 * This file implements a small, thread-safe C wrapper that opens /dev/mvgal0
 * and provides convenience functions to call the MVGAL ioctls:
 *
 *   - mvgal_uapi_init / mvgal_uapi_shutdown
 *   - mvgal_uapi_set_device_path
 *   - mvgal_uapi_get_version
 *   - mvgal_uapi_get_gpu_count
 *   - mvgal_uapi_get_gpu_info
 *   - mvgal_uapi_get_caps
 *   - mvgal_uapi_rescan
 *   - mvgal_uapi_get_stats
 *
 * Functions return 0 on success, or a negative errno value on failure.
 *
 * The wrapper is intentionally minimal and portable: it does not allocate
 * or manage kernel memory, it simply proxies to the kernel UAPI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "mvgal/mvgal_uapi.h"

/* Export macro for shared library symbol visibility */
#if defined(_MSC_VER)
#  define MVGAL_API __declspec(dllexport)
#else
#  define MVGAL_API __attribute__((visibility("default")))
#endif

/* Default device path */
static char g_devpath[256] = "/dev/" MVGAL_DEVICE_NAME;

/* File descriptor and lock protecting it */
static int g_fd = -1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Internal helper: open device if not already open.
 * Returns 0 on success or negative errno on failure.
 */
static int mvgal_ensure_open(void)
{
    int fd = -1;
    int saved_errno = 0;

    if (g_fd >= 0)
        return 0;

    /* Acquire lock and open once */
    if (pthread_mutex_lock(&g_lock) != 0)
        return -EINTR;

    if (g_fd < 0) {
        fd = open(g_devpath, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            saved_errno = errno;
            pthread_mutex_unlock(&g_lock);
            return -saved_errno;
        }
        g_fd = fd;
    }

    pthread_mutex_unlock(&g_lock);
    return 0;
}

/* Public API */

/* Initialize wrapper and optionally set device path.
 * If devpath is NULL, default path is used.
 * Returns 0 or negative errno.
 */
MVGAL_API int mvgal_uapi_init(const char *devpath)
{
    int ret = 0;

    if (devpath && devpath[0] != '\0') {
        /* Copy path under lock to avoid races with concurrent opens */
        if (pthread_mutex_lock(&g_lock) != 0)
            return -EINTR;
        strncpy(g_devpath, devpath, sizeof(g_devpath) - 1);
        g_devpath[sizeof(g_devpath) - 1] = '\0';
        /* If a descriptor was already open for a different path, close it so the next
         * ensure_open will reopen the new path.
         */
        if (g_fd >= 0) {
            close(g_fd);
            g_fd = -1;
        }
        pthread_mutex_unlock(&g_lock);
    }

    ret = mvgal_ensure_open();
    return ret;
}

/* Shut down wrapper, closing the device if open. */
MVGAL_API void mvgal_uapi_shutdown(void)
{
    if (pthread_mutex_lock(&g_lock) != 0)
        return;

    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }

    pthread_mutex_unlock(&g_lock);
}

/* Set the device path to use for subsequent operations. If a device is already
 * open it will be closed so the next operation reopens the new path.
 * Returns 0 or negative errno.
 */
MVGAL_API int mvgal_uapi_set_device_path(const char *devpath)
{
    if (!devpath || devpath[0] == '\0')
        return -EINVAL;

    return mvgal_uapi_init(devpath); /* init implementation updates path and reopens */
}

/* Query version */
MVGAL_API int mvgal_uapi_get_version(struct mvgal_uapi_version *out)
{
    int ret;

    if (!out)
        return -EINVAL;

    ret = mvgal_ensure_open();
    if (ret < 0)
        return ret;

    if (ioctl(g_fd, MVGAL_IOC_QUERY_VERSION, out) != 0)
        return -errno;

    return 0;
}

/* Get GPU count */
MVGAL_API int mvgal_uapi_get_gpu_count(uint32_t *out_count)
{
    int ret;
    struct mvgal_uapi_gpu_count count = {0};

    if (!out_count)
        return -EINVAL;

    ret = mvgal_ensure_open();
    if (ret < 0)
        return ret;

    if (ioctl(g_fd, MVGAL_IOC_GET_GPU_COUNT, &count) != 0)
        return -errno;

    *out_count = count.gpu_count;
    return 0;
}

/* Get GPU info by index */
MVGAL_API int mvgal_uapi_get_gpu_info(uint32_t index, struct mvgal_gpu_info *out_info)
{
    int ret;
    struct mvgal_uapi_gpu_query q;

    if (!out_info)
        return -EINVAL;

    ret = mvgal_ensure_open();
    if (ret < 0)
        return ret;

    memset(&q, 0, sizeof(q));
    q.index = index;

    if (ioctl(g_fd, MVGAL_IOC_GET_GPU_INFO, &q) != 0)
        return -errno;

    /* copy result to caller */
    *out_info = q.info;
    return 0;
}

/* Get capabilities */
MVGAL_API int mvgal_uapi_get_caps(struct mvgal_uapi_caps *out_caps)
{
    int ret;

    if (!out_caps)
        return -EINVAL;

    ret = mvgal_ensure_open();
    if (ret < 0)
        return ret;

    if (ioctl(g_fd, MVGAL_IOC_GET_CAPS, out_caps) != 0)
        return -errno;

    return 0;
}

/* Request rescan */
MVGAL_API int mvgal_uapi_rescan(void)
{
    int ret;

    ret = mvgal_ensure_open();
    if (ret < 0)
        return ret;

    if (ioctl(g_fd, MVGAL_IOC_RESCAN) != 0)
        return -errno;

    return 0;
}

/* Get stats */
MVGAL_API int mvgal_uapi_get_stats(struct mvgal_uapi_stats *out_stats)
{
    int ret;

    if (!out_stats)
        return -EINVAL;

    ret = mvgal_ensure_open();
    if (ret < 0)
        return ret;

    if (ioctl(g_fd, MVGAL_IOC_GET_STATS, out_stats) != 0)
        return -errno;

    return 0;
}

/* Convenience helper: try to reopen device if we detect a stale FD.
 * Returns 0 on success, negative errno on failure.
 */
MVGAL_API int mvgal_uapi_reopen_if_needed(void)
{
    int ret;

    if (pthread_mutex_lock(&g_lock) != 0)
        return -EINTR;

    if (g_fd >= 0) {
        /* quick check: fcntl(F_GETFD) will detect bad fd by returning -1 and setting EBADF */
        if (fcntl(g_fd, F_GETFD) == -1 && errno == EBADF) {
            close(g_fd);
            g_fd = -1;
        }
    }

    if (g_fd < 0) {
        int fd = open(g_devpath, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            int e = errno;
            pthread_mutex_unlock(&g_lock);
            return -e;
        }
        g_fd = fd;
    }

    pthread_mutex_unlock(&g_lock);
    return 0;
}