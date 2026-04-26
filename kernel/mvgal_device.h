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

/* PCI probe/remove */
int mvgal_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void mvgal_pci_remove(struct pci_dev *pdev);

/* Kernel module init/cleanup */
static int __init mvgal_init(void);
static void __exit mvgal_exit(void);

#endif /* _MVGAL_DEVICE_H_ */
