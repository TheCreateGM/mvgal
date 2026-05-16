/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Device Management Header - GPU enumeration and capability detection
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_DEVICE_H_
#define _MVGAL_DEVICE_H_

#include "mvgal_core.h"
#include "mvgal_power.h"

/*
 * struct mvgal_gpu_device - Represents a physical GPU in the MVGAL pool
 */
struct mvgal_gpu_device {
	struct list_head node;            /* Node in the GPU list */
	struct pci_dev *pdev;             /* PCI device */
	struct drm_device *drm;           /* DRM device (if vendor driver exposes one) */
	struct device dev;                /* Linux device for DMA-BUF attachment */

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

	/* NVIDIA-specific capabilities */
	uint32_t compute_capability;      /* NVIDIA compute capability (e.g. 8.6) */
	bool gsp_supported;               /* GSP firmware supported */
	uint64_t features;                /* Feature flags (tensor cores, RT cores, etc.) */
	bool nvlink_capable;             /* NVLink supported */

	/* State */
	enum mvgal_power_state power_state;
	bool available;                   /* GPU is available for workloads */
	bool enabled;                    /* GPU is enabled in MVGAL pool */

	/* Statistics */
	uint32_t utilization;             /* Current utilization percentage */
	uint64_t memory_used;              /* Used memory in bytes */
	int32_t temperature;              /* Temperature in Celsius */

	/* Power management (Section 5) */
	uint32_t gpu_index;               /* GPU index in MVGAL pool */
	uint32_t power_draw;              /* Current power draw in watts */
	struct mvgal_gpu_dvfs *dvfs;      /* DVFS state */
	struct mvgal_thermal *thermal;   /* Thermal state */

	/* Vendor-specific operations */
	const struct mvgal_vendor_ops *ops;
	void *vendor_priv;                /* Vendor-specific private data */

	/* Synchronization */
	struct mutex lock;                /* Protects GPU state */
};

/*
 * Function declarations
 */

/* Device initialization and cleanup */
int mvgal_device_init(struct mvgal_device **dev_out);
void mvgal_device_fini(struct mvgal_device *dev);

/* GPU management */
struct mvgal_gpu_device *mvgal_gpu_alloc(enum mvgal_vendor_id vendor);
void mvgal_gpu_free(struct mvgal_gpu_device *gpu);
int mvgal_gpu_add(struct mvgal_device *dev, struct mvgal_gpu_device *gpu);
void mvgal_gpu_remove(struct mvgal_device *dev, struct mvgal_gpu_device *gpu);
void mvgal_gpu_cleanup_all(struct mvgal_device *dev);
struct mvgal_gpu_device *mvgal_gpu_find_by_vendor(struct mvgal_device *dev, enum mvgal_vendor_id vendor);

/* GPU enumeration */
int mvgal_enumerate_gpus(struct mvgal_device *dev);

/* Capability profile */
void mvgal_compute_capability_profile(struct mvgal_device *dev);

/* P2P detection (Section 4.2.2) */
int mvgal_detect_p2p_support(struct mvgal_gpu_device *gpu1, struct mvgal_gpu_device *gpu2);
void mvgal_detect_all_p2p(struct mvgal_device *dev);

/* PCI probe/remove */
int mvgal_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void mvgal_pci_remove(struct pci_dev *pdev);

/* Kernel module init/cleanup */
static int __init mvgal_init(void);
static void __exit mvgal_exit(void);

#endif /* _MVGAL_DEVICE_H_ */
