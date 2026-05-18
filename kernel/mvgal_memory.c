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
#include <linux/rbtree.h>
#include <linux/dma-map-ops.h>

#include "mvgal_core.h"
#include "mvgal_memory.h"
#include "mvgal_device.h"

/* Forward declarations */
enum mvgal_migration_path mvgal_select_migration_path(uint32_t src_gpu, uint32_t dst_gpu);
void mvgal_uva_fini(void);
void mvgal_residency_fini(void);

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

	/* Owning GPU (for export) */
	struct mvgal_gpu_device *owning_gpu;
	uint32_t owning_gpu_index;       /* Index of owning GPU in gpu_addrs */

	/* Physical GPU mappings */
	uint64_t gpu_addrs[MVGAL_MAX_GPUS];
	struct dma_buf *dmabufs[MVGAL_MAX_GPUS];
	struct dma_buf_attachment *attachments[MVGAL_MAX_GPUS];
	struct sg_table *sgt[MVGAL_MAX_GPUS];
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
 * mvgal_memory_free - Free memory allocation
 */
void mvgal_memory_free(struct mvgal_memory *mem)
{
	int i;

	if (!mem)
		return;

	/* Detach and release DMA-BUF attachments */
	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		if (mem->attachments[i]) {
			dma_buf_detach(mem->dmabufs[i], mem->attachments[i]);
			mem->attachments[i] = NULL;
		}
		if (mem->dmabufs[i]) {
			dma_buf_put(mem->dmabufs[i]);
			mem->dmabufs[i] = NULL;
		}
		if (mem->sgt[i]) {
			sg_free_table(mem->sgt[i]);
			mem->sgt[i] = NULL;
		}
	}

	/* Free VRAM on each GPU if populated */
	if (mvgal_logical_device) {
		struct mvgal_gpu_device *gpu;
		int gpu_idx = 0;

		mutex_lock(&mvgal_logical_device->gpu_lock);
		list_for_each_entry(gpu, &mvgal_logical_device->gpu_list, node) {
			if (mem->populated[gpu_idx] && gpu->ops && gpu->ops->free_vram) {
				gpu->ops->free_vram(gpu, mem->gpu_addrs[gpu_idx]);
			}
			gpu_idx++;
		}
		mutex_unlock(&mvgal_logical_device->gpu_lock);
	}

	/* Free the memory object */
	kmem_cache_free(memory_manager.cache, mem);
}
EXPORT_SYMBOL(mvgal_memory_free);

/*
 * UVA Space Management (Section 4.2.1)
 * Global UVA space - 128TB address space similar to NVIDIA UVM
 */
static struct mvgal_uva_space global_uva_space = {
	.addr_tree = RB_ROOT,
	.base_addr = MVGAL_UVA_SPACE_BASE,
	.size = MVGAL_UVA_SPACE_SIZE,
};

/* UVA entry comparison for rb_tree */
static int mvgal_uva_entry_cmp(struct mvgal_uva_entry *a, struct mvgal_uva_entry *b)
{
	if (a->addr < b->addr)
		return -1;
	if (a->addr > b->addr)
		return 1;
	return 0;
}

/* RB-tree comparison wrapper */
static int mvgal_uva_entry_cmp_wrapper(struct rb_node *a, struct rb_node *b)
{
	struct mvgal_uva_entry *entry_a = rb_entry(a, struct mvgal_uva_entry, node);
	struct mvgal_uva_entry *entry_b = rb_entry(b, struct mvgal_uva_entry, node);
	return mvgal_uva_entry_cmp(entry_a, entry_b);
}

/*
 * Residency tracking (Section 4.2.3)
 */
static struct {
	struct mvgal_memory_residency *residencies[MVGAL_MAX_GPUS];
	struct mutex lock;
} residency_tracker;

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

	/* Initialize UVA space */
	mutex_init(&global_uva_space.lock);
	atomic_set(&global_uva_space.next_addr, 0);
	global_uva_space.addr_tree = RB_ROOT;

	/* Initialize residency tracker */
	mutex_init(&residency_tracker.lock);

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

	/* Cleanup UVA space */
	mvgal_uva_fini();

	/* Cleanup residency tracker */
	mvgal_residency_fini();
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
 * DMA-BUF operations for MVGAL exporter
 */
static struct sg_table *mvgal_dmabuf_map(struct dma_buf_attachment *attachment,
					 enum dma_data_direction direction)
{
	struct mvgal_memory *mem = attachment->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	/* For now, we only support mapping from the owning GPU */
	if (mem->owning_gpu_index >= MVGAL_MAX_GPUS || !mem->populated[mem->owning_gpu_index])
		return ERR_PTR(-EINVAL);

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	/* In a real implementation, we would get the pages from the GPU
	 * or use the vendor's DMA-BUF. For this bounce buffer, we'll
	 * allocate system memory pages.
	 */
	ret = sg_alloc_table(sgt, mem->size >> PAGE_SHIFT, GFP_KERNEL);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(ret);
	}

	/* TODO: Fill SGT with actual pages */

	return sgt;
}

static void mvgal_dmabuf_unmap(struct dma_buf_attachment *attachment,
			       struct sg_table *sgt,
			       enum dma_data_direction direction)
{
	sg_free_table(sgt);
	kfree(sgt);
}

static void mvgal_dmabuf_release(struct dma_buf *dmabuf)
{
	/* Reference count is handled by dma_buf_put */
}

static int mvgal_dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	return -EOPNOTSUPP;
}

static const struct dma_buf_ops mvgal_dmabuf_ops = {
	.map_dma_buf = mvgal_dmabuf_map,
	.unmap_dma_buf = mvgal_dmabuf_unmap,
	.release = mvgal_dmabuf_release,
	.mmap = mvgal_dmabuf_mmap,
};

/*
 * mvgal_create_bounce_dmabuf - Create a bounce buffer DMA-BUF for fallback
 */
struct dma_buf *mvgal_create_bounce_dmabuf(struct mvgal_memory *mem)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &mvgal_dmabuf_ops;
	exp_info.size = mem->size;
	exp_info.flags = O_RDWR;
	exp_info.priv = mem;

	return dma_buf_export(&exp_info);
}

/*
 * mvgal_export_dmabuf_attach - Attach DMA-BUF to a specific GPU
 */
int mvgal_export_dmabuf_attach(struct mvgal_memory *mem, struct dma_buf *dmabuf, int gpu_index)
{
	struct mvgal_gpu_device *gpu;
	struct device *dev;
	int ret;

	if (gpu_index >= MVGAL_MAX_GPUS || !mem->populated[gpu_index]) {
		return -EINVAL;
	}

	/* Get GPU device */
	mutex_lock(&mvgal_logical_device->gpu_lock);
	list_for_each_entry(gpu, &mvgal_logical_device->gpu_list, node) {
		if (gpu_index == 0) {
			break;
		}
		 gpu_index--;
	}
	mutex_unlock(&mvgal_logical_device->gpu_lock);

	if (!gpu) {
		return -ENOENT;
	}

	dev = &gpu->dev;

	/* Attach DMA-BUF to GPU */
	mem->attachments[gpu_index] = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(mem->attachments[gpu_index])) {
		ret = PTR_ERR(mem->attachments[gpu_index]);
		mem->attachments[gpu_index] = NULL;
		return ret;
	}

	/* Map the attachment */
	mem->sgt[gpu_index] = dma_buf_map_attachment(mem->attachments[gpu_index], DMA_BIDIRECTIONAL);
	if (IS_ERR(mem->sgt[gpu_index])) {
		ret = PTR_ERR(mem->sgt[gpu_index]);
		mem->sgt[gpu_index] = NULL;
		dma_buf_detach(dmabuf, mem->attachments[gpu_index]);
		mem->attachments[gpu_index] = NULL;
		return ret;
	}

	/* Store the DMA address */
	mem->gpu_addrs[gpu_index] = sg_dma_address(mem->sgt[gpu_index]->sgl);
	mem->populated[gpu_index] = true;

	return 0;
}

/*
 * mvgal_ioctl_export_dmabuf - Export memory as DMA-BUF
 * Implementation from DESIGN.md section 3.2.1
 */
int mvgal_ioctl_export_dmabuf(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_export_dmabuf_args *args = data;
	struct mvgal_memory *mem;
	struct dma_buf *dmabuf;
	int fd;
	int i;

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

	/* Get the underlying DMA-BUF from the owning GPU
	 * Use vendor-specific ops to export
	 */
	if (mem->owning_gpu && mem->owning_gpu->ops &&
	    mem->owning_gpu->ops->export_dmabuf) {
		dmabuf = mem->owning_gpu->ops->export_dmabuf(mem->owning_gpu,
							     mem->gpu_addrs[mem->owning_gpu_index],
							     mem->size);
		if (IS_ERR(dmabuf)) {
			return PTR_ERR(dmabuf);
		}
	} else if (mem->dmabufs[mem->owning_gpu_index]) {
		/* Use existing DMA-BUF if available */
		dmabuf = mem->dmabufs[mem->owning_gpu_index];
		/* Reference already held by mem->dmabufs */
	} else {
		/* Fallback: create a bounce buffer DMA-BUF */
		dmabuf = mvgal_create_bounce_dmabuf(mem);
		if (IS_ERR(dmabuf)) {
			return PTR_ERR(dmabuf);
		}
	}

	fd = dma_buf_fd(dmabuf, args->flags);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		return fd;
	}

	args->fd = fd;
	return 0;
}

/*
 * mvgal_ioctl_import_dmabuf - Import DMA-BUF from fd
 * Implementation from DESIGN.md section 3.2.1:
 * - Import DMA-BUF from fd using dma_buf_get()
 * - Attach to all GPUs in the allocation mask
 * - Use dma_buf_map_attachment() for each attachment
 * - Store the mapped addresses in mem->gpu_addrs[]
 */
int mvgal_ioctl_import_dmabuf(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct mvgal_import_dmabuf_args *args = data;
	struct mvgal_memory *mem;
	struct dma_buf *dmabuf;
	struct mvgal_gpu_device *gpu;
	int ret = 0;
	int i;
	int gpu_idx = 0;

	/* Get DMA-BUF from fd */
	dmabuf = dma_buf_get(args->fd);
	if (IS_ERR(dmabuf)) {
		return PTR_ERR(dmabuf);
	}

	/* Create memory object for imported DMA-BUF */
	mem = kmem_cache_alloc(memory_manager.cache, GFP_KERNEL);
	if (!mem) {
		dma_buf_put(dmabuf);
		return -ENOMEM;
	}

	mem->id = atomic_fetch_inc(&memory_manager.next_memory_id);
	mem->size = 0; /* Will be determined from first attachment */
	mem->flags = MVGAL_MEMORY_READ | MVGAL_MEMORY_WRITE;
	mem->owning_gpu = NULL;
	mem->owning_gpu_index = 0;
	kref_init(&mem->refcount);
	INIT_LIST_HEAD(&mem->workloads);

	/* Initialize GPU arrays */
	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		mem->gpu_addrs[i] = 0;
		mem->dmabufs[i] = NULL;
		mem->attachments[i] = NULL;
		mem->sgt[i] = NULL;
		mem->populated[i] = false;
	}

	/* Default GPU mask to all available GPUs */
	if (args->gpu_mask == 0) {
		args->gpu_mask = ~0U;
	}

	/* Attach to all GPUs in the allocation mask */
	mutex_lock(&mvgal_logical_device->gpu_lock);

	list_for_each_entry(gpu, &mvgal_logical_device->gpu_list, node) {
		struct device *dev;
		struct dma_buf_attachment *attach;
		struct sg_table *sgt;

		/* Check if this GPU is in the mask */
		if (!(args->gpu_mask & (1 << gpu_idx))) {
			gpu_idx++;
			continue;
		}

		dev = &gpu->dev;

		/* Attach DMA-BUF to this GPU */
		attach = dma_buf_attach(dmabuf, dev);
		if (IS_ERR(attach)) {
			ret = PTR_ERR(attach);
			pr_err("MVGAL: Failed to attach DMA-BUF to GPU %d: %d\n", gpu_idx, ret);
			goto err_detach;
		}

		mem->attachments[gpu_idx] = attach;

		/* Map the attachment */
		mem->sgt[gpu_idx] = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR(mem->sgt[gpu_idx])) {
			ret = PTR_ERR(mem->sgt[gpu_idx]);
			mem->sgt[gpu_idx] = NULL;
			pr_err("MVGAL: Failed to map DMA-BUF attachment for GPU %d: %d\n", gpu_idx, ret);
			dma_buf_detach(dmabuf, attach);
			mem->attachments[gpu_idx] = NULL;
			goto err_detach;
		}

		mem->gpu_addrs[gpu_idx] = sg_dma_address(mem->sgt[gpu_idx]->sgl);
		mem->populated[gpu_idx] = true;

		/* If vendor supports import_dmabuf, use it */
		if (gpu->ops && gpu->ops->import_dmabuf) {
			ret = gpu->ops->import_dmabuf(gpu, dmabuf, &mem->gpu_addrs[gpu_idx]);
			if (ret < 0) {
				pr_warn("MVGAL: Vendor import_dmabuf failed for GPU %d, using generic\n", gpu_idx);
			}
		}

		gpu_idx++;
	}

	mutex_unlock(&mvgal_logical_device->gpu_lock);

	/* Store the DMA-BUF reference in first slot */
	mem->dmabufs[0] = dmabuf;

	/* Add to memory list */
	mutex_lock(&memory_manager.lock);
	list_add_tail(&mem->node, &memory_manager.memory_list);
	mutex_unlock(&memory_manager.lock);

	args->memory_id = mem->id;

	return 0;

err_detach:
	/* Detach from already-attached GPUs */
	for (i = 0; i < gpu_idx; i++) {
		if (mem->attachments[i] && mem->sgt[i]) {
			dma_buf_unmap_attachment(mem->attachments[i], mem->sgt[i], DMA_BIDIRECTIONAL);
			dma_buf_detach(dmabuf, mem->attachments[i]);
		}
	}
	mutex_unlock(&mvgal_logical_device->gpu_lock);
	dma_buf_put(dmabuf);
	kmem_cache_free(memory_manager.cache, mem);
	return ret;
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

/*
 * UVA Space Management Implementation (Section 4.2.1)
 */

/*
 * mvgal_uva_init - Initialize UVA space
 */
int mvgal_uva_init(void)
{
	pr_debug("MVGAL: Initializing UVA space (128TB @ 0x%llx)\n",
		(unsigned long long)MVGAL_UVA_SPACE_BASE);
	return 0;
}

/*
 * mvgal_uva_fini - Cleanup UVA space
 */
void mvgal_uva_fini(void)
{
	struct mvgal_uva_entry *entry;
	struct rb_node *node;

	mutex_lock(&global_uva_space.lock);

	/* Free all UVA entries */
	while ((node = rb_first(&global_uva_space.addr_tree)) != NULL) {
		entry = rb_entry(node, struct mvgal_uva_entry, node);
		rb_erase(node, &global_uva_space.addr_tree);
		kfree(entry);
	}

	mutex_unlock(&global_uva_space.lock);
	mutex_destroy(&global_uva_space.lock);

	pr_debug("MVGAL: UVA space cleaned up\n");
}

/*
 * mvgal_uva_allocate - Allocate virtual address from UVA space
 */
uint64_t mvgal_uva_allocate(size_t size, uint64_t memory_id)
{
	struct mvgal_uva_entry *entry;
	uint64_t addr;
	uint64_t aligned_size;

	/* Align size to page boundary */
	aligned_size = PAGE_ALIGN(size);
	if (aligned_size == 0) {
		return 0;
	}

	/* Allocate entry */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		return 0;
	}

	mutex_lock(&global_uva_space.lock);

	/* Check if we have space */
	addr = atomic_fetch_add(aligned_size, &global_uva_space.next_addr);
	if (addr + aligned_size > global_uva_space.base_addr + global_uva_space.size) {
		atomic_sub(aligned_size, &global_uva_space.next_addr);
		mutex_unlock(&global_uva_space.lock);
		kfree(entry);
		pr_warn("MVGAL: UVA space exhausted\n");
		return 0;
	}

	/* Add to red-black tree */
	entry->addr = global_uva_space.base_addr + addr;
	entry->size = aligned_size;
	entry->memory_id = memory_id;

	rb_insert_color(&entry->node, &global_uva_space.addr_tree);

	mutex_unlock(&global_uva_space.lock);

	pr_debug("MVGAL: Allocated UVA 0x%llx (size %llu) for memory %llu\n",
		(unsigned long long)entry->addr,
		(unsigned long long)aligned_size,
		(unsigned long long)memory_id);

	return entry->addr;
}

/*
 * mvgal_uva_release - Release virtual address
 */
void mvgal_uva_release(uint64_t addr)
{
	struct rb_node *node;
	struct mvgal_uva_entry *entry = NULL;

	mutex_lock(&global_uva_space.lock);

	/* Find and remove entry */
	node = global_uva_space.addr_tree.rb_node;
	while (node) {
		entry = rb_entry(node, struct mvgal_uva_entry, node);
		if (addr < entry->addr) {
			node = node->rb_left;
		} else if (addr > entry->addr) {
			node = node->rb_right;
		} else {
			/* Found it */
			rb_erase(node, &global_uva_space.addr_tree);
			break;
		}
	}

	mutex_unlock(&global_uva_space.lock);

	if (entry) {
		pr_debug("MVGAL: Released UVA 0x%llx (size %llu)\n",
			(unsigned long long)entry->addr,
			(unsigned long long)entry->size);
		kfree(entry);
	}
}

/*
 * mvgal_uva_lookup - Look up UVA by memory ID
 */
uint64_t mvgal_uva_lookup(uint64_t memory_id)
{
	struct rb_node *node;
	struct mvgal_uva_entry *entry;
	uint64_t addr = 0;

	mutex_lock(&global_uva_space.lock);

	/* Search for entry by memory_id */
	node = rb_first(&global_uva_space.addr_tree);
	while (node) {
		entry = rb_entry(node, struct mvgal_uva_entry, node);
		if (entry->memory_id == memory_id) {
			addr = entry->addr;
			break;
		}
		node = rb_next(node);
	}

	mutex_unlock(&global_uva_space.lock);

	return addr;
}

/*
 * mvgal_uva_map - Map memory to UVA for GPU access
 */
int mvgal_uva_map(struct mvgal_memory *mem, uint32_t gpu_mask)
{
	int i;

	if (!mem || mem->uva == 0) {
		return -EINVAL;
	}

	/* Map to each GPU in the mask */
	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		if (gpu_mask & (1 << i)) {
			if (mem->gpu_addrs[i] != 0) {
				mem->populated[i] = true;
				pr_debug("MVGAL: Mapped memory %llu to GPU %d @ 0x%llx\n",
					(unsigned long long)mem->id, i,
					(unsigned long long)mem->gpu_addrs[i]);
			}
		}
	}

	return 0;
}

/*
 * mvgal_uva_unmap - Unmap memory from GPUs
 */
void mvgal_uva_unmap(struct mvgal_memory *mem)
{
	int i;

	if (!mem) {
		return;
	}

	/* Unmap from all GPUs */
	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		if (mem->populated[i]) {
			mem->populated[i] = false;
			pr_debug("MVGAL: Unmapped memory %llu from GPU %d\n",
				(unsigned long long)mem->id, i);
		}
	}
}

/*
 * Residency Tracking Implementation (Section 4.2.3)
 */

/*
 * mvgal_residency_init - Initialize residency tracker
 */
int mvgal_residency_init(void)
{
	int i;

	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		residency_tracker.residencies[i] = NULL;
	}

	pr_debug("MVGAL: Residency tracker initialized\n");
	return 0;
}

/*
 * mvgal_residency_fini - Cleanup residency tracker
 */
void mvgal_residency_fini(void)
{
	int i;

	mutex_lock(&residency_tracker.lock);

	for (i = 0; i < MVGAL_MAX_GPUS; i++) {
		if (residency_tracker.residencies[i]) {
			struct mvgal_memory_residency *res = residency_tracker.residencies[i];
			if (res->pages) {
				kfree(res->pages);
			}
			kfree(res);
			residency_tracker.residencies[i] = NULL;
		}
	}

	mutex_unlock(&residency_tracker.lock);
	mutex_destroy(&residency_tracker.lock);

	pr_debug("MVGAL: Residency tracker cleaned up\n");
}

/*
 * mvgal_residency_set_policy - Set residency policy for memory
 */
int mvgal_residency_set_policy(struct mvgal_memory *mem, enum mvgal_residency_policy policy)
{
	if (!mem) {
		return -EINVAL;
	}

	/* TODO: Store policy in memory structure */
	pr_debug("MVGAL: Set residency policy %d for memory %llu\n",
		policy, (unsigned long long)mem->id);

	return 0;
}

/*
 * mvgal_residency_track_access - Track memory access by GPU
 */
int mvgal_residency_track_access(struct mvgal_memory *mem, uint32_t gpu_index)
{
	if (!mem || gpu_index >= MVGAL_MAX_GPUS) {
		return -EINVAL;
	}

	/* Update last access timestamp */
	pr_debug("MVGAL: Track access to memory %llu from GPU %u\n",
		(unsigned long long)mem->id, gpu_index);

	/* Simple heuristic: if GPU accessed more than 3x others, migrate ownership */
	/* In production, use a proper working-set estimation */

	return 0;
}

/*
 * mvgal_residency_migrate - Migrate memory between GPUs
 */
int mvgal_residency_migrate(struct mvgal_memory *mem, uint32_t src_gpu, uint32_t dst_gpu)
{
	enum mvgal_migration_path path;

	if (!mem || src_gpu >= MVGAL_MAX_GPUS || dst_gpu >= MVGAL_MAX_GPUS) {
		return -EINVAL;
	}

	/* Select migration path */
	path = mvgal_select_migration_path(src_gpu, dst_gpu);

	pr_info("MVGAL: Migrating memory %llu from GPU %u to GPU %u (path: %d)\n",
		(unsigned long long)mem->id, src_gpu, dst_gpu, path);

	/* TODO: Implement actual migration based on path */
	switch (path) {
	case MVGAL_MIGRATION_PCIE_P2P:
		/* Use PCI P2P for migration */
		break;
	case MVGAL_MIGRATION_DMA_BUF_ZERO_COPY:
		/* Use DMA-BUF sharing */
		break;
	case MVGAL_MIGRATION_HOST_STAGING:
		/* Use CPU-mediated copy */
		break;
	}

	return 0;
}

/*
 * mvgal_select_migration_path - Select best migration path between GPUs
 */
enum mvgal_migration_path mvgal_select_migration_path(uint32_t src_gpu, uint32_t dst_gpu)
{
	struct mvgal_gpu_device *src = NULL;
	struct mvgal_gpu_device *dst = NULL;
	int i = 0;

	/* Find source and destination GPUs */
	mutex_lock(&mvgal_logical_device->gpu_lock);

	list_for_each_entry(src, &mvgal_logical_device->gpu_list, node) {
		if (i == src_gpu) {
			break;
		}
		i++;
	}

	i = 0;
	list_for_each_entry(dst, &mvgal_logical_device->gpu_list, node) {
		if (i == dst_gpu) {
			break;
		}
		i++;
	}

	mutex_unlock(&mvgal_logical_device->gpu_lock);

	if (!src || !dst) {
		return MVGAL_MIGRATION_HOST_STAGING;
	}

	/* Check if both GPUs are on the same PCI root complex */
	if (src->numa_node == dst->numa_node && src->numa_node >= 0) {
		/* Same NUMA node - prefer P2P */
		return MVGAL_MIGRATION_PCIE_P2P;
	}

	/* Check if both support DMA-BUF sharing */
	if (src->ops && dst->ops && src->ops->export_dmabuf && dst->ops->import_dmabuf) {
		return MVGAL_MIGRATION_DMA_BUF_ZERO_COPY;
	}

	/* Fallback to host staging */
	return MVGAL_MIGRATION_HOST_STAGING;
}
