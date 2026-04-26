/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Kernel Core Module - DRM registration and module entry point
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/drm.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "mvgal_core.h"
#include "mvgal_device.h"

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

/* Character device */
static dev_t mvgal_devt;
static struct class *mvgal_class;
static struct cdev *mvgal_cdev;

/* DRM driver */
static struct drm_driver mvgal_drm_driver;

/* MVGAL logical device */
struct mvgal_device *mvgal_logical_device;

/*
 * DRM operations
 */
static int mvgal_drm_open(struct drm_device *drm, struct drm_file *file)
{
	int ret;

	drm_debug(&mvgal_logical_device->drm, "Open called\n");

	/* Allocate and initialize per-file private data */
	file->driver_priv = kzalloc(sizeof(struct mvgal_file), GFP_KERNEL);
	if (!file->driver_priv) {
		return -ENOMEM;
	}

	return 0;
}

static void mvgal_drm_postclose(struct drm_device *drm, struct drm_file *file)
{
	struct mvgal_file *mvgal_file = file->driver_priv;

	drm_debug(&mvgal_logical_device->drm, "Postclose called\n");

	/* Cleanup per-file data */
	kfree(mvgal_file);
	file->driver_priv = NULL;
}

static const struct drm_ioctl_desc mvgal_ioctls[] = {
	/* Device query and management */
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_QUERY_DEVICES, mvgal_ioctl_query_devices, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_QUERY_CAPABILITIES, mvgal_ioctl_query_capabilities, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	
	/* Workload submission */
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_SUBMIT_WORKLOAD, mvgal_ioctl_submit_workload, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	
	/* Memory management */
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_ALLOC_MEMORY, mvgal_ioctl_alloc_memory, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_FREE_MEMORY, mvgal_ioctl_free_memory, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_IMPORT_DMABUF, mvgal_ioctl_import_dmabuf, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_EXPORT_DMABUF, mvgal_ioctl_export_dmabuf, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	
	/* Synchronization */
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_WAIT_FENCE, mvgal_ioctl_wait_fence, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_SIGNAL_FENCE, mvgal_ioctl_signal_fence, DRM_UNLOCKED|DRM_RENDER_ALLOW),
	
	/* GPU affinity */
	DRM_IOCTL_DEF_DRV(MVGAL_IOCTL_SET_GPU_AFFINITY, mvgal_ioctl_set_gpu_affinity, DRM_UNLOCKED|DRM_RENDER_ALLOW),
};

static const struct drm_driver mvgal_drm_driver = {
	.driver_features = DRIVER_RENDER | DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_PRIME,
	.ioctls = mvgal_ioctls,
	.num_ioctls = ARRAY_SIZE(mvgal_ioctls),
	.open = mvgal_drm_open,
	.postclose = mvgal_drm_postclose,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

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
	{ PCI_DEVICE(0x1A82, 0x4000) }, /* Moore Threadsqtt S2000 */
	
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
static int __init mvgal_init(void)
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
	mvgal_class = class_create(THIS_MODULE, DRIVER_NAME);
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

	/* Register DRM driver */
	ret = drm_init(&mvgal_drm_driver, NULL);
	if (ret < 0) {
		pr_err("MVGAL: DRM initialization failed\n");
		goto err_device_destroy;
	}

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
	drm_cleanup(&mvgal_drm_driver);
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
static void __exit mvgal_exit(void)
{
	pr_info("MVGAL: Cleanup starting\n");

	/* Unregister PCI driver */
	pci_unregister_driver(&mvgal_pci_driver);

	/* Cleanup logical device */
	mvgal_device_fini(mvgal_logical_device);

	/* Cleanup DRM */
	drm_cleanup(&mvgal_drm_driver);

	/* Destroy character device */
	device_destroy(mvgal_class, mvgal_devt);
	cdev_del(mvgal_cdev);
	class_destroy(mvgal_class);
	unregister_chrdev_region(mvgal_devt, MVGAL_MAX_DEVICES);

	pr_info("MVGAL: Cleanup complete\n");
}

module_init(mvgal_init);
module_exit(mvgal_exit);

MODULE_AUTHOR("AxoGM");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(__stringify(DRIVER_MAJOR) "." __stringify(DRIVER_MINOR) "." __stringify(DRIVER_PATCHLEVEL));
MODULE_DEVICE_TABLE(pci, mvgal_pci_table);
