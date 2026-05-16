/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Kernel-side Workload Scheduler - Queue management and dispatch
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "mvgal_core.h"
#include "mvgal_scheduler.h"
#include "mvgal_device.h"

/*
 * Global scheduler state
 */
struct mvgal_scheduler_state scheduler;

/*
 * mvgal_scheduler_init - Initialize the scheduler
 */
int mvgal_scheduler_init(void)
{
	int i;

	for (i = 0; i < MVGAL_NUM_PRIORITY_QUEUES; i++) {
		INIT_LIST_HEAD(&scheduler.queues[i]);
	}
	
	mutex_init(&scheduler.lock);
	atomic_set(&scheduler.next_workload_id, 1);
	init_waitqueue_head(&scheduler.waitq);
	
	/* Create workqueue for dispatching workloads */
	scheduler.dispatch_wq = create_singlethread_workqueue("mvgal_dispatch");
	if (!scheduler.dispatch_wq) {
		return -ENOMEM;
	}

	return 0;
}

/*
 * mvgal_scheduler_fini - Cleanup the scheduler
 */
void mvgal_scheduler_fini(void)
{
	if (scheduler.dispatch_wq) {
		destroy_workqueue(scheduler.dispatch_wq);
	}
	mutex_destroy(&scheduler.lock);
}

/*
 * mvgal_workload_alloc - Allocate a new workload
 */
struct mvgal_workload *mvgal_workload_alloc(void)
{
	struct mvgal_workload *workload;

	workload = kzalloc(sizeof(*workload), GFP_KERNEL);
	if (!workload) {
		return NULL;
	}

	workload->id = atomic_fetch_inc(&scheduler.next_workload_id);
	workload->priority = MVGAL_DEFAULT_PRIORITY;
	workload->gpu_mask = ~0U; /* All GPUs by default */
	workload->state = MVGAL_SCHEDULE_STATE_PENDING;
	init_completion(&workload->completion);
	workload->num_memory_resources = 0;

	return workload;
}

/*
 * mvgal_workload_free - Free a workload
 */
void mvgal_workload_free(struct mvgal_workload *workload)
{
	if (!workload) {
		return;
	}
	kfree(workload);
}

/*
 * mvgal_workload_submit - Submit a workload to the scheduler
 */
int mvgal_workload_submit(struct mvgal_workload *workload)
{
	int priority_queue;

	if (!workload) {
		return -EINVAL;
	}

	/* Clamp priority */
	if (workload->priority > MVGAL_MAX_PRIORITY) {
		workload->priority = MVGAL_MAX_PRIORITY;
	}

	/* Determine which priority queue to use */
	priority_queue = workload->priority >> MVGAL_PRIORITY_SHIFT;
	if (priority_queue >= MVGAL_NUM_PRIORITY_QUEUES) {
		priority_queue = MVGAL_NUM_PRIORITY_QUEUES - 1;
	}

	mutex_lock(&scheduler.lock);
	
	/* Add to appropriate priority queue */
	list_add_tail(&workload->node, &scheduler.queues[priority_queue]);
	
	mutex_unlock(&scheduler.lock);

	/* Wake up the dispatcher */
	wake_up(&scheduler.waitq);

	return 0;
}

/*
 * mvgal_workload_wait - Wait for a workload to complete
 */
int mvgal_workload_wait(struct mvgal_workload *workload, uint32_t timeout_ms)
{
	if (!workload) {
		return -EINVAL;
	}

	if (workload->state == MVGAL_SCHEDULE_STATE_COMPLETE ||
		workload->state == MVGAL_SCHEDULE_STATE_ERROR) {
		return workload->result;
	}

	/* Wait with timeout */
	if (timeout_ms == 0) {
		/* Wait forever */
		wait_for_completion(&workload->completion);
	} else {
		unsigned long timeout_jiffies = msecs_to_jiffies(timeout_ms);
		if (!wait_for_completion_timeout(&workload->completion, timeout_jiffies)) {
			return -ETIMEDOUT;
		}
	}

	return workload->result;
}

/*
 * mvgal_workload_signal - Signal that a workload is complete
 */
void mvgal_workload_signal(struct mvgal_workload *workload, int result)
{
	if (!workload) {
		return;
	}

	workload->result = result;
	workload->state = (result == 0) ? MVGAL_SCHEDULE_STATE_COMPLETE : MVGAL_SCHEDULE_STATE_ERROR;
	workload->signaled = true;
	complete(&workload->completion);
}

/*
 * mvgal_scheduler_dispatch - Dispatch thread main function
 */
static void mvgal_scheduler_dispatch(struct work_struct *work)
{
	struct mvgal_workload *workload;
	struct mvgal_gpu_device *gpu;
	int ret;
	int i;

	/* Process all priority queues from highest to lowest */
	for (i = MVGAL_NUM_PRIORITY_QUEUES - 1; i >= 0; i--) {
		mutex_lock(&scheduler.lock);
		
		while (!list_empty(&scheduler.queues[i])) {
			workload = list_first_entry(&scheduler.queues[i], struct mvgal_workload, node);
			list_del(&workload->node);
			
			mutex_unlock(&scheduler.lock);
			
			/* Find an available GPU */
			gpu = mvgal_find_available_gpu(workload->gpu_mask);
			if (!gpu) {
				/* No GPU available, requeue */
				mutex_lock(&scheduler.lock);
				list_add_tail(&workload->node, &scheduler.queues[i]);
				mutex_unlock(&scheduler.lock);
				return;
			}
			
			/* Mark as running */
			workload->state = MVGAL_SCHEDULE_STATE_RUNNING;
			workload->assigned_gpu = gpu;
			workload->start_time = ktime_get_ns();
			
			/* Submit to GPU */
			if (gpu->ops && gpu->ops->submit_cs) {
				ret = gpu->ops->submit_cs(gpu, workload);
			} else {
				/* No vendor ops, mark as complete immediately */
				ret = 0;
			}
			
			workload->end_time = ktime_get_ns();
			
			/* Signal completion */
			mvgal_workload_signal(workload, ret);
			
			mutex_lock(&scheduler.lock);
		}
		
		mutex_unlock(&scheduler.lock);
	}
}

/*
 * mvgal_find_available_gpu - Find an available GPU matching the mask
 */
struct mvgal_gpu_device *mvgal_find_available_gpu(uint32_t gpu_mask)
{
	struct mvgal_gpu_device *gpu;
	int best_utilization = INT_MAX;
	struct mvgal_gpu_device *best_gpu = NULL;

	/* Iterate through all GPUs and pick the one with lowest utilization */
	mutex_lock(&mvgal_logical_device->gpu_lock);
	
	list_for_each_entry(gpu, &mvgal_logical_device->gpu_list, node) {
		int gpu_idx = gpu->pdev->devfn & 0x1F; /* PCI device number */
		
		/* Check if this GPU is in the mask */
		if (gpu_mask & (1 << gpu_idx)) {
			/* Check if enabled and available */
			if (gpu->enabled && gpu->available) {
				/* TODO: Query actual utilization from vendor driver */
				if (gpu->utilization < (uint32_t)best_utilization) {
					best_utilization = gpu->utilization;
					best_gpu = gpu;
				}
			}
		}
	}
	
	mutex_unlock(&mvgal_logical_device->gpu_lock);
	
	return best_gpu;
}

/*
 * mvgal_ioctl_submit_workload - Handle MVGAL_IOCTL_SUBMIT_WORKLOAD
 * 
 * Implements workload submission according to DESIGN.md section 3.2.3
 * Validates parameters, allocates workload, selects GPU, and submits to vendor driver
 */
int mvgal_ioctl_submit_workload(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_submit_workload_args *args = data;
	struct mvgal_workload *workload;
	struct mvgal_gpu_device *gpu;
	int ret;

	/* Validate workload parameters - check command buffer size limit */
	if (args->command_buffer_size > MVGAL_MAX_WORKLOAD_SIZE) {
		return -EINVAL;
	}

	/* Validate workload type */
	if (args->workload_type > MVGAL_WORKLOAD_TRANSFER) {
		return -EINVAL;
	}

	/* Allocate workload structure */
	workload = mvgal_workload_alloc();
	if (!workload) {
		return -ENOMEM;
	}

	/* Copy workload data from user space */
	if (copy_from_user(&workload->command_buffer_addr, &args->command_buffer_addr, sizeof(uint64_t))) {
		ret = -EFAULT;
		goto err_free;
	}
	if (copy_from_user(&workload->command_buffer_size, &args->command_buffer_size, sizeof(size_t))) {
		ret = -EFAULT;
		goto err_free;
	}
	if (copy_from_user(&workload->priority, &args->priority, sizeof(uint32_t))) {
		ret = -EFAULT;
		goto err_free;
	}
	if (copy_from_user(&workload->gpu_mask, &args->gpu_mask, sizeof(uint32_t))) {
		ret = -EFAULT;
		goto err_free;
	}

	/* Set workload type from args - per DESIGN.md section 3.2.3 */
	workload->type = args->workload_type;

	/* Select GPU based on scheduler policy - per DESIGN.md section 3.2.3 */
	gpu = mvgal_find_available_gpu(workload->gpu_mask);
	if (!gpu) {
		ret = -ENODEV;
		goto err_free;
	}

	workload->assigned_gpu = gpu;

	/* Submit to vendor driver - per DESIGN.md section 3.2.3 */
	if (gpu->ops && gpu->ops->submit_cs) {
		ret = gpu->ops->submit_cs(gpu, workload);
	} else {
		/* No vendor ops - mark complete immediately for testing */
		ret = 0;
		workload->state = MVGAL_SCHEDULE_STATE_COMPLETE;
	}

	if (ret < 0) {
		goto err_free;
	}

	/* Return workload ID */
	args->workload_id = workload->id;

	/* Wait if requested - preserve existing wait functionality */
	if (args->flags & MVGAL_SUBMIT_FLAG_WAIT) {
		ret = mvgal_workload_wait(workload, args->timeout_ms);
		args->result = ret;
	}

	/* Store workload in file private data for later reference */
	if (file->driver_priv) {
		struct mvgal_file *mvgal_file = file->driver_priv;
		mutex_lock(&mvgal_file->lock);
		list_add_tail(&workload->node, &mvgal_file->workloads);
		mutex_unlock(&mvgal_file->lock);
	}

	return 0;

err_free:
	mvgal_workload_free(workload);
	return ret;
}
