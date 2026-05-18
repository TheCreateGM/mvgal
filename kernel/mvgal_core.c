/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Kernel Core Module - DRM registration and module entry point
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/ioctl.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm.h>
#include <drm/drm_print.h>

#include <mvgal/mvgal_uapi.h>
#include "mvgal_core.h"
#include "mvgal_device.h"
#include "mvgal_scheduler.h"
#include "mvgal_memory.h"
#include "mvgal_sync.h"

/* Use IOCTL definitions from header files */

#define DRIVER_NAME "mvgal"
#define DRIVER_DESC "Multi-Vendor GPU Aggregation Layer"
#define DRIVER_DATE "20260426"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 2
#define DRIVER_PATCHLEVEL 0

/* Module parameters */
static bool enable_debug = false;
module_param(enable_debug, bool, 0644);
MODULE_PARM_DESC(enable_debug, "Enable debug logging");

/* Import DMA_BUF namespace for dma-buf driver-private symbol resolution */
#ifdef MODULE_IMPORT_NS
MODULE_IMPORT_NS("DMA_BUF");
#endif

/* Character device */
static dev_t mvgal_devt;
static struct class *mvgal_class;
static struct cdev *mvgal_cdev;

/* DRM driver - actual definition below after ioctls */
/* MVGAL logical device */
struct mvgal_device *mvgal_logical_device;
EXPORT_SYMBOL_GPL(mvgal_logical_device);

/*
 * DRM operations
 */
static int mvgal_drm_open(struct drm_device *drm, struct drm_file *file)
{
	int ret;

	/* Allocate and initialize per-file private data */
	file->driver_priv = kzalloc(sizeof(struct mvgal_file), GFP_KERNEL);
	if (!file->driver_priv) {
		return -ENOMEM;
	}

	pr_debug("Open called\n");

	return 0;
}

static void mvgal_drm_postclose(struct drm_device *drm, struct drm_file *file)
{
	struct mvgal_file *mvgal_file = file->driver_priv;

	/* Cleanup per-file data */
	kfree(mvgal_file);
	file->driver_priv = NULL;

	pr_info("MVGAL: Postclose called\n");
}

#if IS_ENABLED(CONFIG_DRM)
static const struct drm_ioctl_desc mvgal_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MVGAL_QUERY_DEVICES, mvgal_ioctl_query_devices, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_QUERY_CAPABILITIES, mvgal_ioctl_query_capabilities, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_SUBMIT_WORKLOAD, mvgal_ioctl_submit_workload, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_ALLOC_MEMORY, mvgal_ioctl_alloc_memory, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_FREE_MEMORY, mvgal_ioctl_free_memory, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_IMPORT_DMABUF, mvgal_ioctl_import_dmabuf, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_EXPORT_DMABUF, mvgal_ioctl_export_dmabuf, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_WAIT_FENCE, mvgal_ioctl_wait_fence, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_SIGNAL_FENCE, mvgal_ioctl_signal_fence, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_SET_GPU_AFFINITY, mvgal_ioctl_set_gpu_affinity, DRM_RENDER_ALLOW),
};

const struct drm_driver mvgal_drm_driver = {
	.driver_features = DRIVER_RENDER | DRIVER_HAVE_IRQ | DRIVER_GEM,
	.ioctls = mvgal_ioctls,
	.num_ioctls = ARRAY_SIZE(mvgal_ioctls),
	.open = mvgal_drm_open,
	.postclose = mvgal_drm_postclose,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};
EXPORT_SYMBOL_GPL(mvgal_drm_driver);
#endif /* CONFIG_DRM */

/* PCI device table */
static const struct pci_device_id mvgal_pci_table[] = {
	/* AMD GPUs */
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, 0x1638) }, /* AMD Radeon RX 6800 */
	{ PCI_DEVICE(PCI_VENDOR_ID_ATI, 0x73A0) }, /* AMD Radeon RX 6700 XT */
	
	/* NVIDIA GPUs */
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x2204) }, /* NVIDIA GA102 */
	{ PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, 0x2206) }, /* NVIDIA GA104 */
	
	/* Intel GPUs */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x4680) }, /* Intel DG2 */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x4690) }, /* Intel DG2 */
	
	/* Moore Threads GPUs */
	{ PCI_DEVICE(0x1ED5, 0x4000) }, /* Moore Threadsqtt S2000 */
	
	{ 0, } /* Terminator */
};

MODULE_DEVICE_TABLE(pci, mvgal_pci_table);

/* PCI driver */
static struct pci_driver mvgal_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = mvgal_pci_table,
	.probe = mvgal_pci_probe,
	.remove = mvgal_pci_remove,
};

/* Character device file operations */
static const struct file_operations mvgal_fops = {
	.owner = THIS_MODULE,
	.open = mvgal_char_open,
	.release = mvgal_char_release,
	.unlocked_ioctl = mvgal_char_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.mmap = mvgal_char_mmap,
};

/* Module initialization */
int mvgal_init(void)
{
	int ret;

	pr_info("MVGAL: Multi-Vendor GPU Aggregation Layer initializing\n");

	/* Allocate device numbers */
	ret = alloc_chrdev_region(&mvgal_devt, 0, MVGAL_MAX_DEVICES, DRIVER_NAME);
	if (ret < 0) {
		pr_err("MVGAL: Failed to allocate device numbers\n");
		return ret;
	}

	/* Create device class */
	mvgal_class = class_create(DRIVER_NAME);
	if (IS_ERR(mvgal_class)) {
		ret = PTR_ERR(mvgal_class);
		pr_err("MVGAL: Failed to create device class\n");
		goto err_unregister_chrdev;
	}

	/* Create character device */
	mvgal_cdev = cdev_alloc();
	if (!mvgal_cdev) {
		ret = -ENOMEM;
		pr_err("MVGAL: Failed to allocate cdev\n");
		goto err_class_destroy;
	}

	cdev_init(mvgal_cdev, &mvgal_fops);
	mvgal_cdev->owner = THIS_MODULE;

	ret = cdev_add(mvgal_cdev, mvgal_devt, MVGAL_MAX_DEVICES);
	if (ret < 0) {
		pr_err("MVGAL: Failed to add character device\n");
		goto err_cdev_del;
	}

	/* Create device node */
	device_create(mvgal_class, NULL, mvgal_devt, NULL, "%s0", DRIVER_NAME);

	/* Register DRM driver - disabled for kernel 7.x compatibility */
#if 0
	ret = drm_register_driver(&mvgal_drm_driver);
	if (ret < 0) {
		pr_err("MVGAL: DRM initialization failed\n");
		goto err_device_destroy;
	}
#endif

	/* Initialize logical device */
	ret = mvgal_device_init(&mvgal_logical_device);
	if (ret < 0) {
		pr_err("MVGAL: Logical device initialization failed\n");
		goto err_drm_cleanup;
	}

	/* Register PCI driver for GPU detection */
	ret = pci_register_driver(&mvgal_pci_driver);
	if (ret < 0) {
		pr_err("MVGAL: PCI driver registration failed\n");
		goto err_device_fini;
	}

	pr_info("MVGAL: Initialization complete. Device node: /dev/mvgal0\n");

	return 0;

err_device_fini:
	mvgal_device_fini(mvgal_logical_device);
err_drm_cleanup:
	/* drm_unregister_driver(&mvgal_drm_driver); */
err_device_destroy:
	device_destroy(mvgal_class, mvgal_devt);
	cdev_del(mvgal_cdev);
err_cdev_del:
	kfree(mvgal_cdev);
err_class_destroy:
	class_destroy(mvgal_class);
err_unregister_chrdev:
	unregister_chrdev_region(mvgal_devt, MVGAL_MAX_DEVICES);

	return ret;
}

/* Module cleanup */
void mvgal_exit(void)
{
	pr_info("MVGAL: Cleanup starting\n");

	/* Unregister PCI driver */
	pci_unregister_driver(&mvgal_pci_driver);

	/* Cleanup logical device */
	mvgal_device_fini(mvgal_logical_device);

	/* Cleanup DRM */
	/* drm_unregister_driver(&mvgal_drm_driver); */

	/* Destroy character device */
	device_destroy(mvgal_class, mvgal_devt);
	cdev_del(mvgal_cdev);
	class_destroy(mvgal_class);
	unregister_chrdev_region(mvgal_devt, MVGAL_MAX_DEVICES);

	pr_info("MVGAL: Cleanup complete\n");
}

/*
 * Aggregation stack entry points — invoked from mvgal_main.c when
 * MVGAL_BUILD_FULL_STACK=1 is set in Kbuild (see kernel/Kbuild).
 */
/*
 * Aggregation stack entry points — invoked from mvgal_main.c when
 * MVGAL_BUILD_FULL_STACK=1 is set in Kbuild (see kernel/Kbuild).
 */
int mvgal_stack_init(void)
{
	return mvgal_init();
}
EXPORT_SYMBOL_GPL(mvgal_stack_init);

void mvgal_stack_exit(void)
{
	mvgal_exit();
}
EXPORT_SYMBOL_GPL(mvgal_stack_exit);

/*
 * Character device operations implementation
 */

int mvgal_char_open(struct inode *inode, struct file *filp)
{
	struct mvgal_file *file_priv;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&file_priv->workloads);
	mutex_init(&file_priv->lock);
	file_priv->next_workload_id = 1;

	filp->private_data = file_priv;
	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_char_open);

int mvgal_char_release(struct inode *inode, struct file *filp)
{
	struct mvgal_file *file_priv = filp->private_data;

	if (file_priv) {
		struct mvgal_workload *workload, *tmp;
		
		mutex_lock(&file_priv->lock);
		list_for_each_entry_safe(workload, tmp, &file_priv->workloads, node) {
			list_del(&workload->node);
			/* Workload lifecycle is managed by scheduler/subsystem, we just unlink it from file */
		}
		mutex_unlock(&file_priv->lock);
		
		mutex_destroy(&file_priv->lock);
		kfree(file_priv);
		filp->private_data = NULL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_char_release);

int mvgal_char_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn = vma->vm_pgoff;

	/* Basic implementation: map physical pages provided via vm_pgoff */
	/* In a real implementation, we would validate the PFN against our allocations */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_char_mmap);

long mvgal_char_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	long ret = 0;
	struct drm_file mock_file;

	/* Setup a mock drm_file so we can call the DRM-based ioctl handlers */
	memset(&mock_file, 0, sizeof(mock_file));
	mock_file.driver_priv = filp->private_data;

	/* Route standard MVGAL_IOC_... (magic 'M') ioctls */
	if (_IOC_TYPE(cmd) == MVGAL_IOC_MAGIC) {
		switch (cmd) {
		case MVGAL_IOC_QUERY_VERSION: {
			struct mvgal_uapi_version version = {
				.major = MVGAL_UAPI_VERSION_MAJOR,
				.minor = MVGAL_UAPI_VERSION_MINOR,
				.patch = MVGAL_UAPI_VERSION_PATCH,
				.feature_flags = MVGAL_UAPI_FEATURE_ENUMERATION |
						 MVGAL_UAPI_FEATURE_PCI_TOPOLOGY |
						 MVGAL_UAPI_FEATURE_HOTPLUG_MONITOR |
						 MVGAL_UAPI_FEATURE_RESCAN |
						 MVGAL_UAPI_FEATURE_FUTURE_DMABUF |
						 MVGAL_UAPI_FEATURE_FUTURE_SUBMISSION,
			};
			if (copy_to_user(uarg, &version, sizeof(version)))
				return -EFAULT;
			return 0;
		}
		case MVGAL_IOC_GET_GPU_COUNT: {
			struct mvgal_uapi_gpu_count count = {0};
			if (mvgal_logical_device)
				count.gpu_count = mvgal_logical_device->gpu_count;
			if (copy_to_user(uarg, &count, sizeof(count)))
				return -EFAULT;
			return 0;
		}
		case MVGAL_IOC_GET_GPU_INFO: {
			struct mvgal_uapi_gpu_query query;
			struct mvgal_gpu_device *gpu;
			uint32_t idx = 0;

			if (copy_from_user(&query, uarg, sizeof(query)))
				return -EFAULT;

			if (!mvgal_logical_device)
				return -ENODEV;

			mutex_lock(&mvgal_logical_device->gpu_lock);
			list_for_each_entry(gpu, &mvgal_logical_device->gpu_list, node) {
				if (idx == query.index) {
					memset(&query.info, 0, sizeof(query.info));
					query.info.index = gpu->gpu_index;
					query.info.vendor_id = gpu->pci_vendor_id;
					query.info.device_id = gpu->pci_device_id;
					query.info.numa_node = gpu->numa_node;
					query.info.pcie_link_gen = gpu->pcie_gen;
					query.info.pcie_link_width = gpu->pcie_lanes;
					strscpy(query.info.name, gpu->name, sizeof(query.info.name));
					if (gpu->pdev) {
						const char *drv = dev_driver_string(&gpu->pdev->dev);
						strscpy(query.info.driver, drv ? drv : "unknown", sizeof(query.info.driver));
						snprintf(query.info.bdf, sizeof(query.info.bdf), "%04x:%02x:%02x.%u",
							 pci_domain_nr(gpu->pdev->bus), gpu->pdev->bus->number,
							 PCI_SLOT(gpu->pdev->devfn), PCI_FUNC(gpu->pdev->devfn));
					}
					mutex_unlock(&mvgal_logical_device->gpu_lock);

					if (copy_to_user(uarg, &query, sizeof(query)))
						return -EFAULT;
					return 0;
				}
				idx++;
			}
			mutex_unlock(&mvgal_logical_device->gpu_lock);
			return -EINVAL;
		}
		case MVGAL_IOC_GET_CAPS: {
			struct mvgal_uapi_caps caps = {0};
			if (mvgal_logical_device) {
				caps.enabled = 1;
				caps.gpu_count = mvgal_logical_device->gpu_count;
				caps.topology_generation = 1;
				caps.max_pcie_link_gen = 4;
				caps.max_pcie_link_width = 16;
				caps.feature_flags = MVGAL_UAPI_FEATURE_ENUMERATION |
						     MVGAL_UAPI_FEATURE_PCI_TOPOLOGY |
						     MVGAL_UAPI_FEATURE_HOTPLUG_MONITOR |
						     MVGAL_UAPI_FEATURE_RESCAN |
						     MVGAL_UAPI_FEATURE_FUTURE_DMABUF |
						     MVGAL_UAPI_FEATURE_FUTURE_SUBMISSION;
				caps.largest_prefetchable_bar_bytes = 256 * 1024 * 1024;
				caps.largest_mmio_bar_bytes = 16 * 1024 * 1024;
			}
			if (copy_to_user(uarg, &caps, sizeof(caps)))
				return -EFAULT;
			return 0;
		}
		case MVGAL_IOC_RESCAN: {
			if (mvgal_logical_device) {
				/* Simply re-enumerate GPUs */
				mvgal_gpu_cleanup_all(mvgal_logical_device);
				mvgal_enumerate_gpus(mvgal_logical_device);
			}
			return 0;
		}
		case MVGAL_IOC_GET_STATS: {
			struct mvgal_uapi_stats stats = {0};
			if (copy_to_user(uarg, &stats, sizeof(stats)))
				return -EFAULT;
			return 0;
		}
		case MVGAL_IOC_ENABLE:
		case MVGAL_IOC_DISABLE:
			return 0;
		default:
			return -EINVAL;
		}
	}

	/* Route DRM-based aggregation ioctls */
	switch (cmd) {
	case DRM_IOCTL_MVGAL_QUERY_DEVICES: {
		struct drm_mvgal_generic args;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		ret = mvgal_ioctl_query_devices(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &args, &mock_file);
		if (ret == 0 && copy_to_user(uarg, &args, sizeof(args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_QUERY_CAPABILITIES: {
		struct drm_mvgal_generic args;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		ret = mvgal_ioctl_query_capabilities(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &args, &mock_file);
		if (ret == 0 && copy_to_user(uarg, &args, sizeof(args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_SUBMIT_WORKLOAD: {
		struct drm_mvgal_generic args;
		void __user *workload_user_ptr;
		struct mvgal_submit_workload_args w_args;
		
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		workload_user_ptr = (void __user *)(uintptr_t)args.data;
		if (!workload_user_ptr)
			return -EINVAL;
		if (copy_from_user(&w_args, workload_user_ptr, sizeof(w_args)))
			return -EFAULT;
		ret = mvgal_ioctl_submit_workload(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &w_args, &mock_file);
		if (ret == 0 && copy_to_user(workload_user_ptr, &w_args, sizeof(w_args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_ALLOC_MEMORY: {
		struct drm_mvgal_generic args;
		struct mvgal_alloc_memory_args mem_args;
		void __user *mem_user_ptr;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		mem_user_ptr = (void __user *)(uintptr_t)args.data;
		if (!mem_user_ptr)
			return -EINVAL;
		if (copy_from_user(&mem_args, mem_user_ptr, sizeof(mem_args)))
			return -EFAULT;
		ret = mvgal_ioctl_alloc_memory(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &mem_args, &mock_file);
		if (ret == 0 && copy_to_user(mem_user_ptr, &mem_args, sizeof(mem_args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_FREE_MEMORY: {
		struct drm_mvgal_generic args;
		struct mvgal_free_memory_args mem_args;
		void __user *mem_user_ptr;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		mem_user_ptr = (void __user *)(uintptr_t)args.data;
		if (!mem_user_ptr)
			return -EINVAL;
		if (copy_from_user(&mem_args, mem_user_ptr, sizeof(mem_args)))
			return -EFAULT;
		ret = mvgal_ioctl_free_memory(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &mem_args, &mock_file);
		if (ret == 0 && copy_to_user(mem_user_ptr, &mem_args, sizeof(mem_args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_IMPORT_DMABUF: {
		struct drm_mvgal_generic args;
		struct mvgal_import_dmabuf_args dmabuf_args;
		void __user *dmabuf_user_ptr;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		dmabuf_user_ptr = (void __user *)(uintptr_t)args.data;
		if (!dmabuf_user_ptr)
			return -EINVAL;
		if (copy_from_user(&dmabuf_args, dmabuf_user_ptr, sizeof(dmabuf_args)))
			return -EFAULT;
		ret = mvgal_ioctl_import_dmabuf(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &dmabuf_args, &mock_file);
		if (ret == 0 && copy_to_user(dmabuf_user_ptr, &dmabuf_args, sizeof(dmabuf_args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_EXPORT_DMABUF: {
		struct drm_mvgal_generic args;
		struct mvgal_export_dmabuf_args dmabuf_args;
		void __user *dmabuf_user_ptr;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		dmabuf_user_ptr = (void __user *)(uintptr_t)args.data;
		if (!dmabuf_user_ptr)
			return -EINVAL;
		if (copy_from_user(&dmabuf_args, dmabuf_user_ptr, sizeof(dmabuf_args)))
			return -EFAULT;
		ret = mvgal_ioctl_export_dmabuf(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &dmabuf_args, &mock_file);
		if (ret == 0 && copy_to_user(dmabuf_user_ptr, &dmabuf_args, sizeof(dmabuf_args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_WAIT_FENCE: {
		struct drm_mvgal_generic args;
		struct mvgal_wait_fence_args fence_args;
		void __user *fence_user_ptr;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		fence_user_ptr = (void __user *)(uintptr_t)args.data;
		if (!fence_user_ptr)
			return -EINVAL;
		if (copy_from_user(&fence_args, fence_user_ptr, sizeof(fence_args)))
			return -EFAULT;
		ret = mvgal_ioctl_wait_fence(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &fence_args, &mock_file);
		if (ret == 0 && copy_to_user(fence_user_ptr, &fence_args, sizeof(fence_args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_SIGNAL_FENCE: {
		struct drm_mvgal_generic args;
		struct mvgal_signal_fence_args fence_args;
		void __user *fence_user_ptr;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		fence_user_ptr = (void __user *)(uintptr_t)args.data;
		if (!fence_user_ptr)
			return -EINVAL;
		if (copy_from_user(&fence_args, fence_user_ptr, sizeof(fence_args)))
			return -EFAULT;
		ret = mvgal_ioctl_signal_fence(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &fence_args, &mock_file);
		if (ret == 0 && copy_to_user(fence_user_ptr, &fence_args, sizeof(fence_args)))
			return -EFAULT;
		return ret;
	}
	case DRM_IOCTL_MVGAL_SET_GPU_AFFINITY: {
		struct drm_mvgal_generic args;
		if (copy_from_user(&args, uarg, sizeof(args)))
			return -EFAULT;
		ret = mvgal_ioctl_set_gpu_affinity(mvgal_logical_device ? mvgal_logical_device->drm : NULL, &args, &mock_file);
		if (ret == 0 && copy_to_user(uarg, &args, sizeof(args)))
			return -EFAULT;
		return ret;
	}
	default:
		return -ENOTTY;
	}
}
EXPORT_SYMBOL_GPL(mvgal_char_ioctl);

/*
 * DRM ioctl handler definitions
 */

int mvgal_ioctl_query_devices(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct drm_mvgal_generic *args = data;
	struct mvgal_gpu_device *gpu;
	uint32_t count = 0;
	void __user *user_ptr = (void __user *)(uintptr_t)args->data;

	if (!user_ptr)
		return -EINVAL;

	if (!mvgal_logical_device)
		return -ENODEV;

	mutex_lock(&mvgal_logical_device->gpu_lock);
	list_for_each_entry(gpu, &mvgal_logical_device->gpu_list, node) {
		struct mvgal_gpu_info info;
		memset(&info, 0, sizeof(info));
		info.index = gpu->gpu_index;
		info.vendor_id = gpu->pci_vendor_id;
		info.device_id = gpu->pci_device_id;
		info.numa_node = gpu->numa_node;
		info.pcie_link_gen = gpu->pcie_gen;
		info.pcie_link_width = gpu->pcie_lanes;
		strscpy(info.name, gpu->name, sizeof(info.name));
		
		/* Copy to userspace */
		if (copy_to_user(user_ptr + count * sizeof(struct mvgal_gpu_info), &info, sizeof(info))) {
			mutex_unlock(&mvgal_logical_device->gpu_lock);
			return -EFAULT;
		}
		count++;
		if (count >= MVGAL_MAX_GPUS)
			break;
	}
	mutex_unlock(&mvgal_logical_device->gpu_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_ioctl_query_devices);

int mvgal_ioctl_query_capabilities(struct drm_device *drm, void *data, struct drm_file *file)
{
	struct drm_mvgal_generic *args = data;
	void __user *user_ptr = (void __user *)(uintptr_t)args->data;
	struct mvgal_capability_profile profile;

	if (!user_ptr)
		return -EINVAL;

	if (!mvgal_logical_device)
		return -ENODEV;

	memset(&profile, 0, sizeof(profile));
	/* Copy capability profile from physical pool */
	profile = mvgal_logical_device->caps;

	if (copy_to_user(user_ptr, &profile, sizeof(profile)))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_ioctl_query_capabilities);

int mvgal_ioctl_set_gpu_affinity(struct drm_device *drm, void *data, struct drm_file *file)
{
	/* Affinity stub */
	return 0;
}
EXPORT_SYMBOL_GPL(mvgal_ioctl_set_gpu_affinity);
