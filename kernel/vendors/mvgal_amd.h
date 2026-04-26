/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * AMD GPU Driver Integration Header
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_AMD_H_
#define _MVGAL_AMD_H_

#include "../mvgal_core.h"

/* AMD PCI vendor ID */
#define PCI_VENDOR_ID_ATI 0x1002

/*
 * AMD-specific flags
 */
#define MVGAL_AMD_FLAG_VRAM_VISIBLE (1 << 0) /* Allocation is CPU-visible */

/*
 * Function declarations
 */

/* Initialization */
extern const struct mvgal_vendor_ops mvgal_amd_ops;

int mvgal_amd_init(struct mvgal_gpu_device *gpu);
void mvgal_amd_fini(struct mvgal_gpu_device *gpu);

/* Command submission */
int mvgal_amd_submit_cs(struct mvgal_gpu_device *gpu, struct mvgal_workload *workload);

/* Memory management */
int mvgal_amd_alloc_vram(struct mvgal_gpu_device *gpu, size_t size, uint64_t *gpu_addr);
void mvgal_amd_free_vram(struct mvgal_gpu_device *gpu, uint64_t gpu_addr);

/* Power management */
int mvgal_amd_wait_idle(struct mvgal_gpu_device *gpu, uint32_t timeout_ms);
int mvgal_amd_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state);

/* DMA-BUF */
struct dma_buf *mvgal_amd_export_dmabuf(struct mvgal_gpu_device *gpu, uint64_t gpu_addr, size_t size);
int mvgal_amd_import_dmabuf(struct mvgal_gpu_device *gpu, struct dma_buf *buf, uint64_t *gpu_addr);

/* Utilization */
int mvgal_amd_query_utilization(struct mvgal_gpu_device *gpu, uint32_t *util_percent);

#endif /* _MVGAL_AMD_H_ */
