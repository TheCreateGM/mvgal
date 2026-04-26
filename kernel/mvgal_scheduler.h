/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Kernel-side Workload Scheduler Header
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_SCHEDULER_H_
#define _MVGAL_SCHEDULER_H_

#include "mvgal_core.h"

/* Maximum number of priority queues */
#define MVGAL_NUM_PRIORITY_QUEUES 4

/* Priority shift for queue selection */
#define MVGAL_PRIORITY_SHIFT 2

/* Default priority */
#define MVGAL_DEFAULT_PRIORITY 8

/* Maximum priority */
#define MVGAL_MAX_PRIORITY 15

/* Maximum memory resources per workload */
#define MVGAL_MAX_MEMORY_RESOURCES 16

/*
 * Workload state
 */
enum mvgal_schedule_state {
	MVGAL_SCHEDULE_STATE_PENDING = 0,
	MVGAL_SCHEDULE_STATE_RUNNING = 1,
	MVGAL_SCHEDULE_STATE_COMPLETE = 2,
	MVGAL_SCHEDULE_STATE_ERROR = 3,
};

/*
 * Workload type
 */
enum mvgal_workload_type {
	MVGAL_WORKLOAD_GRAPHICS = 0,
	MVGAL_WORKLOAD_COMPUTE = 1,
	MVGAL_WORKLOAD_TRANSFER = 2,
};

/* Submit flags */
#define MVGAL_SUBMIT_FLAG_WAIT  (1 << 0) /* Wait for completion */
#define MVGAL_SUBMIT_FLAG_SIGNAL (1 << 1) /* Signal fence on completion */

/*
 * IOCTL structures
 */

/* Submit workload arguments */
struct mvgal_submit_workload_args {
	uint32_t workload_id;        /* Output: assigned workload ID */
	uint32_t priority;          /* Input: priority level (0-15) */
	uint32_t gpu_mask;          /* Input: bitmask of allowed GPUs */
	uint32_t flags;             /* Input: submit flags */
	uint32_t timeout_ms;        /* Input: timeout for wait */
	int32_t result;            /* Output: execution result */
	uint64_t command_buffer_addr;/* Input: command buffer address */
	size_t command_buffer_size;  /* Input: command buffer size */
	uint32_t num_memory_resources; /* Input: number of memory resources */
	/* Followed by mvgal_memory_resource structs */
};

/*
 * Function declarations
 */

/* Scheduler initialization */
int mvgal_scheduler_init(void);
void mvgal_scheduler_fini(void);

/* Workload management */
struct mvgal_workload *mvgal_workload_alloc(void);
void mvgal_workload_free(struct mvgal_workload *workload);
int mvgal_workload_submit(struct mvgal_workload *workload);
int mvgal_workload_wait(struct mvgal_workload *workload, uint32_t timeout_ms);
void mvgal_workload_signal(struct mvgal_workload *workload, int result);

/* GPU selection */
struct mvgal_gpu_device *mvgal_find_available_gpu(uint32_t gpu_mask);

/* IOCTL handler */
int mvgal_ioctl_submit_workload(struct drm_device *drm, void *data, struct drm_file *file);

#endif /* _MVGAL_SCHEDULER_H_ */
