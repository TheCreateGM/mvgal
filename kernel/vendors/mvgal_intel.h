/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Intel GPU Driver Integration Header
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_INTEL_H_
#define _MVGAL_INTEL_H_

#include "../mvgal_core.h"

/* Intel PCI vendor ID */
#define PCI_VENDOR_ID_INTEL 0x8086

/* Intel device IDs for discrete GPUs (DG2) */
#define INTEL_DG2_DEVICE_ID_BASE 0x4680
#define INTEL_DG2_DEVICE_ID_END  0x469F

/*
 * Function declarations
 */

/* Initialization */
extern const struct mvgal_vendor_ops mvgal_intel_ops;

int mvgal_intel_init(struct mvgal_gpu_device *gpu);
void mvgal_intel_fini(struct mvgal_gpu_device *gpu);

/* Command submission */
int mvgal_intel_submit_cs(struct mvgal_gpu_device *gpu, struct mvgal_workload *workload);

/* Memory management */
int mvgal_intel_alloc_vram(struct mvgal_gpu_device *gpu, size_t size, uint64_t *gpu_addr);
void mvgal_intel_free_vram(struct mvgal_gpu_device *gpu, uint64_t gpu_addr);

/* Power management */
int mvgal_intel_wait_idle(struct mvgal_gpu_device *gpu, uint32_t timeout_ms);
int mvgal_intel_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state);

/* DMA-BUF */
struct dma_buf *mvgal_intel_export_dmabuf(struct mvgal_gpu_device *gpu, uint64_t gpu_addr, size_t size);
int mvgal_intel_import_dmabuf(struct mvgal_gpu_device *gpu, struct dma_buf *buf, uint64_t *gpu_addr);

/* Utilization */
int mvgal_intel_query_utilization(struct mvgal_gpu_device *gpu, uint32_t *util_percent);

#endif /* _MVGAL_INTEL_H_ */
