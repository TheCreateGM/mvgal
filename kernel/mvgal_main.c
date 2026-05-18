// SPDX-License-Identifier: GPL-2.0-only
/*
 * MVGAL kernel Phase 1 foundation.
 *
 * PCI display-class GPU discovery, UAPI ioctls, and sysfs under /sys/class/mvgal/.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/compat.h>

#include <mvgal/mvgal_uapi.h>
#include "mvgal_core.h"
#include "mvgal_memory.h"
#include "mvgal_device.h"

#define MVGAL_KERNEL_VERSION "0.2.2"

struct mvgal_gpu_sysfs {
	struct kobject kobj;
	u32 index;
};

struct mvgal_gpu_slot {
	struct mvgal_gpu_info info;
	bool present;
	bool enabled;
	char power_state[8];
	struct mvgal_gpu_sysfs *sysfs;
};

struct mvgal_device {
	struct mutex lock;
	struct mvgal_gpu_slot gpus[MVGAL_UAPI_MAX_GPUS];
	struct mvgal_uapi_stats stats;
	struct notifier_block pci_notifier;
	dev_t devt;
	struct device *class_dev;
	u32 gpu_count;
	u32 topology_generation;
	bool enabled;
	bool notifier_registered;
};

static struct mvgal_device mvgal_dev = {
	.lock = __MUTEX_INITIALIZER(mvgal_dev.lock),
	.enabled = true,
};

static struct cdev mvgal_cdev;
static struct class *mvgal_class;

static bool mvgal_is_display_controller(const struct pci_dev *pdev)
{
	const u32 class_code = pdev->class >> 8;

	return (class_code == PCI_CLASS_DISPLAY_VGA) ||
	       (class_code == PCI_CLASS_DISPLAY_3D) ||
	       (class_code == PCI_CLASS_DISPLAY_OTHER);
}

static const char *mvgal_vendor_name(u16 vendor_id)
{
	switch (vendor_id) {
	case MVGAL_UAPI_VENDOR_AMD:
		return "AMD";
	case MVGAL_UAPI_VENDOR_NVIDIA:
		return "NVIDIA";
	case MVGAL_UAPI_VENDOR_INTEL:
		return "Intel";
	case MVGAL_UAPI_VENDOR_MOORE_THREADS:
		return "Moore Threads";
	default:
		return "Unknown";
	}
}

static u32 mvgal_vendor_mask(u16 vendor_id)
{
	switch (vendor_id) {
	case MVGAL_UAPI_VENDOR_AMD:
		return MVGAL_UAPI_VENDOR_MASK_AMD;
	case MVGAL_UAPI_VENDOR_NVIDIA:
		return MVGAL_UAPI_VENDOR_MASK_NVIDIA;
	case MVGAL_UAPI_VENDOR_INTEL:
		return MVGAL_UAPI_VENDOR_MASK_INTEL;
	case MVGAL_UAPI_VENDOR_MOORE_THREADS:
		return MVGAL_UAPI_VENDOR_MASK_MOORE_THREADS;
	default:
		return MVGAL_UAPI_VENDOR_MASK_OTHER;
	}
}

static void mvgal_fill_link_info(struct pci_dev *pdev, struct mvgal_gpu_info *info)
{
	u16 link_status = 0;
	int ret;

	if (!pci_is_pcie(pdev))
		return;

	info->flags |= MVGAL_UAPI_GPU_FLAG_PCIE;

	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &link_status);
	if (ret != PCIBIOS_SUCCESSFUL)
		return;

	{
		u16 gen = link_status & PCI_EXP_LNKSTA_CLS;

		if (gen < 1)
			gen = 1;
		else if (gen > 6)
			gen = 6;

		info->pcie_link_gen = gen;
	}

	{
		u16 width = (link_status & PCI_EXP_LNKSTA_NLW) >> 4;

		if (width < 1)
			width = 1;
		else if (width > 16)
			width = 16;

		info->pcie_link_width = width;
	}
}

static void mvgal_fill_bar_info(struct pci_dev *pdev, struct mvgal_gpu_info *info)
{
	int bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		resource_size_t len = pci_resource_len(pdev, bar);
		unsigned long flags = pci_resource_flags(pdev, bar);

		if ((len == 0) || ((flags & IORESOURCE_MEM) == 0))
			continue;

		if ((flags & IORESOURCE_PREFETCH) != 0) {
			if (len > info->largest_prefetchable_bar_bytes)
				info->largest_prefetchable_bar_bytes = len;
		} else if (len > info->largest_mmio_bar_bytes) {
			info->largest_mmio_bar_bytes = len;
		}
	}
}

static void mvgal_fill_gpu_info(struct pci_dev *pdev, u32 index,
				struct mvgal_gpu_info *info)
{
	const char *driver_name = dev_driver_string(&pdev->dev);
	const u32 domain = pci_domain_nr(pdev->bus);
	const u32 class_code = pdev->class >> 8;
	int numa_node = dev_to_node(&pdev->dev);

	memset(info, 0, sizeof(*info));

	info->index = index;
	info->vendor_id = pdev->vendor;
	info->device_id = pdev->device;
	info->subsystem_vendor_id = pdev->subsystem_vendor;
	info->subsystem_device_id = pdev->subsystem_device;
	info->pci_domain = domain;
	info->pci_bus = pdev->bus->number;
	info->pci_slot = PCI_SLOT(pdev->devfn);
	info->pci_function = PCI_FUNC(pdev->devfn);
	info->class_code = class_code;
	info->numa_node = (numa_node < 0) ? U32_MAX : (u32)numa_node;

	if (mvgal_is_display_controller(pdev))
		info->flags |= MVGAL_UAPI_GPU_FLAG_DISPLAY_CLASS;

	if (pdev->driver != NULL)
		info->flags |= MVGAL_UAPI_GPU_FLAG_DRIVER_BOUND;

	if (pdev->multifunction)
		info->flags |= MVGAL_UAPI_GPU_FLAG_MULTIFUNCTION;

	if (pdev->is_virtfn)
		info->flags |= MVGAL_UAPI_GPU_FLAG_VIRTUAL_FUNCTION;

	if ((pdev->vendor == MVGAL_UAPI_VENDOR_INTEL) &&
	    (class_code == PCI_CLASS_DISPLAY_VGA))
		info->flags |= MVGAL_UAPI_GPU_FLAG_INTEGRATED_GUESS;

	mvgal_fill_link_info(pdev, info);
	mvgal_fill_bar_info(pdev, info);

	strscpy(info->driver,
		(driver_name != NULL) ? driver_name : "unbound",
		sizeof(info->driver));

	snprintf(info->bdf, sizeof(info->bdf), "%04x:%02x:%02x.%u",
		 domain, info->pci_bus, info->pci_slot, info->pci_function);

	snprintf(info->name, sizeof(info->name), "%s GPU [%04x:%04x]",
		 mvgal_vendor_name(pdev->vendor), pdev->vendor, pdev->device);
}

static void mvgal_gpu_sysfs_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct mvgal_gpu_sysfs, kobj));
}

static void mvgal_sysfs_gpu_remove_locked(struct mvgal_device *mvdev, u32 index)
{
	struct mvgal_gpu_sysfs *gs;

	if (index >= MVGAL_UAPI_MAX_GPUS)
		return;

	gs = mvdev->gpus[index].sysfs;
	mvdev->gpus[index].sysfs = NULL;
	if (gs != NULL)
		kobject_put(&gs->kobj);
}

static void mvgal_sysfs_gpus_remove_all_locked(struct mvgal_device *mvdev)
{
	u32 i;

	for (i = 0; i < MVGAL_UAPI_MAX_GPUS; i++)
		mvgal_sysfs_gpu_remove_locked(mvdev, i);
}

static ssize_t mvgal_gpu_enabled_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	struct mvgal_gpu_sysfs *gs = container_of(kobj, struct mvgal_gpu_sysfs, kobj);
	bool enabled;

	mutex_lock(&mvgal_dev.lock);
	if (gs->index >= mvgal_dev.gpu_count) {
		mutex_unlock(&mvgal_dev.lock);
		return -EINVAL;
	}
	enabled = mvgal_dev.gpus[gs->index].enabled;
	mutex_unlock(&mvgal_dev.lock);

	return sysfs_emit(buf, "%d\n", enabled ? 1 : 0);
}

static ssize_t mvgal_gpu_enabled_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	struct mvgal_gpu_sysfs *gs = container_of(kobj, struct mvgal_gpu_sysfs, kobj);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret != 0 || val > 1)
		return -EINVAL;

	mutex_lock(&mvgal_dev.lock);
	if (gs->index >= mvgal_dev.gpu_count) {
		mutex_unlock(&mvgal_dev.lock);
		return -EINVAL;
	}
	mvgal_dev.gpus[gs->index].enabled = (val != 0);
	mutex_unlock(&mvgal_dev.lock);

	return count;
}

static ssize_t mvgal_gpu_pci_path_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct mvgal_gpu_sysfs *gs = container_of(kobj, struct mvgal_gpu_sysfs, kobj);
	ssize_t len;

	mutex_lock(&mvgal_dev.lock);
	if (gs->index >= mvgal_dev.gpu_count) {
		mutex_unlock(&mvgal_dev.lock);
		return -EINVAL;
	}
	len = sysfs_emit(buf, "%s\n", mvgal_dev.gpus[gs->index].info.bdf);
	mutex_unlock(&mvgal_dev.lock);

	return len;
}

static ssize_t mvgal_gpu_power_state_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	struct mvgal_gpu_sysfs *gs = container_of(kobj, struct mvgal_gpu_sysfs, kobj);
	ssize_t len;

	mutex_lock(&mvgal_dev.lock);
	if (gs->index >= mvgal_dev.gpu_count) {
		mutex_unlock(&mvgal_dev.lock);
		return -EINVAL;
	}
	len = sysfs_emit(buf, "%s\n", mvgal_dev.gpus[gs->index].power_state);
	mutex_unlock(&mvgal_dev.lock);

	return len;
}

static ssize_t mvgal_gpu_power_state_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	struct mvgal_gpu_sysfs *gs = container_of(kobj, struct mvgal_gpu_sysfs, kobj);
	char state[8];
	size_t copy_len;

	if (count == 0)
		return -EINVAL;

	copy_len = min(count, sizeof(state) - 1);
	memcpy(state, buf, copy_len);
	state[copy_len] = '\0';
	state[strcspn(state, "\n")] = '\0';

	if (strcmp(state, "auto") != 0 && strcmp(state, "on") != 0 &&
	    strcmp(state, "off") != 0)
		return -EINVAL;

	mutex_lock(&mvgal_dev.lock);
	if (gs->index >= mvgal_dev.gpu_count) {
		mutex_unlock(&mvgal_dev.lock);
		return -EINVAL;
	}
	strscpy(mvgal_dev.gpus[gs->index].power_state, state,
		sizeof(mvgal_dev.gpus[gs->index].power_state));
	mutex_unlock(&mvgal_dev.lock);

	return count;
}

static struct kobj_attribute mvgal_gpu_enabled_attr =
	__ATTR(enabled, 0644, mvgal_gpu_enabled_show, mvgal_gpu_enabled_store);
static struct kobj_attribute mvgal_gpu_pci_path_attr =
	__ATTR(pci_path, 0444, mvgal_gpu_pci_path_show, NULL);
static struct kobj_attribute mvgal_gpu_power_state_attr =
	__ATTR(power_state, 0644, mvgal_gpu_power_state_show,
	       mvgal_gpu_power_state_store);

static struct attribute *mvgal_gpu_attrs[] = {
	&mvgal_gpu_enabled_attr.attr,
	&mvgal_gpu_pci_path_attr.attr,
	&mvgal_gpu_power_state_attr.attr,
	NULL,
};

static const struct attribute_group mvgal_gpu_attr_group = {
	.attrs = mvgal_gpu_attrs,
};

static struct kobj_type mvgal_gpu_ktype = {
	.release = mvgal_gpu_sysfs_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

static int mvgal_sysfs_gpu_create_locked(struct mvgal_device *mvdev, u32 index)
{
	struct kobject *parent;
	struct mvgal_gpu_sysfs *gs;
	char name[16];
	int ret;

	if (mvdev->class_dev == NULL || index >= mvdev->gpu_count)
		return -EINVAL;

	if (mvdev->gpus[index].sysfs != NULL)
		return 0;

	gs = kzalloc(sizeof(*gs), GFP_KERNEL);
	if (gs == NULL)
		return -ENOMEM;

	gs->index = index;
	parent = &mvdev->class_dev->kobj;
	snprintf(name, sizeof(name), "gpu%u", index);

	ret = kobject_init_and_add(&gs->kobj, &mvgal_gpu_ktype, parent, "%s", name);
	if (ret != 0) {
		kfree(gs);
		return ret;
	}

	ret = sysfs_create_group(&gs->kobj, &mvgal_gpu_attr_group);
	if (ret != 0) {
		kobject_put(&gs->kobj);
		return ret;
	}

	mvdev->gpus[index].sysfs = gs;
	return 0;
}

static void mvgal_sysfs_gpus_refresh_locked(struct mvgal_device *mvdev)
{
	u32 i;

	mvgal_sysfs_gpus_remove_all_locked(mvdev);
	for (i = 0; i < mvdev->gpu_count; i++)
		mvgal_sysfs_gpu_create_locked(mvdev, i);
}

static ssize_t mvgal_gpu_count_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u32 count;

	mutex_lock(&mvgal_dev.lock);
	count = mvgal_dev.gpu_count;
	mutex_unlock(&mvgal_dev.lock);

	return sysfs_emit(buf, "%u\n", count);
}

static ssize_t mvgal_topology_generation_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	u32 gen;

	mutex_lock(&mvgal_dev.lock);
	gen = mvgal_dev.topology_generation;
	mutex_unlock(&mvgal_dev.lock);

	return sysfs_emit(buf, "%u\n", gen);
}

static void mvgal_rescan_locked(struct mvgal_device *mvdev);

static ssize_t mvgal_rescan_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret != 0 || val != 1)
		return -EINVAL;

	mutex_lock(&mvgal_dev.lock);
	mvgal_rescan_locked(&mvgal_dev);
	mvgal_sysfs_gpus_refresh_locked(&mvgal_dev);
	mutex_unlock(&mvgal_dev.lock);

	return count;
}

static DEVICE_ATTR(gpu_count, 0444, mvgal_gpu_count_show, NULL);
static DEVICE_ATTR(topology_generation, 0444, mvgal_topology_generation_show, NULL);
static DEVICE_ATTR(rescan, 0200, NULL, mvgal_rescan_store);

static struct attribute *mvgal_dev_attrs[] = {
	&dev_attr_gpu_count.attr,
	&dev_attr_topology_generation.attr,
	&dev_attr_rescan.attr,
	NULL,
};

static const struct attribute_group mvgal_dev_attr_group = {
	.attrs = mvgal_dev_attrs,
};

static const struct attribute_group *mvgal_dev_attr_groups[] = {
	&mvgal_dev_attr_group,
	NULL,
};

static void mvgal_rescan_locked(struct mvgal_device *mvdev)
{
	struct pci_dev *pdev = NULL;
	u32 index = 0;

	memset(mvdev->gpus, 0, sizeof(mvdev->gpus));

	while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
		if (!mvgal_is_display_controller(pdev))
			continue;

		if (index >= MVGAL_UAPI_MAX_GPUS) {
			pr_warn("mvgal: GPU count exceeds UAPI limit (%u)\n",
				MVGAL_UAPI_MAX_GPUS);
			break;
		}

		mvgal_fill_gpu_info(pdev, index, &mvdev->gpus[index].info);
		mvdev->gpus[index].present = true;
		mvdev->gpus[index].enabled = true;
		strscpy(mvdev->gpus[index].power_state, "auto",
			sizeof(mvdev->gpus[index].power_state));
		index++;
	}

	mvdev->gpu_count = index;
	mvdev->topology_generation++;
	mvdev->stats.rescans++;
}

static void mvgal_build_caps_locked(struct mvgal_device *mvdev,
				    struct mvgal_uapi_caps *caps)
{
	u32 i;

	memset(caps, 0, sizeof(*caps));

	caps->enabled = mvdev->enabled ? 1U : 0U;
	caps->gpu_count = mvdev->gpu_count;
	caps->topology_generation = mvdev->topology_generation;
	caps->feature_flags = MVGAL_UAPI_FEATURE_ENUMERATION |
			      MVGAL_UAPI_FEATURE_PCI_TOPOLOGY |
			      MVGAL_UAPI_FEATURE_HOTPLUG_MONITOR |
			      MVGAL_UAPI_FEATURE_RESCAN |
			      MVGAL_UAPI_FEATURE_READ_ONLY_SKELETON |
			      MVGAL_UAPI_FEATURE_FUTURE_DMABUF |
			      MVGAL_UAPI_FEATURE_FUTURE_SUBMISSION;

	for (i = 0; i < mvdev->gpu_count; i++) {
		const struct mvgal_gpu_info *info = &mvdev->gpus[i].info;

		caps->vendor_mask |= mvgal_vendor_mask((u16)info->vendor_id);
		if (info->pcie_link_gen > caps->max_pcie_link_gen)
			caps->max_pcie_link_gen = info->pcie_link_gen;
		if (info->pcie_link_width > caps->max_pcie_link_width)
			caps->max_pcie_link_width = info->pcie_link_width;
		if (info->largest_prefetchable_bar_bytes >
		    caps->largest_prefetchable_bar_bytes)
			caps->largest_prefetchable_bar_bytes =
				info->largest_prefetchable_bar_bytes;
		if (info->largest_mmio_bar_bytes > caps->largest_mmio_bar_bytes)
			caps->largest_mmio_bar_bytes = info->largest_mmio_bar_bytes;
	}
}

static void mvgal_trigger_rescan(struct mvgal_device *mvdev)
{
	mutex_lock(&mvdev->lock);
	mvgal_rescan_locked(mvdev);
	mvgal_sysfs_gpus_refresh_locked(mvdev);
	mutex_unlock(&mvdev->lock);
}

static int mvgal_pci_bus_notifier(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct device *dev = data;
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return NOTIFY_DONE;

	pdev = to_pci_dev(dev);
	if (!mvgal_is_display_controller(pdev))
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
	case BUS_NOTIFY_DEL_DEVICE:
	case BUS_NOTIFY_BOUND_DRIVER:
	case BUS_NOTIFY_UNBOUND_DRIVER:
		mutex_lock(&mvgal_dev.lock);
		mvgal_dev.stats.hotplug_events++;
		mvgal_rescan_locked(&mvgal_dev);
		mvgal_sysfs_gpus_refresh_locked(&mvgal_dev);
		mutex_unlock(&mvgal_dev.lock);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static int mvgal_open(struct inode *inode, struct file *file)
{
	mutex_lock(&mvgal_dev.lock);
	mvgal_dev.stats.open_count++;
	mutex_unlock(&mvgal_dev.lock);

	return nonseekable_open(inode, file);
}

static int mvgal_release(struct inode *inode, struct file *file)
{
	mutex_lock(&mvgal_dev.lock);
	mvgal_dev.stats.release_count++;
	mutex_unlock(&mvgal_dev.lock);

	return 0;
}

static long mvgal_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mvgal_uapi_version version = {
		.major = MVGAL_UAPI_VERSION_MAJOR,
		.minor = MVGAL_UAPI_VERSION_MINOR,
		.patch = MVGAL_UAPI_VERSION_PATCH,
		.feature_flags = MVGAL_UAPI_FEATURE_ENUMERATION |
				 MVGAL_UAPI_FEATURE_PCI_TOPOLOGY |
				 MVGAL_UAPI_FEATURE_HOTPLUG_MONITOR |
				 MVGAL_UAPI_FEATURE_RESCAN |
				 MVGAL_UAPI_FEATURE_READ_ONLY_SKELETON |
				 MVGAL_UAPI_FEATURE_FUTURE_DMABUF |
				 MVGAL_UAPI_FEATURE_FUTURE_SUBMISSION,
	};
	struct mvgal_uapi_gpu_count count = {0};
	struct mvgal_uapi_gpu_query query = {0};
	struct mvgal_uapi_stats stats = {0};
	struct mvgal_uapi_caps caps = {0};
	void __user *uarg = (void __user *)arg;
	long ret = 0;

	if (_IOC_TYPE(cmd) != MVGAL_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&mvgal_dev.lock);
	mvgal_dev.stats.ioctl_count++;

	switch (cmd) {
	case MVGAL_IOC_QUERY_VERSION:
		mutex_unlock(&mvgal_dev.lock);
		if (copy_to_user(uarg, &version, sizeof(version)))
			return -EFAULT;
		return 0;

	case MVGAL_IOC_GET_GPU_COUNT:
		count.gpu_count = mvgal_dev.gpu_count;
		mutex_unlock(&mvgal_dev.lock);
		if (copy_to_user(uarg, &count, sizeof(count)))
			return -EFAULT;
		return 0;

	case MVGAL_IOC_GET_GPU_INFO:
		if (copy_from_user(&query, uarg, sizeof(query))) {
			ret = -EFAULT;
			break;
		}
		if (query.index >= mvgal_dev.gpu_count) {
			ret = -EINVAL;
			break;
		}
		query.info = mvgal_dev.gpus[query.index].info;
		mutex_unlock(&mvgal_dev.lock);
		if (copy_to_user(uarg, &query, sizeof(query)))
			return -EFAULT;
		return 0;

	case MVGAL_IOC_ENABLE:
		mvgal_dev.enabled = true;
		mutex_unlock(&mvgal_dev.lock);
		return 0;

	case MVGAL_IOC_DISABLE:
		mvgal_dev.enabled = false;
		mutex_unlock(&mvgal_dev.lock);
		return 0;

	case MVGAL_IOC_GET_STATS:
		stats = mvgal_dev.stats;
		mutex_unlock(&mvgal_dev.lock);
		if (copy_to_user(uarg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;

	case MVGAL_IOC_GET_CAPS:
		mvgal_build_caps_locked(&mvgal_dev, &caps);
		mutex_unlock(&mvgal_dev.lock);
		if (copy_to_user(uarg, &caps, sizeof(caps)))
			return -EFAULT;
		return 0;

	case MVGAL_IOC_RESCAN:
		mvgal_rescan_locked(&mvgal_dev);
		mvgal_sysfs_gpus_refresh_locked(&mvgal_dev);
		mutex_unlock(&mvgal_dev.lock);
		return 0;

	case MVGAL_IOC_EXPORT_DMABUF: {
#ifdef MVGAL_FULL_STACK
		struct mvgal_export_dmabuf_args dmabuf_args;
		if (copy_from_user(&dmabuf_args, uarg, sizeof(dmabuf_args)))
			return -EFAULT;
		ret = mvgal_ioctl_export_dmabuf(NULL, &dmabuf_args, NULL);
		if (ret == 0 && copy_to_user(uarg, &dmabuf_args, sizeof(dmabuf_args)))
			return -EFAULT;
#else
		mvgal_dev.stats.unsupported_dmabuf_exports++;
		ret = -EOPNOTSUPP;
#endif
		break;
	}

	case MVGAL_IOC_IMPORT_DMABUF: {
#ifdef MVGAL_FULL_STACK
		struct mvgal_import_dmabuf_args dmabuf_args;
		if (copy_from_user(&dmabuf_args, uarg, sizeof(dmabuf_args)))
			return -EFAULT;
		ret = mvgal_ioctl_import_dmabuf(NULL, &dmabuf_args, NULL);
		if (ret == 0 && copy_to_user(uarg, &dmabuf_args, sizeof(dmabuf_args)))
			return -EFAULT;
#else
		mvgal_dev.stats.unsupported_dmabuf_imports++;
		ret = -EOPNOTSUPP;
#endif
		break;
	}

	case MVGAL_IOC_ALLOC_CROSS_VENDOR: {
#ifdef MVGAL_FULL_STACK
		struct mvgal_alloc_memory_args alloc_args;
		if (copy_from_user(&alloc_args, uarg, sizeof(alloc_args)))
			return -EFAULT;
		ret = mvgal_ioctl_alloc_memory(NULL, &alloc_args, NULL);
		if (ret == 0 && copy_to_user(uarg, &alloc_args, sizeof(alloc_args)))
			return -EFAULT;
#else
		mvgal_dev.stats.unsupported_cross_vendor_allocs++;
		ret = -EOPNOTSUPP;
#endif
		break;
	}

	case MVGAL_IOC_FREE_CROSS_VENDOR: {
#ifdef MVGAL_FULL_STACK
		struct mvgal_free_memory_args free_args;
		if (copy_from_user(&free_args, uarg, sizeof(free_args)))
			return -EFAULT;
		ret = mvgal_ioctl_free_memory(NULL, &free_args, NULL);
#else
		mvgal_dev.stats.unsupported_cross_vendor_allocs++;
		ret = -EOPNOTSUPP;
#endif
		break;
	}

	default:
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&mvgal_dev.lock);
	return ret;
}

static const struct file_operations mvgal_fops = {
	.owner = THIS_MODULE,
	.open = mvgal_open,
	.release = mvgal_release,
	.unlocked_ioctl = mvgal_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = noop_llseek,
};

static int __init mvgal_module_init(void)
{
	struct device *dev;
	int ret;

	pr_info("mvgal: loading kernel module %s\n", MVGAL_KERNEL_VERSION);

	ret = alloc_chrdev_region(&mvgal_dev.devt, 0, 1, "mvgal");
	if (ret < 0) {
		pr_err("mvgal: alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}

	cdev_init(&mvgal_cdev, &mvgal_fops);
	mvgal_cdev.owner = THIS_MODULE;

	ret = cdev_add(&mvgal_cdev, mvgal_dev.devt, 1);
	if (ret < 0) {
		pr_err("mvgal: cdev_add failed: %d\n", ret);
		goto err_chrdev;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	mvgal_class = class_create("mvgal");
#else
	mvgal_class = class_create(THIS_MODULE, "mvgal");
#endif
	if (IS_ERR(mvgal_class)) {
		ret = PTR_ERR(mvgal_class);
		pr_err("mvgal: class_create failed: %d\n", ret);
		goto err_cdev;
	}

	dev = device_create_with_groups(mvgal_class, NULL, mvgal_dev.devt, NULL,
					mvgal_dev_attr_groups, MVGAL_DEVICE_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		pr_err("mvgal: device_create failed: %d\n", ret);
		goto err_class;
	}

	mvgal_dev.class_dev = dev;

	mvgal_trigger_rescan(&mvgal_dev);

	mvgal_dev.pci_notifier.notifier_call = mvgal_pci_bus_notifier;
	ret = bus_register_notifier(&pci_bus_type, &mvgal_dev.pci_notifier);
	if (ret == 0)
		mvgal_dev.notifier_registered = true;
	else
		pr_warn("mvgal: hotplug notifier unavailable: %d\n", ret);

#ifdef MVGAL_FULL_STACK
	{
		extern int mvgal_stack_init(void);

		ret = mvgal_stack_init();
		if (ret < 0)
			pr_warn("mvgal: aggregation stack init failed: %d\n", ret);
	}
#endif

	pr_info("mvgal: /dev/%s ready with %u GPU(s), sysfs at /sys/class/mvgal/%s/\n",
		MVGAL_DEVICE_NAME, mvgal_dev.gpu_count, MVGAL_DEVICE_NAME);
	return 0;

err_class:
	class_destroy(mvgal_class);
	mvgal_class = NULL;
err_cdev:
	cdev_del(&mvgal_cdev);
err_chrdev:
	unregister_chrdev_region(mvgal_dev.devt, 1);
	return ret;
}

static void __exit mvgal_module_exit(void)
{
#ifdef MVGAL_FULL_STACK
	{
		extern void mvgal_stack_exit(void);

		mvgal_stack_exit();
	}
#endif

	if (mvgal_dev.notifier_registered) {
		bus_unregister_notifier(&pci_bus_type, &mvgal_dev.pci_notifier);
		mvgal_dev.notifier_registered = false;
	}

	mutex_lock(&mvgal_dev.lock);
	mvgal_sysfs_gpus_remove_all_locked(&mvgal_dev);
	mutex_unlock(&mvgal_dev.lock);

	if (mvgal_dev.class_dev != NULL) {
		device_destroy(mvgal_class, mvgal_dev.devt);
		mvgal_dev.class_dev = NULL;
	}

	class_destroy(mvgal_class);
	mvgal_class = NULL;

	cdev_del(&mvgal_cdev);
	unregister_chrdev_region(mvgal_dev.devt, 1);

	pr_info("mvgal: kernel module unloaded\n");
}

module_init(mvgal_module_init);
module_exit(mvgal_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MVGAL Project");
MODULE_DESCRIPTION("MVGAL kernel foundation: PCI GPU topology, UAPI, and sysfs");
MODULE_VERSION(MVGAL_KERNEL_VERSION);
