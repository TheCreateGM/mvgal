/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 *
 * Power Management Kernel Header - DVFS, Thermal, Power Budget
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_POWER_H_
#define _MVGAL_POWER_H_

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/thermal.h>

/* Forward declarations */
struct mvgal_gpu_device;

/**
 * DVFS modes
 */
enum mvgal_dvfs_mode {
	MVGAL_DVFS_PERFORMANCE = 0,
	MVGAL_DVFS_BALANCED,
	MVGAL_DVFS_POWERSAVE,
	MVGAL_DVFS_CUSTOM
};

/**
 * DVFS policy structure
 */
struct mvgal_dvfs_policy {
	enum mvgal_dvfs_mode mode;
	uint32_t min_freq_mhz;
	uint32_t max_freq_mhz;
	uint32_t up_threshold;
	uint32_t down_threshold;
	uint32_t step_delay_ms;
};

/**
 * Per-GPU DVFS state
 */
struct mvgal_gpu_dvfs {
	struct mvgal_dvfs_policy policy;
	uint32_t current_freq_mhz;
	uint32_t target_freq_mhz;
	struct delayed_work dvfs_work;
	struct mvgal_gpu_device *gpu;
 spinlock_t lock;
	bool enabled;
};

/**
 * Thermal throttling state
 */
struct mvgal_thermal {
	int32_t current_temp_c;
	int32_t throttle_temp_c;
	int32_t critical_temp_c;
	uint32_t throttle_level;
	struct thermal_zone_device *tz;
	struct mvgal_gpu_device *gpu;
	struct delayed_work thermal_work;
	bool enabled;
};

/**
 * Per-GPU power info
 */
struct mvgal_gpu_power {
	uint32_t min_watts;
	uint32_t max_watts;
	uint32_t current_watts;
	uint32_t limit_watts;
};

/**
 * Power budget management
 */
struct mvgal_power_budget {
	uint32_t total_watts;
	uint32_t headroom_watts;
	uint32_t allocated_watts;
	struct mvgal_gpu_power gpu_power[8];
	uint32_t gpu_count;
 spinlock_t lock;
};

/* DVFS and thermal are used via pointers in struct mvgal_gpu_device (defined in mvgal_device.h) */

/* DVFS functions */
int mvgal_dvfs_init(struct mvgal_gpu_device *gpu);
void mvgal_dvfs_fini(struct mvgal_gpu_device *gpu);
int mvgal_dvfs_start(struct mvgal_gpu_device *gpu);
int mvgal_dvfs_stop(struct mvgal_gpu_device *gpu);
int mvgal_dvfs_set_mode(struct mvgal_gpu_device *gpu, enum mvgal_dvfs_mode mode);

/* Thermal functions */
int mvgal_thermal_init(struct mvgal_gpu_device *gpu);
void mvgal_thermal_fini(struct mvgal_gpu_device *gpu);
int mvgal_thermal_start(struct mvgal_gpu_device *gpu);
int mvgal_thermal_stop(struct mvgal_gpu_device *gpu);
int mvgal_thermal_set_thresholds(struct mvgal_gpu_device *gpu,
				 int32_t throttle_temp, int32_t critical_temp);

/* Power budget functions */
int mvgal_power_budget_init(void);
void mvgal_power_budget_fini(void);
int mvgal_power_budget_register_gpu(struct mvgal_gpu_device *gpu, uint32_t index);
int mvgal_power_budget_unregister_gpu(uint32_t index);
int mvgal_power_budget_distribute(uint32_t *gpu_indices, uint32_t num_gpus,
				  uint32_t priority);
int mvgal_power_budget_get_gpu(uint32_t index, struct mvgal_gpu_power *power);
int mvgal_power_budget_set_total(uint32_t total_watts);
int mvgal_power_budget_set_headroom(uint32_t headroom_watts);

/* Utility functions */
int mvgal_set_freq_limit(struct mvgal_gpu_device *gpu, uint32_t throttle_percent);
int mvgal_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state);

#endif /* _MVGAL_POWER_H_ */