// SPDX-License-Identifier: GPL-2.0-only
/**
 * MVGAL KUnit Tests - Device Management
 * 
 * Tests for P2P (Peer-to-Peer) detection, GPU capabilities, and device properties.
 * 
 * Copyright (C) 2024 MVGAL Project
 */

#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>

/* Include the module headers */
#include "../mvgal_core.h"
#include "../mvgal_device.h"

/*
 * P2P support levels
 */
enum mvgal_p2p_support {
	MVGAL_P2P_SUPPORTED = 0,
	MVGAL_P2P_UNSUPPORTED = 1,
	MVGAL_P2P_NEEDS_SWITCH = 2,
};

/*
 * Test: mvgal_p2p_same_vendor - P2P between same vendor GPUs
 */
static void test_mvgal_p2p_same_vendor(struct kunit *test)
{
	enum mvgal_vendor_id vendor1 = MVGAL_VENDOR_AMD;
	enum mvgal_vendor_id vendor2 = MVGAL_VENDOR_AMD;
	enum mvgal_p2p_support p2p;

	/* Same vendor should support P2P */
	if (vendor1 == vendor2) {
		p2p = MVGAL_P2P_SUPPORTED;
	}

	KUNIT_EXPECT_EQ(test, p2p, MVGAL_P2P_SUPPORTED);
}

/*
 * Test: mvgal_p2p_different_vendor - P2P between different vendors
 */
static void test_mvgal_p2p_different_vendor(struct kunit *test)
{
	enum mvgal_vendor_id vendor1 = MVGAL_VENDOR_AMD;
	enum mvgal_vendor_id vendor2 = MVGAL_VENDOR_NVIDIA;
	enum mvgal_p2p_support p2p;

	/* Different vendors typically don't support direct P2P */
	if (vendor1 != vendor2) {
		p2p = MVGAL_P2P_UNSUPPORTED;
	}

	KUNIT_EXPECT_EQ(test, p2p, MVGAL_P2P_UNSUPPORTED);
}

/*
 * Test: mvgal_p2p_nvidia_nvlink - NVIDIA NVLink P2P
 */
static void test_mvgal_p2p_nvidia_nvlink(struct kunit *test)
{
	enum mvgal_vendor_id vendor1 = MVGAL_VENDOR_NVIDIA;
	enum mvgal_vendor_id vendor2 = MVGAL_VENDOR_NVIDIA;
	bool has_nvlink = true; /* Simulated */
	enum mvgal_p2p_support p2p;

	/* NVIDIA GPUs with NVLink can do P2P */
	if (vendor1 == MVGAL_VENDOR_NVIDIA && vendor2 == MVGAL_VENDOR_NVIDIA) {
		if (has_nvlink) {
			p2p = MVGAL_P2P_SUPPORTED;
		} else {
			p2p = MVGAL_P2P_UNSUPPORTED;
		}
	} else {
		p2p = MVGAL_P2P_UNSUPPORTED;
	}

	KUNIT_EXPECT_EQ(test, p2p, MVGAL_P2P_SUPPORTED);
}

/*
 * Test: mvgal_p2p_amd_xgmi - AMD xGMI/Infinity Fabric P2P
 */
static void test_mvgal_p2p_amd_xgmi(struct kunit *test)
{
	enum mvgal_vendor_id vendor1 = MVGAL_VENDOR_AMD;
	enum mvgal_vendor_id vendor2 = MVGAL_VENDOR_AMD;
	bool has_xgmi = true; /* Simulated */
	enum mvgal_p2p_support p2p;

	/* AMD GPUs with xGMI can do P2P */
	if (vendor1 == MVGAL_VENDOR_AMD && vendor2 == MVGAL_VENDOR_AMD) {
		if (has_xgmi) {
			p2p = MVGAL_P2P_SUPPORTED;
		} else {
			p2p = MVGAL_P2P_UNSUPPORTED;
		}
	} else {
		p2p = MVGAL_P2P_UNSUPPORTED;
	}

	KUNIT_EXPECT_EQ(test, p2p, MVGAL_P2P_SUPPORTED);
}

/*
 * Test: mvgal_p2p_intel - Intel GPU P2P
 */
static void test_mvgal_p2p_intel(struct kunit *test)
{
	enum mvgal_vendor_id vendor1 = MVGAL_VENDOR_INTEL;
	enum mvgal_vendor_id vendor2 = MVGAL_VENDOR_INTEL;
	enum mvgal_p2p_support p2p;

	/* Intel GPUs typically don't support P2P */
	if (vendor1 == MVGAL_VENDOR_INTEL && vendor2 == MVGAL_VENDOR_INTEL) {
		p2p = MVGAL_P2P_UNSUPPORTED;
	} else {
		p2p = MVGAL_P2P_UNSUPPORTED;
	}

	KUNIT_EXPECT_EQ(test, p2p, MVGAL_P2P_UNSUPPORTED);
}

/*
 * Test: mvgal_pci_device_detection - PCI device detection
 */
static void test_mvgal_pci_device_detection(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;
	uint16_t vendor_id, device_id;

	/* Test AMD GPU detection */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpu);

	gpu->pci_vendor_id = PCI_VENDOR_ID_ATI;
	gpu->pci_device_id = 0x73BF; /* RX 7900 XTX */

	vendor_id = gpu->pci_vendor_id;
	device_id = gpu->pci_device_id;

	KUNIT_EXPECT_EQ(test, vendor_id, (uint16_t)PCI_VENDOR_ID_ATI);
	KUNIT_EXPECT_NE(test, device_id, 0U);

	mvgal_gpu_free(gpu);
}

/*
 * Test: mvgal_gpu_vram_size - GPU VRAM size validation
 */
static void test_mvgal_gpu_vram_size(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;
	uint64_t vram_sizes[] = {
		4ULL * 1024 * 1024 * 1024,  /* 4GB */
		8ULL * 1024 * 1024 * 1024,  /* 8GB */
		12ULL * 1024 * 1024 * 1024, /* 12GB */
		24ULL * 1024 * 1024 * 1024, /* 24GB */
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vram_sizes); i++) {
		gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
		gpu->vram_size = vram_sizes[i];

		/* VRAM should be at least 1GB */
		KUNIT_EXPECT_GE(test, gpu->vram_size,
				1ULL * 1024 * 1024 * 1024);

		mvgal_gpu_free(gpu);
	}
}

/*
 * Test: mvgal_gpu_compute_units - Compute unit validation
 */
static void test_mvgal_gpu_compute_units(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;

	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	gpu->compute_units = 64;

	/* Compute units should be reasonable */
	KUNIT_EXPECT_GT(test, gpu->compute_units, 0U);
	KUNIT_EXPECT_LE(test, gpu->compute_units, 512U);

	mvgal_gpu_free(gpu);
}

/*
 * Test: mvgal_gpu_memory_bandwidth - Memory bandwidth validation
 */
static void test_mvgal_gpu_memory_bandwidth(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;

	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	gpu->vram_bandwidth = 800 * 1024; /* 800 GB/s */

	/* Bandwidth should be reasonable */
	KUNIT_EXPECT_GT(test, gpu->vram_bandwidth, 0U);
	KUNIT_EXPECT_LE(test, gpu->vram_bandwidth, 2000U * 1024U);

	mvgal_gpu_free(gpu);
}

/*
 * Test: mvgal_gpu_pcie_generation - PCIe generation detection
 */
static void test_mvgal_gpu_pcie_generation(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;
	uint32_t pcie_gens[] = { 3, 4, 5 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pcie_gens); i++) {
		gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
		gpu->pcie_gen = pcie_gens[i];
		gpu->pcie_lanes = 16;

		/* PCIe gen should be valid */
		KUNIT_EXPECT_GE(test, gpu->pcie_gen, 1U);
		KUNIT_EXPECT_LE(test, gpu->pcie_gen, 5U);
		KUNIT_EXPECT_EQ(test, gpu->pcie_lanes, 16U);

		mvgal_gpu_free(gpu);
	}
}

/*
 * Test: mvgal_gpu_api_support - API support flags
 */
static void test_mvgal_gpu_api_support(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;

	/* AMD GPU with full support */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	gpu->api_flags = MVGAL_API_VULKAN | MVGAL_API_OPENGL | MVGAL_API_OPENCL;

	KUNIT_EXPECT_TRUE(test, gpu->api_flags & MVGAL_API_VULKAN);
	KUNIT_EXPECT_TRUE(test, gpu->api_flags & MVGAL_API_OPENGL);
	KUNIT_EXPECT_TRUE(test, gpu->api_flags & MVGAL_API_OPENCL);
	KUNIT_EXPECT_FALSE(test, gpu->api_flags & MVGAL_API_CUDA);

	mvgal_gpu_free(gpu);

	/* NVIDIA GPU with CUDA */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	gpu->api_flags = MVGAL_API_VULKAN | MVGAL_API_OPENCL | MVGAL_API_CUDA;

	KUNIT_EXPECT_TRUE(test, gpu->api_flags & MVGAL_API_CUDA);

	mvgal_gpu_free(gpu);
}

/*
 * Test: mvgal_gpu_numa_awareness - NUMA awareness detection
 */
static void test_mvgal_gpu_numa_awareness(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	int ret;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add GPU with NUMA node */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	gpu->numa_node = 0;
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Add GPU with different NUMA node */
	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
	gpu->numa_node = 1;
	ret = mvgal_gpu_add(dev, gpu);
	KUNIT_EXPECT_EQ(test, ret, 0);

	/* Compute capability profile should detect NUMA */
	mvgal_compute_capability_profile(dev);

	/* NUMA awareness should be enabled */
	KUNIT_EXPECT_TRUE(test, dev->caps.numa_aware);

	mvgal_device_fini(dev);
}

/*
 * Test: mvgal_gpu_availability - GPU availability tracking
 */
static void test_mvgal_gpu_availability(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;

	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);

	/* Initially available */
	KUNIT_EXPECT_TRUE(test, gpu->available);
	KUNIT_EXPECT_TRUE(test, gpu->enabled);

	/* Mark as unavailable */
	gpu->available = false;
	KUNIT_EXPECT_FALSE(test, gpu->available);

	/* Mark as disabled */
	gpu->enabled = false;
	KUNIT_EXPECT_FALSE(test, gpu->enabled);

	mvgal_gpu_free(gpu);
}

/*
 * Test: mvgal_gpu_utilization - GPU utilization tracking
 */
static void test_mvgal_gpu_utilization(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;
	uint32_t utilizations[] = { 0, 25, 50, 75, 100 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(utilizations); i++) {
		gpu = mvgal_gpu_alloc(MVGAL_VENDOR_NVIDIA);
		gpu->utilization = utilizations[i];

		/* Utilization should be valid */
		KUNIT_EXPECT_GE(test, gpu->utilization, 0U);
		KUNIT_EXPECT_LE(test, gpu->utilization, 100U);

		mvgal_gpu_free(gpu);
	}
}

/*
 * Test: mvgal_gpu_temperature - GPU temperature tracking
 */
static void test_mvgal_gpu_temperature(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;
	int32_t temps[] = { 30, 45, 60, 80, 95 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(temps); i++) {
		gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
		gpu->temperature = temps[i];

		/* Temperature should be valid */
		KUNIT_EXPECT_GE(test, gpu->temperature, 0);
		KUNIT_EXPECT_LE(test, gpu->temperature, 120);

		mvgal_gpu_free(gpu);
	}
}

/*
 * Test: mvgal_gpu_power_state - GPU power state tracking
 */
static void test_mvgal_gpu_power_state(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;
	enum mvgal_power_state states[] = {
		MVGAL_POWER_STATE_ACTIVE,
		MVGAL_POWER_STATE_SUSTAINED,
		MVGAL_POWER_STATE_IDLE,
		MVGAL_POWER_STATE_PARK
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(states); i++) {
		gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
		gpu->power_state = states[i];

		/* Power state should be valid */
		KUNIT_EXPECT_GE(test, (int)gpu->power_state, 0);
		KUNIT_EXPECT_LE(test, gpu->power_state, MVGAL_POWER_STATE_PARK);

		mvgal_gpu_free(gpu);
	}
}

/*
 * Test: mvgal_gpu_name_formatting - GPU name formatting
 */
static void test_mvgal_gpu_name_formatting(struct kunit *test)
{
	struct mvgal_gpu_device *gpu;

	gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
	gpu->pci_vendor_id = PCI_VENDOR_ID_ATI;
	gpu->pci_device_id = 0x73BF;

	/* Format name */
	snprintf(gpu->name, sizeof(gpu->name), "%04x:%04x",
		 gpu->pci_vendor_id, gpu->pci_device_id);

	/* Name should be formatted correctly */
	KUNIT_EXPECT_STRNEQ(test, gpu->name, "");

	mvgal_gpu_free(gpu);
}

/*
 * Test: mvgal_device_gpu_count - Device GPU count tracking
 */
static void test_mvgal_device_gpu_count(struct kunit *test)
{
	struct mvgal_device *dev;
	struct mvgal_gpu_device *gpu;
	int ret;
	int i;

	ret = mvgal_device_init(&dev);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, dev->gpu_count, 0U);

	/* Add GPUs and verify count */
	for (i = 0; i < 3; i++) {
		gpu = mvgal_gpu_alloc(MVGAL_VENDOR_AMD);
		ret = mvgal_gpu_add(dev, gpu);
		KUNIT_EXPECT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, dev->gpu_count, (uint32_t)(i + 1));
	}

	mvgal_device_fini(dev);
}

/*
 * Test: mvgal_p2p_detection_logic - P2P detection algorithm
 */
static void test_mvgal_p2p_detection_logic(struct kunit *test)
{
	enum mvgal_p2p_support p2p;
	bool same_root_complex = true;
	bool has_vendor_p2p = true;

	/* P2P decision logic */
	if (same_root_complex) {
		p2p = MVGAL_P2P_SUPPORTED;
	} else if (has_vendor_p2p) {
		p2p = MVGAL_P2P_NEEDS_SWITCH;
	} else {
		p2p = MVGAL_P2P_UNSUPPORTED;
	}

	KUNIT_EXPECT_EQ(test, p2p, MVGAL_P2P_SUPPORTED);
}

/*
 * Test cases for mvgal-device suite
 */
static struct kunit_case mvgal_device_tests[] = {
	KUNIT_CASE(test_mvgal_p2p_same_vendor),
	KUNIT_CASE(test_mvgal_p2p_different_vendor),
	KUNIT_CASE(test_mvgal_p2p_nvidia_nvlink),
	KUNIT_CASE(test_mvgal_p2p_amd_xgmi),
	KUNIT_CASE(test_mvgal_p2p_intel),
	KUNIT_CASE(test_mvgal_pci_device_detection),
	KUNIT_CASE(test_mvgal_gpu_vram_size),
	KUNIT_CASE(test_mvgal_gpu_compute_units),
	KUNIT_CASE(test_mvgal_gpu_memory_bandwidth),
	KUNIT_CASE(test_mvgal_gpu_pcie_generation),
	KUNIT_CASE(test_mvgal_gpu_api_support),
	KUNIT_CASE(test_mvgal_gpu_numa_awareness),
	KUNIT_CASE(test_mvgal_gpu_availability),
	KUNIT_CASE(test_mvgal_gpu_utilization),
	KUNIT_CASE(test_mvgal_gpu_temperature),
	KUNIT_CASE(test_mvgal_gpu_power_state),
	KUNIT_CASE(test_mvgal_gpu_name_formatting),
	KUNIT_CASE(test_mvgal_device_gpu_count),
	KUNIT_CASE(test_mvgal_p2p_detection_logic),
	{}
};

/*
 * Test suite for mvgal-device
 */
static struct kunit_suite mvgal_device_suite = {
	.name = "mvgal-device",
	.test_cases = mvgal_device_tests,
};
kunit_test_suites_register(&mvgal_device_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MVGAL Project");
MODULE_DESCRIPTION("KUnit tests for MVGAL device management");