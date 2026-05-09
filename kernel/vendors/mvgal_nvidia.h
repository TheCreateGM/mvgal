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

/*
 * Per-GPU NVIDIA private data
 */
struct mvgal_nvidia_priv {
	struct pci_dev *pdev;
	char device_path[128];           /* Path to /dev/nvidia* device */
	int device_fd;                   /* File descriptor for device (user-space only) */
	bool nv_drm_available;          /* NVIDIA DRM driver is loaded */
	
	/* GSP (GPU System Processor) Firmware */
	bool gsp_firmware_loaded;       /* GSP firmware is active */
	uint32_t gsp_version;           /* GSP firmware version */
	
	/* Capabilities queried from NVIDIA */
	uint64_t vram_total;
	uint64_t vram_free;
	uint32_t gpu_utilization;
	int32_t gpu_temperature;
	
	/* DMA-BUF support */
	struct {
		bool can_export;
		bool can_import;
		bool nv_dmabuf_available;     /* NVIDIA DMA-BUF driver available */
	} dma_buf;
	
	/* NVLink support */
	bool nvlink_capable;            /* GPU has NVLink */
	int nvlink_link_count;          /* Number of NVLink links */
};

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

/* Device detection helpers */
u64 mvgal_nvidia_estimate_vram(u16 device_id);
u32 mvgal_nvidia_estimate_bandwidth(u16 device_id);
float mvgal_nvidia_get_compute_capability(u16 device_id);
u32 mvgal_nvidia_get_sm_count(u16 device_id);
bool mvgal_nvidia_is_rtx_series(u16 device_id);
bool mvgal_nvidia_has_nvlink(struct pci_dev *pdev);

#endif /* _MVGAL_NVIDIA_H_ */
