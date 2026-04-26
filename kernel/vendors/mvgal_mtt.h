/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Moore Threads (MTT) GPU Driver Integration Header
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_MTT_H_
#define _MVGAL_MTT_H_

#include "../mvgal_core.h"

/* Moore Threads PCI vendor ID */
#define PCI_VENDOR_ID_MOORE_THREADS 0x1A82

/*
 * Moore Threads device IDs
 */
#define MTT_S2000_DEVICE_ID 0x4000

/*
 * Function declarations
 */

/* Initialization */
extern const struct mvgal_vendor_ops mvgal_mtt_ops;

int mvgal_mtt_init(struct mvgal_gpu_device *gpu);
void mvgal_mtt_fini(struct mvgal_gpu_device *gpu);

/* Command submission */
int mvgal_mtt_submit_cs(struct mvgal_gpu_device *gpu, struct mvgal_workload *workload);

/* Memory management */
int mvgal_mtt_alloc_vram(struct mvgal_gpu_device *gpu, size_t size, uint64_t *gpu_addr);
void mvgal_mtt_free_vram(struct mvgal_gpu_device *gpu, uint64_t gpu_addr);

/* Power management */
int mvgal_mtt_wait_idle(struct mvgal_gpu_device *gpu, uint32_t timeout_ms);
int mvgal_mtt_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state);

/* DMA-BUF */
struct dma_buf *mvgal_mtt_export_dmabuf(struct mvgal_gpu_device *gpu, uint64_t gpu_addr, size_t size);
int mvgal_mtt_import_dmabuf(struct mvgal_gpu_device *gpu, struct dma_buf *buf, uint64_t *gpu_addr);

/* Utilization */
int mvgal_mtt_query_utilization(struct mvgal_gpu_device *gpu, uint32_t *util_percent);

#endif /* _MVGAL_MTT_H_ */
