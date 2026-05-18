/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Device Management - Logical device creation and GPU enumeration
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/dma-buf.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>

#include "mvgal_core.h"
#include "mvgal_device.h"
#include "mvgal_memory.h"
#include "mvgal_power.h"

/* Previously included mvgal_sync.h for fence definitions */
#include "mvgal_sync.h"

/* Vendor operations */
#include "vendors/mvgal_amd.h"
#include "vendors/mvgal_nvidia.h"
#include "vendors/mvgal_intel.h"
#include "vendors/mvgal_mtt.h"

/* DRM driver (defined in mvgal_core.c) */
extern struct drm_driver mvgal_drm_driver;

/*
 * mvgal_device_init - Initialize the MVGAL logical device
 */
int mvgal_device_init(struct mvgal_device **dev_out)
{
	struct mvgal_device *dev;
	int ret = 0;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		return -ENOMEM;
	}

	/* Initialize DRM device (kernel 6.8+ uses drm_dev_alloc) */
	dev->drm = drm_dev_alloc(&mvgal_drm_driver, NULL);
	if (IS_ERR(dev->drm)) {
		ret = PTR_ERR(dev->drm);
		pr_err("MVGAL: Failed to allocate DRM device\n");
		goto err_free_dev;
	}

	/* Initialize lists and locks */
	INIT_LIST_HEAD(&dev->gpu_list);
	mutex_init(&dev->gpu_lock);
	INIT_LIST_HEAD(&dev->workload_queue);
	mutex_init(&dev->workload_lock);
	init_waitqueue_head(&dev->workload_waitq);
	INIT_LIST_HEAD(&dev->fence_list);
	mutex_init(&dev->fence_lock);
	atomic_set(&dev->next_fence_seq, 1);

	dev->gpu_count = 0;
	dev->initialized = true;
	dev->daemon_connected = false;

	*dev_out = dev;
	return 0;

err_free_dev:
	kfree(dev);
	return ret;
}

/**
 * mvgal_device_fini - Cleanup the MVGAL logical device
 */
void mvgal_device_fini(struct mvgal_device *dev)
{
	if (!dev) {
		return;
	}

	pr_debug("MVGAL: Cleaning up logical device\n");

	/* Cleanup all GPUs */
	mvgal_gpu_cleanup_all(dev);

	/* Cleanup DRM device */
	drm_dev_put(dev->drm);

	/* Free device */
	kfree(dev);
}

/**
 * mvgal_gpu_alloc - Allocate a new GPU device structure
 */
struct mvgal_gpu_device *mvgal_gpu_alloc(enum mvgal_vendor_id vendor)
{
	struct mvgal_gpu_device *gpu;

	gpu = kzalloc(sizeof(*gpu), GFP_KERNEL);
	if (!gpu) {
		return NULL;
	}

	gpu->vendor = vendor;
	gpu->available = true;
	gpu->enabled = true;
	gpu->gpu_index = 0;
	gpu->power_draw = 0;
	gpu->dvfs = NULL;
	gpu->thermal = NULL;
	mutex_init(&gpu->lock);

	/* Set vendor-specific operations */
	switch (vendor) {
	case MVGAL_VENDOR_AMD:
		gpu->ops = &mvgal_amd_ops;
		break;
	case MVGAL_VENDOR_NVIDIA:
		gpu->ops = &mvgal_nvidia_ops;
		break;
	case MVGAL_VENDOR_INTEL:
		gpu->ops = &mvgal_intel_ops;
		break;
	case MVGAL_VENDOR_MTT:
		gpu->ops = &mvgal_mtt_ops;
		break;
	default:
		gpu->ops = NULL;
		break;
	}

	return gpu;
}

/**
 * mvgal_gpu_free - Free a GPU device structure
 */
void mvgal_gpu_free(struct mvgal_gpu_device *gpu)
{
	if (!gpu) {
		return;
	}

	/* Stop and cleanup DVFS */
	if (gpu->dvfs) {
		mvgal_dvfs_stop(gpu);
		mvgal_dvfs_fini(gpu);
	}

	/* Stop and cleanup thermal */
	if (gpu->thermal) {
		mvgal_thermal_stop(gpu);
		mvgal_thermal_fini(gpu);
	}

	/* Unregister from power budget */
	mvgal_power_budget_unregister_gpu(gpu->gpu_index);

	if (gpu->ops && gpu->ops->fini) {
		gpu->ops->fini(gpu);
	}

	mutex_destroy(&gpu->lock);
	kfree(gpu->vendor_priv);
	kfree(gpu);
}

/**
 * mvgal_gpu_add - Add a GPU to the MVGAL logical device
 */
int mvgal_gpu_add(struct mvgal_device *dev, struct mvgal_gpu_device *gpu)
{
	int ret;

	if (!dev || !gpu) {
		return -EINVAL;
	}

	mutex_lock(&dev->gpu_lock);

	if (dev->gpu_count >= MVGAL_MAX_GPUS) {
		mutex_unlock(&dev->gpu_lock);
		return -ENOSPC;
	}

	/* Set GPU index */
	gpu->gpu_index = dev->gpu_count;

	/* Initialize GPU if ops are available */
	if (gpu->ops && gpu->ops->init) {
		ret = gpu->ops->init(gpu);
		if (ret < 0) {
			mutex_unlock(&dev->gpu_lock);
			return ret;
		}
	}

	/* Initialize DVFS for this GPU */
	ret = mvgal_dvfs_init(gpu);
	if (ret < 0) {
		pr_warn("MVGAL: Failed to initialize DVFS for GPU %s: %d\n",
			gpu->name, ret);
		/* Non-fatal - continue without DVFS */
	}

	/* Initialize thermal throttling for this GPU */
	ret = mvgal_thermal_init(gpu);
	if (ret < 0) {
		pr_warn("MVGAL: Failed to initialize thermal for GPU %s: %d\n",
			gpu->name, ret);
		/* Non-fatal - continue without thermal */
	}

	/* Register GPU with power budget */
	ret = mvgal_power_budget_register_gpu(gpu, gpu->gpu_index);
	if (ret < 0) {
		pr_warn("MVGAL: Failed to register GPU %s with power budget: %d\n",
			gpu->name, ret);
		/* Non-fatal - continue without power budget */
	}

	/* Add to GPU list */
	list_add_tail(&gpu->node, &dev->gpu_list);
	dev->gpu_count++;

	pr_info("MVGAL: Added GPU '%s' (vendor=%d, vram=%llu MB, index=%u)\n",
		gpu->name, gpu->vendor,
		(unsigned long long)(gpu->vram_size / (1024ULL * 1024ULL)),
		gpu->gpu_index);

	mutex_unlock(&dev->gpu_lock);

	return 0;
}

/**
 * mvgal_gpu_remove - Remove a GPU from the MVGAL logical device
 */
void mvgal_gpu_remove(struct mvgal_device *dev, struct mvgal_gpu_device *gpu)
{
	if (!dev || !gpu) {
		return;
	}

	mutex_lock(&dev->gpu_lock);
	
	list_del(&gpu->node);
	dev->gpu_count--;

	mutex_unlock(&dev->gpu_lock);

	pr_info("MVGAL: Removed GPU '%s'\n", gpu->name);

	mvgal_gpu_free(gpu);
}

/**
 * mvgal_gpu_cleanup_all - Cleanup all GPUs in the device
 */
void mvgal_gpu_cleanup_all(struct mvgal_device *dev)
{
	struct mvgal_gpu_device *gpu, *tmp;

	mutex_lock(&dev->gpu_lock);
	
	list_for_each_entry_safe(gpu, tmp, &dev->gpu_list, node) {
		list_del(&gpu->node);
		dev->gpu_count--;
		mutex_unlock(&dev->gpu_lock);
		
		mvgal_gpu_free(gpu);
		
		mutex_lock(&dev->gpu_lock);
	}
	
	mutex_unlock(&dev->gpu_lock);
}

/**
 * mvgal_gpu_find_by_vendor - Find a GPU by vendor ID
 */
struct mvgal_gpu_device *mvgal_gpu_find_by_vendor(struct mvgal_device *dev, enum mvgal_vendor_id vendor)
{
	struct mvgal_gpu_device *gpu;

	mutex_lock(&dev->gpu_lock);
	
	list_for_each_entry(gpu, &dev->gpu_list, node) {
		if (gpu->vendor == vendor) {
			mutex_unlock(&dev->gpu_lock);
			return gpu;
		}
	}
	
	mutex_unlock(&dev->gpu_lock);
	return NULL;
}

/**
 * mvgal_enumerate_gpus - Enumerate all GPUs and populate the device
 * Called during module initialization to discover all physical GPUs
 */
int mvgal_enumerate_gpus(struct mvgal_device *dev)
{
	struct pci_dev *pdev = NULL;
	int ret = 0;

	pr_info("MVGAL: Enumerating GPUs via PCI bus\n");

	/* Iterate through all PCI devices */
	while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
		uint16_t vendor_id = pdev->vendor;
		uint16_t device_id = pdev->device;
		enum mvgal_vendor_id mvgal_vendor = MVGAL_VENDOR_UNKNOWN;

		/* Determine vendor */
		if (vendor_id == PCI_VENDOR_ID_ATI) {
			mvgal_vendor = MVGAL_VENDOR_AMD;
		} else if (vendor_id == PCI_VENDOR_ID_NVIDIA) {
			mvgal_vendor = MVGAL_VENDOR_NVIDIA;
		} else if (vendor_id == PCI_VENDOR_ID_INTEL) {
			/* Check if it's a GPU (not a CPU integrated graphics) */
			if ((pdev->class >> 8) == PCI_BASE_CLASS_DISPLAY) {
				mvgal_vendor = MVGAL_VENDOR_INTEL;
			}
		} else if (vendor_id == 0x1ED5) {
			mvgal_vendor = MVGAL_VENDOR_MTT;
		}

		if (mvgal_vendor != MVGAL_VENDOR_UNKNOWN) {
			struct mvgal_gpu_device *gpu;

			gpu = mvgal_gpu_alloc(mvgal_vendor);
			if (!gpu) {
				ret = -ENOMEM;
				pci_dev_put(pdev);
				goto out;
			}

			/* Set basic info */
			gpu->pdev = pdev;
			gpu->pci_vendor_id = vendor_id;
			gpu->pci_device_id = device_id;
			snprintf(gpu->name, sizeof(gpu->name), "%04x:%04x", vendor_id, device_id);

			/* TODO: Query actual capabilities from vendor driver */
			gpu->vram_size = 8 * 1024 * 1024 * 1024; /* Default 8GB */
			gpu->vram_bandwidth = 500 * 1024; /* Default 500 GB/s */
			gpu->compute_units = 64; /* Default 64 CUs */
			gpu->api_flags = MVGAL_API_VULKAN | MVGAL_API_OPENCL;
			gpu->pcie_gen = 4; /* Default PCIe 4.0 */
			gpu->pcie_lanes = 16; /* Default x16 */
			gpu->numa_node = dev_to_node(&pdev->dev);

			/* Add to device */
			ret = mvgal_gpu_add(dev, gpu);
			if (ret < 0) {
				pci_dev_put(pdev);
				goto out;
			}
			
			/* Don't put pdev - we keep the reference */
			continue;
		}

		pci_dev_put(pdev);
	}

out:
	pci_dev_put(pdev);
	return ret;
}

/**
 * mvgal_compute_capability_profile - Compute unified capability profile
 */
void mvgal_compute_capability_profile(struct mvgal_device *dev)
{
	struct mvgal_gpu_device *gpu;
	uint64_t total_vram = 0;
	uint32_t total_cus = 0;
	uint32_t total_bandwidth = 0;
	uint32_t common_api_flags = ~0U;
	uint32_t union_api_flags = 0;
	bool all_same_api = true;
	bool has_graphics = false;
	bool has_compute_only = false;

	mutex_lock(&dev->gpu_lock);

	list_for_each_entry(gpu, &dev->gpu_list, node) {
		total_vram += gpu->vram_size;
		total_cus += gpu->compute_units;
		total_bandwidth += gpu->vram_bandwidth;
		common_api_flags &= gpu->api_flags;
		union_api_flags |= gpu->api_flags;

		if (gpu->api_flags & MVGAL_API_VULKAN) {
			has_graphics = true;
		}
	}

	/* Determine tier */
	dev->caps.tier = TIER_FULL;
	if (common_api_flags != union_api_flags) {
		if (!has_graphics) {
			dev->caps.tier = TIER_COMPUTE_ONLY;
		} else {
			dev->caps.tier = TIER_MIXED;
		}
	}

	dev->caps.total_vram = total_vram;
	dev->caps.max_compute_units = total_cus;
	dev->caps.max_memory_bandwidth = total_bandwidth;
	dev->caps.supported_api_flags = union_api_flags;
	dev->caps.min_vulkan_version = 0x10300; /* Vulkan 1.3 default */
	dev->caps.gpu_count = dev->gpu_count;
	dev->caps.p2p_supported = true; /* TODO: Detect actual P2P support */
	dev->caps.numa_aware = true; /* TODO: Detect actual NUMA support */

	mutex_unlock(&dev->gpu_lock);

	pr_info("MVGAL: Capability profile: tier=%d, vram=%llu GB, cus=%d, bandwidth=%d GB/s\n",
		dev->caps.tier,
		(unsigned long long)(dev->caps.total_vram / (1024ULL * 1024ULL * 1024ULL)),
		dev->caps.max_compute_units,
		(int)(dev->caps.max_memory_bandwidth / 1024U));
}

/*
 * P2P Detection and Enablement (Section 4.2.2)
 */

/**
 * mvgal_detect_p2p_support - Detect P2P capability between two GPUs
 * 
 * Checks if two GPUs can perform peer-to-peer DMA transfers.
 * Returns one of:
 *   MVGAL_P2P_SUPPORTED - Same PCI root complex, P2P possible
 *   MVGAL_P2P_NVLINK - NVIDIA GPUs with NVLink connection
 *   MVGAL_P2P_XGMI - AMD GPUs with xGMI/Infinity Fabric
 *   MVGAL_P2P_UNSUPPORTED - P2P not possible
 */
int mvgal_detect_p2p_support(struct mvgal_gpu_device *gpu1,
			    struct mvgal_gpu_device *gpu2)
{
	if (!gpu1 || !gpu2) {
		return MVGAL_P2P_UNSUPPORTED;
	}

	/* Same GPU - not P2P */
	if (gpu1 == gpu2) {
		return MVGAL_P2P_UNSUPPORTED;
	}

	/* Check if both GPUs are on the same PCI root complex */
	/* This uses the kernel's pci_p2pdma_distance() when available */
	if (gpu1->numa_node == gpu2->numa_node && gpu1->numa_node >= 0) {
		/* Same NUMA node - likely same root complex */
		pr_debug("MVGAL: GPUs %s and %s on same NUMA node (%d) - P2P supported\n",
			gpu1->name, gpu2->name, gpu1->numa_node);
		return MVGAL_P2P_SUPPORTED;
	}

	/* Check vendor-specific P2P support */
	if (gpu1->vendor == MVGAL_VENDOR_NVIDIA &&
	    gpu2->vendor == MVGAL_VENDOR_NVIDIA) {
		/* Check NVLink topology */
		pr_debug("MVGAL: NVIDIA GPUs detected, checking NVLink...\n");
		/* TODO: Call mvgal_nvidia_check_nvlink() when vendor ops available */
		return MVGAL_P2P_NVLINK;
	}

	if (gpu1->vendor == MVGAL_VENDOR_AMD &&
	    gpu2->vendor == MVGAL_VENDOR_AMD) {
		/* Check xGMI/Infinity Fabric */
		pr_debug("MVGAL: AMD GPUs detected, checking xGMI...\n");
		/* TODO: Call mvgal_amd_check_xgmi() when vendor ops available */
		return MVGAL_P2P_XGMI;
	}

	/* Different vendors or different root complexes */
	pr_debug("MVGAL: P2P not supported between %s and %s\n",
		gpu1->name, gpu2->name);
	return MVGAL_P2P_UNSUPPORTED;
}

/**
 * mvgal_detect_all_p2p - Detect P2P support for all GPU pairs
 * 
 * Populates the P2P support matrix in the device capability profile.
 */
void mvgal_detect_all_p2p(struct mvgal_device *dev)
{
	struct mvgal_gpu_device *gpu1, *gpu2;
	int i = 0, j;

	if (!dev) {
		return;
	}

	mutex_lock(&dev->gpu_lock);

	/* Reset P2P support flag */
	dev->caps.p2p_supported = false;

	list_for_each_entry(gpu1, &dev->gpu_list, node) {
		j = 0;
		list_for_each_entry(gpu2, &dev->gpu_list, node) {
			if (gpu1 == gpu2) {
				j++;
				continue;
			}

			int p2p = mvgal_detect_p2p_support(gpu1, gpu2);
			pr_debug("MVGAL: P2P[%d][%d] = %d\n", i, j, p2p);
			if (p2p != MVGAL_P2P_UNSUPPORTED) {
				dev->caps.p2p_supported = true;
			}
			j++;
		}
		i++;
	}

	mutex_unlock(&dev->gpu_lock);

	pr_info("MVGAL: P2P support detected: %s\n",
		dev->caps.p2p_supported ? "enabled" : "disabled");
}

/**
 * mvgal_pci_probe - PCI probe function for GPU detection
 */
int mvgal_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mvgal_gpu_device *gpu;
	enum mvgal_vendor_id vendor = MVGAL_VENDOR_UNKNOWN;
	int ret;

	pr_info("MVGAL: Probing PCI device %04x:%04x\n", pdev->vendor, pdev->device);

	/* Determine vendor */
	if (pdev->vendor == PCI_VENDOR_ID_ATI) {
		vendor = MVGAL_VENDOR_AMD;
	} else if (pdev->vendor == PCI_VENDOR_ID_NVIDIA) {
		vendor = MVGAL_VENDOR_NVIDIA;
	} else if (pdev->vendor == PCI_VENDOR_ID_INTEL) {
		if ((pdev->class >> 8) == PCI_BASE_CLASS_DISPLAY) {
			vendor = MVGAL_VENDOR_INTEL;
		}
	} else if (pdev->vendor == 0x1ED5) {
		vendor = MVGAL_VENDOR_MTT;
	}

	if (vendor == MVGAL_VENDOR_UNKNOWN) {
		return -ENODEV;
	}

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		return ret;
	}

	pci_set_master(pdev);

	gpu = mvgal_gpu_alloc(vendor);
	if (!gpu) {
		return -ENOMEM;
	}

	gpu->pdev = pdev;
	gpu->pci_vendor_id = pdev->vendor;
	gpu->pci_device_id = pdev->device;
	snprintf(gpu->name, sizeof(gpu->name), "%04x:%04x", pdev->vendor, pdev->device);

	/* Initialize hardware-specific capabilities via vendor-specific init */
	gpu->pcie_gen = pcie_get_speed_cap(pdev);
	gpu->pcie_lanes = pcie_get_width_cap(pdev);
	gpu->numa_node = dev_to_node(&pdev->dev);

	ret = mvgal_gpu_add(mvgal_logical_device, gpu);
	if (ret < 0) {
		pci_disable_device(pdev);
		mvgal_gpu_free(gpu);
		return ret;
	}

	pci_set_drvdata(pdev, gpu);
	return 0;
}

/**
 * mvgal_pci_remove - PCI remove function
 */
void mvgal_pci_remove(struct pci_dev *pdev)
{
	struct mvgal_gpu_device *gpu = pci_get_drvdata(pdev);

	if (gpu) {
		mvgal_gpu_remove(mvgal_logical_device, gpu);
		pci_disable_device(pdev);
	}
}
