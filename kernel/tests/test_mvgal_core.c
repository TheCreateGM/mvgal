// SPDX-License-Identifier: GPL-2.0-only
/**
 * MVGAL KUnit Tests - Core Module
 * 
 * Tests for device initialization, GPU enumeration, and capability profiles.
 * 
 * Copyright (C) 2024 MVGAL Project
 */

#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>

/* Include the module headers */
#include "../mvgal_core.h"
#include "../mvgal_device.h"

/*
 * Test: mvgal_device_init - Logical device initialization
 */
static void test_mvgal_device_init(struct kunit *test)
{
	struct mvgal_device *dev = NULL;
	int ret;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, dev);
	KUNIT_EXPECT_TRUE(test, dev->initialized);
	KUNIT_EXPECT_EQ(test, dev->gpu_count, 0U);

	/* Cleanup */
	mvgal_device_fini(dev);
}

/*
 * Test: mvgal_device_init_failure - Failed initialization handling
 */
static void test_mvgal_device_init_failure(struct kunit *test)
{
	struct mvgal_device *dev = NULL;
	int ret;

	/* Test with NULL output pointer */
	ret = mvgal_device_init(NULL);
	KUNIT_EXPECT_NE(test, ret, 0);
}

/*
 * Test: mvgal_gpu_alloc - GPU structure allocation
 */
static void test_mvgal_gpu_alloc(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;

	/* Test AMD GPU allocation */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	KUNIT_EXPECT_EQ(test, gpu->vendor, MVGAL_VENDOR_AMD);
	KUNIT_EXPECT_TRUE(test, gpu->available);
	KUNIT_EXPECT_TRUE(test, gpu->enabled);
	mvgal_gpu_free(gpu);

	/* Test NVIDIA GPU allocation */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	KUNIT_EXPECT_EQ(test, gpu->vendor, MVGAL_VENDOR_NVIDIA);
	mvgal_gpu_free(gpu);

	/* Test Intel GPU allocation */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_INTEL);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	KUNIT_EXPECT_EQ(test, gpu->vendor, MVGAL_VENDOR_INTEL);
	mvgal_gpu_free(gpu);

	/* Test MTT GPU allocation */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_MTT);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	KUNIT_EXPECT_EQ(test, gpu->vendor, MVGAL_VENDOR_MTT);
	mvgal_gpu_free(gpu);

	/* Test unknown vendor */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_UNKNOWN);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	KUNIT_EXPECT_EQ(test, gpu->vendor, MVGAL_VENDOR_UNKNOWN);
	mvgal_gpu_free(gpu);
}

/*
 * Test: mvgal_gpu_add_remove - GPU add and remove operations
 */
static void test_mvgal_gpu_add_remove(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	int ret;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add a GPU */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);

	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, dev->gpu_count, 1U);

	/* Add another GPU */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);

	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, dev->gpu_count, 2U);

	/* Remove a GPU - need to find it first */
	gpu = mvgal_gpu_find_by_vendor(dev, MVGAL_VENDOR_AMD);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	mvgal_gpu_remove(dev, gpu);
	KUNIT_EXPECT_EQ(test, dev->gpu_count, 1U);

	mvgal_device_fini(dev);
}

/*
 * Test: mvgal_gpu_find_by_vendor - GPU lookup by vendor
 */
static void test_mvgal_gpu_find_by_vendor(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	int ret;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add GPUs */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Find by vendor */
	gpu = mvgal_gpu_find_by_vendor(dev, MVGAL_VENDOR_AMD);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	KUNIT_EXPECT_EQ(test, gpu->vendor, MVGAL_VENDOR_AMD);

	gpu = mvgal_gpu_find_by_vendor(dev, MVGAL_VENDOR_NVIDIA);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);
	KUNIT_EXPECT_EQ(test, gpu->vendor, MVGAL_VENDOR_NVIDIA);

	/* Find non-existent vendor */
	gpu = mvgal_gpu_find_by_vendor(dev, MVGAL_VENDOR_INTEL);
	KUNIT_EXPECT_PTR_EQ(test, gpu, NULL);

	mvgal_device_fini(dev);
}

/*
 * Test: mvgal_compute_capability_profile - Capability profile computation
 */
static void test_mvgal_capability_profile(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	int ret;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add AMD GPU with 8GB VRAM */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	gpu->vram_size = 8ULL * 1024 * 1024 * 1024;
	gpu->vram_bandwidth = 500 * 1024;
	gpu->compute_units = 64;
	gpu->api_flags = MVGAL_API_VULKAN | MVGAL_API_OPENCL;
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add NVIDIA GPU with 12GB VRAM */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	gpu->vram_size = 12ULL * 1024 * 1024 * 1024;
	gpu->vram_bandwidth = 800 * 1024;
	gpu->compute_units = 82;
	gpu->api_flags = MVGAL_API_VULKAN | MVGAL_API_OPENCL | MVGAL_API_CUDA;
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Compute capability profile */
	mvgal_compute_capability_profile(dev);

	/* Verify aggregated capabilities */
	KUNIT_EXPECT_EQ(test, dev->caps.gpu_count, 2U);
	KUNIT_EXPECT_EQ(test, dev->caps.total_vram,
			(8ULL + 12ULL) * 1024 * 1024 * 1024);
	KUNIT_EXPECT_EQ(test, dev->caps.max_compute_units, 64U + 82U);
	KUNIT_EXPECT_TRUE(test, dev->caps.p2p_supported);
	KUNIT_EXPECT_TRUE(test, dev->caps.numa_aware);

	mvgal_device_fini(dev);
}

/*
 * Test: mvgal_gpu_cleanup_all - Cleanup all GPUs
 */
static void test_mvgal_gpu_cleanup_all(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	int ret;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add multiple GPUs */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_INTEL);
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, dev->gpu_count, 3U);

	/* Cleanup all */
	mvgal_gpu_cleanup_all(dev);
	KUNIT_EXPECT_EQ(test, dev->gpu_count, 0U);

	mvgal_device_fini(dev);
}

/*
 * Test: mvgal_max_gpus - Maximum GPU limit enforcement
 */
static void test_mvgal_max_gpus(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	int ret;
	int i;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add maximum GPUs */
	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
		ret = mvgal_gpu_add(dev, gpu);
		if (ret == 0) {
			continue;
		}
		/* Expected to fail after max GPUs */
		KUNIT_EXPECT_EQ(test, ret, -ENOSPC);
		mvgal_gpu_free(gpu);
		break;
	}

	mvgal_device_fini(dev);
}

/*
 * Test cases for mvgal-core suite
 */
static struct kunit_case mvgal_core_tests[] = {
	KUNIT_CASE(test_mvgal_device_init),
	KUNIT_CASE(test_mvgal_device_init_failure),
	KUNIT_CASE(test_mvgal_gpu_alloc),
	KUNIT_CASE(test_mvgal_gpu_add_remove),
	KUNIT_CASE(test_mvgal_gpu_find_by_vendor),
	KUNIT_CASE(test_mvgal_capability_profile),
	KUNIT_CASE(test_mvgal_gpu_cleanup_all),
	KUNIT_CASE(test_mvgal_max_gpus),
	{}
};

/*
 * Test suite for mvgal-core
 */
static struct kunit_suite mvgal_core_suite = {
	.name = "mvgal-core",
	.test_cases = mvgal_core_tests,
};
kunit_test_suites_register(&mvgal_core_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MVGAL Project");
MODULE_DESCRIPTION("KUnit tests for MVGAL core module");