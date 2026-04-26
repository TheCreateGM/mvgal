/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Device Management - Logical device creation and GPU enumeration
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/drm.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/dma-buf.h>

#include "mvgal_core.h"
#include "mvgal_device.h"

/* Previously included mvgal_sync.h for fence definitions */
#include "mvgal_sync.h"

/**
 * struct mvgal_gpu_device - Represents a physical GPU in the MVGAL pool
 */
struct mvgal_gpu_device {
	struct list_head node;            /* Node in the GPU list */
	struct pci_dev *pdev;             /* PCI device */
	struct drm_device *drm;           /* DRM device (if vendor driver exposes one) */
	
	/* Identity */
	enum mvgal_vendor_id vendor;      /* Vendor ID */
	uint16_t pci_vendor_id;           /* PCI vendor ID */
	uint16_t pci_device_id;           /* PCI device ID */
	char name[64];                    /* Human-readable name */
	
	/* Capabilities */
	uint64_t vram_size;               /* Total VRAM in bytes */
	uint32_t vram_bandwidth;          /* Memory bandwidth in MB/s */
	uint32_t compute_units;           /* Number of compute units */
	uint32_t api_flags;              /* Bitmask of supported APIs */
	uint32_t pcie_gen;               /* PCIe generation (1-5) */
	uint32_t pcie_lanes;             /* PCIe lane count */
	uint32_t numa_node;               /* NUMA node (-1 if unknown) */
	
	/* State */
	enum mvgal_power_state power_state;
	bool available;                   /* GPU is available for workloads */
	bool enabled;                    /* GPU is enabled in MVGAL pool */
	
	/* Statistics */
	uint32_t utilization;             /* Current utilization percentage */
	uint64_t memory_used;             /* Used memory in bytes */
	int32_t temperature;              /* Temperature in Celsius */
	
	/* Vendor-specific operations */
	const struct mvgal_vendor_ops *ops;
	void *vendor_priv;                /* Vendor-specific private data */
	
	/* Synchronization */
	struct mutex lock;                /* Protects GPU state */
};

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

	/* Initialize DRM device */
	ret = drm_dev_init(&dev->drm, &mvgal_drm_driver, NULL);
	if (ret < 0) {
		pr_err("MVGAL: Failed to initialize DRM device\n");
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
	drm_dev_put(&dev->drm);

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
	if (!dev || !gpu) {
		return -EINVAL;
	}

	mutex_lock(&dev->gpu_lock);
	
	if (dev->gpu_count >= MVGAL_MAX_GPUS) {
		mutex_unlock(&dev->gpu_lock);
		return -ENOSPC;
	}

	/* Initialize GPU if ops are available */
	if (gpu->ops && gpu->ops->init) {
		int ret = gpu->ops->init(gpu);
		if (ret < 0) {
			mutex_unlock(&dev->gpu_lock);
			return ret;
		}
	}

	/* Add to GPU list */
	list_add_tail(&gpu->node, &dev->gpu_list);
	dev->gpu_count++;

	pr_info("MVGAL: Added GPU '%s' (vendor=%d, vram=%llu MB)\n",
		gpu->name, gpu->vendor,
		(unsigned long long)(gpu->vram_size / (1024ULL * 1024ULL)));

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
		} else if (vendor_id == 0x1A82) {
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
			gpu->numa_node = pdev->numa_node;

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
