/**
 * @file mvgal_kernel.c
 * @brief MVGAL kernel skeleton
 *
 * This module provides a minimal, read-only Phase 2 kernel foundation:
 * PCI-backed GPU discovery, topology change tracking, and a small ioctl UAPI.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "../../include/mvgal/mvgal_uapi.h"

#define MVGAL_KERNEL_VERSION "0.1.0"

struct mvgal_gpu_slot {
    struct mvgal_gpu_info info;
    bool present;
};

struct mvgal_device {
    struct mutex lock;
    struct mvgal_gpu_slot gpus[MVGAL_UAPI_MAX_GPUS];
    struct mvgal_uapi_stats stats;
    struct notifier_block pci_notifier;
    dev_t devt;
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

    if (!pci_is_pcie(pdev)) {
        return;
    }

    info->flags |= MVGAL_UAPI_GPU_FLAG_PCIE;

    ret = pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &link_status);
    if (ret != PCIBIOS_SUCCESSFUL) {
        return;
    }

    /*
     * The PCIe LNKSTA register encodes the negotiated link speed and width.
     * Defensive clamps are applied here before exposing values to userspace:
     *  - link generation: clamp to [1, 6] (practical PCIe Gen1..Gen6 range)
     *  - link width: clamp to [1, 16] (common max lanes per slot)
     *
     * This avoids exposing obviously invalid values produced by malformed
     * registers or vendor quirks (e.g., very large integers).
     */
    {
        u16 raw_gen = link_status & PCI_EXP_LNKSTA_CLS;
        u16 gen = raw_gen;

        if (gen < 1)
            gen = 1;
        else if (gen > 6)
            gen = 6;

        info->pcie_link_gen = gen;
    }

    {
        u16 raw_width = (link_status & PCI_EXP_LNKSTA_NLW) >> 4;
        u16 width = raw_width;

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

        if ((len == 0) || ((flags & IORESOURCE_MEM) == 0)) {
            continue;
        }

        if ((flags & IORESOURCE_PREFETCH) != 0) {
            if (len > info->largest_prefetchable_bar_bytes) {
                info->largest_prefetchable_bar_bytes = len;
            }
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

    if (mvgal_is_display_controller(pdev)) {
        info->flags |= MVGAL_UAPI_GPU_FLAG_DISPLAY_CLASS;
    }

    if (pdev->driver != NULL) {
        info->flags |= MVGAL_UAPI_GPU_FLAG_DRIVER_BOUND;
    }

    if (pdev->multifunction) {
        info->flags |= MVGAL_UAPI_GPU_FLAG_MULTIFUNCTION;
    }

    if (pdev->is_virtfn) {
        info->flags |= MVGAL_UAPI_GPU_FLAG_VIRTUAL_FUNCTION;
    }

    if ((pdev->vendor == MVGAL_UAPI_VENDOR_INTEL) &&
        (class_code == PCI_CLASS_DISPLAY_VGA)) {
        info->flags |= MVGAL_UAPI_GPU_FLAG_INTEGRATED_GUESS;
    }

    mvgal_fill_link_info(pdev, info);
    mvgal_fill_bar_info(pdev, info);

    strscpy(info->driver,
            (driver_name != NULL) ? driver_name : "unbound",
            sizeof(info->driver));

    snprintf(info->bdf, sizeof(info->bdf), "%04x:%02x:%02x.%u",
             domain,
             info->pci_bus,
             info->pci_slot,
             info->pci_function);

    snprintf(info->name, sizeof(info->name), "%s GPU [%04x:%04x]",
             mvgal_vendor_name(pdev->vendor),
             pdev->vendor,
             pdev->device);
}

static void mvgal_rescan_locked(struct mvgal_device *mvdev)
{
    struct pci_dev *pdev = NULL;
    u32 index = 0;

    memset(mvdev->gpus, 0, sizeof(mvdev->gpus));

    while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
        if (!mvgal_is_display_controller(pdev)) {
            continue;
        }

        if (index >= MVGAL_UAPI_MAX_GPUS) {
            pr_warn("MVGAL: GPU count exceeds UAPI limit (%u)\n",
                    MVGAL_UAPI_MAX_GPUS);
            break;
        }

        mvgal_fill_gpu_info(pdev, index, &mvdev->gpus[index].info);
        mvdev->gpus[index].present = true;
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
        if (info->pcie_link_gen > caps->max_pcie_link_gen) {
            caps->max_pcie_link_gen = info->pcie_link_gen;
        }
        if (info->pcie_link_width > caps->max_pcie_link_width) {
            caps->max_pcie_link_width = info->pcie_link_width;
        }
        if (info->largest_prefetchable_bar_bytes >
            caps->largest_prefetchable_bar_bytes) {
            caps->largest_prefetchable_bar_bytes =
                info->largest_prefetchable_bar_bytes;
        }
        if (info->largest_mmio_bar_bytes > caps->largest_mmio_bar_bytes) {
            caps->largest_mmio_bar_bytes = info->largest_mmio_bar_bytes;
        }
    }
}

static void mvgal_trigger_rescan(struct mvgal_device *mvdev)
{
    mutex_lock(&mvdev->lock);
    mvgal_rescan_locked(mvdev);
    mutex_unlock(&mvdev->lock);
}

static int mvgal_pci_bus_notifier(struct notifier_block *nb,
                                  unsigned long action, void *data)
{
    struct device *dev = data;
    struct pci_dev *pdev;

    if (!dev_is_pci(dev)) {
        return NOTIFY_DONE;
    }

    pdev = to_pci_dev(dev);
    if (!mvgal_is_display_controller(pdev)) {
        return NOTIFY_DONE;
    }

    switch (action) {
    case BUS_NOTIFY_ADD_DEVICE:
    case BUS_NOTIFY_DEL_DEVICE:
    case BUS_NOTIFY_BOUND_DRIVER:
    case BUS_NOTIFY_UNBOUND_DRIVER:
        mutex_lock(&mvgal_dev.lock);
        mvgal_dev.stats.hotplug_events++;
        mvgal_rescan_locked(&mvgal_dev);
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

    return 0;
}

static int mvgal_release(struct inode *inode, struct file *file)
{
    mutex_lock(&mvgal_dev.lock);
    mvgal_dev.stats.release_count++;
    mutex_unlock(&mvgal_dev.lock);

    return 0;
}

static ssize_t mvgal_read(struct file *file, char __user *buf,
                          size_t count, loff_t *ppos)
{
    return 0;
}

static ssize_t mvgal_write(struct file *file, const char __user *buf,
                           size_t count, loff_t *ppos)
{
    return -EOPNOTSUPP;
}

static unsigned int mvgal_poll(struct file *file, poll_table *wait)
{
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
    long ret = 0;

    if (_IOC_TYPE(cmd) != MVGAL_IOC_MAGIC) {
        return -ENOTTY;
    }

    mutex_lock(&mvgal_dev.lock);
    mvgal_dev.stats.ioctl_count++;

    switch (cmd) {
    case MVGAL_IOC_QUERY_VERSION:
        mutex_unlock(&mvgal_dev.lock);
        if (copy_to_user((void __user *)arg, &version, sizeof(version))) {
            return -EFAULT;
        }
        return 0;

    case MVGAL_IOC_GET_GPU_COUNT:
        count.gpu_count = mvgal_dev.gpu_count;
        mutex_unlock(&mvgal_dev.lock);
        if (copy_to_user((void __user *)arg, &count, sizeof(count))) {
            return -EFAULT;
        }
        return 0;

    case MVGAL_IOC_GET_GPU_INFO:
        if (copy_from_user(&query, (void __user *)arg, sizeof(query))) {
            ret = -EFAULT;
            break;
        }
        if (query.index >= mvgal_dev.gpu_count) {
            ret = -EINVAL;
            break;
        }
        query.info = mvgal_dev.gpus[query.index].info;
        mutex_unlock(&mvgal_dev.lock);
        if (copy_to_user((void __user *)arg, &query, sizeof(query))) {
            return -EFAULT;
        }
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
        if (copy_to_user((void __user *)arg, &stats, sizeof(stats))) {
            return -EFAULT;
        }
        return 0;

    case MVGAL_IOC_GET_CAPS:
        mvgal_build_caps_locked(&mvgal_dev, &caps);
        mutex_unlock(&mvgal_dev.lock);
        if (copy_to_user((void __user *)arg, &caps, sizeof(caps))) {
            return -EFAULT;
        }
        return 0;

    case MVGAL_IOC_RESCAN:
        mvgal_rescan_locked(&mvgal_dev);
        mutex_unlock(&mvgal_dev.lock);
        return 0;

    case MVGAL_IOC_EXPORT_DMABUF:
        mvgal_dev.stats.unsupported_dmabuf_exports++;
        ret = -EOPNOTSUPP;
        break;

    case MVGAL_IOC_IMPORT_DMABUF:
        mvgal_dev.stats.unsupported_dmabuf_imports++;
        ret = -EOPNOTSUPP;
        break;

    case MVGAL_IOC_ALLOC_CROSS_VENDOR:
    case MVGAL_IOC_FREE_CROSS_VENDOR:
        mvgal_dev.stats.unsupported_cross_vendor_allocs++;
        ret = -EOPNOTSUPP;
        break;

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
    .read = mvgal_read,
    .write = mvgal_write,
    .poll = mvgal_poll,
    .unlocked_ioctl = mvgal_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = mvgal_ioctl,
#endif
};

static int __init mvgal_module_init(void)
{
    int ret;

    pr_info("MVGAL: loading kernel skeleton %s\n", MVGAL_KERNEL_VERSION);

    ret = alloc_chrdev_region(&mvgal_dev.devt, 0, 1, "mvgal");
    if (ret < 0) {
        pr_err("MVGAL: alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    cdev_init(&mvgal_cdev, &mvgal_fops);
    mvgal_cdev.owner = THIS_MODULE;

    ret = cdev_add(&mvgal_cdev, mvgal_dev.devt, 1);
    if (ret < 0) {
        pr_err("MVGAL: cdev_add failed: %d\n", ret);
        goto err_chrdev;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    mvgal_class = class_create("mvgal");
#else
    mvgal_class = class_create(THIS_MODULE, "mvgal");
#endif
    if (IS_ERR(mvgal_class)) {
        ret = PTR_ERR(mvgal_class);
        pr_err("MVGAL: class_create failed: %d\n", ret);
        goto err_cdev;
    }

    if (IS_ERR(device_create(mvgal_class, NULL, mvgal_dev.devt, NULL,
                             MVGAL_DEVICE_NAME))) {
        ret = -ENODEV;
        pr_err("MVGAL: device_create failed\n");
        goto err_class;
    }

    mvgal_trigger_rescan(&mvgal_dev);

    mvgal_dev.pci_notifier.notifier_call = mvgal_pci_bus_notifier;
    ret = bus_register_notifier(&pci_bus_type, &mvgal_dev.pci_notifier);
    if (ret == 0) {
        mvgal_dev.notifier_registered = true;
    } else {
        pr_warn("MVGAL: hotplug notifier unavailable: %d\n", ret);
    }

    pr_info("MVGAL: /dev/%s ready with %u GPU(s)\n",
            MVGAL_DEVICE_NAME, mvgal_dev.gpu_count);
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
    if (mvgal_dev.notifier_registered) {
        bus_unregister_notifier(&pci_bus_type, &mvgal_dev.pci_notifier);
        mvgal_dev.notifier_registered = false;
    }

    device_destroy(mvgal_class, mvgal_dev.devt);
    class_destroy(mvgal_class);
    mvgal_class = NULL;

    cdev_del(&mvgal_cdev);
    unregister_chrdev_region(mvgal_dev.devt, 1);

    pr_info("MVGAL: kernel skeleton unloaded\n");
}

module_init(mvgal_module_init);
module_exit(mvgal_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AxoGM");
MODULE_DESCRIPTION("MVGAL kernel skeleton for PCI GPU topology and ioctl queries");
MODULE_VERSION(MVGAL_KERNEL_VERSION);
