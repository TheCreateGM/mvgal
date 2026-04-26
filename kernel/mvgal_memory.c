/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Kernel-side Memory Management - DMA-BUF integration and unified memory
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/dma-buf.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "mvgal_core.h"
#include "mvgal_memory.h"

/*
 * Memory allocation structure
 */
struct mvgal_memory {
	struct list_head node;            /* Node in memory list */
	uint64_t id;                     /* Unique memory ID */
	size_t size;                     /* Size in bytes */
	uint32_t flags;                  /* Allocation flags */
	
	/* Unified virtual address */
	uint64_t uva;                    /* Unified virtual address */
	
	/* Physical GPU mappings */
	uint64_t gpu_addrs[MVGAL_MAX_GPUS];
	struct dma_buf *dmabufs[MVGAL_MAX_GPUS];
	bool populated[MVGAL_MAX_GPUS];   /* True if populated on this GPU */
	
	/* Reference counting */
	struct kref refcount;
	
	/* Use in workloads */
	struct list_head workloads;      /* Workloads using this memory */
};

/* Memory allocator */
static struct {
	struct list_head memory_list;
	struct mutex lock;
	atomic_t next_memory_id;
	struct kmem_cache *cache;
} memory_manager;

/*
 * mvgal_memory_init - Initialize memory manager
 */
int mvgal_memory_init(void)
{
	INIT_LIST_HEAD(&memory_manager.memory_list);
	mutex_init(&memory_manager.lock);
	atomic_set(&memory_manager.next_memory_id, 1);
	
	/* Create slab cache for memory allocations */
	memory_manager.cache = kmem_cache_create("mvgal_memory",
			sizeof(struct mvgal_memory), 0,
			SLAB_HWCACHE_ALIGN, NULL);
	if (!memory_manager.cache) {
		return -ENOMEM;
	}

	return 0;
}

/*
 * mvgal_memory_fini - Cleanup memory manager
 */
void mvgal_memory_fini(void)
{
	struct mvgal_memory *mem, *tmp;

	mutex_lock(&memory_manager.lock);
	
	list_for_each_entry_safe(mem, tmp, &memory_manager.memory_list, node) {
		list_del(&mem->node);
		mutex_unlock(&memory_manager.lock);
		
		mvgal_memory_free(mem);
		
		mutex_lock(&memory_manager.lock);
	}
	
	mutex_unlock(&memory_manager.lock);
	mutex_destroy(&memory_manager.lock);
	
	if (memory_manager.cache) {
		kmem_cache_destroy(memory_manager.cache);
	}
}

/*
 * mvgal_memory_alloc - Allocate memory for a workload
 */
int mvgal_ioctl_alloc_memory(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_alloc_memory_args *args = data;
	struct mvgal_memory *mem;
	struct mvgal_gpu_device *gpu = NULL;
	int ret = 0;
	int i;

	/* Create memory object */
	mem = kmem_cache_alloc(memory_manager.cache, GFP_KERNEL);
	if (!mem) {
		return -ENOMEM;
	}

	mem->id = atomic_fetch_inc(&memory_manager.next_memory_id);
	mem->size = args->size;
	mem->flags = args->flags;
	kref_init(&mem->refcount);
	INIT_LIST_HEAD(&mem->workloads);

	/* Allocate on each requested GPU or default to all */
	if (args->gpu_mask == 0) {
		/* Allocate on all GPUs */
		args->gpu_mask = ~0U;
	}

	mutex_lock(&mvgal_logical_device->gpu_lock);
	
	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		if (args->gpu_mask & (1 << i)) {
			gpu = list_first_entry_or_null(&mvgal_logical_device->gpu_list,
				struct mvgal_gpu_device, node);
			if (!gpu) {
				continue;
			}

			/* Allocate on this GPU if ops are available */
			if (gpu->ops && gpu->ops->alloc_vram) {
				ret = gpu->ops->alloc_vram(gpu, args->size, &mem->gpu_addrs[i]);
				if (ret == 0) {
					mem->populated[i] = true;
				} else {
					mem->gpu_addrs[i] = 0;
					mem->populated[i] = false;
				}
			} else {
				/* Fallback: Use DMA-BUF export from vendor driver */
				mem->gpu_addrs[i] = 0;
				mem->populated[i] = false;
			}
		}
	}
	
	mutex_unlock(&mvgal_logical_device->gpu_lock);

	if (ret < 0) {
		/* Free any allocations we made */
		for (i = 0; i < MVGAL_MAX_GPUS; i++) {
			if (mem->populated[i] && gpu && gpu->ops && gpu->ops->free_vram) {
				gpu->ops->free_vram(gpu, mem->gpu_addrs[i]);
			}
		}
		kmem_cache_free(memory_manager.cache, mem);
		return ret;
	}

	/* Add to memory list */
	mutex_lock(&memory_manager.lock);
	list_add_tail(&mem->node, &memory_manager.memory_list);
	mutex_unlock(&memory_manager.lock);

	args->memory_id = mem->id;
	args->uva = 0; /* TODO: Assign unified virtual address */

	return 0;
}

/*
 * mvgal_memory_free - Free memory allocation
 */
int mvgal_ioctl_free_memory(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_free_memory_args *args = data;
	struct mvgal_memory *mem;
	int i;

	/* Find memory object */
	mutex_lock(&memory_manager.lock);
	
	list_for_each_entry(mem, &memory_manager.memory_list, node) {
		if (mem->id == args->memory_id) {
			list_del(&mem->node);
			break;
		}
	}
	
	mutex_unlock(&memory_manager.lock);

	if (&mem->node == &memory_manager.memory_list) {
		return -ENOENT;
	}

	/* Free on all GPUs */
	mutex_lock(&mvgal_logical_device->gpu_lock);
	
	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		if (mem->populated[i] && mem->dmabufs[i]) {
			dma_buf_put(mem->dmabufs[i]);
			mem->dmabufs[i] = NULL;
		}
	}
	
	mutex_unlock(&mvgal_logical_device->gpu_lock);

	/* Free memory */
	kmem_cache_free(memory_manager.cache, mem);

	return 0;
}

/*
 * mvgal_memory_export_dmabuf - Export memory as DMA-BUF
 */
int mvgal_ioctl_export_dmabuf(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_export_dmabuf_args *args = data;
	struct mvgal_memory *mem;
	struct dma_buf *dmabuf;
	int fd;

	/* Find memory object */
	mutex_lock(&memory_manager.lock);
	
	list_for_each_entry(mem, &memory_manager.memory_list, node) {
		if (mem->id == args->memory_id) {
			break;
		}
	}
	
	mutex_unlock(&memory_manager.lock);

	if (&mem->node == &memory_manager.memory_list) {
		return -ENOENT;
	}

	/* Export first GPU's DMA-BUF */
	if (mem->dmabufs[0]) {
		dmabuf = mem->dmabufs[0];
		fd = dma_buf_fd(dmabuf, args->flags);
		if (fd < 0) {
			return fd;
		}
		args->fd = fd;
		return 0;
	}

	return -EINVAL;
}

/*
 * mvgal_memory_import_dmabuf - Import DMA-BUF from another device
 */
int mvgal_ioctl_import_dmabuf(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_import_dmabuf_args *args = data;
	struct dma_buf *dmabuf;
	int ret = 0;

	/* Get DMA-BUF from fd */
	dmabuf = dma_buf_get(args->fd);
	if (IS_ERR(dmabuf)) {
		return PTR_ERR(dmabuf);
	}

	/* TODO: Import into all GPUs in the mask */
	/* For now, just import into the first GPU */

	dma_buf_put(dmabuf);

	args->memory_id = 0; /* TODO: Return memory ID */

	return 0;
}

/*
 * mvgal_memory_release - Release memory reference (kref callback)
 */
static void mvgal_memory_release(struct kref *kref)
{
	struct mvgal_memory *mem = container_of(kref, struct mvgal_memory, refcount);
	
	/* Free on all GPUs */
	kmem_cache_free(memory_manager.cache, mem);
}

/*
 * mvgal_memory_get - Get reference to memory
 */
void mvgal_memory_get(struct mvgal_memory *mem)
{
	kref_get(&mem->refcount);
}

/*
 * mvgal_memory_put - Release reference to memory
 */
void mvgal_memory_put(struct mvgal_memory *mem)
{
	kref_put(&mem->refcount, mvgal_memory_release);
}
