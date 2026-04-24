/*
 * mvgald_dev_runner.c
 *
 * Simple non-daemonized development runner for MVGAL.
 *
 * This tool uses the libmvgal UAPI wrapper (declared here as externs)
 * to connect to /dev/mvgal0 and list topology information. It is
 * intended for local development: run it in the foreground to verify
 * the kernel module and the userspace wrapper are correctly linked.
 *
 * Build:
 *   (CMake target provided in tools/monitor/CMakeLists.txt)
 *
 * Usage:
 *   mvgald_dev_runner        - list detected GPUs and caps
 *   mvgald_dev_runner -r     - request a rescan before listing
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <inttypes.h>
#include "mvgal/mvgal_uapi.h"

/* libmvgal wrapper function prototypes (the wrapper is a small shared lib
 * built in tools/libmvgal). We declare the functions here so the runner can
 * link without depending on an additional header in this tree.
 */
extern int mvgal_uapi_init(const char *devpath);
extern void mvgal_uapi_shutdown(void);
extern int mvgal_uapi_get_version(struct mvgal_uapi_version *out);
extern int mvgal_uapi_get_gpu_count(uint32_t *out_count);
extern int mvgal_uapi_get_gpu_info(uint32_t index, struct mvgal_gpu_info *out_info);
extern int mvgal_uapi_get_caps(struct mvgal_uapi_caps *out_caps);
extern int mvgal_uapi_rescan(void);
extern int mvgal_uapi_get_stats(struct mvgal_uapi_stats *out_stats);

static const char *vendor_name(uint32_t vendor_id)
{
    switch (vendor_id) {
    case MVGAL_UAPI_VENDOR_AMD: return "AMD";
    case MVGAL_UAPI_VENDOR_NVIDIA: return "NVIDIA";
    case MVGAL_UAPI_VENDOR_INTEL: return "Intel";
    case MVGAL_UAPI_VENDOR_MOORE_THREADS: return "MooreThreads";
    default: return "Unknown";
    }
}

static void print_gpu_info(const struct mvgal_gpu_info *info)
{
    if (!info) return;

    printf("GPU[%u]: %s\n", info->index, info->name);
    printf("  Vendor: 0x%04x (%s)  Device: 0x%04x\n",
           (unsigned)info->vendor_id, vendor_name(info->vendor_id),
           (unsigned)info->device_id);
    printf("  Subsystem: 0x%04x:0x%04x\n",
           (unsigned)info->subsystem_vendor_id, (unsigned)info->subsystem_device_id);
    printf("  PCI BDF: %s  (domain=%u bus=%u slot=%u func=%u)\n",
           info->bdf,
           (unsigned)info->pci_domain,
           (unsigned)info->pci_bus,
           (unsigned)info->pci_slot,
           (unsigned)info->pci_function);
    printf("  Class code: 0x%08x  NUMA node: %u\n", info->class_code, info->numa_node);
    printf("  PCIe: gen=%u width=x%u\n", info->pcie_link_gen, info->pcie_link_width);
    printf("  Largest prefetchable BAR: %" PRIu64 " bytes\n", (uint64_t)info->largest_prefetchable_bar_bytes);
    printf("  Largest MMIO BAR: %" PRIu64 " bytes\n", (uint64_t)info->largest_mmio_bar_bytes);
    printf("  Driver: %s\n", info->driver ? info->driver : "unknown");

    printf("  Flags: 0x%08x [", info->flags);
    int first = 1;
    if (info->flags & MVGAL_UAPI_GPU_FLAG_DISPLAY_CLASS) { if (!first) printf("|"); printf("DISPLAY_CLASS"); first = 0; }
    if (info->flags & MVGAL_UAPI_GPU_FLAG_PCIE) { if (!first) printf("|"); printf("PCIE"); first = 0; }
    if (info->flags & MVGAL_UAPI_GPU_FLAG_DRIVER_BOUND) { if (!first) printf("|"); printf("DRIVER_BOUND"); first = 0; }
    if (info->flags & MVGAL_UAPI_GPU_FLAG_MULTIFUNCTION) { if (!first) printf("|"); printf("MULTIFUNCTION"); first = 0; }
    if (info->flags & MVGAL_UAPI_GPU_FLAG_VIRTUAL_FUNCTION) { if (!first) printf("|"); printf("VIRTUAL_FUNCTION"); first = 0; }
    if (info->flags & MVGAL_UAPI_GPU_FLAG_INTEGRATED_GUESS) { if (!first) printf("|"); printf("INTEGRATED_GUESS"); first = 0; }
    printf("]\n");
}

static void print_caps(const struct mvgal_uapi_caps *caps)
{
    if (!caps) return;
    printf("MVGAL caps:\n");
    printf("  enabled: %u\n", caps->enabled);
    printf("  gpu_count: %u\n", caps->gpu_count);
    printf("  topology_generation: %u\n", caps->topology_generation);
    printf("  vendor_mask: 0x%08x\n", caps->vendor_mask);
    printf("  max_pcie_link_gen: %u  max_pcie_link_width: %u\n",
           caps->max_pcie_link_gen, caps->max_pcie_link_width);
    printf("  largest_prefetchable_bar_bytes: %" PRIu64 "\n", (uint64_t)caps->largest_prefetchable_bar_bytes);
    printf("  largest_mmio_bar_bytes: %" PRIu64 "\n", (uint64_t)caps->largest_mmio_bar_bytes);

    printf("  feature_flags: 0x%08x [", caps->feature_flags);
    int first = 1;
    if (caps->feature_flags & MVGAL_UAPI_FEATURE_ENUMERATION) { if (!first) printf("|"); printf("ENUMERATION"); first = 0; }
    if (caps->feature_flags & MVGAL_UAPI_FEATURE_PCI_TOPOLOGY) { if (!first) printf("|"); printf("PCI_TOPOLOGY"); first = 0; }
    if (caps->feature_flags & MVGAL_UAPI_FEATURE_HOTPLUG_MONITOR) { if (!first) printf("|"); printf("HOTPLUG_MONITOR"); first = 0; }
    if (caps->feature_flags & MVGAL_UAPI_FEATURE_RESCAN) { if (!first) printf("|"); printf("RESCAN"); first = 0; }
    if (caps->feature_flags & MVGAL_UAPI_FEATURE_READ_ONLY_SKELETON) { if (!first) printf("|"); printf("READ_ONLY_SKELETON"); first = 0; }
    if (caps->feature_flags & MVGAL_UAPI_FEATURE_FUTURE_DMABUF) { if (!first) printf("|"); printf("FUTURE_DMABUF"); first = 0; }
    if (caps->feature_flags & MVGAL_UAPI_FEATURE_FUTURE_SUBMISSION) { if (!first) printf("|"); printf("FUTURE_SUBMISSION"); first = 0; }
    printf("]\n");
}

static void print_stats(const struct mvgal_uapi_stats *s)
{
    if (!s) return;
    printf("MVGAL stats:\n");
    printf("  open_count: %" PRIu64 "\n", (uint64_t)s->open_count);
    printf("  release_count: %" PRIu64 "\n", (uint64_t)s->release_count);
    printf("  ioctl_count: %" PRIu64 "\n", (uint64_t)s->ioctl_count);
    printf("  rescans: %" PRIu64 "\n", (uint64_t)s->rescans);
    printf("  hotplug_events: %" PRIu64 "\n", (uint64_t)s->hotplug_events);
    printf("  unsupported_dmabuf_exports: %" PRIu64 "\n", (uint64_t)s->unsupported_dmabuf_exports);
    printf("  unsupported_dmabuf_imports: %" PRIu64 "\n", (uint64_t)s->unsupported_dmabuf_imports);
    printf("  unsupported_cross_vendor_allocs: %" PRIu64 "\n", (uint64_t)s->unsupported_cross_vendor_allocs);
}

int main(int argc, char **argv)
{
    int do_rescan = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rescan") == 0) {
            do_rescan = 1;
        } else {
            fprintf(stderr, "Usage: %s [-r|--rescan]\n", argv[0]);
            return 2;
        }
    }

    /* Initialize libmvgal wrapper (NULL uses default device path /dev/mvgal0) */
    if (mvgal_uapi_init(NULL) != 0) {
        fprintf(stderr, "mvgal_uapi_init() failed\n");
        return 1;
    }

    if (do_rescan) {
        if (mvgal_uapi_rescan() != 0) {
            fprintf(stderr, "mvgal_uapi_rescan() failed\n");
            /* continue to attempt readout even if rescan fails */
        } else {
            printf("Rescan requested.\n");
        }
    }

    struct mvgal_uapi_version ver;
    if (mvgal_uapi_get_version(&ver) == 0) {
        printf("MVGAL UAPI version: %u.%u.%u\n", ver.major, ver.minor, ver.patch);
        printf("Feature flags: 0x%08x\n", ver.feature_flags);
    } else {
        fprintf(stderr, "mvgal_uapi_get_version() failed\n");
    }

    uint32_t count = 0;
    if (mvgal_uapi_get_gpu_count(&count) != 0) {
        fprintf(stderr, "mvgal_uapi_get_gpu_count() failed\n");
        mvgal_uapi_shutdown();
        return 1;
    }

    printf("Detected %u GPU(s)\n", count);

    for (uint32_t i = 0; i < count && i < MVGAL_UAPI_MAX_GPUS; ++i) {
        struct mvgal_gpu_info info;
        if (mvgal_uapi_get_gpu_info(i, &info) != 0) {
            fprintf(stderr, "mvgal_uapi_get_gpu_info(%u) failed\n", i);
            continue;
        }
        print_gpu_info(&info);
        printf("\n");
    }

    struct mvgal_uapi_caps caps;
    if (mvgal_uapi_get_caps(&caps) == 0) {
        print_caps(&caps);
    } else {
        fprintf(stderr, "mvgal_uapi_get_caps() failed\n");
    }

    struct mvgal_uapi_stats stats;
    if (mvgal_uapi_get_stats(&stats) == 0) {
        print_stats(&stats);
    } else {
        /* stats are optional */
    }

    /* Shutdown wrapper */
    mvgal_uapi_shutdown();

    return 0;
}