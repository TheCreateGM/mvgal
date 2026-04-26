/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Cross-Vendor Synchronization Header
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_SYNC_H_
#define _MVGAL_SYNC_H_

#include <linux/types.h>

/* Maximum number of timelines */
#define MVGAL_MAX_TIMELINES 64

/* Fence flags */
#define MVGAL_FENCE_SIGNALED (1 << 0) /* Fence is already signaled */

/*
 * IOCTL structures
 */

/* Wait fence arguments */
struct mvgal_wait_fence_args {
	uint32_t fence_id;    /* Input: fence ID to wait for */
	uint64_t timeout_ns;  /* Input: timeout in nanoseconds */
};

/* Signal fence arguments */
struct mvgal_signal_fence_args {
	uint32_t fence_id;    /* Input: fence ID to signal */
};

/*
 * Forward declaration for mvgal_fence (used in mvgal_device.c)
 */
struct mvgal_fence;

/*
 * Function declarations
 */

/* Synchronization subsystem initialization */
int mvgal_sync_init(void);
void mvgal_sync_fini(void);

/* Timeline management */
struct mvgal_timeline *mvgal_timeline_create(void);
struct mvgal_timeline;
int mvgal_timeline_signal(struct mvgal_timeline *timeline, uint64_t value);
int mvgal_timeline_wait(struct mvgal_timeline *timeline, uint64_t wait_value, uint64_t timeout_ns);

/* Fence management */
struct mvgal_fence *mvgal_fence_alloc(struct mvgal_timeline *timeline, uint32_t flags);
void mvgal_fence_free(struct mvgal_fence *fence);
int mvgal_fence_signal(struct mvgal_fence *fence);
int mvgal_fence_wait(struct mvgal_fence *fence, uint64_t timeout_ns);

/* IOCTL handlers */
int mvgal_ioctl_wait_fence(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_signal_fence(struct drm_device *drm, void *data, struct drm_file *file);

#endif /* _MVGAL_SYNC_H_ */
