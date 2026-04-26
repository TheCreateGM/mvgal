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

#endif /* _MVGAL_MEMORY_H_ */
