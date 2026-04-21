/**
 * @file gpu_manager.c
 * @brief GPU detection and management implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module detects physical GPUs, normalizes vendor-specific metadata into
 * a common descriptor, and exposes logical-device aggregation helpers.
 */

#include "mvgal_gpu.h"
#include "mvgal_log.h"
#include "mvgal_config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PCI_VENDOR_ID_AMD           0x1002U
#define PCI_VENDOR_ID_NVIDIA        0x10DEU
#define PCI_VENDOR_ID_INTEL         0x8086U
#define PCI_VENDOR_ID_MOORE_THREADS 0x1EACU

#define MAX_GPUS 16U
#define MAX_GPU_CALLBACKS 8U
#define MAX_CUSTOM_DRIVERS 8U
#define MVGAL_ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define MVGAL_LOGICAL_DEVICE_MAGIC 0x4D564C47U

typedef struct {
    mvgal_gpu_callback_t callback;
    void *user_data;
} mvgal_gpu_callback_entry_t;

typedef struct {
    char name[64];
    void *probe_func;
    void *init_func;
    void *user_data;
} mvgal_custom_driver_entry_t;

typedef struct {
    uint32_t magic;
    mvgal_logical_device_descriptor_t descriptor;
} mvgal_logical_device_t;

typedef struct {
    pthread_mutex_t lock;
    mvgal_gpu_descriptor_t gpus[MAX_GPUS];
    mvgal_gpu_health_thresholds_t health_thresholds[MAX_GPUS];
    mvgal_gpu_callback_entry_t callbacks[MAX_GPU_CALLBACKS];
    mvgal_custom_driver_entry_t drivers[MAX_CUSTOM_DRIVERS];
    uint32_t gpu_count;
    uint32_t driver_count;
    bool initialized;
    bool scanned;
} mvgal_gpu_manager_state_t;

typedef struct {
    bool enabled;
    uint32_t poll_interval_ms;
    bool running;
    pthread_t thread;
    mvgal_gpu_health_callback_t callback;
    void *callback_user_data;
} mvgal_health_monitor_state_t;

static const mvgal_gpu_health_thresholds_t DEFAULT_HEALTH_THRESHOLDS = {
    .temp_warning_celsius = 80.0f,
    .temp_critical_celsius = 95.0f,
    .utilization_warning = 80.0f,
    .utilization_critical = 95.0f,
    .memory_warning = 85.0f,
    .memory_critical = 95.0f,
};

static mvgal_gpu_manager_state_t g_gpu_manager = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static mvgal_health_monitor_state_t g_health_monitor = {0};

mvgal_error_t mvgal_gpu_manager_init(void);
void mvgal_gpu_manager_shutdown(void);

static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static bool path_exists(const char *path)
{
    return path != NULL && access(path, F_OK) == 0;
}

static bool parse_indexed_node(const char *name, const char *prefix, uint32_t *index)
{
    size_t prefix_len;
    const char *digits;
    char *end = NULL;
    unsigned long value;

    if (name == NULL || prefix == NULL || index == NULL) {
        return false;
    }

    prefix_len = strlen(prefix);
    if (strncmp(name, prefix, prefix_len) != 0) {
        return false;
    }

    digits = name + prefix_len;
    if (*digits == '\0') {
        return false;
    }

    for (const char *it = digits; *it != '\0'; ++it) {
        if (!isdigit((unsigned char)*it)) {
            return false;
        }
    }

    errno = 0;
    value = strtoul(digits, &end, 10);
    if (errno != 0 || end == NULL || *end != '\0' || value > UINT32_MAX) {
        return false;
    }

    *index = (uint32_t)value;
    return true;
}

static bool read_file_trimmed(const char *path, char *buffer, size_t buffer_size)
{
    FILE *file;

    if (path == NULL || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    if (fgets(buffer, (int)buffer_size, file) == NULL) {
        fclose(file);
        return false;
    }

    fclose(file);
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return true;
}

static bool read_file_u64(const char *path, uint64_t *value)
{
    char buffer[128];
    char *end = NULL;
    unsigned long long parsed;

    if (value == NULL || !read_file_trimmed(path, buffer, sizeof(buffer))) {
        return false;
    }

    errno = 0;
    parsed = strtoull(buffer, &end, 0);
    if (errno != 0 || end == buffer) {
        return false;
    }

    *value = (uint64_t)parsed;
    return true;
}

static bool read_file_u16(const char *path, uint16_t *value)
{
    uint64_t parsed;

    if (value == NULL || !read_file_u64(path, &parsed) || parsed > UINT16_MAX) {
        return false;
    }

    *value = (uint16_t)parsed;
    return true;
}

static bool normalize_node_path(const char *path, char *normalized, size_t normalized_size)
{
    char resolved[PATH_MAX];

    if (path == NULL || normalized == NULL || normalized_size == 0U) {
        return false;
    }

    if (realpath(path, resolved) != NULL) {
        (void)snprintf(normalized, normalized_size, "%s", resolved);
        return true;
    }

    (void)snprintf(normalized, normalized_size, "%s", path);
    return true;
}

static bool read_link_basename(const char *path, char *buffer, size_t buffer_size)
{
    char resolved[PATH_MAX];
    const char *basename;

    if (path == NULL || buffer == NULL || buffer_size == 0U) {
        return false;
    }

    if (realpath(path, resolved) == NULL) {
        return false;
    }

    basename = strrchr(resolved, '/');
    basename = basename == NULL ? resolved : basename + 1;
    (void)snprintf(buffer, buffer_size, "%s", basename);
    return true;
}

static bool parse_pci_address(const char *device_realpath,
                              uint16_t *domain,
                              uint8_t *bus,
                              uint8_t *device,
                              uint8_t *function)
{
    const char *basename;
    unsigned int parsed_domain;
    unsigned int parsed_bus;
    unsigned int parsed_device;
    unsigned int parsed_function;

    if (device_realpath == NULL) {
        return false;
    }

    basename = strrchr(device_realpath, '/');
    basename = basename == NULL ? device_realpath : basename + 1;

    if (sscanf(basename, "%x:%x:%x.%x",
               &parsed_domain,
               &parsed_bus,
               &parsed_device,
               &parsed_function) != 4) {
        return false;
    }

    if (parsed_domain > UINT16_MAX || parsed_bus > UINT8_MAX ||
        parsed_device > UINT8_MAX || parsed_function > UINT8_MAX) {
        return false;
    }

    if (domain != NULL) {
        *domain = (uint16_t)parsed_domain;
    }
    if (bus != NULL) {
        *bus = (uint8_t)parsed_bus;
    }
    if (device != NULL) {
        *device = (uint8_t)parsed_device;
    }
    if (function != NULL) {
        *function = (uint8_t)parsed_function;
    }

    return true;
}

static const char *vendor_name(mvgal_vendor_t vendor)
{
    switch (vendor) {
        case MVGAL_VENDOR_AMD:
            return "AMD";
        case MVGAL_VENDOR_NVIDIA:
            return "NVIDIA";
        case MVGAL_VENDOR_INTEL:
            return "Intel";
        case MVGAL_VENDOR_MOORE_THREADS:
            return "Moore Threads";
        case MVGAL_VENDOR_QUALCOMM:
            return "Qualcomm";
        case MVGAL_VENDOR_ARM:
            return "ARM";
        case MVGAL_VENDOR_BROADCOM:
            return "Broadcom";
        case MVGAL_VENDOR_UNKNOWN:
        default:
            return "Unknown";
    }
}

static mvgal_api_type_t default_api_mask(mvgal_vendor_t vendor)
{
    mvgal_api_type_t mask = MVGAL_API_VULKAN | MVGAL_API_OPENGL;

    switch (vendor) {
        case MVGAL_VENDOR_NVIDIA:
            mask |= MVGAL_API_OPENCL | MVGAL_API_CUDA;
            break;
        case MVGAL_VENDOR_AMD:
        case MVGAL_VENDOR_INTEL:
        case MVGAL_VENDOR_MOORE_THREADS:
            mask |= MVGAL_API_OPENCL;
            break;
        case MVGAL_VENDOR_UNKNOWN:
        default:
            break;
    }

    return mask;
}

static uint64_t default_feature_mask(mvgal_vendor_t vendor, mvgal_gpu_type_t type)
{
    uint64_t features = MVGAL_FEATURE_GRAPHICS | MVGAL_FEATURE_COMPUTE;

    if (type != MVGAL_GPU_TYPE_VIRTUAL) {
        features |= MVGAL_FEATURE_DMA_BUF;
    }

    if (vendor == MVGAL_VENDOR_AMD || vendor == MVGAL_VENDOR_INTEL ||
        vendor == MVGAL_VENDOR_MOORE_THREADS) {
        features |= MVGAL_FEATURE_CROSS_VENDOR;
    }

    if (vendor == MVGAL_VENDOR_NVIDIA || vendor == MVGAL_VENDOR_AMD) {
        features |= MVGAL_FEATURE_P2P_TRANSFER;
    }

    if (type == MVGAL_GPU_TYPE_INTEGRATED) {
        features |= MVGAL_FEATURE_UNIFIED_MEMORY;
    }

    return features;
}

static mvgal_memory_type_t default_memory_type(mvgal_gpu_type_t type)
{
    if (type == MVGAL_GPU_TYPE_INTEGRATED) {
        return MVGAL_MEMORY_TYPE_SHARED;
    }

    if (type == MVGAL_GPU_TYPE_VIRTUAL) {
        return MVGAL_MEMORY_TYPE_UNIFIED;
    }

    return MVGAL_MEMORY_TYPE_GDDR;
}

static float default_compute_score(mvgal_vendor_t vendor, mvgal_gpu_type_t type)
{
    if (type == MVGAL_GPU_TYPE_VIRTUAL) {
        return 10.0f;
    }

    switch (vendor) {
        case MVGAL_VENDOR_NVIDIA:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 92.0f : 55.0f;
        case MVGAL_VENDOR_AMD:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 84.0f : 52.0f;
        case MVGAL_VENDOR_INTEL:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 62.0f : 38.0f;
        case MVGAL_VENDOR_MOORE_THREADS:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 68.0f : 44.0f;
        case MVGAL_VENDOR_UNKNOWN:
        default:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 45.0f : 28.0f;
    }
}

static float default_graphics_score(mvgal_vendor_t vendor, mvgal_gpu_type_t type)
{
    if (type == MVGAL_GPU_TYPE_VIRTUAL) {
        return 10.0f;
    }

    switch (vendor) {
        case MVGAL_VENDOR_NVIDIA:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 90.0f : 50.0f;
        case MVGAL_VENDOR_AMD:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 86.0f : 54.0f;
        case MVGAL_VENDOR_INTEL:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 64.0f : 42.0f;
        case MVGAL_VENDOR_MOORE_THREADS:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 70.0f : 40.0f;
        case MVGAL_VENDOR_UNKNOWN:
        default:
            return type == MVGAL_GPU_TYPE_DISCRETE ? 42.0f : 24.0f;
    }
}

static float default_memory_bandwidth_gbps(mvgal_gpu_type_t type)
{
    if (type == MVGAL_GPU_TYPE_DISCRETE) {
        return 448.0f;
    }

    if (type == MVGAL_GPU_TYPE_INTEGRATED) {
        return 128.0f;
    }

    return 32.0f;
}

static float default_tdp_watts(mvgal_gpu_type_t type)
{
    if (type == MVGAL_GPU_TYPE_DISCRETE) {
        return 250.0f;
    }

    if (type == MVGAL_GPU_TYPE_INTEGRATED) {
        return 45.0f;
    }

    return 25.0f;
}

static mvgal_pcie_generation_t parse_pcie_generation(const char *link_speed)
{
    float speed = 0.0f;

    if (link_speed == NULL || sscanf(link_speed, "%f", &speed) != 1) {
        return MVGAL_PCIE_UNKNOWN;
    }

    if (speed >= 64.0f) {
        return MVGAL_PCIE_GEN6;
    }
    if (speed >= 32.0f) {
        return MVGAL_PCIE_GEN5;
    }
    if (speed >= 16.0f) {
        return MVGAL_PCIE_GEN4;
    }
    if (speed >= 8.0f) {
        return MVGAL_PCIE_GEN3;
    }
    if (speed >= 5.0f) {
        return MVGAL_PCIE_GEN2;
    }
    if (speed >= 2.5f) {
        return MVGAL_PCIE_GEN1;
    }

    return MVGAL_PCIE_UNKNOWN;
}

static float pcie_bandwidth_from_link(mvgal_pcie_generation_t generation, uint8_t lanes)
{
    float per_lane_gbps;

    switch (generation) {
        case MVGAL_PCIE_GEN1:
            per_lane_gbps = 2.0f;
            break;
        case MVGAL_PCIE_GEN2:
            per_lane_gbps = 4.0f;
            break;
        case MVGAL_PCIE_GEN3:
            per_lane_gbps = 7.877f;
            break;
        case MVGAL_PCIE_GEN4:
            per_lane_gbps = 15.754f;
            break;
        case MVGAL_PCIE_GEN5:
            per_lane_gbps = 31.508f;
            break;
        case MVGAL_PCIE_GEN6:
            per_lane_gbps = 63.016f;
            break;
        case MVGAL_PCIE_UNKNOWN:
        default:
            per_lane_gbps = 0.0f;
            break;
    }

    return per_lane_gbps * (float)lanes;
}

static uint64_t api_mask_to_list(mvgal_api_type_t mask, mvgal_api_type_t *apis, size_t capacity)
{
    static const mvgal_api_type_t known_apis[] = {
        MVGAL_API_VULKAN,
        MVGAL_API_OPENGL,
        MVGAL_API_OPENCL,
        MVGAL_API_CUDA,
        MVGAL_API_D3D11,
        MVGAL_API_D3D12,
        MVGAL_API_METAL,
        MVGAL_API_WEBGPU,
        MVGAL_API_VA_API,
    };

    size_t count = 0U;

    if (apis == NULL || capacity == 0U) {
        return 0U;
    }

    memset(apis, 0, sizeof(*apis) * capacity);
    for (size_t i = 0; i < MVGAL_ARRAY_LEN(known_apis) && count < capacity; ++i) {
        if ((mask & known_apis[i]) != 0U) {
            apis[count++] = known_apis[i];
        }
    }

    return (uint64_t)count;
}

static mvgal_api_type_t gpu_api_mask(const mvgal_gpu_descriptor_t *gpu)
{
    mvgal_api_type_t mask = 0;

    if (gpu == NULL) {
        return 0;
    }

    for (uint32_t i = 0; i < gpu->api_count && i < MVGAL_ARRAY_LEN(gpu->supported_apis); ++i) {
        mask |= gpu->supported_apis[i];
    }

    return mask;
}

static float gpu_selection_score(const mvgal_gpu_descriptor_t *gpu)
{
    double vram_gb = 0.0;

    if (gpu == NULL) {
        return -1.0f;
    }

    vram_gb = (double)gpu->vram_total / (1024.0 * 1024.0 * 1024.0);
    return gpu->compute_score + gpu->graphics_score + (float)vram_gb;
}

static bool node_matches(const char *stored_path, const char *query_path)
{
    char normalized_stored[PATH_MAX];
    char normalized_query[PATH_MAX];

    if (stored_path == NULL || stored_path[0] == '\0' || query_path == NULL) {
        return false;
    }

    if (strcmp(stored_path, query_path) == 0) {
        return true;
    }

    if (!normalize_node_path(stored_path, normalized_stored, sizeof(normalized_stored)) ||
        !normalize_node_path(query_path, normalized_query, sizeof(normalized_query))) {
        return false;
    }

    return strcmp(normalized_stored, normalized_query) == 0;
}

static uint64_t read_first_u64(const char *device_root,
                               const char *const *relative_paths,
                               size_t path_count)
{
    char full_path[PATH_MAX];
    uint64_t value = 0;

    if (device_root == NULL) {
        return 0;
    }

    for (size_t i = 0; i < path_count; ++i) {
        (void)snprintf(full_path, sizeof(full_path), "%s/%s", device_root, relative_paths[i]);
        if (read_file_u64(full_path, &value)) {
            return value;
        }
    }

    return 0;
}

static bool gpu_identity_matches(const mvgal_gpu_descriptor_t *left,
                                 const mvgal_gpu_descriptor_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }

    if (left->pci_domain == right->pci_domain &&
        left->pci_bus == right->pci_bus &&
        left->pci_device == right->pci_device &&
        left->pci_function == right->pci_function &&
        (left->pci_domain != 0U || left->pci_bus != 0U ||
         left->pci_device != 0U || left->pci_function != 0U)) {
        return true;
    }

    return node_matches(left->drm_node, right->drm_node) ||
           node_matches(left->drm_render_node, right->drm_render_node) ||
           node_matches(left->nvidia_node, right->nvidia_node);
}

static void finalize_gpu_descriptor(mvgal_gpu_descriptor_t *gpu)
{
    mvgal_api_type_t api_mask;
    double vram_total_gb;

    if (gpu == NULL) {
        return;
    }

    if (gpu->vendor == MVGAL_VENDOR_UNKNOWN) {
        switch (gpu->vendor_id) {
            case PCI_VENDOR_ID_AMD:
                gpu->vendor = MVGAL_VENDOR_AMD;
                break;
            case PCI_VENDOR_ID_NVIDIA:
                gpu->vendor = MVGAL_VENDOR_NVIDIA;
                break;
            case PCI_VENDOR_ID_INTEL:
                gpu->vendor = MVGAL_VENDOR_INTEL;
                break;
            case PCI_VENDOR_ID_MOORE_THREADS:
                gpu->vendor = MVGAL_VENDOR_MOORE_THREADS;
                break;
            default:
                break;
        }
    }

    if (gpu->type == 0) {
        gpu->type = gpu->vendor == MVGAL_VENDOR_INTEL
            ? MVGAL_GPU_TYPE_INTEGRATED
            : MVGAL_GPU_TYPE_DISCRETE;
    }

    if (gpu->memory_type == MVGAL_MEMORY_TYPE_UNKNOWN) {
        gpu->memory_type = default_memory_type(gpu->type);
    }

    if (gpu->vram_total == 0U) {
        if (gpu->type == MVGAL_GPU_TYPE_INTEGRATED) {
            gpu->vram_total = 1024ULL * 1024ULL * 1024ULL;
        } else if (gpu->type == MVGAL_GPU_TYPE_VIRTUAL) {
            gpu->vram_total = 4ULL * 1024ULL * 1024ULL * 1024ULL;
        } else {
            gpu->vram_total = 8ULL * 1024ULL * 1024ULL * 1024ULL;
        }
    }

    if (gpu->vram_used > gpu->vram_total) {
        gpu->vram_used = gpu->vram_total;
    }

    gpu->vram_free = gpu->vram_total - gpu->vram_used;

    if (gpu->features == 0U) {
        gpu->features = default_feature_mask(gpu->vendor, gpu->type);
    }

    if (gpu->api_count == 0U) {
        api_mask = default_api_mask(gpu->vendor);
        gpu->api_count = (uint32_t)api_mask_to_list(api_mask,
                                                    gpu->supported_apis,
                                                    MVGAL_ARRAY_LEN(gpu->supported_apis));
    }

    if (gpu->compute_score <= 0.0f) {
        gpu->compute_score = default_compute_score(gpu->vendor, gpu->type);
    }

    if (gpu->graphics_score <= 0.0f) {
        gpu->graphics_score = default_graphics_score(gpu->vendor, gpu->type);
    }

    if (gpu->memory_bandwidth_gbps <= 0.0f) {
        gpu->memory_bandwidth_gbps = default_memory_bandwidth_gbps(gpu->type);
    }

    if (gpu->pcie_bandwidth_gbps <= 0.0f && gpu->pcie_gen != MVGAL_PCIE_UNKNOWN &&
        gpu->pcie_lanes > 0U) {
        gpu->pcie_bandwidth_gbps = pcie_bandwidth_from_link(gpu->pcie_gen, gpu->pcie_lanes);
    }

    if (gpu->thermal_design_power_w <= 0.0f) {
        gpu->thermal_design_power_w = default_tdp_watts(gpu->type);
    }

    if (gpu->driver_name[0] == '\0') {
        if (gpu->nvidia_node[0] != '\0') {
            (void)snprintf(gpu->driver_name, sizeof(gpu->driver_name), "nvidia");
        } else if (gpu->drm_node[0] != '\0') {
            (void)snprintf(gpu->driver_name, sizeof(gpu->driver_name), "drm");
        } else {
            (void)snprintf(gpu->driver_name, sizeof(gpu->driver_name), "unknown");
        }
    }

    if (gpu->driver_version[0] == '\0') {
        (void)snprintf(gpu->driver_version, sizeof(gpu->driver_version), "0.0");
    }

    if (gpu->name[0] == '\0') {
        vram_total_gb = (double)gpu->vram_total / (1024.0 * 1024.0 * 1024.0);
        (void)snprintf(gpu->name,
                       sizeof(gpu->name),
                       "%s GPU %04X (%u GiB)",
                       vendor_name(gpu->vendor),
                       (unsigned int)gpu->device_id,
                       (unsigned int)vram_total_gb);
    }

    gpu->driver_loaded = gpu->driver_name[0] != '\0';
    gpu->available = path_exists(gpu->drm_node) || path_exists(gpu->drm_render_node) ||
                     path_exists(gpu->nvidia_node) || path_exists(gpu->kfd_node);
    gpu->enabled = true;
}

static bool find_render_node_for_device(const char *device_realpath,
                                        char *render_node,
                                        size_t render_node_size)
{
    DIR *drm_dir;
    struct dirent *entry;
    char link_path[PATH_MAX];
    char entry_realpath[PATH_MAX];

    if (device_realpath == NULL || render_node == NULL || render_node_size == 0U) {
        return false;
    }

    drm_dir = opendir("/sys/class/drm");
    if (drm_dir == NULL) {
        return false;
    }

    while ((entry = readdir(drm_dir)) != NULL) {
        uint32_t unused_index;

        if (!parse_indexed_node(entry->d_name, "renderD", &unused_index)) {
            continue;
        }

        (void)snprintf(link_path, sizeof(link_path), "/sys/class/drm/%s/device", entry->d_name);
        if (realpath(link_path, entry_realpath) == NULL) {
            continue;
        }

        if (strcmp(entry_realpath, device_realpath) == 0) {
            snprintf(render_node, render_node_size, "/dev/dri/%.30s", entry->d_name);
            closedir(drm_dir);
            return true;
        }
    }

    closedir(drm_dir);
    return false;
}

static mvgal_error_t scan_drm_devices_locked(void)
{
    DIR *drm_dir;
    struct dirent *entry;

    drm_dir = opendir("/sys/class/drm");
    if (drm_dir == NULL) {
        MVGAL_LOG_DEBUG("No DRM sysfs nodes found");
        return MVGAL_SUCCESS;
    }

    while ((entry = readdir(drm_dir)) != NULL && g_gpu_manager.gpu_count < MAX_GPUS) {
        uint32_t card_index;
        char device_link[PATH_MAX];
        char device_realpath[PATH_MAX];
        char path[PATH_MAX];
        char link_speed[64];
        uint64_t used_vram;
        mvgal_gpu_descriptor_t *gpu;
        static const char *const total_candidates[] = {
            "mem_info_vram_total",
            "mem_info_vis_vram_total",
            "lmem_total_bytes",
        };
        static const char *const used_candidates[] = {
            "mem_info_vram_used",
            "mem_info_vis_vram_used",
            "lmem_used_bytes",
        };

        if (!parse_indexed_node(entry->d_name, "card", &card_index)) {
            continue;
        }

        (void)snprintf(device_link, sizeof(device_link), "/sys/class/drm/%s/device", entry->d_name);
        if (realpath(device_link, device_realpath) == NULL) {
            continue;
        }

        gpu = &g_gpu_manager.gpus[g_gpu_manager.gpu_count];
        memset(gpu, 0, sizeof(*gpu));
        gpu->id = g_gpu_manager.gpu_count;
        gpu->memory_type = MVGAL_MEMORY_TYPE_UNKNOWN;
        gpu->pcie_gen = MVGAL_PCIE_UNKNOWN;
        snprintf(gpu->drm_node, sizeof(gpu->drm_node), "/dev/dri/%.30s", entry->d_name);

        (void)find_render_node_for_device(device_realpath,
                                          gpu->drm_render_node,
                                          sizeof(gpu->drm_render_node));

        (void)snprintf(path, sizeof(path), "%s/vendor", device_link);
        (void)read_file_u16(path, &gpu->vendor_id);
        (void)snprintf(path, sizeof(path), "%s/device", device_link);
        (void)read_file_u16(path, &gpu->device_id);
        (void)snprintf(path, sizeof(path), "%s/subsystem_vendor", device_link);
        (void)read_file_u16(path, &gpu->subsystem_vendor_id);
        (void)snprintf(path, sizeof(path), "%s/subsystem_device", device_link);
        (void)read_file_u16(path, &gpu->subsystem_id);

        (void)parse_pci_address(device_realpath,
                                &gpu->pci_domain,
                                &gpu->pci_bus,
                                &gpu->pci_device,
                                &gpu->pci_function);

        (void)snprintf(path, sizeof(path), "%s/current_link_speed", device_link);
        if (read_file_trimmed(path, link_speed, sizeof(link_speed))) {
            gpu->pcie_gen = parse_pcie_generation(link_speed);
        }

        (void)snprintf(path, sizeof(path), "%s/current_link_width", device_link);
        {
            uint64_t lanes = 0;
            if (read_file_u64(path, &lanes) && lanes <= UINT8_MAX) {
                gpu->pcie_lanes = (uint8_t)lanes;
            }
        }

        gpu->type = gpu->vendor_id == PCI_VENDOR_ID_INTEL
            ? MVGAL_GPU_TYPE_INTEGRATED
            : MVGAL_GPU_TYPE_DISCRETE;

        gpu->vram_total = read_first_u64(device_link,
                                         total_candidates,
                                         MVGAL_ARRAY_LEN(total_candidates));
        used_vram = read_first_u64(device_link,
                                   used_candidates,
                                   MVGAL_ARRAY_LEN(used_candidates));
        gpu->vram_used = used_vram;

        (void)snprintf(path, sizeof(path), "%s/driver", device_link);
        (void)read_link_basename(path, gpu->driver_name, sizeof(gpu->driver_name));

        if (gpu->vendor_id == PCI_VENDOR_ID_AMD && path_exists("/dev/kfd")) {
            (void)snprintf(gpu->kfd_node, sizeof(gpu->kfd_node), "/dev/kfd");
        }

        finalize_gpu_descriptor(gpu);
        g_gpu_manager.health_thresholds[g_gpu_manager.gpu_count] = DEFAULT_HEALTH_THRESHOLDS;
        g_gpu_manager.gpu_count++;
    }

    closedir(drm_dir);
    return MVGAL_SUCCESS;
}

static mvgal_error_t scan_nvidia_devices_locked(void)
{
    DIR *dev_dir;
    struct dirent *entry;

    dev_dir = opendir("/dev");
    if (dev_dir == NULL) {
        return MVGAL_SUCCESS;
    }

    while ((entry = readdir(dev_dir)) != NULL && g_gpu_manager.gpu_count < MAX_GPUS) {
        uint32_t nvidia_index;
        char node_path[64];
        bool attached = false;

        if (!parse_indexed_node(entry->d_name, "nvidia", &nvidia_index)) {
            continue;
        }

        snprintf(node_path, sizeof(node_path), "/dev/%.30s", entry->d_name);

        for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
            mvgal_gpu_descriptor_t *gpu = &g_gpu_manager.gpus[i];

            if (gpu->vendor == MVGAL_VENDOR_NVIDIA && gpu->nvidia_node[0] == '\0') {
                (void)snprintf(gpu->nvidia_node, sizeof(gpu->nvidia_node), "%s", node_path);
                finalize_gpu_descriptor(gpu);
                attached = true;
                break;
            }
        }

        if (attached) {
            continue;
        }

        mvgal_gpu_descriptor_t *gpu = &g_gpu_manager.gpus[g_gpu_manager.gpu_count];
        memset(gpu, 0, sizeof(*gpu));
        gpu->id = g_gpu_manager.gpu_count;
        gpu->memory_type = MVGAL_MEMORY_TYPE_UNKNOWN;
        gpu->pcie_gen = MVGAL_PCIE_UNKNOWN;
        gpu->vendor = MVGAL_VENDOR_NVIDIA;
        gpu->vendor_id = PCI_VENDOR_ID_NVIDIA;
        gpu->type = MVGAL_GPU_TYPE_DISCRETE;
        (void)snprintf(gpu->nvidia_node, sizeof(gpu->nvidia_node), "%s", node_path);
        (void)snprintf(gpu->driver_name, sizeof(gpu->driver_name), "nvidia");
        gpu->vram_total = 8ULL * 1024ULL * 1024ULL * 1024ULL;
        finalize_gpu_descriptor(gpu);
        g_gpu_manager.health_thresholds[g_gpu_manager.gpu_count] = DEFAULT_HEALTH_THRESHOLDS;
        g_gpu_manager.gpu_count++;
    }

    closedir(dev_dir);
    return MVGAL_SUCCESS;
}

static void add_placeholder_gpu_locked(void)
{
    mvgal_gpu_descriptor_t *gpu;

    if (g_gpu_manager.gpu_count >= MAX_GPUS) {
        return;
    }

    gpu = &g_gpu_manager.gpus[g_gpu_manager.gpu_count];
    memset(gpu, 0, sizeof(*gpu));
    gpu->id = g_gpu_manager.gpu_count;
    gpu->vendor = MVGAL_VENDOR_UNKNOWN;
    gpu->type = MVGAL_GPU_TYPE_VIRTUAL;
    gpu->memory_type = MVGAL_MEMORY_TYPE_UNIFIED;
    gpu->pcie_gen = MVGAL_PCIE_UNKNOWN;
    gpu->vram_total = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    (void)snprintf(gpu->name, sizeof(gpu->name), "MVGAL Placeholder GPU");
    (void)snprintf(gpu->driver_name, sizeof(gpu->driver_name), "mvgal");
    gpu->features = MVGAL_FEATURE_GRAPHICS | MVGAL_FEATURE_COMPUTE;
    gpu->api_count = (uint32_t)api_mask_to_list(MVGAL_API_VULKAN | MVGAL_API_OPENGL | MVGAL_API_OPENCL,
                                                gpu->supported_apis,
                                                MVGAL_ARRAY_LEN(gpu->supported_apis));
    gpu->compute_score = 10.0f;
    gpu->graphics_score = 10.0f;
    finalize_gpu_descriptor(gpu);
    gpu->available = false;
    gpu->driver_loaded = false;
    g_gpu_manager.health_thresholds[g_gpu_manager.gpu_count] = DEFAULT_HEALTH_THRESHOLDS;
    g_gpu_manager.gpu_count++;
}

static void restore_persisted_state_locked(const mvgal_gpu_descriptor_t *old_gpus,
                                           const mvgal_gpu_health_thresholds_t *old_thresholds,
                                           uint32_t old_gpu_count)
{
    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
        bool restored = false;

        for (uint32_t j = 0; j < old_gpu_count; ++j) {
            if (gpu_identity_matches(&g_gpu_manager.gpus[i], &old_gpus[j])) {
                g_gpu_manager.gpus[i].enabled = old_gpus[j].enabled;
                g_gpu_manager.health_thresholds[i] = old_thresholds[j];
                restored = true;
                break;
            }
        }

        if (!restored) {
            g_gpu_manager.health_thresholds[i] = DEFAULT_HEALTH_THRESHOLDS;
        }

        g_gpu_manager.gpus[i].id = i;
    }
}

static mvgal_error_t rescan_gpus_locked(mvgal_gpu_descriptor_t *snapshot, uint32_t *snapshot_count)
{
    mvgal_gpu_descriptor_t old_gpus[MAX_GPUS];
    mvgal_gpu_health_thresholds_t old_thresholds[MAX_GPUS];
    uint32_t old_gpu_count = g_gpu_manager.gpu_count;

    memcpy(old_gpus, g_gpu_manager.gpus, sizeof(old_gpus));
    memcpy(old_thresholds, g_gpu_manager.health_thresholds, sizeof(old_thresholds));
    memset(g_gpu_manager.gpus, 0, sizeof(g_gpu_manager.gpus));
    memset(g_gpu_manager.health_thresholds, 0, sizeof(g_gpu_manager.health_thresholds));
    g_gpu_manager.gpu_count = 0;

    (void)scan_drm_devices_locked();
    (void)scan_nvidia_devices_locked();

    if (g_gpu_manager.gpu_count == 0U) {
        MVGAL_LOG_WARN("No physical GPUs detected, exposing placeholder GPU");
        add_placeholder_gpu_locked();
    }

    restore_persisted_state_locked(old_gpus, old_thresholds, old_gpu_count);
    g_gpu_manager.scanned = true;

    if (snapshot != NULL && snapshot_count != NULL) {
        memcpy(snapshot,
               g_gpu_manager.gpus,
               sizeof(g_gpu_manager.gpus[0]) * g_gpu_manager.gpu_count);
        *snapshot_count = g_gpu_manager.gpu_count;
    }

    return MVGAL_SUCCESS;
}

static void notify_gpu_callbacks(const mvgal_gpu_descriptor_t *snapshot, uint32_t snapshot_count)
{
    mvgal_gpu_callback_entry_t callbacks[MAX_GPU_CALLBACKS];
    uint32_t callback_count = 0;

    if (snapshot == NULL || snapshot_count == 0U) {
        return;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < MVGAL_ARRAY_LEN(g_gpu_manager.callbacks); ++i) {
        if (g_gpu_manager.callbacks[i].callback != NULL) {
            callbacks[callback_count++] = g_gpu_manager.callbacks[i];
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    for (uint32_t cb = 0; cb < callback_count; ++cb) {
        for (uint32_t gpu_index = 0; gpu_index < snapshot_count; ++gpu_index) {
            callbacks[cb].callback(&snapshot[gpu_index], callbacks[cb].user_data);
        }
    }
}

static mvgal_error_t ensure_gpu_manager_initialized(void)
{
    if (g_gpu_manager.initialized) {
        return MVGAL_SUCCESS;
    }

    return mvgal_gpu_manager_init();
}

static mvgal_gpu_health_level_t evaluate_health_level(
    const mvgal_gpu_health_status_t *status,
    const mvgal_gpu_health_thresholds_t *thresholds)
{
    float memory_usage_percent = 0.0f;

    if (status == NULL || thresholds == NULL) {
        return MVGAL_HEALTH_UNKNOWN;
    }

    if (status->memory_total_mb > 0.0f) {
        memory_usage_percent = (status->memory_used_mb / status->memory_total_mb) * 100.0f;
    }

    if (status->temperature_celsius >= thresholds->temp_critical_celsius ||
        status->utilization_percent >= thresholds->utilization_critical ||
        memory_usage_percent >= thresholds->memory_critical) {
        return MVGAL_HEALTH_CRITICAL;
    }

    if (status->temperature_celsius >= thresholds->temp_warning_celsius ||
        status->utilization_percent >= thresholds->utilization_warning ||
        memory_usage_percent >= thresholds->memory_warning) {
        return MVGAL_HEALTH_WARNING;
    }

    return MVGAL_HEALTH_OK;
}

static mvgal_error_t build_health_status_locked(uint32_t index, mvgal_gpu_health_status_t *status)
{
    const mvgal_gpu_descriptor_t *gpu;
    mvgal_gpu_health_level_t level;

    if (status == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (index >= g_gpu_manager.gpu_count) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    gpu = &g_gpu_manager.gpus[index];
    status->gpu_index = index;
    status->temperature_celsius = gpu->temperature_celsius;
    status->temperature_max_celsius = 100.0f;
    status->utilization_percent = gpu->utilization_percent;
    status->memory_used_mb = (float)((double)gpu->vram_used / (1024.0 * 1024.0));
    status->memory_total_mb = (float)((double)gpu->vram_total / (1024.0 * 1024.0));
    status->timestamp_ns = get_time_ns();

    level = evaluate_health_level(status, &g_gpu_manager.health_thresholds[index]);
    status->is_healthy = level == MVGAL_HEALTH_OK;

    return MVGAL_SUCCESS;
}

static void *health_monitor_thread(void *arg)
{
    (void)arg;

    while (g_health_monitor.running) {
        if (g_health_monitor.enabled && g_health_monitor.callback != NULL) {
            uint32_t gpu_count = 0;

            if (mvgal_gpu_get_count() > 0) {
                gpu_count = (uint32_t)mvgal_gpu_get_count();
            }

            for (uint32_t i = 0; i < gpu_count; ++i) {
                mvgal_gpu_health_status_t status;
                mvgal_gpu_health_thresholds_t thresholds;

                if (mvgal_gpu_get_health_status(i, &status) == MVGAL_SUCCESS &&
                    mvgal_gpu_get_health_thresholds(i, &thresholds) == MVGAL_SUCCESS) {
                    g_health_monitor.callback(i,
                                              &status,
                                              &thresholds,
                                              g_health_monitor.callback_user_data);
                }
            }
        }

        if (g_health_monitor.poll_interval_ms > 0U) {
            struct timespec sleep_time = {
                .tv_sec = (time_t)(g_health_monitor.poll_interval_ms / 1000U),
                .tv_nsec = (long)((g_health_monitor.poll_interval_ms % 1000U) * 1000000U),
            };
            nanosleep(&sleep_time, NULL);
        }
    }

    return NULL;
}

mvgal_error_t mvgal_gpu_manager_init(void)
{
    mvgal_gpu_descriptor_t snapshot[MAX_GPUS];
    uint32_t snapshot_count = 0;

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (g_gpu_manager.initialized) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_SUCCESS;
    }

    MVGAL_LOG_INFO("Initializing GPU manager");
    g_gpu_manager.initialized = true;
    (void)rescan_gpus_locked(snapshot, &snapshot_count);
    pthread_mutex_unlock(&g_gpu_manager.lock);

    MVGAL_LOG_INFO("Found %u GPU(s)", snapshot_count);
    notify_gpu_callbacks(snapshot, snapshot_count);
    return MVGAL_SUCCESS;
}

void mvgal_gpu_manager_shutdown(void)
{
    if (g_health_monitor.running) {
        (void)mvgal_gpu_enable_health_monitoring(false, 0);
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (!g_gpu_manager.initialized) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return;
    }

    MVGAL_LOG_INFO("Shutting down GPU manager");
    memset(g_gpu_manager.gpus, 0, sizeof(g_gpu_manager.gpus));
    memset(g_gpu_manager.health_thresholds, 0, sizeof(g_gpu_manager.health_thresholds));
    g_gpu_manager.gpu_count = 0;
    g_gpu_manager.scanned = false;
    g_gpu_manager.initialized = false;
    pthread_mutex_unlock(&g_gpu_manager.lock);
}

int32_t mvgal_gpu_get_count(void)
{
    int32_t count;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return (int32_t)MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    count = (int32_t)g_gpu_manager.gpu_count;
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return count;
}

int32_t mvgal_gpu_enumerate(mvgal_gpu_descriptor_t *gpus, uint32_t count)
{
    uint32_t to_copy;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return (int32_t)MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (gpus == NULL || count == 0U) {
        to_copy = g_gpu_manager.gpu_count;
    } else {
        to_copy = count < g_gpu_manager.gpu_count ? count : g_gpu_manager.gpu_count;
        memcpy(gpus, g_gpu_manager.gpus, sizeof(*gpus) * to_copy);
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return (int32_t)to_copy;
}

mvgal_error_t mvgal_gpu_get_descriptor(uint32_t index, mvgal_gpu_descriptor_t *gpu)
{
    if (gpu == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    *gpu = g_gpu_manager.gpus[index];
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_find_by_pci(uint16_t domain,
                                    uint8_t bus,
                                    uint8_t device,
                                    uint8_t function,
                                    mvgal_gpu_descriptor_t *gpu)
{
    if (gpu == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
        const mvgal_gpu_descriptor_t *candidate = &g_gpu_manager.gpus[i];

        if (candidate->pci_domain == domain &&
            candidate->pci_bus == bus &&
            candidate->pci_device == device &&
            candidate->pci_function == function) {
            *gpu = *candidate;
            pthread_mutex_unlock(&g_gpu_manager.lock);
            return MVGAL_SUCCESS;
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_ERROR_GPU_NOT_FOUND;
}

mvgal_error_t mvgal_gpu_find_by_node(const char *node_path, mvgal_gpu_descriptor_t *gpu)
{
    if (node_path == NULL || gpu == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
        const mvgal_gpu_descriptor_t *candidate = &g_gpu_manager.gpus[i];

        if (node_matches(candidate->drm_node, node_path) ||
            node_matches(candidate->drm_render_node, node_path) ||
            node_matches(candidate->nvidia_node, node_path) ||
            node_matches(candidate->kfd_node, node_path)) {
            *gpu = *candidate;
            pthread_mutex_unlock(&g_gpu_manager.lock);
            return MVGAL_SUCCESS;
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_ERROR_GPU_NOT_FOUND;
}

int32_t mvgal_gpu_find_by_vendor(mvgal_vendor_t vendor,
                                 mvgal_gpu_descriptor_t *gpus,
                                 uint32_t count)
{
    uint32_t found = 0;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return (int32_t)MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
        if (g_gpu_manager.gpus[i].vendor != vendor) {
            continue;
        }

        if (gpus != NULL && found < count) {
            gpus[found] = g_gpu_manager.gpus[i];
        }
        found++;
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return (int32_t)found;
}

mvgal_error_t mvgal_gpu_select_best(const mvgal_gpu_selection_criteria_t *criteria,
                                    mvgal_gpu_descriptor_t *selected)
{
    float best_score = -1.0f;
    bool found = false;

    if (selected == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
        const mvgal_gpu_descriptor_t *gpu = &g_gpu_manager.gpus[i];
        float score = 0.0f;

        if (!gpu->enabled) {
            continue;
        }

        if (criteria != NULL) {
            mvgal_api_type_t mask = gpu_api_mask(gpu);

            if (criteria->required_features != 0U &&
                (gpu->features & criteria->required_features) != criteria->required_features) {
                continue;
            }

            if (criteria->required_api != MVGAL_API_NONE &&
                (mask & criteria->required_api) != criteria->required_api) {
                continue;
            }

            if (criteria->min_vram > 0U && gpu->vram_total < criteria->min_vram) {
                continue;
            }

            if (criteria->use_compute_score) {
                score += gpu->compute_score;
            }
            if (criteria->use_graphics_score) {
                score += gpu->graphics_score;
            }
            if (criteria->use_memory) {
                score += (float)((double)gpu->vram_free / (1024.0 * 1024.0 * 1024.0));
            }
            if (criteria->use_features && criteria->preferred_features != 0U) {
                uint64_t matched = gpu->features & criteria->preferred_features;
                score += 5.0f * (float)__builtin_popcountll((unsigned long long)matched);
            }
            if (criteria->preferred_vendor != MVGAL_VENDOR_UNKNOWN &&
                gpu->vendor == criteria->preferred_vendor) {
                score += 10.0f;
            }
        } else {
            score = gpu_selection_score(gpu);
        }

        score += gpu->available ? 5.0f : 0.0f;

        if (!found || score > best_score) {
            best_score = score;
            *selected = *gpu;
            found = true;
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return found ? MVGAL_SUCCESS : MVGAL_ERROR_GPU_NOT_FOUND;
}

mvgal_error_t mvgal_gpu_enable(uint32_t index, bool enable)
{
    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    g_gpu_manager.gpus[index].enabled = enable;
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return MVGAL_SUCCESS;
}

bool mvgal_gpu_is_enabled(uint32_t index)
{
    bool enabled = false;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return false;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index < g_gpu_manager.gpu_count) {
        enabled = g_gpu_manager.gpus[index].enabled;
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return enabled;
}

mvgal_error_t mvgal_gpu_enable_all(void)
{
    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
        g_gpu_manager.gpus[i].enabled = true;
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_disable_all(void)
{
    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
        g_gpu_manager.gpus[i].enabled = false;
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_primary(mvgal_gpu_descriptor_t *gpu)
{
    return mvgal_gpu_select_best(NULL, gpu);
}

bool mvgal_gpu_has_feature(uint32_t index, uint64_t feature)
{
    bool result = false;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return false;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index < g_gpu_manager.gpu_count) {
        result = (g_gpu_manager.gpus[index].features & feature) == feature;
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return result;
}

bool mvgal_gpu_has_api(uint32_t index, mvgal_api_type_t api)
{
    bool result = false;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return false;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index < g_gpu_manager.gpu_count) {
        result = (gpu_api_mask(&g_gpu_manager.gpus[index]) & api) == api;
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return result;
}

mvgal_error_t mvgal_gpu_get_memory_stats(uint32_t index,
                                         uint64_t *total,
                                         uint64_t *used,
                                         uint64_t *free)
{
    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    if (total != NULL) {
        *total = g_gpu_manager.gpus[index].vram_total;
    }
    if (used != NULL) {
        *used = g_gpu_manager.gpus[index].vram_used;
    }
    if (free != NULL) {
        *free = g_gpu_manager.gpus[index].vram_free;
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_memory_info(uint32_t index,
                                        uint64_t *total,
                                        uint64_t *used,
                                        uint64_t *free)
{
    return mvgal_gpu_get_memory_stats(index, total, used, free);
}

mvgal_error_t mvgal_gpu_get_utilization(uint32_t index, float *utilization)
{
    if (utilization == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    *utilization = g_gpu_manager.gpus[index].utilization_percent;
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_temperature(uint32_t index, float *temperature)
{
    if (temperature == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    *temperature = g_gpu_manager.gpus[index].temperature_celsius;
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_power(uint32_t index, float *power_w)
{
    if (power_w == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    *power_w = g_gpu_manager.gpus[index].current_power_w;
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_register_callback(mvgal_gpu_callback_t callback, void *user_data)
{
    if (callback == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < MVGAL_ARRAY_LEN(g_gpu_manager.callbacks); ++i) {
        if (g_gpu_manager.callbacks[i].callback == callback) {
            pthread_mutex_unlock(&g_gpu_manager.lock);
            return MVGAL_ERROR_ALREADY_INITIALIZED;
        }
        if (g_gpu_manager.callbacks[i].callback == NULL) {
            g_gpu_manager.callbacks[i].callback = callback;
            g_gpu_manager.callbacks[i].user_data = user_data;
            pthread_mutex_unlock(&g_gpu_manager.lock);
            return MVGAL_SUCCESS;
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_ERROR_BUSY;
}

mvgal_error_t mvgal_gpu_unregister_callback(mvgal_gpu_callback_t callback)
{
    if (callback == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < MVGAL_ARRAY_LEN(g_gpu_manager.callbacks); ++i) {
        if (g_gpu_manager.callbacks[i].callback == callback) {
            g_gpu_manager.callbacks[i].callback = NULL;
            g_gpu_manager.callbacks[i].user_data = NULL;
            pthread_mutex_unlock(&g_gpu_manager.lock);
            return MVGAL_SUCCESS;
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_ERROR_NOT_FOUND;
}

mvgal_error_t mvgal_gpu_rescan(void)
{
    mvgal_gpu_descriptor_t snapshot[MAX_GPUS];
    uint32_t snapshot_count = 0;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    (void)rescan_gpus_locked(snapshot, &snapshot_count);
    pthread_mutex_unlock(&g_gpu_manager.lock);

    notify_gpu_callbacks(snapshot, snapshot_count);
    return MVGAL_SUCCESS;
}

mvgal_gpu_t mvgal_gpu_get_handle(uint32_t index)
{
    mvgal_gpu_t handle = NULL;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return NULL;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index < g_gpu_manager.gpu_count) {
        handle = (mvgal_gpu_t)&g_gpu_manager.gpus[index];
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return handle;
}

mvgal_gpu_t mvgal_gpu_get_handle_by_node(const char *node_path)
{
    mvgal_gpu_descriptor_t descriptor;

    if (mvgal_gpu_find_by_node(node_path, &descriptor) != MVGAL_SUCCESS) {
        return NULL;
    }

    return mvgal_gpu_get_handle(descriptor.id);
}

mvgal_error_t mvgal_gpu_register_driver(const char *name,
                                        void *probe_func,
                                        void *init_func,
                                        void *user_data)
{
    if (name == NULL || name[0] == '\0') {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (g_gpu_manager.driver_count >= MAX_CUSTOM_DRIVERS) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_BUSY;
    }

    (void)snprintf(g_gpu_manager.drivers[g_gpu_manager.driver_count].name,
                   sizeof(g_gpu_manager.drivers[g_gpu_manager.driver_count].name),
                   "%s",
                   name);
    g_gpu_manager.drivers[g_gpu_manager.driver_count].probe_func = probe_func;
    g_gpu_manager.drivers[g_gpu_manager.driver_count].init_func = init_func;
    g_gpu_manager.drivers[g_gpu_manager.driver_count].user_data = user_data;
    g_gpu_manager.driver_count++;
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_device_create(uint32_t gpu_count,
                                  const uint32_t *gpu_indices,
                                  void **device)
{
    mvgal_logical_device_t *logical_device;
    float best_member_score = -1.0f;
    bool first_member = true;

    if (device == NULL || gpu_indices == NULL || gpu_count == 0U || gpu_count > MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    logical_device = calloc(1, sizeof(*logical_device));
    if (logical_device == NULL) {
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }

    logical_device->magic = MVGAL_LOGICAL_DEVICE_MAGIC;
    logical_device->descriptor.gpu_count = gpu_count;
    logical_device->descriptor.descriptor.id = UINT32_MAX;
    logical_device->descriptor.descriptor.type = MVGAL_GPU_TYPE_VIRTUAL;
    logical_device->descriptor.descriptor.memory_type = MVGAL_MEMORY_TYPE_UNIFIED;
    logical_device->descriptor.descriptor.enabled = true;
    logical_device->descriptor.descriptor.available = true;
    (void)snprintf(logical_device->descriptor.descriptor.driver_name,
                   sizeof(logical_device->descriptor.descriptor.driver_name),
                   "mvgal");
    (void)snprintf(logical_device->descriptor.descriptor.driver_version,
                   sizeof(logical_device->descriptor.descriptor.driver_version),
                   "0.1");

    pthread_mutex_lock(&g_gpu_manager.lock);
    for (uint32_t i = 0; i < gpu_count; ++i) {
        const mvgal_gpu_descriptor_t *member;
        mvgal_api_type_t member_api_mask;
        float member_score;

        if (gpu_indices[i] >= g_gpu_manager.gpu_count) {
            pthread_mutex_unlock(&g_gpu_manager.lock);
            free(logical_device);
            return MVGAL_ERROR_GPU_NOT_FOUND;
        }

        for (uint32_t j = 0; j < i; ++j) {
            if (gpu_indices[j] == gpu_indices[i]) {
                pthread_mutex_unlock(&g_gpu_manager.lock);
                free(logical_device);
                return MVGAL_ERROR_INVALID_ARGUMENT;
            }
        }

        member = &g_gpu_manager.gpus[gpu_indices[i]];
        if (!member->enabled) {
            pthread_mutex_unlock(&g_gpu_manager.lock);
            free(logical_device);
            return MVGAL_ERROR_INCOMPATIBLE;
        }

        logical_device->descriptor.gpu_indices[i] = gpu_indices[i];
        logical_device->descriptor.gpu_mask |= (1ULL << gpu_indices[i]);
        logical_device->descriptor.descriptor.vram_total += member->vram_total;
        logical_device->descriptor.descriptor.vram_free += member->vram_free;
        logical_device->descriptor.descriptor.vram_used += member->vram_used;
        logical_device->descriptor.descriptor.compute_score += member->compute_score;
        logical_device->descriptor.descriptor.graphics_score += member->graphics_score;
        logical_device->descriptor.descriptor.memory_bandwidth_gbps += member->memory_bandwidth_gbps;
        logical_device->descriptor.descriptor.pcie_bandwidth_gbps += member->pcie_bandwidth_gbps;
        logical_device->descriptor.descriptor.current_power_w += member->current_power_w;
        if (member->temperature_celsius > logical_device->descriptor.descriptor.temperature_celsius) {
            logical_device->descriptor.descriptor.temperature_celsius = member->temperature_celsius;
        }

        member_api_mask = gpu_api_mask(member);
        logical_device->descriptor.aggregate_features |= member->features;
        logical_device->descriptor.aggregate_api_mask |= member_api_mask;

        if (first_member) {
            logical_device->descriptor.common_features = member->features;
            logical_device->descriptor.common_api_mask = member_api_mask;
            logical_device->descriptor.descriptor.vendor = member->vendor;
            logical_device->descriptor.descriptor.memory_type = member->memory_type;
            first_member = false;
        } else {
            logical_device->descriptor.common_features &= member->features;
            logical_device->descriptor.common_api_mask &= member_api_mask;
            if (logical_device->descriptor.descriptor.vendor != member->vendor ||
                logical_device->descriptor.descriptor.memory_type != member->memory_type) {
                logical_device->descriptor.heterogeneous = true;
            }
        }

        member_score = gpu_selection_score(member);
        if (member_score > best_member_score) {
            best_member_score = member_score;
            logical_device->descriptor.primary_gpu_index = gpu_indices[i];
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    logical_device->descriptor.descriptor.features = logical_device->descriptor.common_features;
    logical_device->descriptor.descriptor.api_count =
        (uint32_t)api_mask_to_list(logical_device->descriptor.common_api_mask,
                                   logical_device->descriptor.descriptor.supported_apis,
                                   MVGAL_ARRAY_LEN(logical_device->descriptor.descriptor.supported_apis));

    if (logical_device->descriptor.heterogeneous) {
        logical_device->descriptor.descriptor.vendor = MVGAL_VENDOR_UNKNOWN;
        logical_device->descriptor.descriptor.memory_type = MVGAL_MEMORY_TYPE_UNIFIED;
    }

    (void)snprintf(logical_device->descriptor.descriptor.name,
                   sizeof(logical_device->descriptor.descriptor.name),
                   "MVGAL Logical GPU (%u member%s)",
                   gpu_count,
                   gpu_count == 1U ? "" : "s");

    *device = logical_device;
    return MVGAL_SUCCESS;
}

void mvgal_device_destroy(void *device)
{
    mvgal_logical_device_t *logical_device = (mvgal_logical_device_t *)device;

    if (logical_device == NULL || logical_device->magic != MVGAL_LOGICAL_DEVICE_MAGIC) {
        return;
    }

    logical_device->magic = 0U;
    free(logical_device);
}

mvgal_error_t mvgal_device_get_descriptor(void *device,
                                          mvgal_logical_device_descriptor_t *descriptor)
{
    mvgal_logical_device_t *logical_device = (mvgal_logical_device_t *)device;

    if (logical_device == NULL || descriptor == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (logical_device->magic != MVGAL_LOGICAL_DEVICE_MAGIC) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    *descriptor = logical_device->descriptor;
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_health_status(uint32_t index,
                                          mvgal_gpu_health_status_t *status)
{
    mvgal_error_t result;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    result = build_health_status_locked(index, status);
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return result;
}

mvgal_error_t mvgal_gpu_get_health_level(uint32_t index,
                                         mvgal_gpu_health_level_t *level)
{
    mvgal_gpu_health_status_t status;
    mvgal_gpu_health_thresholds_t thresholds;
    mvgal_error_t result;

    if (level == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    result = build_health_status_locked(index, &status);
    if (result == MVGAL_SUCCESS) {
        thresholds = g_gpu_manager.health_thresholds[index];
        *level = evaluate_health_level(&status, &thresholds);
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return result;
}

bool mvgal_gpu_all_healthy(void)
{
    bool healthy = true;

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return false;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (g_gpu_manager.gpu_count == 0U) {
        healthy = false;
    } else {
        for (uint32_t i = 0; i < g_gpu_manager.gpu_count; ++i) {
            mvgal_gpu_health_status_t status;

            if (build_health_status_locked(i, &status) != MVGAL_SUCCESS || !status.is_healthy) {
                healthy = false;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_gpu_manager.lock);

    return healthy;
}

mvgal_error_t mvgal_gpu_get_health_thresholds(uint32_t index,
                                              mvgal_gpu_health_thresholds_t *thresholds)
{
    if (thresholds == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    *thresholds = g_gpu_manager.health_thresholds[index];
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_set_health_thresholds(
    uint32_t index,
    const mvgal_gpu_health_thresholds_t *thresholds)
{
    if (thresholds == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (ensure_gpu_manager_initialized() != MVGAL_SUCCESS) {
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_lock(&g_gpu_manager.lock);
    if (index >= g_gpu_manager.gpu_count) {
        pthread_mutex_unlock(&g_gpu_manager.lock);
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    g_gpu_manager.health_thresholds[index] = *thresholds;
    pthread_mutex_unlock(&g_gpu_manager.lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_register_health_callback(
    mvgal_gpu_health_callback_t callback,
    void *user_data)
{
    if (callback == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (g_health_monitor.callback != NULL) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }

    g_health_monitor.callback = callback;
    g_health_monitor.callback_user_data = user_data;
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_unregister_health_callback(
    mvgal_gpu_health_callback_t callback)
{
    if (callback == NULL || g_health_monitor.callback != callback) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    g_health_monitor.callback = NULL;
    g_health_monitor.callback_user_data = NULL;
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_enable_health_monitoring(bool enabled,
                                                 uint32_t poll_interval_ms)
{
    if (enabled && !g_health_monitor.running) {
        g_health_monitor.enabled = true;
        g_health_monitor.poll_interval_ms = poll_interval_ms > 0U ? poll_interval_ms : 1000U;
        g_health_monitor.running = true;
        if (pthread_create(&g_health_monitor.thread, NULL, health_monitor_thread, NULL) != 0) {
            g_health_monitor.running = false;
            return MVGAL_ERROR_DRIVER;
        }
        MVGAL_LOG_INFO("Health monitoring started (interval=%u ms)",
                       g_health_monitor.poll_interval_ms);
    } else if (enabled) {
        g_health_monitor.enabled = true;
        g_health_monitor.poll_interval_ms = poll_interval_ms > 0U ? poll_interval_ms
                                                                  : g_health_monitor.poll_interval_ms;
    } else if (!enabled && g_health_monitor.running) {
        g_health_monitor.running = false;
        pthread_join(g_health_monitor.thread, NULL);
        g_health_monitor.enabled = false;
        MVGAL_LOG_INFO("Health monitoring stopped");
    } else {
        g_health_monitor.enabled = false;
    }

    return MVGAL_SUCCESS;
}
