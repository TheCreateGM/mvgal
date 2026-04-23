/**
 * @file mvgal_uapi.h
 * @brief Shared MVGAL kernel/userspace UAPI definitions
 */

#ifndef MVGAL_UAPI_H
#define MVGAL_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define MVGAL_UAPI_VERSION_MAJOR 0U
#define MVGAL_UAPI_VERSION_MINOR 1U
#define MVGAL_UAPI_VERSION_PATCH 0U

#define MVGAL_DEVICE_NAME "mvgal0"

#define MVGAL_UAPI_MAX_GPUS 32U
#define MVGAL_UAPI_NAME_LEN 128U
#define MVGAL_UAPI_DRIVER_LEN 32U
#define MVGAL_UAPI_BDF_LEN 16U

enum mvgal_uapi_vendor_id {
    MVGAL_UAPI_VENDOR_UNKNOWN = 0x0000,
    MVGAL_UAPI_VENDOR_AMD = 0x1002,
    MVGAL_UAPI_VENDOR_NVIDIA = 0x10DE,
    MVGAL_UAPI_VENDOR_INTEL = 0x8086,
    MVGAL_UAPI_VENDOR_MOORE_THREADS = 0x1ED5,
};

enum mvgal_uapi_vendor_mask {
    MVGAL_UAPI_VENDOR_MASK_AMD = 1U << 0,
    MVGAL_UAPI_VENDOR_MASK_NVIDIA = 1U << 1,
    MVGAL_UAPI_VENDOR_MASK_INTEL = 1U << 2,
    MVGAL_UAPI_VENDOR_MASK_MOORE_THREADS = 1U << 3,
    MVGAL_UAPI_VENDOR_MASK_OTHER = 1U << 31,
};

enum mvgal_uapi_feature_flags {
    MVGAL_UAPI_FEATURE_ENUMERATION = 1U << 0,
    MVGAL_UAPI_FEATURE_PCI_TOPOLOGY = 1U << 1,
    MVGAL_UAPI_FEATURE_HOTPLUG_MONITOR = 1U << 2,
    MVGAL_UAPI_FEATURE_RESCAN = 1U << 3,
    MVGAL_UAPI_FEATURE_READ_ONLY_SKELETON = 1U << 4,
    MVGAL_UAPI_FEATURE_FUTURE_DMABUF = 1U << 5,
    MVGAL_UAPI_FEATURE_FUTURE_SUBMISSION = 1U << 6,
};

enum mvgal_uapi_gpu_flags {
    MVGAL_UAPI_GPU_FLAG_DISPLAY_CLASS = 1U << 0,
    MVGAL_UAPI_GPU_FLAG_PCIE = 1U << 1,
    MVGAL_UAPI_GPU_FLAG_DRIVER_BOUND = 1U << 2,
    MVGAL_UAPI_GPU_FLAG_MULTIFUNCTION = 1U << 3,
    MVGAL_UAPI_GPU_FLAG_VIRTUAL_FUNCTION = 1U << 4,
    MVGAL_UAPI_GPU_FLAG_INTEGRATED_GUESS = 1U << 5,
};

struct mvgal_uapi_version {
    __u32 major;
    __u32 minor;
    __u32 patch;
    __u32 feature_flags;
};

struct mvgal_uapi_stats {
    __u64 open_count;
    __u64 release_count;
    __u64 ioctl_count;
    __u64 rescans;
    __u64 hotplug_events;
    __u64 unsupported_dmabuf_exports;
    __u64 unsupported_dmabuf_imports;
    __u64 unsupported_cross_vendor_allocs;
};

struct mvgal_uapi_caps {
    __u32 enabled;
    __u32 gpu_count;
    __u32 vendor_mask;
    __u32 topology_generation;
    __u32 max_pcie_link_gen;
    __u32 max_pcie_link_width;
    __u32 feature_flags;
    __u32 reserved0;
    __u64 largest_prefetchable_bar_bytes;
    __u64 largest_mmio_bar_bytes;
    __u64 reserved[4];
};

struct mvgal_gpu_info {
    __u32 index;
    __u32 vendor_id;
    __u32 device_id;
    __u32 subsystem_vendor_id;
    __u32 subsystem_device_id;
    __u32 pci_domain;
    __u32 pci_bus;
    __u32 pci_slot;
    __u32 pci_function;
    __u32 class_code;
    __u32 numa_node;
    __u32 pcie_link_gen;
    __u32 pcie_link_width;
    __u32 flags;
    __u32 reserved0;
    __u64 largest_prefetchable_bar_bytes;
    __u64 largest_mmio_bar_bytes;
    char name[MVGAL_UAPI_NAME_LEN];
    char driver[MVGAL_UAPI_DRIVER_LEN];
    char bdf[MVGAL_UAPI_BDF_LEN];
    __u8 reserved[16];
};

struct mvgal_uapi_gpu_count {
    __u32 gpu_count;
    __u32 reserved0;
};

struct mvgal_uapi_gpu_query {
    __u32 index;
    __u32 reserved0;
    struct mvgal_gpu_info info;
};

struct mvgal_uapi_dmabuf_request {
    __s32 fd;
    __s32 gpu_index;
    __u32 flags;
    __u32 reserved0;
    __u64 size;
    __u64 reserved[4];
};

struct mvgal_uapi_cross_vendor_alloc {
    __u32 src_vendor_id;
    __u32 dst_vendor_id;
    __u32 flags;
    __u32 reserved0;
    __u64 size;
    __u64 token;
    __u64 reserved[4];
};

#define MVGAL_IOC_MAGIC 'M'

#define MVGAL_IOC_QUERY_VERSION _IOR(MVGAL_IOC_MAGIC, 0x00, struct mvgal_uapi_version)
#define MVGAL_IOC_GET_GPU_COUNT _IOR(MVGAL_IOC_MAGIC, 0x01, struct mvgal_uapi_gpu_count)
#define MVGAL_IOC_GET_GPU_INFO _IOWR(MVGAL_IOC_MAGIC, 0x02, struct mvgal_uapi_gpu_query)
#define MVGAL_IOC_ENABLE _IO(MVGAL_IOC_MAGIC, 0x03)
#define MVGAL_IOC_DISABLE _IO(MVGAL_IOC_MAGIC, 0x04)
#define MVGAL_IOC_GET_STATS _IOR(MVGAL_IOC_MAGIC, 0x05, struct mvgal_uapi_stats)
#define MVGAL_IOC_GET_CAPS _IOR(MVGAL_IOC_MAGIC, 0x06, struct mvgal_uapi_caps)
#define MVGAL_IOC_RESCAN _IO(MVGAL_IOC_MAGIC, 0x07)

#define MVGAL_IOC_EXPORT_DMABUF _IOWR(MVGAL_IOC_MAGIC, 0x10, struct mvgal_uapi_dmabuf_request)
#define MVGAL_IOC_IMPORT_DMABUF _IOWR(MVGAL_IOC_MAGIC, 0x11, struct mvgal_uapi_dmabuf_request)
#define MVGAL_IOC_ALLOC_CROSS_VENDOR _IOWR(MVGAL_IOC_MAGIC, 0x12, struct mvgal_uapi_cross_vendor_alloc)
#define MVGAL_IOC_FREE_CROSS_VENDOR _IOW(MVGAL_IOC_MAGIC, 0x13, __u64)

#endif
