/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Kernel-side Memory Management Header
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef _MVGAL_MEMORY_H_
#define _MVGAL_MEMORY_H_

#include <linux/types.h>
#include <linux/dma-buf.h>
#include <linux/rbtree.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/list.h>

/* Forward declaration */
struct mvgal_gpu_device;

/* Memory allocation flags */
#define MVGAL_MEMORY_READ     (1 << 0) /* Memory will be read */
#define MVGAL_MEMORY_WRITE    (1 << 1) /* Memory will be written */
#define MVGAL_MEMORY_EXECUTE  (1 << 2) /* Memory will be executed */
#define MVGAL_MEMORY_COHERENT (1 << 3) /* Memory is coherent */
#define MVGAL_MEMORY_NONCACHED (1 << 4) /* Non-cached memory */

/* Memory types */
#define MVGAL_MEMORY_NORMAL    0 /* Normal GPU memory */
#define MVGAL_MEMORY_HOST      1 /* Host (system) memory */
#define MVGAL_MEMORY_VISIBLE   2 /* CPU-visible GPU memory */
#define MVGAL_MEMORY_COHERENT  3 /* CPU-coherent GPU memory */

/*
 * UVA Space Management (Section 4.2.1)
 * 128TB unified virtual address space similar to NVIDIA UVM
 */
#define MVGAL_UVA_SPACE_SIZE   (128ULL * 1024 * 1024 * 1024 * 1024) /* 128TB */
#define MVGAL_UVA_SPACE_BASE   0x100000000000ULL  /* 1TB base address */
#define MVGAL_UVA_ALIGNMENT    PAGE_SIZE

/* UVA allocation entry for red-black tree */
struct mvgal_uva_entry {
	struct rb_node node;
	uint64_t addr;           /* Start address */
	uint64_t size;           /* Size in bytes */
	uint64_t memory_id;      /* Associated memory ID */
};

/* Global UVA space structure */
struct mvgal_uva_space {
	struct rb_root addr_tree;        /* Red-black tree of allocations */
	uint64_t base_addr;              /* Base of virtual address space */
	uint64_t size;                  /* Total size */
	atomic_t next_addr;             /* Next available address */
	struct mutex lock;              /* Protects UVA space */
};

/*
 * P2P Support Detection (Section 4.2.2)
 */
enum mvgal_p2p_support {
	MVGAL_P2P_UNSUPPORTED = 0,       /* P2P not possible */
	MVGAL_P2P_SUPPORTED = 1,        /* P2P via same PCI root complex */
	MVGAL_P2P_NVLINK = 2,           /* P2P via NVIDIA NVLink */
	MVGAL_P2P_XGMI = 3,             /* P2P via AMD xGMI/Infinity Fabric */
};

/*
 * Residency Tracking (Section 4.2.3)
 */
enum mvgal_residency_policy {
	MVGAL_RESIDENCY_EAGER = 0,       /* Replicate to all GPUs immediately */
	MVGAL_RESIDENCY_LAZY = 1,        /* Migrate on first access */
	MVGAL_RESIDENCY_ON_DEMAND = 2,   /* Keep on owning GPU, migrate on demand */
};

/* Memory residency tracking per GPU */
struct mvgal_memory_residency {
	uint32_t gpu_index;              /* Which GPU currently owns the pages */
	uint64_t page_count;             /* Number of resident pages */
	struct page **pages;             /* Array of page pointers */
	unsigned long last_access;       /* Timestamp of last access */
	enum mvgal_residency_policy policy;
};

/* Migration path selection */
enum mvgal_migration_path {
	MVGAL_MIGRATION_DMA_BUF_ZERO_COPY = 0,  /* Direct DMA-BUF sharing */
	MVGAL_MIGRATION_PCIE_P2P = 1,           /* Peer-to-peer DMA */
	MVGAL_MIGRATION_HOST_STAGING = 2,        /* CPU-mediated copy */
};

/*
 * IOCTL structures
 */

/* Allocate memory arguments */
struct mvgal_alloc_memory_args {
	uint64_t memory_id;    /* Output: assigned memory ID */
	uint64_t uva;          /* Output: unified virtual address */
	size_t size;           /* Input: size in bytes */
	uint32_t flags;        /* Input: allocation flags */
	uint32_t gpu_mask;     /* Input: bitmask of GPUs to allocate on (0 = all) */
	uint32_t memory_type; /* Input: memory type */
};

/* Free memory arguments */
struct mvgal_free_memory_args {
	uint64_t memory_id;    /* Input: memory ID to free */
};

/* Export DMA-BUF arguments */
struct mvgal_export_dmabuf_args {
	uint64_t memory_id;    /* Input: memory ID to export */
	int fd;                /* Output: DMA-BUF file descriptor */
	uint32_t flags;       /* Input: export flags */
};

/* Import DMA-BUF arguments */
struct mvgal_import_dmabuf_args {
	int fd;                /* Input: DMA-BUF file descriptor */
	uint64_t memory_id;    /* Output: assigned memory ID */
	uint32_t gpu_mask;     /* Input: bitmask of GPUs to import into */
};

/*
 * Function declarations
 */

/* Memory manager initialization */
int mvgal_memory_init(void);
void mvgal_memory_fini(void);

/* Memory allocation and free */
struct mvgal_memory;
int mvgal_ioctl_alloc_memory(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_free_memory(struct drm_device *drm, void *data, struct drm_file *file);

/* DMA-BUF operations */
int mvgal_ioctl_export_dmabuf(struct drm_device *drm, void *data, struct drm_file *file);
int mvgal_ioctl_import_dmabuf(struct drm_device *drm, void *data, struct drm_file *file);

/* Reference counting */
void mvgal_memory_get(struct mvgal_memory *mem);
void mvgal_memory_put(struct mvgal_memory *mem);

/* DMA-BUF helper functions */
struct dma_buf *mvgal_create_bounce_dmabuf(struct mvgal_memory *mem);
int mvgal_export_dmabuf_attach(struct mvgal_memory *mem, struct dma_buf *dmabuf, int gpu_index);

#endif /* _MVGAL_MEMORY_H_ */
