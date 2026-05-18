/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Kernel Core Module Header - Main data structures and definitions
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */
 
#ifndef _MVGAL_CORE_H_
#define _MVGAL_CORE_H_
 
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/dma-buf.h>
#include <linux/cdev.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm.h>
 
/* Maximum number of MVGAL devices */
#define MVGAL_MAX_DEVICES 16
 
/* Maximum number of physical GPUs per logical device */
#define MVGAL_MAX_GPUS 8
 
/* Maximum IOCTL argument size */
#define MVGAL_MAX_IOCTL_ARGS 4096
 
/*
 * DRM-private ioctl offsets (relative to DRM_COMMAND_BASE + 0x80).
 * DRM_IOCTL_DEF_DRV(MVGAL_*, ...) expects DRM_IOCTL_MVGAL_* names.
 */
/* Private DRM ioctl indices (must stay within the driver ioctl table size) */
#define MVGAL_QUERY_DEVICES		0x00
#define MVGAL_QUERY_CAPABILITIES	0x01
#define MVGAL_SUBMIT_WORKLOAD		0x02
#define MVGAL_ALLOC_MEMORY		0x03
#define MVGAL_FREE_MEMORY		0x04
#define MVGAL_IMPORT_DMABUF		0x05
#define MVGAL_EXPORT_DMABUF		0x06
#define MVGAL_WAIT_FENCE		0x07
#define MVGAL_SIGNAL_FENCE		0x08
#define MVGAL_SET_GPU_AFFINITY		0x09

/* Placeholder UAPI types for DRM ioctl sizing (handlers validate real structs) */
struct drm_mvgal_generic {
	__u64 data;
};

#define DRM_IOCTL_MVGAL_QUERY_DEVICES \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_QUERY_DEVICES, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_QUERY_CAPABILITIES \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_QUERY_CAPABILITIES, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_SUBMIT_WORKLOAD \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_SUBMIT_WORKLOAD, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_ALLOC_MEMORY \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_ALLOC_MEMORY, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_FREE_MEMORY \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_FREE_MEMORY, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_IMPORT_DMABUF \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_IMPORT_DMABUF, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_EXPORT_DMABUF \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_EXPORT_DMABUF, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_WAIT_FENCE \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_WAIT_FENCE, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_SIGNAL_FENCE \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_SIGNAL_FENCE, struct drm_mvgal_generic)
#define DRM_IOCTL_MVGAL_SET_GPU_AFFINITY \
	DRM_IOWR(DRM_COMMAND_BASE + MVGAL_SET_GPU_AFFINITY, struct drm_mvgal_generic)

/* Legacy aliases for in-tree ioctl handlers */
#define MVGAL_IOCTL_QUERY_DEVICES	DRM_IOCTL_MVGAL_QUERY_DEVICES
#define MVGAL_IOCTL_QUERY_CAPABILITIES	DRM_IOCTL_MVGAL_QUERY_CAPABILITIES
#define MVGAL_IOCTL_SUBMIT_WORKLOAD	DRM_IOCTL_MVGAL_SUBMIT_WORKLOAD
#define MVGAL_IOCTL_ALLOC_MEMORY	DRM_IOCTL_MVGAL_ALLOC_MEMORY
#define MVGAL_IOCTL_FREE_MEMORY		DRM_IOCTL_MVGAL_FREE_MEMORY
#define MVGAL_IOCTL_IMPORT_DMABUF	DRM_IOCTL_MVGAL_IMPORT_DMABUF
#define MVGAL_IOCTL_EXPORT_DMABUF	DRM_IOCTL_MVGAL_EXPORT_DMABUF
#define MVGAL_IOCTL_WAIT_FENCE		DRM_IOCTL_MVGAL_WAIT_FENCE
#define MVGAL_IOCTL_SIGNAL_FENCE	DRM_IOCTL_MVGAL_SIGNAL_FENCE
#define MVGAL_IOCTL_SET_GPU_AFFINITY	DRM_IOCTL_MVGAL_SET_GPU_AFFINITY
 
/*
 * Capability tiers
 */
enum mvgal_capability_tier {
	TIER_FULL = 0,        /* All GPUs support same API set */
	TIER_COMPUTE_ONLY = 1, /* Heterogeneous compute but not all support graphics */
	TIER_MIXED = 2,       /* Some GPUs graphics-only, some compute-only */
};
 
/*
 * Power state
 */
enum mvgal_power_state {
	MVGAL_POWER_STATE_ACTIVE = 0,
	MVGAL_POWER_STATE_SUSTAINED = 1,
	MVGAL_POWER_STATE_IDLE = 2,
	MVGAL_POWER_STATE_PARK = 3,
	MVGAL_POWER_STATE_OFF = 4,
};
 
/*
 * Vendor IDs
 */
enum mvgal_vendor_id {
	MVGAL_VENDOR_UNKNOWN = 0,
	MVGAL_VENDOR_AMD = 1,
	MVGAL_VENDOR_NVIDIA = 2,
	MVGAL_VENDOR_INTEL = 3,
	MVGAL_VENDOR_MTT = 4,
};
 
/*
 * API support flags
 */
#define MVGAL_API_VULKAN   (1 << 0)
#define MVGAL_API_OPENGL   (1 << 1)
#define MVGAL_API_OPENCL   (1 << 2)
#define MVGAL_API_CUDA     (1 << 3)
#define MVGAL_API_SYCL     (1 << 4)
 
/*
 * GPU feature flags
 */
#define MVGAL_FEATURE_TENSOR_CORES  (1 << 0)
#define MVGAL_FEATURE_RT_CORES      (1 << 1)
#define MVGAL_FEATURE_PEER_ACCESS   (1 << 2)
 
/*
 * Forward declarations
 */
struct mvgal_gpu_device;
struct mvgal_vendor_ops;
struct mvgal_fence;
struct mvgal_workload;
 
/*
 * Vendor operations structure
 * Each GPU vendor implements these functions
 */
struct mvgal_vendor_ops {
	int (*init)(struct mvgal_gpu_device *dev);
	void (*fini)(struct mvgal_gpu_device *dev);
	int (*submit_cs)(struct mvgal_gpu_device *dev, struct mvgal_workload *workload);
	int (*alloc_vram)(struct mvgal_gpu_device *dev, size_t size, uint64_t *gpu_addr);
	void (*free_vram)(struct mvgal_gpu_device *dev, uint64_t gpu_addr);
	int (*wait_idle)(struct mvgal_gpu_device *dev, uint32_t timeout_ms);
	int (*set_power_state)(struct mvgal_gpu_device *dev, enum mvgal_power_state state);
	struct dma_buf *(*export_dmabuf)(struct mvgal_gpu_device *dev, uint64_t gpu_addr, size_t size);
	int (*import_dmabuf)(struct mvgal_gpu_device *dev, struct dma_buf *buf, uint64_t *gpu_addr);
	int (*query_utilization)(struct mvgal_gpu_device *dev, uint32_t *util_percent);
};
 
/*
 * Unified capability profile for the logical MVGAL device
 */
struct mvgal_capability_profile {
	uint64_t total_vram;           /* Sum of all GPU VRAM */
	uint32_t max_compute_units;   /* Aggregate compute unit count */
	uint32_t max_memory_bandwidth;/* Aggregate memory bandwidth in MB/s */
	uint32_t supported_api_flags;  /* Bitmask of MVGAL_API_* flags */
	uint32_t min_vulkan_version;   /* Minimum Vulkan version across all GPUs */
	enum mvgal_capability_tier tier;
	uint32_t gpu_count;            /* Number of physical GPUs */
	bool p2p_supported;            /* Peer-to-peer DMA supported */
	bool numa_aware;               /* NUMA-aware placement possible */
};
 
/*
 * Per-file private data
 */
struct mvgal_file {
	struct list_head workloads;
	struct mutex lock;
	uint32_t next_workload_id;
};
 
/*
 * Main MVGAL logical device structure
 */
struct mvgal_device {
	struct drm_device *drm;
	struct device *dev;
	struct cdev cdev;
	dev_t devt;
 
	/* GPU list */
	struct list_head gpu_list;
	struct mutex gpu_lock;
	uint32_t gpu_count;
 
	/* Capability profile */
	struct mvgal_capability_profile caps;
 
	/* Workload queue */
	struct list_head workload_queue;
	struct mutex workload_lock;
	wait_queue_head_t workload_waitq;
 
	/* Fence management */
	struct list_head fence_list;
	struct mutex fence_lock;
	atomic_t next_fence_seq;
 
	/* Flags */
	bool initialized;
	bool daemon_connected;
};
 
/*
 * Extern declarations
 */
extern struct mvgal_device *mvgal_logical_device;
 
/*
 * IOCTL handlers (implemented in various files)
 */
int mvgal_ioctl_query_devices(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_query_capabilities(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_submit_workload(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_alloc_memory(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_free_memory(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_import_dmabuf(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_export_dmabuf(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_wait_fence(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_signal_fence(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_set_gpu_affinity(struct drm_device *drm, void *data, struct drm_file *file);
 
/*
 * Character device operations
 */
int mvgal_char_open(struct inode *inode, struct file *filp);
int mvgal_char_release(struct inode *inode, struct file *filp);
long mvgal_char_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int mvgal_char_mmap(struct file *filp, struct vm_area_struct *vma);
 
/* PCI probe/remove */
int mvgal_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id);
void mvgal_pci_remove(struct pci_dev *pdev);
 
/* Device initialization */
int mvgal_device_init(struct mvgal_device **dev_out);
void mvgal_device_fini(struct mvgal_device *dev);
 
/* GPU management */
struct mvgal_gpu_device *mvgal_gpu_alloc(enum mvgal_vendor_id vendor);
void mvgal_gpu_free(struct mvgal_gpu_device *gpu);
int mvgal_gpu_add(struct mvgal_device *dev, struct mvgal_gpu_device *gpu);
void mvgal_gpu_remove(struct mvgal_device *dev, struct mvgal_gpu_device *gpu);

/* Full aggregation stack (linked when MVGAL_BUILD_FULL_STACK=1) */
int mvgal_stack_init(void);
void mvgal_stack_exit(void);

#endif /* _MVGAL_CORE_H_ */
