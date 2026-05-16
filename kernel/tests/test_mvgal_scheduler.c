// SPDX-License-Identifier: GPL-2.0-only
/**
 * MVGAL KUnit Tests - Workload Scheduler
 * 
 * Tests for workload submission, priority queues, and GPU selection.
 * 
 * Copyright (C) 2024 MVGAL Project
 */

#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/completion.h>

/* Include the module headers */
#include "../mvgal_core.h"
#include "../mvgal_device.h"
#include "../mvgal_scheduler.h"

/*
 * Test: mvgal_scheduler_init - Scheduler initialization
 */
static void test_mvgal_scheduler_init(struct kunit *test)
{
	int ret;

	ret = mvgal_scheduler_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	mvgal_scheduler_fini();
}

/*
 * Test: mvgal_scheduler_init_twice - Double initialization handling
 */
static void test_mvgal_scheduler_init_twice(struct kunit *test)
{
	int ret;

	ret = mvgal_scheduler_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Second init should also succeed */
	ret = mvgal_scheduler_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	mvgal_scheduler_fini();
}

/*
 * Test: mvgal_workload_alloc - Workload structure allocation
 */
static void test_mvgal_workload_alloc(struct kunit *test)
{
	struct mvgal_workload *workload;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);
	KUNIT_EXPECT_GT(test, workload->id, 0U);
	KUNIT_EXPECT_EQ(test, workload->priority, MVGAL_DEFAULT_PRIORITY);
	KUNIT_EXPECT_EQ(test, workload->gpu_mask, ~0U);
	KUNIT_EXPECT_EQ(test, workload->state, MVGAL_SCHEDULE_STATE_PENDING);
	KUNIT_EXPECT_EQ(test, workload->num_memory_resources, 0U);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_alloc_multiple - Multiple workload allocation
 */
static void test_mvgal_workload_alloc_multiple(struct kunit *test)
{
	struct mvgal_workload *workloads[5];
	int i;

	for (i = 0; i < 5; i++) {
		workloads[i] = mvgal_workload_alloc();
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workloads[i]);
	}

	/* Verify unique IDs */
	for (i = 0; i < 5; i++) {
		int j;
		for (j = i + 1; j < 5; j++) {
			KUNIT_EXPECT_NE(test, workloads[i]->id, workloads[j]->id);
		}
	}

	/* Free all */
	for (i = 0; i < 5; i++) {
		mvgal_workload_free(workloads[i]);
	}
}

/*
 * Test: mvgal_workload_properties - Workload property setting
 */
static void test_mvgal_workload_properties(struct kunit *test)
{
	struct mvgal_workload *workload;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	/* Set workload properties */
	workload->priority = 15;
	workload->gpu_mask = 0x3; /* GPUs 0 and 1 */
	workload->type = MVGAL_WORKLOAD_COMPUTE;
	workload->command_buffer_addr = 0x1000;
	workload->command_buffer_size = 4096;

	/* Verify properties */
	KUNIT_EXPECT_EQ(test, workload->priority, 15U);
	KUNIT_EXPECT_EQ(test, workload->gpu_mask, 0x3U);
	KUNIT_EXPECT_EQ(test, workload->type, MVGAL_WORKLOAD_COMPUTE);
	KUNIT_EXPECT_EQ(test, workload->command_buffer_addr, 0x1000ULL);
	KUNIT_EXPECT_EQ(test, workload->command_buffer_size, (size_t)4096);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_priority_clamping - Priority value clamping
 */
static void test_mvgal_workload_priority_clamping(struct kunit *test)
{
	struct mvgal_workload *workload;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	/* Test priority clamping in submit */
	workload->priority = MVGAL_MAX_PRIORITY + 5;
	mvgal_workload_submit(workload);
	KUNIT_EXPECT_LE(test, workload->priority, MVGAL_MAX_PRIORITY);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_submit - Workload submission
 */
static void test_mvgal_workload_submit(struct kunit *test)
{
	struct mvgal_workload *workload;
	int ret;

	ret = mvgal_scheduler_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	ret = mvgal_workload_submit(workload);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, workload->state, MVGAL_SCHEDULE_STATE_PENDING);

	mvgal_workload_free(workload);
	mvgal_scheduler_fini();
}

/*
 * Test: mvgal_workload_submit_multiple - Multiple workload submission
 */
static void test_mvgal_workload_submit_multiple(struct kunit *test)
{
	struct mvgal_workload *workloads[4];
	int ret;
	int i;

	ret = mvgal_scheduler_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Submit workloads with different priorities */
	for (i = 0; i < 4; i++) {
		workloads[i] = mvgal_workload_alloc();
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workloads[i]);
		workloads[i]->priority = i * 4; /* 0, 4, 8, 12 */
		ret = mvgal_workload_submit(workloads[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);
	}

	/* All should be pending */
	for (i = 0; i < 4; i++) {
		KUNIT_EXPECT_EQ(test, workloads[i]->state, MVGAL_SCHEDULE_STATE_PENDING);
	}

	/* Free all */
	for (i = 0; i < 4; i++) {
		mvgal_workload_free(workloads[i]);
	}

	mvgal_scheduler_fini();
}

/*
 * Test: mvgal_workload_signal - Workload completion signaling
 */
static void test_mvgal_workload_signal(struct kunit *test)
{
	struct mvgal_workload *workload;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	/* Signal success */
	mvgal_workload_signal(workload, 0);
	KUNIT_EXPECT_EQ(test, workload->state, MVGAL_SCHEDULE_STATE_COMPLETE);
	KUNIT_EXPECT_EQ(test, workload->result, 0);
	KUNIT_EXPECT_TRUE(test, workload->signaled);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_signal_error - Workload error signaling
 */
static void test_mvgal_workload_signal_error(struct kunit *test)
{
	struct mvgal_workload *workload;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	/* Signal error */
	mvgal_workload_signal(-EIO);
	KUNIT_EXPECT_EQ(test, workload->state, MVGAL_SCHEDULE_STATE_ERROR);
	KUNIT_EXPECT_EQ(test, workload->result, -EIO);
	KUNIT_EXPECT_TRUE(test, workload->signaled);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_wait - Workload wait for completion
 */
static void test_mvgal_workload_wait(struct kunit *test)
{
	struct mvgal_workload *workload;
	int ret;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	/* Signal completion before waiting */
	mvgal_workload_signal(workload, 0);

	/* Wait should return immediately */
	ret = mvgal_workload_wait(workload, 1000);
	KUNIT_EXPECT_EQ(test, ret, 0);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_wait_timeout - Workload wait with timeout
 */
static void test_mvgal_workload_wait_timeout(struct kunit *test)
{
	struct mvgal_workload *workload;
	int ret;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	/* Don't signal - should timeout */
	ret = mvgal_workload_wait(workload, 10); /* 10ms timeout */
	KUNIT_EXPECT_EQ(test, ret, -ETIMEDOUT);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_types - Different workload types
 */
static void test_mvgal_workload_types(struct kunit *test)
{
	struct mvgal_workload *workload;

	/* Test graphics workload */
	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);
	workload->type = MVGAL_WORKLOAD_GRAPHICS;
	KUNIT_EXPECT_EQ(test, workload->type, MVGAL_WORKLOAD_GRAPHICS);
	mvgal_workload_free(workload);

	/* Test compute workload */
	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);
	workload->type = MVGAL_WORKLOAD_COMPUTE;
	KUNIT_EXPECT_EQ(test, workload->type, MVGAL_WORKLOAD_COMPUTE);
	mvgal_workload_free(workload);

	/* Test transfer workload */
	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);
	workload->type = MVGAL_WORKLOAD_TRANSFER;
	KUNIT_EXPECT_EQ(test, workload->type, MVGAL_WORKLOAD_TRANSFER);
	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_workload_memory_resources - Memory resource tracking
 */
static void test_mvgal_workload_memory_resources(struct kunit *test)
{
	struct mvgal_workload *workload;
	int i;

	workload = mvgal_workload_alloc();
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, workload);

	/* Add memory resources */
	for (i = 0; i < MVGAL_MAX_MEMORY_RESOURCES; i++) {
		workload->memory_addrs[i] = 0x1000 + (i * 4096);
		workload->memory_sizes[i] = 4096;
	}
	workload->num_memory_resources = MVGAL_MAX_MEMORY_RESOURCES;

	/* Verify */
	KUNIT_EXPECT_EQ(test, workload->num_memory_resources, MVGAL_MAX_MEMORY_RESOURCES);

	mvgal_workload_free(workload);
}

/*
 * Test: mvgal_find_available_gpu - GPU selection
 */
static void test_mvgal_find_available_gpu(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	struct mvgal_gpu_device *selected;
	int ret;

	/* Initialize device and scheduler */
	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = mvgal_scheduler_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add an available GPU */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	gpu->available = true;
	gpu->enabled = true;
	gpu->utilization = 50;
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Find available GPU */
	selected = mvgal_find_available_gpu(~0U);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, selected);
	KUNIT_EXPECT_EQ(test, selected->vendor, MVGAL_VENDOR_AMD);

	mvgal_device_fini(dev);
	mvgal_scheduler_fini();
}

/*
 * Test: mvgal_find_available_gpu_mask - GPU selection with mask
 */
static void test_mvgal_find_available_gpu_mask(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu1, *gpu2;
	struct mvgal_gpu_device *selected;
	int ret;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = mvgal_scheduler_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add two GPUs */
	gpu1 = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	gpu1->available = true;
	gpu1->enabled = true;
	ret = mvgal_gpu_add(dev, gpu1);
	KUNIT_EXPECT_EQ(test, ret, 0);

	gpu2 = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	gpu2->available = true;
	gpu2->enabled = true;
	ret = mvgal_gpu_add(dev, gpu2);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Select only first GPU */
	selected = mvgal_find_available_gpu(0x1);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, selected);
	KUNIT_EXPECT_EQ(test, selected->vendor, MVGAL_VENDOR_AMD);

	/* Select only second GPU */
	selected = mvgal_find_available_gpu(0x2);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, selected);
	KUNIT_EXPECT_EQ(test, selected->vendor, MVGAL_VENDOR_NVIDIA);

	mvgal_device_fini(dev);
	mvgal_scheduler_fini();
}

/*
 * Test cases for mvgal-scheduler suite
 */
static struct kunit_case mvgal_scheduler_tests[] = {
	KUNIT_CASE(test_mvgal_scheduler_init),
	KUNIT_CASE(test_mvgal_scheduler_init_twice),
	KUNIT_CASE(test_mvgal_workload_alloc),
	KUNIT_CASE(test_mvgal_workload_alloc_multiple),
	KUNIT_CASE(test_mvgal_workload_properties),
	KUNIT_CASE(test_mvgal_workload_priority_clamping),
	KUNIT_CASE(test_mvgal_workload_submit),
	KUNIT_CASE(test_mvgal_workload_submit_multiple),
	KUNIT_CASE(test_mvgal_workload_signal),
	KUNIT_CASE(test_mvgal_workload_signal_error),
	KUNIT_CASE(test_mvgal_workload_wait),
	KUNIT_CASE(test_mvgal_workload_wait_timeout),
	KUNIT_CASE(test_mvgal_workload_types),
	KUNIT_CASE(test_mvgal_workload_memory_resources),
	KUNIT_CASE(test_mvgal_find_available_gpu),
	KUNIT_CASE(test_mvgal_find_available_gpu_mask),
	{}
};

/*
 * Test suite for mvgal-scheduler
 */
static struct kunit_suite mvgal_scheduler_suite = {
	.name = "mvgal-scheduler",
	.test_cases = mvgal_scheduler_tests,
};
kunit_test_suites_register(&mvgal_scheduler_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MVGAL Project");
MODULE_DESCRIPTION("KUnit tests for MVGAL workload scheduler");