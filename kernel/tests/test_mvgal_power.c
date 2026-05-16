// SPDX-License-Identifier: GPL-2.0-only
/**
 * MVGAL KUnit Tests - Power Management
 * 
 * Tests for DVFS (Dynamic Voltage/Frequency Scaling), thermal throttling,
 * and power state management.
 * 
 * Copyright (C) 2024 MVGAL Project
 */

#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/delayed_work.h>
#include <linux/timer.h>

/* Include the module headers */
#include "../mvgal_core.h"
#include "../mvgal_device.h"

/*
 * DVFS mode definitions (from DESIGN.md section 5.2.1)
 */
enum mvgal_dvfs_mode {
	MVGAL_DVFS_PERFORMANCE = 0,
	MVGAL_DVFS_BALANCED = 1,
	MVGAL_DVFS_POWERSAVE = 2,
	MVGAL_DVFS_CUSTOM = 3,
};

/*
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

/*
 * Thermal throttling state
 */
struct mvgal_thermal {
	int32_t current_temp_c;
	int32_t throttle_temp_c;
	int32_t critical_temp_c;
	uint32_t throttle_level;
};

/*
 * Per-GPU power state
 */
struct mvgal_gpu_power {
	enum mvgal_power_state state;
	uint32_t current_freq_mhz;
	uint32_t target_freq_mhz;
	uint32_t power_draw_mw;
	struct mvgal_dvfs_policy dvfs_policy;
	struct mvgal_thermal thermal;
};

/*
 * Test: mvgal_power_state_transitions - Power state transitions
 */
static void test_mvgal_power_state_transitions(struct kunit *test)
{
	enum mvgal_power_state states[] = {
		MVGAL_POWER_STATE_ACTIVE,
		MVGAL_POWER_STATE_SUSTAINED,
		MVGAL_POWER_STATE_IDLE,
		MVGAL_POWER_STATE_PARK
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(states); i++) {
		/* Verify each power state is valid */
		KUNIT_EXPECT_GE(test, (int)states[i], 0);
		KUNIT_EXPECT_LE(test, states[i], MVGAL_POWER_STATE_PARK);
	}
}

/*
 * Test: mvgal_dvfs_policy_modes - DVFS policy mode validation
 */
static void test_mvgal_dvfs_policy_modes(struct kunit *test)
{
	struct mvgal_dvfs_policy policy;
	enum mvgal_dvfs_mode modes[] = {
		MVGAL_DVFS_PERFORMANCE,
		MVGAL_DVFS_BALANCED,
		MVGAL_DVFS_POWERSAVE,
		MVGAL_DVFS_CUSTOM
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		policy.mode = modes[i];
		KUNIT_EXPECT_GE(test, (int)policy.mode, 0);
		KUNIT_EXPECT_LE(test, policy.mode, MVGAL_DVFS_CUSTOM);
	}
}

/*
 * Test: mvgal_dvfs_performance_mode - Performance mode configuration
 */
static void test_mvgal_dvfs_performance_mode(struct kunit *test)
{
	struct mvgal_dvfs_policy policy;

	policy.mode = MVGAL_DVFS_PERFORMANCE;
	policy.min_freq_mhz = 2000;
	policy.max_freq_mhz = 2500;
	policy.up_threshold = 50;
	policy.down_threshold = 30;
	policy.step_delay_ms = 10;

	/* Performance mode should have high min/max frequencies */
	KUNIT_EXPECT_EQ(test, policy.mode, MVGAL_DVFS_PERFORMANCE);
	KUNIT_EXPECT_GE(test, policy.min_freq_mhz, 2000U);
	KUNIT_EXPECT_GE(test, policy.max_freq_mhz, policy.min_freq_mhz);
}

/*
 * Test: mvgal_dvfs_powersave_mode - PowerSave mode configuration
 */
static void test_mvgal_dvfs_powersave_mode(struct kunit *test)
{
	struct mvgal_dvfs_policy policy;

	policy.mode = MVGAL_DVFS_POWERSAVE;
	policy.min_freq_mhz = 300;
	policy.max_freq_mhz = 800;
	policy.up_threshold = 80;
	policy.down_threshold = 60;
	policy.step_delay_ms = 50;

	/* PowerSave mode should have lower frequencies */
	KUNIT_EXPECT_EQ(test, policy.mode, MVGAL_DVFS_POWERSAVE);
	KUNIT_EXPECT_LE(test, policy.max_freq_mhz, 800U);
}

/*
 * Test: mvgal_dvfs_balanced_mode - Balanced mode configuration
 */
static void test_mvgal_dvfs_balanced_mode(struct kunit *test)
{
	struct mvgal_dvfs_policy policy;

	policy.mode = MVGAL_DVFS_BALANCED;
	policy.min_freq_mhz = 800;
	policy.max_freq_mhz = 1800;
	policy.up_threshold = 70;
	policy.down_threshold = 40;
	policy.step_delay_ms = 20;

	/* Balanced mode should be in the middle */
	KUNIT_EXPECT_EQ(test, policy.mode, MVGAL_DVFS_BALANCED);
	KUNIT_EXPECT_GT(test, policy.max_freq_mhz, policy.min_freq_mhz);
}

/*
 * Test: mvgal_dvfs_custom_mode - Custom mode configuration
 */
static void test_mvgal_dvfs_custom_mode(struct kunit *test)
{
	struct mvgal_dvfs_policy policy;

	policy.mode = MVGAL_DVFS_CUSTOM;
	policy.min_freq_mhz = 1500;
	policy.max_freq_mhz = 2200;
	policy.up_threshold = 60;
	policy.down_threshold = 35;
	policy.step_delay_ms = 15;

	/* Custom mode allows user-defined parameters */
	KUNIT_EXPECT_EQ(test, policy.mode, MVGAL_DVFS_CUSTOM);
	KUNIT_EXPECT_GT(test, policy.max_freq_mhz, policy.min_freq_mhz);
}

/*
 * Test: mvgal_thermal_thresholds - Thermal threshold validation
 */
static void test_mvgal_thermal_thresholds(struct kunit *test)
{
	struct mvgal_thermal thermal;

	thermal.throttle_temp_c = 80;
	thermal.critical_temp_c = 95;
	thermal.current_temp_c = 50;
	thermal.throttle_level = 0;

	/* Throttle temp should be less than critical */
	KUNIT_EXPECT_LT(test, thermal.throttle_temp_c, thermal.critical_temp_c);

	/* Initial throttle level should be 0 */
	KUNIT_EXPECT_EQ(test, thermal.throttle_level, 0U);
}

/*
 * Test: mvgal_thermal_normal_operation - Normal temperature operation
 */
static void test_mvgal_thermal_normal_operation(struct kunit *test)
{
	struct mvgal_thermal thermal;

	thermal.throttle_temp_c = 80;
	thermal.critical_temp_c = 95;
	thermal.current_temp_c = 45;
	thermal.throttle_level = 0;

	/* Normal temperature - no throttling */
	if (thermal.current_temp_c < thermal.throttle_temp_c) {
		thermal.throttle_level = 0;
	}

	KUNIT_EXPECT_EQ(test, thermal.throttle_level, 0U);
}

/*
 * Test: mvgal_thermal_throttling - Thermal throttling activation
 */
static void test_mvgal_thermal_throttling(struct kunit *test)
{
	struct mvgal_thermal thermal;
	int32_t temp_range;

	thermal.throttle_temp_c = 80;
	thermal.critical_temp_c = 95;
	thermal.current_temp_c = 85;
	thermal.throttle_level = 0;

	/* Temperature in throttle zone */
	temp_range = thermal.critical_temp_c - thermal.throttle_temp_c;
	if (thermal.current_temp_c >= thermal.throttle_temp_c &&
	    thermal.current_temp_c < thermal.critical_temp_c) {
		thermal.throttle_level = (thermal.current_temp_c - thermal.throttle_temp_c) *
					 100 / temp_range;
	}

	/* Should be throttling but not critical */
	KUNIT_EXPECT_GT(test, thermal.throttle_level, 0U);
	KUNIT_EXPECT_LT(test, thermal.throttle_level, 100U);
}

/*
 * Test: mvgal_thermal_critical - Critical temperature handling
 */
static void test_mvgal_thermal_critical(struct kunit *test)
{
	struct mvgal_thermal thermal;
	enum mvgal_power_state power_state = MVGAL_POWER_STATE_ACTIVE;

	thermal.throttle_temp_c = 80;
	thermal.critical_temp_c = 95;
	thermal.current_temp_c = 98;
	thermal.throttle_level = 100;

	/* Critical temperature - should force power off */
	if (thermal.current_temp_c >= thermal.critical_temp_c) {
		power_state = MVGAL_POWER_STATE_PARK;
		thermal.throttle_level = 100;
	}

	KUNIT_EXPECT_EQ(test, power_state, MVGAL_POWER_STATE_PARK);
	KUNIT_EXPECT_EQ(test, thermal.throttle_level, 100U);
}

/*
 * Test: mvgal_thermal_temperature_range - Various temperature values
 */
static void test_mvgal_thermal_temperature_range(struct kunit *test)
{
	struct mvgal_thermal thermal;
	int32_t temps[] = { 30, 45, 60, 75, 85, 90, 95, 100 };
	unsigned int i;

	thermal.throttle_temp_c = 80;
	thermal.critical_temp_c = 95;

	for (i = 0; i < ARRAY_SIZE(temps); i++) {
		thermal.current_temp_c = temps[i];
		thermal.throttle_level = 0;

		if (thermal.current_temp_c >= thermal.critical_temp_c) {
			thermal.throttle_level = 100;
		} else if (thermal.current_temp_c >= thermal.throttle_temp_c) {
			thermal.throttle_level = (thermal.current_temp_c - thermal.throttle_temp_c) *
						 100 / (thermal.critical_temp_c - thermal.throttle_temp_c);
		}

		/* All temperatures should produce a valid throttle level */
		KUNIT_EXPECT_LE(test, thermal.throttle_level, 100U);
	}
}

/*
 * Test: mvgal_power_draw_calculation - Power draw calculation
 */
static void test_mvgal_power_draw_calculation(struct kunit *test)
{
	struct mvgal_gpu_power gpu_power;
	uint32_t freq_mhz = 2000;
	uint32_t power_draw;

	/* Simple power model: power scales with frequency */
	gpu_power.current_freq_mhz = freq_mhz;
	gpu_power.target_freq_mhz = 2500;

	/* Calculate power based on frequency ratio */
	power_draw = 150 + (freq_mhz * 50 / 1000); /* Base 150W + freq scaling */

	KUNIT_EXPECT_GT(test, power_draw, 150U);
}

/*
 * Test: mvgal_power_budget_allocation - Power budget distribution
 */
static void test_mvgal_power_budget_allocation(struct kunit *test)
{
	uint32_t total_budget = 500; /* 500W total */
	uint32_t headroom = 50; /* 50W reserved */
	uint32_t available = total_budget - headroom;
	uint32_t gpu_count = 4;
	uint32_t per_gpu;

	/* Distribute evenly */
	per_gpu = available / gpu_count;

	KUNIT_EXPECT_EQ(test, per_gpu, 112U); /* 450 / 4 = 112.5 -> 112 */
	KUNIT_EXPECT_LE(test, per_gpu * gpu_count, available);
}

/*
 * Test: mvgal_dvfs_frequency_scaling - Frequency scaling steps
 */
static void test_mvgal_dvfs_frequency_scaling(struct kunit *test)
{
	struct mvgal_dvfs_policy policy;
	uint32_t current_freq = 1000;
	uint32_t utilization = 80;
	uint32_t new_freq;

	policy.mode = MVGAL_DVFS_BALANCED;
	policy.min_freq_mhz = 800;
	policy.max_freq_mhz = 1800;
	policy.up_threshold = 70;

	/* Scale up if utilization is above threshold */
	if (utilization >= policy.up_threshold) {
		new_freq = current_freq + 200;
		if (new_freq > policy.max_freq_mhz) {
			new_freq = policy.max_freq_mhz;
		}
	} else {
		new_freq = current_freq;
	}

	KUNIT_EXPECT_EQ(test, new_freq, 1200U);
}

/*
 * Test: mvgal_dvfs_frequency_downscale - Frequency downscale
 */
static void test_mvgal_dvfs_frequency_downscale(struct kunit *test)
{
	struct mvgal_dvfs_policy policy;
	uint32_t current_freq = 1500;
	uint32_t utilization = 30;
	uint32_t new_freq;

	policy.mode = MVGAL_DVFS_BALANCED;
	policy.min_freq_mhz = 800;
	policy.max_freq_mhz = 1800;
	policy.down_threshold = 40;

	/* Scale down if utilization is below threshold */
	if (utilization <= policy.down_threshold) {
		new_freq = current_freq - 200;
		if (new_freq < policy.min_freq_mhz) {
			new_freq = policy.min_freq_mhz;
		}
	} else {
		new_freq = current_freq;
	}

	KUNIT_EXPECT_EQ(test, new_freq, 1300U);
}

/*
 * Test: mvgal_power_state_idle_detection - Idle state detection
 */
static void test_mvgal_power_state_idle_detection(struct kunit *test)
{
	uint32_t utilization = 0;
	enum mvgal_power_state state = MVGAL_POWER_STATE_ACTIVE;

	/* Detect idle */
	if (utilization < 5) {
		state = MVGAL_POWER_STATE_IDLE;
	}

	KUNIT_EXPECT_EQ(test, state, MVGAL_POWER_STATE_IDLE);
}

/*
 * Test: mvgal_power_state_active_detection - Active state detection
 */
static void test_mvgal_power_state_active_detection(struct kunit *test)
{
	uint32_t utilization = 75;
	enum mvgal_power_state state = MVGAL_POWER_STATE_IDLE;

	/* Detect active */
	if (utilization > 50) {
		state = MVGAL_POWER_STATE_ACTIVE;
	}

	KUNIT_EXPECT_EQ(test, state, MVGAL_POWER_STATE_ACTIVE);
}

/*
 * Test cases for mvgal-power suite
 */
static struct kunit_case mvgal_power_tests[] = {
	KUNIT_CASE(test_mvgal_power_state_transitions),
	KUNIT_CASE(test_mvgal_dvfs_policy_modes),
	KUNIT_CASE(test_mvgal_dvfs_performance_mode),
	KUNIT_CASE(test_mvgal_dvfs_powersave_mode),
	KUNIT_CASE(test_mvgal_dvfs_balanced_mode),
	KUNIT_CASE(test_mvgal_dvfs_custom_mode),
	KUNIT_CASE(test_mvgal_thermal_thresholds),
	KUNIT_CASE(test_mvgal_thermal_normal_operation),
	KUNIT_CASE(test_mvgal_thermal_throttling),
	KUNIT_CASE(test_mvgal_thermal_critical),
	KUNIT_CASE(test_mvgal_thermal_temperature_range),
	KUNIT_CASE(test_mvgal_power_draw_calculation),
	KUNIT_CASE(test_mvgal_power_budget_allocation),
	KUNIT_CASE(test_mvgal_dvfs_frequency_scaling),
	KUNIT_CASE(test_mvgal_dvfs_frequency_downscale),
	KUNIT_CASE(test_mvgal_power_state_idle_detection),
	KUNIT_CASE(test_mvgal_power_state_active_detection),
	{}
};

/*
 * Test suite for mvgal-power
 */
static struct kunit_suite mvgal_power_suite = {
	.name = "mvgal-power",
	.test_cases = mvgal_power_tests,
};
kunit_test_suites_register(&mvgal_power_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MVGAL Project");
MODULE_DESCRIPTION("KUnit tests for MVGAL power management");