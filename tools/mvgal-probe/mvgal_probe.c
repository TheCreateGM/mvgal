/*
 * mvgal_probe.c - simple CLI to exercise /dev/mvgal0 ioctls
 *
 * Builds: mvgal/tools/mvgal-probe/CMakeLists.txt adds ../../include to include path.
 *
 * This small utility queries the MVGAL kernel UAPI for topology and basic info.
 *
 * Usage:
 *   mvgal-probe            - list detected GPUs
 *   mvgal-probe -r         - request a rescan then list
 *   mvgal-probe -c         - print capabilities and stats
 *   mvgal-probe -v         - print UAPI version
 *
 * This is intentionally minimal and safe: it only performs read-only queries
 * and uses the UAPI structs defined in mvgal/include/mvgal/mvgal_uapi.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <inttypes.h>

#include "mvgal/mvgal_uapi.h"

#define MVGAL_DEV_PATH "/dev/" MVGAL_DEVICE_NAME

static const char *vendor_name(uint32_t vendor_id)
{
    switch (vendor_id) {
    case MVGAL_UAPI_VENDOR_AMD:
        return "AMD";
    case MVGAL_UAPI_VENDOR_NVIDIA:
        return "NVIDIA";
    case MVGAL_UAPI_VENDOR_INTEL:
        return "Intel";
    case MVGAL_UAPI_VENDOR_MOORE_THREADS:
        return "MooreThreads";
    default:
        return "Unknown";
    }
}

static void print_flags_u32(uint32_t v, const char *labels[], const uint32_t masks[], size_t n)
{
    bool first = true;
    for (size_t i = 0; i < n; ++i) {
        if (v & masks[i]) {
            if (!first) putchar('|');
            printf("%s", labels[i]);
            first = false;
        }
    }
    if (first) printf("none");
}

static void print_gpu_info(const struct mvgal_gpu_info *info)
{
    if (info == NULL) return;

    printf("GPU[%u]: %s\n", info->index, info->name);
    printf("  Vendor: 0x%04x (%s)  Device: 0x%04x  Subsys: 0x%04x:0x%04x\n",
           (unsigned)info->vendor_id,
           vendor_name(info->vendor_id),
           (unsigned)info->device_id,
           (unsigned)info->subsystem_vendor_id,
           (unsigned)info->subsystem_device_id);
    printf("  PCI BDF: %s  (domain=%u bus=%u slot=%u func=%u)\n",
           info->bdf,
           (unsigned)info->pci_domain,
           (unsigned)info->pci_bus,
           (unsigned)info->pci_slot,
           (unsigned)info->pci_function);
    printf("  Class code: 0x%02x  NUMA node: %u\n", (unsigned)info->class_code, (unsigned)info->numa_node);
    printf("  PCIe: gen=%u width=x%u\n", (unsigned)info->pcie_link_gen, (unsigned)info->pcie_link_width);
    printf("  BARs: largest_prefetchable=%" PRIu64 " bytes, largest_mmio=%" PRIu64 " bytes\n",
           (uint64_t)info->largest_prefetchable_bar_bytes, (uint64_t)info->largest_mmio_bar_bytes);
    printf("  Driver: %s\n", info->driver);

    const char *gpu_flag_labels[] = {
        "DISPLAY_CLASS",
        "PCIE",
        "DRIVER_BOUND",
        "MULTIFUNCTION",
        "VIRTUAL_FUNCTION",
        "INTEGRATED_GUESS",
    };
    const uint32_t gpu_flag_masks[] = {
        MVGAL_UAPI_GPU_FLAG_DISPLAY_CLASS,
        MVGAL_UAPI_GPU_FLAG_PCIE,
        MVGAL_UAPI_GPU_FLAG_DRIVER_BOUND,
        MVGAL_UAPI_GPU_FLAG_MULTIFUNCTION,
        MVGAL_UAPI_GPU_FLAG_VIRTUAL_FUNCTION,
        MVGAL_UAPI_GPU_FLAG_INTEGRATED_GUESS,
    };

    printf("  Flags: ");
    print_flags_u32(info->flags, gpu_flag_labels, gpu_flag_masks, sizeof(gpu_flag_masks)/sizeof(gpu_flag_masks[0]));
    printf("\n");
}

static void do_list(int fd)
{
    struct mvgal_uapi_gpu_count count = {0};
    if (ioctl(fd, MVGAL_IOC_GET_GPU_COUNT, &count) != 0) {
        fprintf(stderr, "MVGAL_IOC_GET_GPU_COUNT failed: %s\n", strerror(errno));
        return;
    }

    printf("MVGAL: detected %u GPU(s)\n", count.gpu_count);
    if (count.gpu_count == 0) return;

    for (uint32_t i = 0; i < count.gpu_count; ++i) {
        struct mvgal_uapi_gpu_query q = {0};
        q.index = i;

        if (ioctl(fd, MVGAL_IOC_GET_GPU_INFO, &q) != 0) {
            fprintf(stderr, "MVGAL_IOC_GET_GPU_INFO(%u) failed: %s\n", i, strerror(errno));
            continue;
        }

        print_gpu_info(&q.info);
        putchar('\n');
    }
}

static void do_rescan(int fd)
{
    if (ioctl(fd, MVGAL_IOC_RESCAN) != 0) {
        fprintf(stderr, "MVGAL_IOC_RESCAN failed: %s\n", strerror(errno));
    } else {
        printf("Rescan requested.\n");
    }
}

static void do_caps(int fd)
{
    struct mvgal_uapi_caps caps = {0};
    if (ioctl(fd, MVGAL_IOC_GET_CAPS, &caps) != 0) {
        fprintf(stderr, "MVGAL_IOC_GET_CAPS failed: %s\n", strerror(errno));
        return;
    }

    printf("MVGAL caps:\n");
    printf("  enabled: %u\n", caps.enabled);
    printf("  gpu_count: %u\n", caps.gpu_count);
    printf("  topology_generation: %u\n", caps.topology_generation);
    printf("  vendor_mask: 0x%08x\n", caps.vendor_mask);
    printf("  max_pcie_link_gen: %u  max_pcie_link_width: %u\n",
           caps.max_pcie_link_gen, caps.max_pcie_link_width);
    printf("  largest_prefetchable_bar_bytes: %" PRIu64 "\n", (uint64_t)caps.largest_prefetchable_bar_bytes);
    printf("  largest_mmio_bar_bytes: %" PRIu64 "\n", (uint64_t)caps.largest_mmio_bar_bytes);

    const char *feat_labels[] = {
        "ENUMERATION",
        "PCI_TOPOLOGY",
        "HOTPLUG_MONITOR",
        "RESCAN",
        "READ_ONLY_SKELETON",
        "FUTURE_DMABUF",
        "FUTURE_SUBMISSION"
    };
    const uint32_t feat_masks[] = {
        MVGAL_UAPI_FEATURE_ENUMERATION,
        MVGAL_UAPI_FEATURE_PCI_TOPOLOGY,
        MVGAL_UAPI_FEATURE_HOTPLUG_MONITOR,
        MVGAL_UAPI_FEATURE_RESCAN,
        MVGAL_UAPI_FEATURE_READ_ONLY_SKELETON,
        MVGAL_UAPI_FEATURE_FUTURE_DMABUF,
        MVGAL_UAPI_FEATURE_FUTURE_SUBMISSION
    };

    printf("  feature_flags: 0x%08x (", caps.feature_flags);
    print_flags_u32(caps.feature_flags, feat_labels, feat_masks, sizeof(feat_masks)/sizeof(feat_masks[0]));
    printf(")\n");
}

static void do_stats(int fd)
{
    struct mvgal_uapi_stats s = {0};
    if (ioctl(fd, MVGAL_IOC_GET_STATS, &s) != 0) {
        fprintf(stderr, "MVGAL_IOC_GET_STATS failed: %s\n", strerror(errno));
        return;
    }

    printf("MVGAL stats:\n");
    printf("  open_count: %" PRIu64 "\n", (uint64_t)s.open_count);
    printf("  release_count: %" PRIu64 "\n", (uint64_t)s.release_count);
    printf("  ioctl_count: %" PRIu64 "\n", (uint64_t)s.ioctl_count);
    printf("  rescans: %" PRIu64 "\n", (uint64_t)s.rescans);
    printf("  hotplug_events: %" PRIu64 "\n", (uint64_t)s.hotplug_events);
    printf("  unsupported_dmabuf_exports: %" PRIu64 "\n", (uint64_t)s.unsupported_dmabuf_exports);
    printf("  unsupported_dmabuf_imports: %" PRIu64 "\n", (uint64_t)s.unsupported_dmabuf_imports);
    printf("  unsupported_cross_vendor_allocs: %" PRIu64 "\n", (uint64_t)s.unsupported_cross_vendor_allocs);
}

static void do_version(int fd)
{
    struct mvgal_uapi_version v = {0};
    if (ioctl(fd, MVGAL_IOC_QUERY_VERSION, &v) != 0) {
        fprintf(stderr, "MVGAL_IOC_QUERY_VERSION failed: %s\n", strerror(errno));
        return;
    }

    printf("MVGAL UAPI version: %u.%u.%u\n", v.major, v.minor, v.patch);
    printf("Feature flags: 0x%08x\n", v.feature_flags);
}

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -r    Request rescan then list GPUs\n");
    printf("  -c    Print capabilities\n");
    printf("  -s    Print stats\n");
    printf("  -v    Print UAPI version\n");
    printf("  -h    This help\n");
}

int main(int argc, char **argv)
{
    int opt;
    bool do_r = false;
    bool do_c = false;
    bool do_s = false;
    bool do_v = false;

    while ((opt = getopt(argc, argv, "rcsvh")) != -1) {
        switch (opt) {
        case 'r': do_r = true; break;
        case 'c': do_c = true; break;
        case 's': do_s = true; break;
        case 'v': do_v = true; break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    int fd = open(MVGAL_DEV_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", MVGAL_DEV_PATH, strerror(errno));
        return 2;
    }

    if (do_r) {
        do_rescan(fd);
    }

    if (do_v) {
        do_version(fd);
    }

    if (do_c) {
        do_caps(fd);
    }

    if (do_s) {
        do_stats(fd);
    }

    /* Always list GPUs unless a capabilities-only request was made without listing */
    if (!do_c || (!do_c && !do_s && !do_v && !do_r)) {
        do_list(fd);
    }

    close(fd);
    return 0;
}