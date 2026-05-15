// SPDX-License-Identifier: GPL-2.0-only
/*
 * MVGAL kernel interface shim.
 *
 * This module intentionally exposes a small, stable misc-device ABI. The
 * cross-vendor scheduling, Vulkan device-group emulation, and memory policy
 * live in userspace where they can evolve without binding MVGAL to unstable
 * vendor-driver internals.
 */

#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>

#define MVGAL_IOCTL_MAGIC 'M'
#define MVGAL_IOCTL_GET_VERSION _IOR(MVGAL_IOCTL_MAGIC, 0x00, struct mvgal_kernel_version)
#define MVGAL_IOCTL_GET_GPU_COUNT _IOR(MVGAL_IOCTL_MAGIC, 0x01, __u32)
#define MVGAL_IOCTL_SET_IDLE_POLICY _IOW(MVGAL_IOCTL_MAGIC, 0x02, __u32)

struct mvgal_kernel_version {
	__u16 major;
	__u16 minor;
	__u16 patch;
	__u16 abi;
};

static atomic_t mvgal_open_count = ATOMIC_INIT(0);
static __u32 mvgal_idle_policy = 1;

static __u32 mvgal_count_supported_pci_gpus(void)
{
	struct pci_dev *pdev = NULL;
	__u32 count = 0;

	while ((pdev = pci_get_class(PCI_BASE_CLASS_DISPLAY << 16, pdev)) != NULL) {
		switch (pdev->vendor) {
		case 0x10de: /* NVIDIA */
		case 0x1002: /* AMD */
		case 0x8086: /* Intel */
		case 0x1ed5: /* Moore Threads */
			count++;
			break;
		default:
			break;
		}
	}

	return count;
}

static int mvgal_open(struct inode *inode, struct file *file)
{
	atomic_inc(&mvgal_open_count);
	return nonseekable_open(inode, file);
}

static int mvgal_release(struct inode *inode, struct file *file)
{
	atomic_dec_if_positive(&mvgal_open_count);
	return 0;
}

static long mvgal_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mvgal_kernel_version version = {
		.major = 0,
		.minor = 2,
		.patch = 1,
		.abi = 1,
	};
	__u32 value;

	switch (cmd) {
	case MVGAL_IOCTL_GET_VERSION:
		if (copy_to_user((void __user *)arg, &version, sizeof(version)))
			return -EFAULT;
		return 0;
	case MVGAL_IOCTL_GET_GPU_COUNT:
		value = mvgal_count_supported_pci_gpus();
		if (copy_to_user((void __user *)arg, &value, sizeof(value)))
			return -EFAULT;
		return 0;
	case MVGAL_IOCTL_SET_IDLE_POLICY:
		if (copy_from_user(&value, (void __user *)arg, sizeof(value)))
			return -EFAULT;
		if (value > 2)
			return -EINVAL;
		mvgal_idle_policy = value;
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations mvgal_fops = {
	.owner = THIS_MODULE,
	.open = mvgal_open,
	.release = mvgal_release,
	.unlocked_ioctl = mvgal_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = noop_llseek,
};

static struct miscdevice mvgal_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mvgal0",
	.fops = &mvgal_fops,
	.mode = 0660,
};

static int __init mvgal_init(void)
{
	int ret = misc_register(&mvgal_miscdev);

	if (ret)
		return ret;

	pr_info("mvgal: kernel interface registered, %u supported PCI GPU(s) detected\n",
		mvgal_count_supported_pci_gpus());
	return 0;
}

static void __exit mvgal_exit(void)
{
	misc_deregister(&mvgal_miscdev);
	pr_info("mvgal: kernel interface unregistered, open_count=%d idle_policy=%u\n",
		atomic_read(&mvgal_open_count), mvgal_idle_policy);
}

module_init(mvgal_init);
module_exit(mvgal_exit);

MODULE_DESCRIPTION("MVGAL kernel interface shim");
MODULE_AUTHOR("MVGAL Project");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2.2");
