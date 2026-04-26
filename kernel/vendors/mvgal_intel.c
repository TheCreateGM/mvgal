/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Intel GPU Driver Integration (i915 and Xe)
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Study references:
 * - drivers/gpu/drm/i915/ for integrated graphics
 * - drivers/gpu/drm/xe/ for discrete Xe GPUs
 * - Intel Media Driver: https://github.com/intel/media-driver
 * - Intel Linux kernel: https://github.com/intel/linux-intel-lts
 * - Freedesktop Xe kernel driver: https://gitlab.freedesktop.org/drm/intel
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>

#include "../mvgal_core.h"
#include "../mvgal_device.h"
#include "mvgal_intel.h"

/* Intel vendor operations */
const struct mvgal_vendor_ops mvgal_intel_ops = {
	.init = mvgal_intel_init,
	.fini = mvgal_intel_fini,
	.submit_cs = mvgal_intel_submit_cs,
	.alloc_vram = mvgal_intel_alloc_vram,
	.free_vram = mvgal_intel_free_vram,
	.wait_idle = mvgal_intel_wait_idle,
	.set_power_state = mvgal_intel_set_power_state,
	.export_dmabuf = mvgal_intel_export_dmabuf,
	.import_dmabuf = mvgal_intel_import_dmabuf,
	.query_utilization = mvgal_intel_query_utilization,
};

/*
 * Per-GPU Intel private data
 */
struct mvgal_intel_priv {
	struct pci_dev *pdev;
	struct drm_device *drm_dev;       /* Pointer to i915 or xe DRM device */
	
	/* Driver type */
	bool is_i915;                    /* Using i915 driver */
	bool is_xe;                      /* Using xe driver */
	bool is_dg2;                     /* Intel Arc discrete GPU */
	
	/* Capabilities */
	uint64_t vram_total;             /* Total VRAM for discrete GPUs */
	uint64_t vram_stolen;            /* Stolen memory for integrated graphics */
	uint32_t eu_count;               /* Number of Execution Units */
	
	/* DMA-BUF support */
	bool can_export_dmabuf;
	bool can_import_dmabuf;
	
	/* Power management */
	bool has_rps;                    /* Has Render Power States */
	uint32_t min_freq_mhz;           /* Minimum frequency */
	uint32_t max_freq_mhz;           /* Maximum frequency */
	uint32_t current_freq_mhz;       /* Current frequency */
	
	/* Display */
	bool is_display_connected;      /* This GPU drives a display */
};

/**
 * mvgal_intel_init - Initialize Intel GPU
 */
int mvgal_intel_init(struct mvgal_gpu_device *gpu)
{
	struct mvgal_intel_priv *priv;
	uint16_t device_id = gpu->pci_device_id;

	if (!gpu) {
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		return -ENOMEM;
	}

	priv->pdev = gpu->pdev;
	
	/* Determine driver type based on device ID */
	/* Intel Arc GPUs (DG2 and later) use xe driver */
	priv->is_dg2 = (device_id >= 0x4680 && device_id <= 0x469F) || /* DG2 */
			(device_id >= 0x5680 && device_id <= 0x569F); /* DG2 for laptop */
	
	/* Most Intel GPUs still use i915 */
	priv->is_i915 = !priv->is_dg2;
	priv->is_xe = priv->is_dg2; /* For now, xe = DG2 */

	/* Set capabilities based on driver */
	if (priv->is_i915) {
		/* Integrated graphics have stolen memory */
		priv->vram_stolen = 2 * 1024 * 1024 * 1024; /* 2GB stolen */
		gpu->vram_size = priv->vram_stolen;
		gpu->vram_bandwidth = 100 * 1024; /* ~100 GB/s */
		gpu->compute_units = 128; /* Typical for modern iGPUs */
	} else {
		/* Discrete Xe GPUs have dedicated VRAM */
		priv->vram_total = 8 * 1024 * 1024 * 1024; /* 8GB default */
		gpu->vram_size = priv->vram_total;
		gpu->vram_bandwidth = 256 * 1024; /* ~256 GB/s */
		gpu->compute_units = 512; /* 512 XE cores for DG2 */
	}

	/* Intel supports Vulkan and OpenCL via open-source drivers */
	gpu->api_flags = MVGAL_API_VULKAN | MVGAL_API_OPENCL;
	
	/* If it's a discrete GPU, also support CUDA via oneAPI */
	if (priv->is_dg2) {
		gpu->api_flags |= MVGAL_API_CUDA; /* Via oneAPI */
	}

	/* All Intel GPUs support DMA-BUF */
	priv->can_export_dmabuf = true;
	priv->can_import_dmabuf = true;

	/* Power management */
	priv->has_rps = true; /* Intel GPUs have RPS (Render Power States) */

	/* Set private data */
	gpu->vendor_priv = priv;

	pr_info("MVGAL: Intel GPU initialized (pci %04x:%04x, driver=%s)\n",
		gpu->pci_vendor_id, gpu->pci_device_id,
		priv->is_i915 ? "i915" : "xe");

	return 0;
}

/**
 * mvgal_intel_fini - Cleanup Intel GPU
 */
void mvgal_intel_fini(struct mvgal_gpu_device *gpu)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return;
	}

	kfree(priv);
	gpu->vendor_priv = NULL;

	pr_info("MVGAL: Intel GPU finalized\n");
}

/**
 * mvgal_intel_submit_cs - Submit command stream to Intel GPU
 */
int mvgal_intel_submit_cs(struct mvgal_gpu_device *gpu, struct mvgal_workload *workload)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv || !workload) {
		return -EINVAL;
	}

	/* In the full implementation:
	 * For i915: Use drm_i915_gem_execbuffer2()
	 * For xe: Use xe_exec_queue_submit()
	 * 
	 * Study references:
	 * - i915: drivers/gpu/drm/i915/gem/i915_gem_execbuffer.c
	 * - xe: drivers/gpu/drm/xe/xe_exec.c
	 */

	pr_debug("MVGAL: Intel submit_cs (workload %d, size %zu)\n",
		workload->id, workload->command_buffer_size);

	return 0;
}

/**
 * mvgal_intel_alloc_vram - Allocate VRAM on Intel GPU
 */
int mvgal_intel_alloc_vram(struct mvgal_gpu_device *gpu, size_t size, uint64_t *gpu_addr)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return -EINVAL;
	}

	/* In the full implementation:
	 * For i915: Use i915_gem_object_create() or drm_gem_object_alloc()
	 * For xe: Use xe_bo_create()
	 * 
	 * Study references:
	 * - i915: drivers/gpu/drm/i915/gem/i915_gem_object.c
	 * - xe: drivers/gpu/drm/xe/xe_bo.c
	 */

	*gpu_addr = 0x10000000; /* Fake address for now */
	
	pr_debug("MVGAL: Intel alloc_vram (size %zu, addr %016llx)\n", size, *gpu_addr);

	return 0;
}

/**
 * mvgal_intel_free_vram - Free VRAM on Intel GPU
 */
void mvgal_intel_free_vram(struct mvgal_gpu_device *gpu, uint64_t gpu_addr)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return;
	}

	/* In the full implementation:
	 * For i915: Use drm_gem_object_put() or i915_gem_object_put()
	 * For xe: Use xe_bo_put()
	 */

	pr_debug("MVGAL: Intel free_vram (addr %016llx)\n", gpu_addr);
}

/**
 * mvgal_intel_wait_idle - Wait for Intel GPU to be idle
 */
int mvgal_intel_wait_idle(struct mvgal_gpu_device *gpu, uint32_t timeout_ms)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return -EINVAL;
	}

	/* In the full implementation:
	 * For i915: Use i915_gem_wait_idle()
	 * For xe: Use xe_exec_wait() or xe_device_wait_idle()
	 * 
	 * Study references:
	 * - i915: drivers/gpu/drm/i915/gem/i915_gem_wait.c
	 */

	pr_debug("MVGAL: Intel wait_idle (timeout %ums)\n", timeout_ms);

	return 0;
}

/**
 * mvgal_intel_set_power_state - Set Intel GPU power state
 */
int mvgal_intel_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return -EINVAL;
	}

	/* In the full implementation:
	 * For Intel GPUs, use RPS (Render Power States):
	 * /sys/class/drm/cardX/gt/gt0/rps_cur_freq_mhz
	 * /sys/class/drm/cardX/gt/gt0/rps_boost_freq_mhz
	 * 
	 * For integrated graphics, use:
	 * /sys/class/drm/cardX/power/control
	 * 
	 * Study references:
	 * - drivers/gpu/drm/i915/gt/intel_rps.c
	 */

	pr_debug("MVGAL: Intel set_power_state (state %d)\n", state);

	return 0;
}

/**
 * mvgal_intel_export_dmabuf - Export Intel memory as DMA-BUF
 */
struct dma_buf *mvgal_intel_export_dmabuf(struct mvgal_gpu_device *gpu, uint64_t gpu_addr, size_t size)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return ERR_PTR(-EINVAL);
	}

	if (!priv->can_export_dmabuf) {
		return ERR_PTR(-ENOTSUPP);
	}

	/* In the full implementation:
	 * For i915: Use drm_gem_prime_export() on the GEM object
	 * For xe: Use xe_bo_dmabuf_export()
	 * 
	 * Study references:
	 * - i915: drivers/gpu/drm/i915/gem/i915_gem_dmabuf.c
	 * - xe: drivers/gpu/drm/xe/xe_dmabuf.c
	 */

	pr_debug("MVGAL: Intel export_dmabuf (addr %016llx, size %zu)\n", gpu_addr, size);

	return ERR_PTR(-ENOSYS); /* Not yet implemented */
}

/**
 * mvgal_intel_import_dmabuf - Import DMA-BUF into Intel GPU
 */
int mvgal_intel_import_dmabuf(struct mvgal_gpu_device *gpu, struct dma_buf *buf, uint64_t *gpu_addr)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv || !buf) {
		return -EINVAL;
	}

	if (!priv->can_import_dmabuf) {
		return -ENOTSUPP;
	}

	/* In the full implementation:
	 * For i915: Use drm_gem_prime_import() to create GEM from DMA-BUF
	 * For xe: Use xe_bo_dmabuf_import()
	 * 
	 * Study references:
	 * - i915: drivers/gpu/drm/i915/gem/i915_gem_dmabuf.c
	 * - xe: drivers/gpu/drm/xe/xe_dmabuf.c
	 */

	pr_debug("MVGAL: Intel import_dmabuf\n");

	*gpu_addr = 0x20000000; /* Fake address for now */

	return 0;
}

/**
 * mvgal_intel_query_utilization - Query Intel GPU utilization
 */
int mvgal_intel_query_utilization(struct mvgal_gpu_device *gpu, uint32_t *util_percent)
{
	struct mvgal_intel_priv *priv = gpu->vendor_priv;

	if (!priv || !util_percent) {
		return -EINVAL;
	}

	/* In the full implementation:
	 * 1. For i915: Read from /sys/class/drm/cardX/gt/gt0/busy_percent
	 * 2. For xe: Similar sysfs interface
	 * 3. Or use i915_gem_get_busy_percent() if available
	 */

	*util_percent = 50; /* Fake utilization for now */

	return 0;
}
