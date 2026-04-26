/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Cross-Vendor Synchronization Primitives
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/dma-buf.h>
#include <linux/atomic.h>

#include "mvgal_core.h"
#include "mvgal_sync.h"

/*
 * Fence structure
 * Represents a synchronization fence across GPUs
 */
struct mvgal_fence {
	struct list_head node;            /* Node in fence list */
	uint32_t seq;                     /* Sequence number */
	uint32_t flags;                   /* Fence flags */
	
	/* Timeline */
	uint64_t timeline_value;         /* Value on timeline when signaled */
	
	/* Synchronization */
	wait_queue_head_t waitq;
	atomic_t signaled;                 /* Fence has been signaled */
	
	/* Context */
	struct mvgal_device *dev;        /* Device this fence belongs to */
	void *external_fence;            /* External sync_file or dma_fence */
	
	/* Timeout */
	uint64_t timeout_ns;             /* Timeout in nanoseconds */
};

/*
 *Timeline structure
 */
struct mvgal_timeline {
	struct list_head fences;          /* List of fences on this timeline */
	struct mutex lock;                /* Protects timeline */
	uint64_t current_value;           /* Current timeline value */
	uint64_t next_value;              /* Next value to be assigned */
	
	/* Waiters */
	wait_queue_head_t waitq;
	atomic_t signaled_value;          /* Last signaled value */
};

/* Synchronization manager */
static struct {
	struct mvgal_timeline timelines[MVGAL_MAX_TIMELINES];
	struct list_head fence_list;
	struct mutex lock;
} sync_manager;

/*
 * mvgal_sync_init - Initialize synchronization subsystem
 */
int mvgal_sync_init(void)
{
	int i;

	INIT_LIST_HEAD(&sync_manager.fence_list);
	mutex_init(&sync_manager.lock);

	for (i = 0; i < MVGAL_MAX_TIMELINES; i++) {
		INIT_LIST_HEAD(&sync_manager.timelines[i].fences);
		mutex_init(&sync_manager.timelines[i].lock);
		sync_manager.timelines[i].current_value = 0;
		sync_manager.timelines[i].next_value = 1;
		init_waitqueue_head(&sync_manager.timelines[i].waitq);
		atomic_set(&sync_manager.timelines[i].signaled_value, 0);
	}

	return 0;
}

/*
 * mvgal_sync_fini - Cleanup synchronization subsystem
 */
void mvgal_sync_fini(void)
{
	int i;

	for (i = 0; i < MVGAL_MAX_TIMELINES; i++) {
		mutex_destroy(&sync_manager.timelines[i].lock);
	}
	
	mutex_destroy(&sync_manager.lock);
}

/*
 * mvgal_timeline_create - Create a new timeline for a GPU
 */
struct mvgal_timeline *mvgal_timeline_create(void)
{
	int i;

	mutex_lock(&sync_manager.lock);
	
	/* Find a free timeline */
	for (i = 0; i < MVGAL_MAX_TIMELINES; i++) {
		if (sync_manager.timelines[i].current_value == 0) {
			mutex_unlock(&sync_manager.lock);
			return &sync_manager.timelines[i];
		}
	}
	
	mutex_unlock(&sync_manager.lock);
	return NULL;
}

/*
 * mvgal_fence_alloc - Allocate a new fence
 */
struct mvgal_fence *mvgal_fence_alloc(struct mvgal_timeline *timeline, uint32_t flags)
{
	struct mvgal_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence) {
		return NULL;
	}

	INIT_LIST_HEAD(&fence->node);
	fence->flags = flags;
	fence->dev = mvgal_logical_device;
	init_waitqueue_head(&fence->waitq);
	atomic_set(&fence->signaled, 0);

	if (timeline) {
		mutex_lock(&timeline->lock);
		fence->seq = timeline->next_value++;
		fence->timeline_value = timeline->next_value;
		mutex_unlock(&timeline->lock);
	}

	return fence;
}

/*
 * mvgal_fence_free - Free a fence
 */
void mvgal_fence_free(struct mvgal_fence *fence)
{
	if (!fence) {
		return;
	}

	/* Remove from timeline */
	if (fence->timeline_value > 0) {
		/* TODO: Remove from timeline */
	}

	kfree(fence);
}

/*
 * mvgal_fence_signal - Signal a fence as complete
 */
int mvgal_fence_signal(struct mvgal_fence *fence)
{
	if (!fence) {
		return -EINVAL;
	}

	/* Update timeline */
	if (fence->timeline_value > 0) {
		/* TODO: Update timeline value */
	}

	atomic_set(&fence->signaled, 1);
	wake_up_all(&fence->waitq);

	return 0;
}

/*
 * mvgal_fence_wait - Wait for a fence to signal
 */
int mvgal_fence_wait(struct mvgal_fence *fence, uint64_t timeout_ns)
{
	if (!fence) {
		return -EINVAL;
	}

	if (atomic_read(&fence->signaled)) {
		return 0;
	}

	if (timeout_ns == 0) {
		/* Wait forever */
		wait_event(fence->waitq, atomic_read(&fence->signaled) != 0);
		return 0;
	} else {
		/* Wait with timeout */
		unsigned long timeout_jiffies = nsecs_to_jiffies(timeout_ns);
		if (!wait_event_timeout(fence->waitq,
			atomic_read(&fence->signaled) != 0,
			timeout_jiffies)) {
			return -ETIMEDOUT;
		}
		return 0;
	}
}

/*
 * mvgal_timeline_signal - Signal a timeline to a specific value
 */
int mvgal_timeline_signal(struct mvgal_timeline *timeline, uint64_t value)
{
	if (!timeline) {
		return -EINVAL;
	}

	mutex_lock(&timeline->lock);
	
	if (value > timeline->current_value) {
		timeline->current_value = value;
		atomic_set(&timeline->signaled_value, value);
		wake_up_all(&timeline->waitq);
	}
	
	mutex_unlock(&timeline->lock);

	return 0;
}

/*
 * mvgal_timeline_wait - Wait for a timeline to reach a value
 */
int mvgal_timeline_wait(struct mvgal_timeline *timeline, uint64_t wait_value, uint64_t timeout_ns)
{
	if (!timeline) {
		return -EINVAL;
	}

	mutex_lock(&timeline->lock);
	
	if (timeline->current_value >= wait_value) {
		mutex_unlock(&timeline->lock);
		return 0;
	}

	if (timeout_ns == 0) {
		/* Wait forever */
		mutex_unlock(&timeline->lock);
		wait_event(timeline->waitq, timeline->current_value >= wait_value);
		return 0;
	} else {
		/* Wait with timeout */
		unsigned long timeout_jiffies = nsecs_to_jiffies(timeout_ns);
		mutex_unlock(&timeline->lock);
		if (!wait_event_timeout(timeline->waitq,
			timeline->current_value >= wait_value,
			timeout_jiffies)) {
			return -ETIMEDOUT;
		}
		return 0;
	}
}

/*
 * mvgal_ioctl_wait_fence - Handle MVGAL_IOCTL_WAIT_FENCE
 */
int mvgal_ioctl_wait_fence(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_wait_fence_args *args = data;
	struct mvgal_fence *fence;
	int ret;

	/* Find fence by ID */
	mutex_lock(&sync_manager.lock);
	
	list_for_each_entry(fence, &sync_manager.fence_list, node) {
		if (fence->seq == args->fence_id) {
			break;
		}
	}
	
	if (&fence->node == &sync_manager.fence_list) {
		mutex_unlock(&sync_manager.lock);
		return -ENOENT;
	}
	
	mutex_unlock(&sync_manager.lock);

	/* Wait for fence */
	ret = mvgal_fence_wait(fence, args->timeout_ns);
	mvgal_fence_free(fence);

	return ret;
}

/*
 * mvgal_ioctl_signal_fence - Handle MVGAL_IOCTL_SIGNAL_FENCE
 */
int mvgal_ioctl_signal_fence(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_signal_fence_args *args = data;
	struct mvgal_fence *fence;
	int ret;

	/* Find fence by ID */
	mutex_lock(&sync_manager.lock);
	
	list_for_each_entry(fence, &sync_manager.fence_list, node) {
		if (fence->seq == args->fence_id) {
			break;
		}
	}
	
	if (&fence->node == &sync_manager.fence_list) {
		mutex_unlock(&sync_manager.lock);
		return -ENOENT;
	}
	
	mutex_unlock(&sync_manager.lock);

	/* Signal fence */
	ret = mvgal_fence_signal(fence);
	mvgal_fence_free(fence);

	return ret;
}
