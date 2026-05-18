/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 *
 * Power Management - DVFS, Thermal Throttling, Dynamic Power Budgeting
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/thermal.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "mvgal_core.h"
#include "mvgal_device.h"
#include "mvgal_power.h"

/* Default DVFS parameters */
#define MVGAL_DVFS_DEFAULT_MIN_FREQ_MHZ    300
#define MVGAL_DVFS_DEFAULT_MAX_FREQ_MHZ    2000
#define MVGAL_DVFS_DEFAULT_UP_THRESHOLD    80
#define MVGAL_DVFS_DEFAULT_DOWN_THRESHOLD  30
#define MVGAL_DVFS_DEFAULT_STEP_DELAY_MS  100

/* Default thermal parameters */
#define MVGAL_THERMAL_DEFAULT_THROTTLE_TEMP_C  75
#define MVGAL_THERMAL_DEFAULT_CRITICAL_TEMP_C  95

/* Default power budget parameters */
#define MVGAL_POWER_DEFAULT_TOTAL_WATTS     500
#define MVGAL_POWER_DEFAULT_HEADROOM_WATTS  50

/* DVFS workqueue */
static struct workqueue_struct *mvgal_dvfs_wq;

/* Global power management state */
static struct {
	struct mvgal_power_budget budget;
	bool initialized;
} mvgal_power_state;

/* Forward declarations */
static int mvgal_set_gpu_freq(struct mvgal_gpu_device *gpu, uint32_t freq_mhz);
static int mvgal_get_gpu_freq(struct mvgal_gpu_device *gpu, uint32_t *freq_mhz);
static int mvgal_read_gpu_temp(struct mvgal_gpu_device *gpu, int32_t *temp_c);
static int mvgal_read_gpu_power(struct mvgal_gpu_device *gpu, uint32_t *power_watts);

/**
 * Initialize DVFS for a GPU
 */
int mvgal_dvfs_init(struct mvgal_gpu_device *gpu)
{
	struct mvgal_gpu_dvfs *dvfs;

	if (!gpu)
		return -EINVAL;

	dvfs = kzalloc(sizeof(*dvfs), GFP_KERNEL);
	if (!dvfs)
		return -ENOMEM;

	dvfs->gpu = gpu;
	dvfs->enabled = true;

	/* Set default policy */
	dvfs->policy.mode = MVGAL_DVFS_BALANCED;
	dvfs->policy.min_freq_mhz = MVGAL_DVFS_DEFAULT_MIN_FREQ_MHZ;
	dvfs->policy.max_freq_mhz = MVGAL_DVFS_DEFAULT_MAX_FREQ_MHZ;
	dvfs->policy.up_threshold = MVGAL_DVFS_DEFAULT_UP_THRESHOLD;
	dvfs->policy.down_threshold = MVGAL_DVFS_DEFAULT_DOWN_THRESHOLD;
	dvfs->policy.step_delay_ms = MVGAL_DVFS_DEFAULT_STEP_DELAY_MS;

	/* Initialize to max frequency */
	dvfs->current_freq_mhz = dvfs->policy.max_freq_mhz;
	dvfs->target_freq_mhz = dvfs->current_freq_mhz;

 spin_lock_init(&dvfs->lock);

	INIT_DELAYED_WORK(&dvfs->dvfs_work, NULL);

	gpu->dvfs = dvfs;

	pr_debug("MVGAL: DVFS initialized for GPU %s\n", gpu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_dvfs_init);

/**
 * Cleanup DVFS for a GPU
 */
void mvgal_dvfs_fini(struct mvgal_gpu_device *gpu)
{
	struct mvgal_gpu_dvfs *dvfs;

	if (!gpu || !gpu->dvfs)
		return;

	dvfs = gpu->dvfs;

	cancel_delayed_work_sync(&dvfs->dvfs_work);
	kfree(dvfs);
	gpu->dvfs = NULL;

	pr_debug("MVGAL: DVFS cleaned up for GPU %s\n", gpu->name);
}
EXPORT_SYMBOL_GPL(mvgal_dvfs_fini);

/**
 * DVFS work handler - adjusts frequency based on utilization
 */
static void mvgal_dvfs_work_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mvgal_gpu_dvfs *dvfs = container_of(dwork, struct mvgal_gpu_dvfs, dvfs_work);
	struct mvgal_gpu_device *gpu = dvfs->gpu;
	uint32_t utilization;
	int ret;

	if (!dvfs->enabled)
		return;

	/* Get current utilization */
	if (gpu->ops && gpu->ops->query_utilization) {
		ret = gpu->ops->query_utilization(gpu, &utilization);
		if (ret < 0)
			utilization = 0;
	} else {
		utilization = gpu->utilization;
	}

 spin_lock(&dvfs->lock);

	switch (dvfs->policy.mode) {
	case MVGAL_DVFS_PERFORMANCE:
		dvfs->target_freq_mhz = dvfs->policy.max_freq_mhz;
		break;

	case MVGAL_DVFS_POWERSAVE:
		dvfs->target_freq_mhz = dvfs->policy.min_freq_mhz;
		break;

	case MVGAL_DVFS_BALANCED:
		if (utilization >= dvfs->policy.up_threshold) {
			/* Increase frequency */
			if (dvfs->current_freq_mhz < dvfs->policy.max_freq_mhz) {
				dvfs->target_freq_mhz = min(dvfs->current_freq_mhz + 100,
							    dvfs->policy.max_freq_mhz);
			}
		} else if (utilization <= dvfs->policy.down_threshold) {
			/* Decrease frequency */
			if (dvfs->current_freq_mhz > dvfs->policy.min_freq_mhz) {
				dvfs->target_freq_mhz = max(dvfs->current_freq_mhz - 100,
							    dvfs->policy.min_freq_mhz);
			}
		}
		break;

	case MVGAL_DVFS_CUSTOM:
		/* User-defined curve - use target as-is */
		break;
	}

	/* Apply frequency change */
	if (dvfs->target_freq_mhz != dvfs->current_freq_mhz) {
		mvgal_set_gpu_freq(gpu, dvfs->target_freq_mhz);
		dvfs->current_freq_mhz = dvfs->target_freq_mhz;
		pr_debug("MVGAL: DVFS: GPU %s freq %u MHz (util %u%%)\n",
			 gpu->name, dvfs->current_freq_mhz, utilization);
	}

 spin_unlock(&dvfs->lock);

	/* Schedule next check */
	queue_delayed_work(mvgal_dvfs_wq, &dvfs->dvfs_work,
			  msecs_to_jiffies(dvfs->policy.step_delay_ms));
}

/**
 * Start DVFS monitoring for a GPU
 */
int mvgal_dvfs_start(struct mvgal_gpu_device *gpu)
{
	struct mvgal_gpu_dvfs *dvfs;

	if (!gpu || !gpu->dvfs)
		return -EINVAL;

	dvfs = gpu->dvfs;

	if (!dvfs->enabled)
		return 0;

	/* Replace the work handler */
	INIT_DELAYED_WORK(&dvfs->dvfs_work, mvgal_dvfs_work_handler);

	queue_delayed_work(mvgal_dvfs_wq, &dvfs->dvfs_work,
			  msecs_to_jiffies(dvfs->policy.step_delay_ms));

	pr_info("MVGAL: DVFS started for GPU %s\n", gpu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_dvfs_start);

/**
 * Stop DVFS monitoring for a GPU
 */
int mvgal_dvfs_stop(struct mvgal_gpu_device *gpu)
{
	struct mvgal_gpu_dvfs *dvfs;

	if (!gpu || !gpu->dvfs)
		return -EINVAL;

	dvfs = gpu->dvfs;

	cancel_delayed_work_sync(&dvfs->dvfs_work);

	pr_info("MVGAL: DVFS stopped for GPU %s\n", gpu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_dvfs_stop);

/**
 * Set DVFS mode for a GPU
 */
int mvgal_dvfs_set_mode(struct mvgal_gpu_device *gpu, enum mvgal_dvfs_mode mode)
{
	struct mvgal_gpu_dvfs *dvfs;

	if (!gpu || !gpu->dvfs)
		return -EINVAL;

	dvfs = gpu->dvfs;

 spin_lock(&dvfs->lock);
	dvfs->policy.mode = mode;

	/* Immediately apply based on mode */
	switch (mode) {
	case MVGAL_DVFS_PERFORMANCE:
		dvfs->target_freq_mhz = dvfs->policy.max_freq_mhz;
		break;
	case MVGAL_DVFS_POWERSAVE:
		dvfs->target_freq_mhz = dvfs->policy.min_freq_mhz;
		break;
	default:
		break;
	}

	if (dvfs->target_freq_mhz != dvfs->current_freq_mhz) {
		mvgal_set_gpu_freq(gpu, dvfs->target_freq_mhz);
		dvfs->current_freq_mhz = dvfs->target_freq_mhz;
	}

 spin_unlock(&dvfs->lock);

	pr_info("MVGAL: DVFS mode set to %d for GPU %s\n", mode, gpu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_dvfs_set_mode);

/**
 * Initialize thermal throttling for a GPU
 */
int mvgal_thermal_init(struct mvgal_gpu_device *gpu)
{
	struct mvgal_thermal *thermal;

	if (!gpu)
		return -EINVAL;

	thermal = kzalloc(sizeof(*thermal), GFP_KERNEL);
	if (!thermal)
		return -ENOMEM;

	thermal->gpu = gpu;
	thermal->enabled = true;

	/* Default thermal thresholds */
	thermal->throttle_temp_c = MVGAL_THERMAL_DEFAULT_THROTTLE_TEMP_C;
	thermal->critical_temp_c = MVGAL_THERMAL_DEFAULT_CRITICAL_TEMP_C;
	thermal->current_temp_c = 0;
	thermal->throttle_level = 0;

	INIT_DELAYED_WORK(&thermal->thermal_work, NULL);

	gpu->thermal = thermal;

	pr_debug("MVGAL: Thermal throttling initialized for GPU %s\n", gpu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_thermal_init);

/**
 * Cleanup thermal throttling for a GPU
 */
void mvgal_thermal_fini(struct mvgal_gpu_device *gpu)
{
	struct mvgal_thermal *thermal;

	if (!gpu || !gpu->thermal)
		return;

	thermal = gpu->thermal;

	cancel_delayed_work_sync(&thermal->thermal_work);

	if (thermal->tz) {
		thermal_zone_device_unregister(thermal->tz);
	}

	kfree(thermal);
	gpu->thermal = NULL;

	pr_debug("MVGAL: Thermal throttling cleaned up for GPU %s\n", gpu->name);
}
EXPORT_SYMBOL_GPL(mvgal_thermal_fini);

/**
 * Thermal throttling work handler
 */
static void mvgal_thermal_work_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mvgal_thermal *thermal = container_of(dwork, struct mvgal_thermal, thermal_work);
	struct mvgal_gpu_device *gpu = thermal->gpu;
	int32_t temp;
	int ret;

	if (!thermal->enabled)
		return;

	/* Read current temperature */
	ret = mvgal_read_gpu_temp(gpu, &temp);
	if (ret < 0) {
		temp = 45; /* Default safe temperature */
	}

	thermal->current_temp_c = temp;

	/* Apply throttling based on temperature */
	if (temp >= thermal->critical_temp_c) {
		/* Critical temperature - force GPU off */
		pr_warn("MVGAL: GPU %s critical temperature %dC, forcing power off\n",
			gpu->name, temp);

		if (gpu->ops && gpu->ops->set_power_state) {
			gpu->ops->set_power_state(gpu, MVGAL_POWER_STATE_OFF);
		}
		thermal->throttle_level = 100;
	} else if (temp >= thermal->throttle_temp_c) {
		/* Throttle based on temperature */
		uint32_t throttle = (temp - thermal->throttle_temp_c) * 100 /
				   (thermal->critical_temp_c - thermal->throttle_temp_c);

		thermal->throttle_level = min(throttle, 100U);

		/* Apply frequency limit based on throttle level */
		mvgal_set_freq_limit(gpu, thermal->throttle_level);

		pr_debug("MVGAL: GPU %s throttling %u%% (temp %dC)\n",
			 gpu->name, thermal->throttle_level, temp);
	} else {
		/* Normal temperature - no throttling */
		thermal->throttle_level = 0;
		mvgal_set_freq_limit(gpu, 0);
	}

	/* Schedule next thermal check */
	queue_delayed_work(mvgal_dvfs_wq, &thermal->thermal_work,
			  msecs_to_jiffies(1000));
}

/**
 * Start thermal monitoring for a GPU
 */
int mvgal_thermal_start(struct mvgal_gpu_device *gpu)
{
	struct mvgal_thermal *thermal;

	if (!gpu || !gpu->thermal)
		return -EINVAL;

	thermal = gpu->thermal;

	if (!thermal->enabled)
		return 0;

	INIT_DELAYED_WORK(&thermal->thermal_work, mvgal_thermal_work_handler);

	queue_delayed_work(mvgal_dvfs_wq, &thermal->thermal_work,
			  msecs_to_jiffies(1000));

	pr_info("MVGAL: Thermal monitoring started for GPU %s\n", gpu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_thermal_start);

/**
 * Stop thermal monitoring for a GPU
 */
int mvgal_thermal_stop(struct mvgal_gpu_device *gpu)
{
	struct mvgal_thermal *thermal;

	if (!gpu || !gpu->thermal)
		return -EINVAL;

	thermal = gpu->thermal;

	cancel_delayed_work_sync(&thermal->thermal_work);

	/* Reset throttling */
	thermal->throttle_level = 0;
	mvgal_set_freq_limit(gpu, 0);

	pr_info("MVGAL: Thermal monitoring stopped for GPU %s\n", gpu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_thermal_stop);

/**
 * Set thermal thresholds for a GPU
 */
int mvgal_thermal_set_thresholds(struct mvgal_gpu_device *gpu,
				 int32_t throttle_temp, int32_t critical_temp)
{
	struct mvgal_thermal *thermal;

	if (!gpu || !gpu->thermal)
		return -EINVAL;

	thermal = gpu->thermal;

	if (throttle_temp >= critical_temp)
		return -EINVAL;

	thermal->throttle_temp_c = throttle_temp;
	thermal->critical_temp_c = critical_temp;

	pr_info("MVGAL: Thermal thresholds set for GPU %s: throttle=%dC, critical=%dC\n",
		gpu->name, throttle_temp, critical_temp);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_thermal_set_thresholds);

/**
 * Initialize power budget management
 */
int mvgal_power_budget_init(void)
{
	memset(&mvgal_power_state, 0, sizeof(mvgal_power_state));

	mvgal_power_state.budget.total_watts = MVGAL_POWER_DEFAULT_TOTAL_WATTS;
	mvgal_power_state.budget.headroom_watts = MVGAL_POWER_DEFAULT_HEADROOM_WATTS;
	mvgal_power_state.budget.allocated_watts = 0;
	mvgal_power_state.budget.gpu_count = 0;

 spin_lock_init(&mvgal_power_state.budget.lock);

	mvgal_power_state.initialized = true;

	pr_info("MVGAL: Power budget management initialized (total=%uW, headroom=%uW)\n",
		mvgal_power_state.budget.total_watts,
		mvgal_power_state.budget.headroom_watts);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_init);

/**
 * Cleanup power budget management
 */
void mvgal_power_budget_fini(void)
{
	mvgal_power_state.initialized = false;
	pr_info("MVGAL: Power budget management cleaned up\n");
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_fini);

/**
 * Register a GPU with power budget
 */
int mvgal_power_budget_register_gpu(struct mvgal_gpu_device *gpu, uint32_t index)
{
	struct mvgal_power_budget *budget;

	if (!mvgal_power_state.initialized || index >= MVGAL_MAX_GPUS)
		return -EINVAL;

	budget = &mvgal_power_state.budget;

 spin_lock(&budget->lock);

	if (budget->gpu_count >= MVGAL_MAX_GPUS) {
	 spin_unlock(&budget->lock);
		return -ENOSPC;
	}

	/* Set default power limits */
	budget->gpu_power[index].min_watts = 50;
	budget->gpu_power[index].max_watts = 300;
	budget->gpu_power[index].current_watts = 0;
	budget->gpu_power[index].limit_watts = budget->gpu_power[index].max_watts;

	budget->gpu_count++;

 spin_unlock(&budget->lock);

	pr_info("MVGAL: GPU %u registered with power budget\n", index);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_register_gpu);

/**
 * Unregister a GPU from power budget
 */
int mvgal_power_budget_unregister_gpu(uint32_t index)
{
	struct mvgal_power_budget *budget;

	if (!mvgal_power_state.initialized || index >= MVGAL_MAX_GPUS)
		return -EINVAL;

	budget = &mvgal_power_state.budget;

 spin_lock(&budget->lock);

	budget->gpu_power[index].min_watts = 0;
	budget->gpu_power[index].max_watts = 0;
	budget->gpu_power[index].current_watts = 0;
	budget->gpu_power[index].limit_watts = 0;

	if (budget->gpu_count > 0)
		budget->gpu_count--;

 spin_unlock(&budget->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_unregister_gpu);

/**
 * Distribute power budget based on workload priority
 */
int mvgal_power_budget_distribute(uint32_t *gpu_indices, uint32_t num_gpus,
				  uint32_t priority)
{
	struct mvgal_power_budget *budget;
	uint32_t available;
	uint32_t per_gpu;
	uint32_t i;

	if (!mvgal_power_state.initialized || num_gpus == 0)
		return -EINVAL;

	budget = &mvgal_power_state.budget;

 spin_lock(&budget->lock);

	available = budget->total_watts - budget->headroom_watts;

	/* Distribute power based on priority (0-15) */
	per_gpu = (priority * available) / (15 * num_gpus);
	per_gpu = max(per_gpu, 50U); /* Minimum 50W per GPU */

	for (i = 0; i < num_gpus && i < MVGAL_MAX_GPUS; i++) {
		uint32_t idx = gpu_indices[i];

		budget->gpu_power[idx].min_watts = per_gpu;
		budget->gpu_power[idx].limit_watts = min(per_gpu * 2,
							 budget->gpu_power[idx].max_watts);

		pr_debug("MVGAL: GPU %u allocated %uW (priority %u)\n",
			 idx, per_gpu, priority);
	}

	budget->allocated_watts = per_gpu * num_gpus;

 spin_unlock(&budget->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_distribute);

/**
 * Get current power budget for a GPU
 */
int mvgal_power_budget_get_gpu(uint32_t index, struct mvgal_gpu_power *power)
{
	struct mvgal_power_budget *budget;

	if (!mvgal_power_state.initialized || index >= MVGAL_MAX_GPUS || !power)
		return -EINVAL;

	budget = &mvgal_power_state.budget;

 spin_lock(&budget->lock);
	*power = budget->gpu_power[index];
 spin_unlock(&budget->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_get_gpu);

/**
 * Set total power budget
 */
int mvgal_power_budget_set_total(uint32_t total_watts)
{
	struct mvgal_power_budget *budget;

	if (!mvgal_power_state.initialized)
		return -EINVAL;

	budget = &mvgal_power_state.budget;

 spin_lock(&budget->lock);
	budget->total_watts = total_watts;
 spin_unlock(&budget->lock);

	pr_info("MVGAL: Power budget total set to %uW\n", total_watts);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_set_total);

/**
 * Set power headroom
 */
int mvgal_power_budget_set_headroom(uint32_t headroom_watts)
{
	struct mvgal_power_budget *budget;

	if (!mvgal_power_state.initialized)
		return -EINVAL;

	budget = &mvgal_power_state.budget;

 spin_lock(&budget->lock);
	budget->headroom_watts = headroom_watts;
 spin_unlock(&budget->lock);

	pr_info("MVGAL: Power budget headroom set to %uW\n", headroom_watts);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_power_budget_set_headroom);

/* Helper functions - call vendor-specific implementations */
static int mvgal_set_gpu_freq(struct mvgal_gpu_device *gpu, uint32_t freq_mhz)
{
	/* Vendor-specific implementation would go here */
	/* For now, just update the GPU state */
	if (gpu->ops && gpu->ops->set_power_state) {
		/* Some vendors have frequency control through power state */
	}

	pr_debug("MVGAL: Setting GPU %s frequency to %u MHz\n", gpu->name, freq_mhz);

	return 0;
}

static int mvgal_get_gpu_freq(struct mvgal_gpu_device *gpu, uint32_t *freq_mhz)
{
	if (!gpu || !freq_mhz)
		return -EINVAL;

	/* Vendor-specific implementation would go here */
	*freq_mhz = gpu->dvfs ? gpu->dvfs->current_freq_mhz : 0;

	return 0;
}

static int mvgal_read_gpu_temp(struct mvgal_gpu_device *gpu, int32_t *temp_c)
{
	if (!gpu || !temp_c)
		return -EINVAL;

	/* Use cached temperature if available */
	*temp_c = gpu->temperature;

	return 0;
}

static int mvgal_read_gpu_power(struct mvgal_gpu_device *gpu, uint32_t *power_watts)
{
	if (!gpu || !power_watts)
		return -EINVAL;

	/* Vendor-specific implementation would read from hwmon */
	*power_watts = 0;

	return 0;
}

/**
 * Set frequency limit based on thermal throttle level
 */
int mvgal_set_freq_limit(struct mvgal_gpu_device *gpu, uint32_t throttle_percent)
{
	struct mvgal_gpu_dvfs *dvfs;
	uint32_t max_freq;

	if (!gpu)
		return -EINVAL;

	dvfs = gpu->dvfs;
	if (!dvfs)
		return -EINVAL;

	if (throttle_percent == 0) {
		/* No throttling - use policy max */
		max_freq = dvfs->policy.max_freq_mhz;
	} else {
		/* Apply throttle - reduce max frequency */
		max_freq = dvfs->policy.max_freq_mhz * (100 - throttle_percent) / 100;
		max_freq = max(max_freq, dvfs->policy.min_freq_mhz);
	}

 spin_lock(&dvfs->lock);
	dvfs->target_freq_mhz = min(dvfs->target_freq_mhz, max_freq);

	if (dvfs->current_freq_mhz > max_freq) {
		mvgal_set_gpu_freq(gpu, max_freq);
		dvfs->current_freq_mhz = max_freq;
	}
 spin_unlock(&dvfs->lock);

	pr_debug("MVGAL: GPU %s freq limit %u MHz (throttle %u%%)\n",
		 gpu->name, max_freq, throttle_percent);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_set_freq_limit);

/**
 * Set power state for a GPU
 */
int mvgal_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state)
{
	if (!gpu)
		return -EINVAL;

	if (gpu->ops && gpu->ops->set_power_state) {
		return gpu->ops->set_power_state(gpu, state);
	}

	gpu->power_state = state;

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_set_power_state);

/**
 * Power management initialization - called from main module init
 */
int mvgal_power_module_init(void)
{
	pr_info("MVGAL: Power management module initialized\n");

	/* Create DVFS workqueue */
	mvgal_dvfs_wq = alloc_workqueue("mvgal_dvfs", WQ_UNBOUND | WQ_FREEZABLE, 0);
	if (!mvgal_dvfs_wq) {
		pr_err("MVGAL: Failed to create DVFS workqueue\n");
		return -ENOMEM;
	}

	/* Initialize global power budget */
	mvgal_power_budget_init();

	return 0;
}

/**
 * Power management cleanup - called from main module exit
 */
void mvgal_power_module_exit(void)
{
	pr_info("MVGAL: Power management module exiting\n");

	/* Cleanup power budget */
	mvgal_power_budget_fini();

	/* Destroy workqueue */
	if (mvgal_dvfs_wq) {
		destroy_workqueue(mvgal_dvfs_wq);
		mvgal_dvfs_wq = NULL;
	}
}

/* Linked into mvgal.ko; no separate module metadata. */