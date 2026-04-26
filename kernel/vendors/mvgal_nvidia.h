/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * NVIDIA GPU Driver Integration Header
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_NVIDIA_H_
#define _MVGAL_NVIDIA_H_

#include "../mvgal_core.h"

/*
 * NVIDIA PCI vendor and device IDs
 */
#define PCI_VENDOR_ID_NVIDIA 0x10DE

/*
 * NVIDIA-specific error codes
 */
#define MVGAL_NVIDIA_ERR_NO_DRIVER  1 /* NVIDIA driver not loaded */
#define MVGAL_NVIDIA_ERR_NO_DMABUF  2 /* DMA-BUF not supported */

/*
 * Function declarations
 */

/* Initialization */
extern const struct mvgal_vendor_ops mvgal_nvidia_ops;

int mvgal_nvidia_init(struct mvgal_gpu_device *gpu);
void mvgal_nvidia_fini(struct mvgal_gpu_device *gpu);

/* Command submission */
int mvgal_nvidia_submit_cs(struct mvgal_gpu_device *gpu, struct mvgal_workload *workload);

/* Memory management */
int mvgal_nvidia_alloc_vram(struct mvgal_gpu_device *gpu, size_t size, uint64_t *gpu_addr);
void mvgal_nvidia_free_vram(struct mvgal_gpu_device *gpu, uint64_t gpu_addr);

/* Power management */
int mvgal_nvidia_wait_idle(struct mvgal_gpu_device *gpu, uint32_t timeout_ms);
int mvgal_nvidia_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state);

/* DMA-BUF */
struct dma_buf *mvgal_nvidia_export_dmabuf(struct mvgal_gpu_device *gpu, uint64_t gpu_addr, size_t size);
int mvgal_nvidia_import_dmabuf(struct mvgal_gpu_device *gpu, struct dma_buf *buf, uint64_t *gpu_addr);

/* Utilization */
int mvgal_nvidia_query_utilization(struct mvgal_gpu_device *gpu, uint32_t *util_percent);

#endif /* _MVGAL_NVIDIA_H_ */
