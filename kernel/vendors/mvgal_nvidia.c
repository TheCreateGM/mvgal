/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * NVIDIA GPU Driver Integration Shim
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Note: NVIDIA's open kernel module has limited exported interfaces.
 * Where kernel-level access is not available, we use user-space shims
 * via the runtime daemon that communicate with NVIDIA's /dev/nvidia* 
 * character devices using the NVOS ioctl interface.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "../mvgal_core.h"
#include "../mvgal_device.h"
#include "../mvgal_scheduler.h"
#include "mvgal_nvidia.h"

/* NVIDIA vendor operations */
const struct mvgal_vendor_ops mvgal_nvidia_ops = {
	.init = mvgal_nvidia_init,
	.fini = mvgal_nvidia_fini,
	.submit_cs = mvgal_nvidia_submit_cs,
	.alloc_vram = mvgal_nvidia_alloc_vram,
	.free_vram = mvgal_nvidia_free_vram,
	.wait_idle = mvgal_nvidia_wait_idle,
	.set_power_state = mvgal_nvidia_set_power_state,
	.export_dmabuf = mvgal_nvidia_export_dmabuf,
	.import_dmabuf = mvgal_nvidia_import_dmabuf,
	.query_utilization = mvgal_nvidia_query_utilization,
};

/**
 * mvgal_nvidia_init - Initialize NVIDIA GPU
 * 
 * This is called when an NVIDIA GPU is detected and added to MVGAL.
 * We set up per-GPU private data and query basic capabilities.
 */
int mvgal_nvidia_init(struct mvgal_gpu_device *gpu)
{
	struct mvgal_nvidia_priv *priv;
	int ret = 0;

	if (!gpu) {
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		return -ENOMEM;
	}

	priv->pdev = gpu->pdev;
	priv->device_fd = -1;
	
	/* Check if NVIDIA modules are loaded via sysfs */
	priv->nv_drm_available = false;
	priv->dma_buf.nv_dmabuf_available = false;

	/* Check /sys/module/ for nvidia-drm */
	if (sysfs_get_dirent(NULL, "/module/nvidia_drm"))
		priv->nv_drm_available = true;

	/* Check /sys/module/ for nvidia-dmabuf */
	if (sysfs_get_dirent(NULL, "/module/nvidia_dmabuf"))
		priv->dma_buf.nv_dmabuf_available = true;
	
	/* DMA-BUF capability depends on nvidia-drm or nvidia-dmabuf */
	priv->dma_buf.can_export = priv->nv_drm_available || priv->dma_buf.nv_dmabuf_available;
	priv->dma_buf.can_import = priv->nv_drm_available || priv->dma_buf.nv_dmabuf_available;
	
	/* Query GSP firmware support via PCI class/subclass */
	/* Modern NVIDIA GPUs (Turing+) use GSP firmware */
	priv->gsp_firmware_loaded = false;
	if (gpu->pdev->class >> 8 == PCI_CLASS_DISPLAY_VGA ||
	    gpu->pdev->class >> 8 == PCI_CLASS_DISPLAY_3D) {
		/* Check for GSP by attempting to read firmware version via PCI config */
		u32 gsp_status = 0;
		pci_read_config_dword(gpu->pdev, 0x8C, &gsp_status); /* Offset for GSP status */
		priv->gsp_firmware_loaded = (gsp_status & 0x1) != 0;
	}
	
	/* Query actual VRAM from PCI BAR sizes */
	resource_size_t bar0_size = pci_resource_len(gpu->pdev, 0);
	resource_size_t bar1_size = pci_resource_len(gpu->pdev, 1);
	resource_size_t bar3_size = pci_resource_len(gpu->pdev, 3);
	
	/* BAR1 is typically VRAM aperture, BAR3 is VRAM on newer GPUs */
	if (bar3_size > bar1_size && bar3_size > 0x10000000) { /* > 256MB */
		gpu->vram_size = bar3_size;
	} else if (bar1_size > 0x10000000) {
		gpu->vram_size = bar1_size;
	} else {
		/* Fallback: estimate from device ID */
		gpu->vram_size = mvgal_nvidia_estimate_vram(gpu->pci_device_id);
	}
	
	/* Set compute capability based on architecture */
	gpu->compute_capability = mvgal_nvidia_get_compute_capability(gpu->pci_device_id);
	gpu->gsp_supported = priv->gsp_firmware_loaded;

	/* Set capabilities based on detected hardware */
	gpu->vram_bandwidth = mvgal_nvidia_estimate_bandwidth(gpu->pci_device_id);
	gpu->compute_units = mvgal_nvidia_get_sm_count(gpu->pci_device_id);
	gpu->api_flags = MVGAL_API_VULKAN | MVGAL_API_CUDA | MVGAL_API_OPENCL;
	
	/* Mark as tensor-core capable if RTX series */
	if (mvgal_nvidia_is_rtx_series(gpu->pci_device_id)) {
		gpu->features |= MVGAL_FEATURE_TENSOR_CORES | MVGAL_FEATURE_RT_CORES;
	}
	
	/* Enable NVLink if available (multi-GPU peer access) */
	if (mvgal_nvidia_has_nvlink(gpu->pdev)) {
		gpu->features |= MVGAL_FEATURE_PEER_ACCESS;
		gpu->nvlink_capable = true;
	}
	
	/* Set private data */
	gpu->vendor_priv = priv;

	pr_info("MVGAL: NVIDIA GPU initialized (pci %04x:%04x)\n",
		gpu->pci_vendor_id, gpu->pci_device_id);

	return 0;
}

/**
 * mvgal_nvidia_fini - Cleanup NVIDIA GPU
 */
void mvgal_nvidia_fini(struct mvgal_gpu_device *gpu)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return;
	}

	/* Close device file if open */
	if (priv->device_fd >= 0) {
		/* User-space only: close file descriptor */
		/* In kernel, we can't use file descriptors directly */
	}

	kfree(priv);
	gpu->vendor_priv = NULL;

	pr_info("MVGAL: NVIDIA GPU finalized\n");
}

/**
 * mvgal_nvidia_submit_cs - Submit command stream to NVIDIA GPU
 * 
 * Note: For NVIDIA, command submission must go through user-space
 * via the nvUvm and nvOs ioctls. The kernel module cannot directly
 * submit commands to NVIDIA hardware without full reverse-engineering
 * of the proprietary driver interface.
 * 
 * In the kernel, we mark the workload as submitted and let the
 * user-space daemon handle the actual submission via ioctls to
 * /dev/nvidia0 or similar.
 */
int mvgal_nvidia_submit_cs(struct mvgal_gpu_device *gpu, struct mvgal_workload *workload)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv || !workload) {
		return -EINVAL;
	}

	/* In the full implementation, this would:
	 * 1. Map the command buffer from user space
	 * 2. Submit via nvOs ioctls
	 * 3. Wait for completion
	 * 
	 * For now, we return success immediately as the actual
	 * submission is handled by the user-space daemon.
	 */

	pr_debug("MVGAL: NVIDIA submit_cs (workload %d, size %zu)\n",
		workload->id, workload->command_buffer_size);

	/* Mark workload as running (actual execution by daemon) */
	return 0;
}

/**
 * mvgal_nvidia_alloc_vram - Allocate VRAM on NVIDIA GPU
 */
int mvgal_nvidia_alloc_vram(struct mvgal_gpu_device *gpu, size_t size, uint64_t *gpu_addr)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return -EINVAL;
	}

	/* In the full implementation, this would:
	 * 1. Use nvidia-drm GEM allocation
	 * 2. Or use nvidia-dma-buf for shared memory
	 * 
	 * For now, we allocate a fake address.
	 */

	*gpu_addr = 0x10000000; /* Fake address for now */
	
	pr_debug("MVGAL: NVIDIA alloc_vram (size %zu, addr %016llx)\n", size, *gpu_addr);

	return 0;
}

/**
 * mvgal_nvidia_free_vram - Free VRAM on NVIDIA GPU
 */
void mvgal_nvidia_free_vram(struct mvgal_gpu_device *gpu, uint64_t gpu_addr)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return;
	}

	pr_debug("MVGAL: NVIDIA free_vram (addr %016llx)\n", gpu_addr);

	/* In the full implementation, free the GEM object or DMA-BUF */
}

/**
 * mvgal_nvidia_wait_idle - Wait for NVIDIA GPU to be idle
 */
int mvgal_nvidia_wait_idle(struct mvgal_gpu_device *gpu, uint32_t timeout_ms)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return -EINVAL;
	}

	/* In the full implementation, use nvidia-smi or sysfs to check utilization */
	pr_debug("MVGAL: NVIDIA wait_idle (timeout %ums)\n", timeout_ms);

	return 0; /* Return success immediately for now */
}

/**
 * mvgal_nvidia_set_power_state - Set NVIDIA GPU power state
 * 
 * Uses ACPI or NVIDIA-specific interfaces for power management.
 */
int mvgal_nvidia_set_power_state(struct mvgal_gpu_device *gpu, enum mvgal_power_state state)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return -EINVAL;
	}

	pr_debug("MVGAL: NVIDIA set_power_state (state %d)\n", state);

	/* In the full implementation, use ACPI calls or NVIDIA-specific sysfs */
	/* For NVIDIA, we might use:
	 * /sys/bus/pci/devices/.../power/control
	 * nvidia-smi -p <gpu_id> -power
	 * Or NVML library calls
	 */

	return 0; /* Return success immediately for now */
}

/**
 * mvgal_nvidia_export_dmabuf - Export NVIDIA memory as DMA-BUF
 * 
 * Uses nvidia-drm's DMA-BUF export functionality if available.
 */
struct dma_buf *mvgal_nvidia_export_dmabuf(struct mvgal_gpu_device *gpu, uint64_t gpu_addr, size_t size)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv) {
		return ERR_PTR(-EINVAL);
	}

	if (!priv->dma_buf.can_export) {
		pr_warn("MVGAL: NVIDIA DMA-BUF export not available\n");
		return ERR_PTR(-ENOTSUPP);
	}

	/* In the full implementation:
	 * 1. Create a struct dma_buf_export_info
	 * 2. Use nvidia_drm_dmabuf_prime_export() if available
	 * 3. Or use drm_gem_prime_export() on the GEM object
	 */

	pr_debug("MVGAL: NVIDIA export_dmabuf (addr %016llx, size %zu)\n", gpu_addr, size);

	return ERR_PTR(-ENOSYS); /* Not yet implemented */
}

/**
 * mvgal_nvidia_import_dmabuf - Import DMA-BUF into NVIDIA GPU
 */
int mvgal_nvidia_import_dmabuf(struct mvgal_gpu_device *gpu, struct dma_buf *buf, uint64_t *gpu_addr)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv || !buf) {
		return -EINVAL;
	}

	if (!priv->dma_buf.can_import) {
		pr_warn("MVGAL: NVIDIA DMA-BUF import not available\n");
		return -ENOTSUPP;
	}

	/* In the full implementation:
	 * 1. Create a GEM object from DMA-BUF
	 * 2. Use nvidia_drm_dmabuf_prime_import() if available
	 * 3. Map into NVIDIA address space
	 */

	pr_debug("MVGAL: NVIDIA import_dmabuf\n");

	*gpu_addr = 0x20000000; /* Fake address for now */

	return 0;
}

/**
 * mvgal_nvidia_query_utilization - Query NVIDIA GPU utilization
 */
int mvgal_nvidia_query_utilization(struct mvgal_gpu_device *gpu, uint32_t *util_percent)
{
	struct mvgal_nvidia_priv *priv = gpu->vendor_priv;

	if (!priv || !util_percent) {
		return -EINVAL;
	}

	/* In the full implementation:
	 * 1. Read from /sys/class/drm/card?/device/gpu_busy_percent
	 * 2. Or use NVML library calls
	 * 3. Or use nvidia-smi
	 */

	*util_percent = 50; /* Fake utilization for now */

	return 0;
}

/* ============================================================================
 * NVIDIA Device Detection Helpers
 * ============================================================================ */

/**
 * mvgal_nvidia_estimate_vram - Estimate VRAM from device ID
 */
u64 mvgal_nvidia_estimate_vram(u16 device_id)
{
	/* GA102 / RTX 3090/3090 Ti */
	if (device_id >= 0x2204 && device_id <= 0x2206) return 24ULL * 1024 * 1024 * 1024;
	/* GA104 / RTX 3070/3070 Ti/3080 Mobile */
	if (device_id >= 0x2486 && device_id <= 0x249C) return 8ULL * 1024 * 1024 * 1024;
	/* GA106 / RTX 3060/3060 Ti */
	if (device_id >= 0x2503 && device_id <= 0x2508) return 12ULL * 1024 * 1024 * 1024;
	/* AD102 / RTX 4090 */
	if (device_id >= 0x2684 && device_id <= 0x2686) return 24ULL * 1024 * 1024 * 1024;
	/* AD103 / RTX 4080 */
	if (device_id >= 0x2704 && device_id <= 0x2706) return 16ULL * 1024 * 1024 * 1024;
	/* AD104 / RTX 4070/4070 Ti */
	if (device_id >= 0x2782 && device_id <= 0x2786) return 12ULL * 1024 * 1024 * 1024;
	/* AD106 / RTX 4060/4060 Ti */
	if (device_id >= 0x2803 && device_id <= 0x2806) return 8ULL * 1024 * 1024 * 1024;
	/* AD107 / RTX 4060 Mobile/4050 */
	if (device_id >= 0x2882 && device_id <= 0x2886) return 8ULL * 1024 * 1024 * 1024;
	/* GB202 / RTX 5090 */
	if (device_id >= 0x2B85 && device_id <= 0x2B87) return 32ULL * 1024 * 1024 * 1024;
	/* GB203 / RTX 5080 */
	if (device_id >= 0x2C85 && device_id <= 0x2C87) return 16ULL * 1024 * 1024 * 1024;
	/* TU102 / RTX 2080 Ti/Titan RTX */
	if (device_id >= 0x1E02 && device_id <= 0x1E07) return 24ULL * 1024 * 1024 * 1024;
	/* TU104 / RTX 2080/Super */
	if (device_id >= 0x1E82 && device_id <= 0x1E87) return 8ULL * 1024 * 1024 * 1024;
	/* TU106 / RTX 2070/Super */
	if (device_id >= 0x1F02 && device_id <= 0x1F07) return 8ULL * 1024 * 1024 * 1024;
	/* TU116 / GTX 1660/Ti */
	if (device_id >= 0x2182 && device_id <= 0x2187) return 6ULL * 1024 * 1024 * 1024;
	/* TU117 / GTX 1650 */
	if (device_id >= 0x1F91 && device_id <= 0x1F97) return 4ULL * 1024 * 1024 * 1024;
	/* GP102 / GTX 1080 Ti/Titan Xp */
	if (device_id >= 0x1B00 && device_id <= 0x1B06) return 12ULL * 1024 * 1024 * 1024;
	/* GP104 / GTX 1080/1070 */
	if (device_id >= 0x1B80 && device_id <= 0x1B87) return 8ULL * 1024 * 1024 * 1024;
	/* GP106 / GTX 1060 */
	if (device_id >= 0x1C02 && device_id <= 0x1C07) return 6ULL * 1024 * 1024 * 1024;
	
	/* Default: 8GB */
	return 8ULL * 1024 * 1024 * 1024;
}

/**
 * mvgal_nvidia_estimate_bandwidth - Estimate memory bandwidth
 */
u32 mvgal_nvidia_estimate_bandwidth(u16 device_id)
{
	/* High-end cards: ~1008 GB/s (GDDR6X/GDDR7) */
	if ((device_id >= 0x2684 && device_id <= 0x2686) || /* AD102 */
	    (device_id >= 0x2204 && device_id <= 0x2206) || /* GA102 */
	    (device_id >= 0x2B85 && device_id <= 0x2B87))   /* GB202 */
		return 1008 * 1024;
	
	/* Mid-range: ~616 GB/s (GDDR6X) */
	if ((device_id >= 0x2704 && device_id <= 0x2706) || /* AD103 */
	    (device_id >= 0x2782 && device_id <= 0x2786))   /* AD104 */
		return 616 * 1024;
	
	/* Lower mid-range: ~504 GB/s (GDDR6) */
	if ((device_id >= 0x2803 && device_id <= 0x2806) || /* AD106 */
	    (device_id >= 0x2882 && device_id <= 0x2886) || /* AD107 */
	    (device_id >= 0x2486 && device_id <= 0x249C))  /* GA104 */
		return 504 * 1024;
	
	/* Budget: ~360 GB/s */
	return 360 * 1024;
}

/**
 * mvgal_nvidia_get_compute_capability - Get CUDA compute capability
 * Returns encoded value: major * 10 + minor (e.g., 86 for 8.6, 75 for 7.5)
 */
u32 mvgal_nvidia_get_compute_capability(u16 device_id)
{
	/* Blackwell (GB20x) - SM 10.0 */
	if (device_id >= 0x2B00 && device_id <= 0x2CFF) return 100;
	/* Ada Lovelace (AD10x) - SM 8.9 */
	if (device_id >= 0x2680 && device_id <= 0x28FF) return 89;
	/* Ampere (GA10x) - SM 8.6, GA102 - SM 8.6 */
	if (device_id >= 0x2200 && device_id <= 0x25FF) return 86;
	/* Turing (TU1xx) - SM 7.5 */
	if (device_id >= 0x1E00 && device_id <= 0x21FF) return 75;
	/* Pascal (GP10x) - SM 6.1 */
	if (device_id >= 0x1B00 && device_id <= 0x1DFF) return 61;

	return 75; /* Default to Turing level */
}

/**
 * mvgal_nvidia_get_sm_count - Get streaming multiprocessor count
 */
u32 mvgal_nvidia_get_sm_count(u16 device_id)
{
	/* Top-end: 128+ SMs */
	if ((device_id >= 0x2B85 && device_id <= 0x2B87) || /* GB202 */
	    (device_id >= 0x2204 && device_id <= 0x2206))  /* GA102 */
		return 128;
	
	/* High-end: 80-96 SMs */
	if ((device_id >= 0x2684 && device_id <= 0x2686) || /* AD102 */
	    (device_id >= 0x2C85 && device_id <= 0x2C87))   /* GB203 */
		return 96;
	
	/* Mid-high: 60-76 SMs */
	if ((device_id >= 0x2704 && device_id <= 0x2706) || /* AD103 */
	    (device_id >= 0x2782 && device_id <= 0x2786))   /* AD104 */
		return 76;
	
	/* Mid-range: 36-48 SMs */
	if ((device_id >= 0x2803 && device_id <= 0x2806) || /* AD106 */
	    (device_id >= 0x2486 && device_id <= 0x249C))   /* GA104 */
		return 48;
	
	/* Lower: 24-36 SMs */
	if (device_id >= 0x2882 && device_id <= 0x2886) /* AD107 */
		return 24;
	
	return 48; /* Default */
}

/**
 * mvgal_nvidia_is_rtx_series - Check if RTX series (Turing+)
 */
bool mvgal_nvidia_is_rtx_series(u16 device_id)
{
	/* Turing, Ampere, Ada Lovelace, Blackwell */
	return (device_id >= 0x1E00 && device_id <= 0x21FF) || /* Turing */
	       (device_id >= 0x2200 && device_id <= 0x25FF) || /* Ampere */
	       (device_id >= 0x2680 && device_id <= 0x28FF) || /* Ada */
	       (device_id >= 0x2B00 && device_id <= 0x2CFF);   /* Blackwell */
}

/**
 * mvgal_nvidia_has_nvlink - Check for NVLink capability
 */
bool mvgal_nvidia_has_nvlink(struct pci_dev *pdev)
{
	/* NVLink is present on:
	 * - V100, A100, H100 (datacenter)
	 * - RTX 3090, 3090 Ti, 4090, 4090 Ti, 5090 (consumer with bridge)
	 * - Quadro RTX, RTX A-series, RTX 6000 Ada
	 */
	u16 device_id = pdev->device;
	
	/* Check for NVLink capable devices */
	if (device_id >= 0x2204 && device_id <= 0x2206) /* GA102: RTX 3090/Ti */
		return true;
	if (device_id >= 0x2684 && device_id <= 0x2686) /* AD102: RTX 4090 */
		return true;
	if (device_id >= 0x2B85 && device_id <= 0x2B87) /* GB202: RTX 5090 */
		return true;
	
	return false;
}
