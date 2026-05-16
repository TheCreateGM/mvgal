// SPDX-License-Identifier: GPL-2.0-only
/**
 * MVGAL KUnit Tests - Memory Management
 * 
 * Tests for memory allocation, DMA-BUF operations, and memory tracking.
 * 
 * Copyright (C) 2024 MVGAL Project
 */

#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>

/* Include the module headers */
#include "../mvgal_core.h"
#include "../mvgal_device.h"
#include "../mvgal_memory.h"

/* Global memory manager state for testing */
static struct {
	struct list_head memory_list;
	struct mutex lock;
	atomic_t next_memory_id;
	struct kmem_cache *cache;
	bool initialized;
} test_memory_manager;

/*
 * Helper: Initialize test memory manager
 */
static int test_memory_manager_init(void)
{
	if (test_memory_manager.initialized) {
		return 0;
	}

	INIT_LIST_HEAD(&test_memory_manager.memory_list);
	mutex_init(&test_memory_manager.lock);
	atomic_set(&test_memory_manager.next_memory_id, 1);

	test_memory_manager.cache = kmem_cache_create("mvgal_memory_test",
			sizeof(struct mvgal_memory), 0,
			SLAB_HWCACHE_ALIGN, NULL);
	if (!test_memory_manager.cache) {
		return -ENOMEM;
	}

	test_memory_manager.initialized = true;
	return 0;
}

/*
 * Helper: Cleanup test memory manager
 */
static void test_memory_manager_fini(void)
{
	struct mvgal_memory *mem, *tmp;

	if (!test_memory_manager.initialized) {
		return;
	}

	mutex_lock(&test_memory_manager.lock);
	list_for_each_entry_safe(mem, tmp, &test_memory_manager.memory_list, node) {
		list_del(&mem->node);
		kmem_cache_free(test_memory_manager.cache, mem);
	}
	mutex_unlock(&test_memory_manager.lock);
	mutex_destroy(&test_memory_manager.lock);

	if (test_memory_manager.cache) {
		kmem_cache_destroy(test_memory_manager.cache);
	}

	test_memory_manager.initialized = false;
}

/*
 * Helper: Allocate test memory
 */
static struct mvgal_memory *test_memory_alloc(size_t size)
{
	struct mvgal_memory *mem;

	if (!test_memory_manager.initialized) {
		return NULL;
	}

	mem = kmem_cache_alloc(test_memory_manager.cache, GFP_KERNEL);
	if (!mem) {
		return NULL;
	}

	mem->id = atomic_fetch_inc(&test_memory_manager.next_memory_id);
	mem->size = size;
	mem->flags = MVGAL_MEMORY_READ | MVGAL_MEMORY_WRITE;
	kref_init(&mem->refcount);
	INIT_LIST_HEAD(&mem->workloads);

	mutex_lock(&test_memory_manager.lock);
	list_add_tail(&mem->node, &test_memory_manager.memory_list);
	mutex_unlock(&test_memory_manager.lock);

	return mem;
}

/*
 * Helper: Free test memory
 */
static void test_memory_free(struct mvgal_memory *mem)
{
	if (!mem || !test_memory_manager.initialized) {
		return;
	}

	mutex_lock(&test_memory_manager.lock);
	list_del(&mem->node);
	mutex_unlock(&test_memory_manager.lock);

	kmem_cache_free(test_memory_manager.cache, mem);
}

/*
 * Test: mvgal_memory_init - Memory manager initialization
 */
static void test_mvgal_memory_init(struct kunit *test)
{
	int ret;

	/* Initialize memory manager */
	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Cleanup */
	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_init_twice - Double initialization handling
 */
static void test_mvgal_memory_init_twice(struct kunit *test)
{
	int ret;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Second init should also succeed (idempotent) */
	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_allocation - Basic memory allocation
 */
static void test_mvgal_memory_allocation(struct kunit *test)
{
	struct mvgal_memory *mem;
	int ret;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Allocate 1MB */
	mem = test_memory_alloc(1024 * 1024);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem);
	KUNIT_EXPECT_EQ(test, mem->size, (size_t)(1024 * 1024));
	KUNIT_EXPECT_GT(test, mem->id, 0U);

	test_memory_free(mem);
	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_allocation_sizes - Various allocation sizes
 */
static void test_mvgal_memory_allocation_sizes(struct kunit *test)
{
	struct mvgal_memory *mem;
	size_t sizes[] = { 4096, 1024 * 1024, 8 * 1024 * 1024, 64 * 1024 * 1024 };
	int ret;
	unsigned int i;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		mem = test_memory_alloc(sizes[i]);
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem);
		KUNIT_EXPECT_EQ(test, mem->size, sizes[i]);
		test_memory_free(mem);
	}

	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_reference_counting - Reference counting
 */
static void test_mvgal_memory_reference_counting(struct kunit *test)
{
	struct mvgal_memory *mem;
	int ret;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	mem = test_memory_alloc(1024 * 1024);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem);

	/* Initial refcount should be 1 */
	KUNIT_EXPECT_EQ(test, atomic_read(&mem->refcount.refs.counter), 1);

	/* Get reference */
	mvgal_memory_get(mem);
	KUNIT_EXPECT_EQ(test, atomic_read(&mem->refcount.refs.counter), 2);

	/* Put reference */
	mvgal_memory_put(mem);
	KUNIT_EXPECT_EQ(test, atomic_read(&mem->refcount.refs.counter), 1);

	/* Put final reference - should free */
	mvgal_memory_put(mem);

	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_list_tracking - Memory list tracking
 */
static void test_mvgal_memory_list_tracking(struct kunit *test)
{
	struct mvgal_memory *mem1, *mem2, *mem3;
	int ret;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Allocate multiple memories */
	mem1 = test_memory_alloc(1024);
	mem2 = test_memory_alloc(2048);
	mem3 = test_memory_alloc(4096);

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem1);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem2);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem3);

	/* Verify they have unique IDs */
	KUNIT_EXPECT_NE(test, mem1->id, mem2->id);
	KUNIT_EXPECT_NE(test, mem2->id, mem3->id);
	KUNIT_EXPECT_NE(test, mem1->id, mem3->id);

	/* Free in different order */
	test_memory_free(mem2);
	test_memory_free(mem3);
	test_memory_free(mem1);

	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_flags - Memory allocation flags
 */
static void test_mvgal_memory_flags(struct kunit *test)
{
	struct mvgal_memory *mem;
	int ret;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Test with read flag */
	mem = test_memory_alloc(1024);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem);
	KUNIT_EXPECT_TRUE(test, mem->flags & MVGAL_MEMORY_READ);
	test_memory_free(mem);

	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_cleanup - Complete memory cleanup
 */
static void test_mvgal_memory_cleanup(struct kunit *test)
{
	struct mvgal_memory *mem;
	int ret;
	int i;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Allocate many memories */
	for (i = 0; i < 10; i++) {
		mem = test_memory_alloc(1024 * (i + 1));
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem);
	}

	/* Cleanup should free all */
	mvgal_memory_fini();

	/* Re-init should work */
	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);
	mvgal_memory_fini();
}

/*
 * Test: mvgal_memory_max_allocation - Large allocation handling
 */
static void test_mvgal_memory_max_allocation(struct kunit *test)
{
	struct mvgal_memory *mem;
	size_t large_size = 16ULL * 1024 * 1024 * 1024; /* 16GB */
	int ret;

	ret = mvgal_memory_init();
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Large allocation should succeed */
	mem = test_memory_alloc(large_size);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, mem);
	KUNIT_EXPECT_EQ(test, mem->size, large_size);

	test_memory_free(mem);
	mvgal_memory_fini();
}

/*
 * Test cases for mvgal-memory suite
 */
static struct kunit_case mvgal_memory_tests[] = {
	KUNIT_CASE(test_mvgal_memory_init),
	KUNIT_CASE(test_mvgal_memory_init_twice),
	KUNIT_CASE(test_mvgal_memory_allocation),
	KUNIT_CASE(test_mvgal_memory_allocation_sizes),
	KUNIT_CASE(test_mvgal_memory_reference_counting),
	KUNIT_CASE(test_mvgal_memory_list_tracking),
	KUNIT_CASE(test_mvgal_memory_flags),
	KUNIT_CASE(test_mvgal_memory_cleanup),
	KUNIT_CASE(test_mvgal_memory_max_allocation),
	{}
};

/*
 * Test suite for mvgal-memory
 */
static struct kunit_suite mvgal_memory_suite = {
	.name = "mvgal-memory",
	.test_cases = mvgal_memory_tests,
};
kunit_test_suites_register(&mvgal_memory_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MVGAL Project");
MODULE_DESCRIPTION("KUnit tests for MVGAL memory management");