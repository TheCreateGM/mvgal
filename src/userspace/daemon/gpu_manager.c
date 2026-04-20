/**
 * @file gpu_manager.c
 * @brief GPU detection and management implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module is responsible for detecting all available GPUs on the system,
 * gathering their capabilities, and managing their lifecycle.
 *
 * Note: This is a stub implementation without libdrm-dev dependency.
 * Full implementation would use libdrm to query DRM devices.
 */

#include "mvgal_gpu.h"
#include "mvgal_log.h"
#include "mvgal_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

// =============================================================================
// Time Helper
// =============================================================================

/**
 * @brief Get current time in nanoseconds
 *
 * @return Current time in nanoseconds since epoch
 */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// PCI vendor IDs
#define PCI_VENDOR_ID_AMD       0x1002
#define PCI_VENDOR_ID_NVIDIA    0x10DE
#define PCI_VENDOR_ID_INTEL     0x8086

// Maximum number of GPUs
#define MAX_GPUS 16

// GPU detection state
static struct {
    mvgal_gpu_descriptor_t gpus[MAX_GPUS];
    uint32_t gpu_count;
    bool initialized;
    bool scanned;
} g_gpu_manager = {0};

// Forward declarations
static mvgal_error_t scan_drm_devices(void);
static mvgal_error_t scan_nvidia_devices(void);

/**
 * @brief Initialize GPU manager
 */
mvgal_error_t mvgal_gpu_manager_init(void) {
    if (g_gpu_manager.initialized) {
        return MVGAL_SUCCESS;
    }
    
    MVGAL_LOG_INFO("Initializing GPU manager");
    
    g_gpu_manager.gpu_count = 0;
    g_gpu_manager.scanned = false;
    g_gpu_manager.initialized = true;
    
    // Perform initial scan
    scan_drm_devices();
    scan_nvidia_devices();
    
    if (g_gpu_manager.gpu_count == 0) {
        MVGAL_LOG_WARN("No GPUs detected - creating placeholder");
        // Create a dummy GPU for testing
        if (g_gpu_manager.gpu_count < MAX_GPUS) {
            mvgal_gpu_descriptor_t *gpu = &g_gpu_manager.gpus[g_gpu_manager.gpu_count];
            memset(gpu, 0, sizeof(*gpu));
            gpu->id = g_gpu_manager.gpu_count;
            snprintf(gpu->name, sizeof(gpu->name), "Unknown GPU %d", g_gpu_manager.gpu_count);
            gpu->vendor = MVGAL_VENDOR_UNKNOWN;
            gpu->type = MVGAL_GPU_TYPE_VIRTUAL;
            gpu->vram_total = (uint64_t)4 * 1024 * 1024 * 1024; // 4GB
            gpu->vram_free = gpu->vram_total;
            gpu->enabled = true;
            gpu->available = false;
            gpu->driver_loaded = false;
            gpu->features = 0;
            g_gpu_manager.gpu_count++;
        }
    }
    
    g_gpu_manager.scanned = true;
    MVGAL_LOG_INFO("Found %d GPU(s)", g_gpu_manager.gpu_count);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Shutdown GPU manager
 */
void mvgal_gpu_manager_shutdown(void) {
    if (!g_gpu_manager.initialized) {
        return;
    }
    
    MVGAL_LOG_INFO("Shutting down GPU manager");
    g_gpu_manager.gpu_count = 0;
    g_gpu_manager.scanned = false;
    g_gpu_manager.initialized = false;
}

/**
 * @brief Scan DRM devices (/dev/dri/)
 */
static mvgal_error_t scan_drm_devices(void) {
    DIR *drm_dir = opendir("/dev/dri");
    if (!drm_dir) {
        drm_dir = opendir("/dev/dri/by-path");
        if (!drm_dir) {
            MVGAL_LOG_DEBUG("No DRM devices found at /dev/dri");
            return MVGAL_SUCCESS;
        }
    }
    
    struct dirent *entry;
    while ((entry = readdir(drm_dir)) != NULL && g_gpu_manager.gpu_count < MAX_GPUS) {
        if (strncmp(entry->d_name, "card", 4) == 0 || 
            strncmp(entry->d_name, "render", 6) == 0) {
            
            mvgal_gpu_descriptor_t *gpu = &g_gpu_manager.gpus[g_gpu_manager.gpu_count];
            memset(gpu, 0, sizeof(*gpu));
            
            gpu->id = g_gpu_manager.gpu_count;
            snprintf(gpu->name, sizeof(gpu->name), "DRM GPU %d", g_gpu_manager.gpu_count);
            snprintf(gpu->drm_node, sizeof(gpu->drm_node), "/dev/dri/%.32s", entry->d_name);
            
            if (strstr(entry->d_name, "nvidia") || strstr(entry->d_name, "NVIDIA")) {
                gpu->vendor = MVGAL_VENDOR_NVIDIA;
            } else if (strstr(entry->d_name, "amd") || strstr(entry->d_name, "AMD")) {
                gpu->vendor = MVGAL_VENDOR_AMD;
            } else if (strstr(entry->d_name, "intel") || strstr(entry->d_name, "i915")) {
                gpu->vendor = MVGAL_VENDOR_INTEL;
            } else {
                gpu->vendor = MVGAL_VENDOR_UNKNOWN;
            }
            
            gpu->type = MVGAL_GPU_TYPE_DISCRETE;
            gpu->vram_total = (uint64_t)4 * 1024 * 1024 * 1024;
            gpu->vram_free = gpu->vram_total;
            gpu->enabled = true;
            gpu->available = true;
            gpu->driver_loaded = true;
            snprintf(gpu->driver_name, sizeof(gpu->driver_name), "drm");
            snprintf(gpu->driver_version, sizeof(gpu->driver_version), "0.0");
            
            g_gpu_manager.gpu_count++;
            
            MVGAL_LOG_DEBUG("Found DRM GPU: %s", gpu->name);
        }
    }
    
    closedir(drm_dir);
    return MVGAL_SUCCESS;
}

/**
 * @brief Scan NVIDIA devices (/dev/nvidia*)
 */
static mvgal_error_t scan_nvidia_devices(void) {
    DIR *dev_dir = opendir("/dev");
    if (!dev_dir) {
        MVGAL_LOG_DEBUG("Cannot open /dev directory");
        return MVGAL_SUCCESS;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dev_dir)) != NULL && g_gpu_manager.gpu_count < MAX_GPUS) {
        if (strncmp(entry->d_name, "nvidia", 6) == 0) {
            mvgal_gpu_descriptor_t *gpu = &g_gpu_manager.gpus[g_gpu_manager.gpu_count];
            memset(gpu, 0, sizeof(*gpu));
            
            gpu->id = g_gpu_manager.gpu_count;
            snprintf(gpu->name, sizeof(gpu->name), "NVIDIA GPU %d", g_gpu_manager.gpu_count);
            snprintf(gpu->nvidia_node, sizeof(gpu->nvidia_node), "/dev/%.32s", entry->d_name);
            
            gpu->vendor = MVGAL_VENDOR_NVIDIA;
            gpu->type = MVGAL_GPU_TYPE_DISCRETE;
            gpu->vram_total = (uint64_t)8 * 1024 * 1024 * 1024;
            gpu->vram_free = gpu->vram_total;
            gpu->enabled = true;
            gpu->available = true;
            gpu->driver_loaded = true;
            snprintf(gpu->driver_name, sizeof(gpu->driver_name), "nvidia");
            snprintf(gpu->driver_version, sizeof(gpu->driver_version), "0.0");
            
            g_gpu_manager.gpu_count++;
            
            MVGAL_LOG_DEBUG("Found NVIDIA GPU: %s", gpu->name);
        }
    }
    
    closedir(dev_dir);
    return MVGAL_SUCCESS;
}

// =============================================================================
// Public API functions from mvgal_gpu.h
// =============================================================================

int32_t mvgal_gpu_get_count(void) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    return (int32_t)g_gpu_manager.gpu_count;
}

int32_t mvgal_gpu_enumerate(mvgal_gpu_descriptor_t *gpus, uint32_t count) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    
    if (gpus == NULL || count == 0) {
        return (int32_t)g_gpu_manager.gpu_count;
    }
    
    uint32_t to_copy = count < g_gpu_manager.gpu_count ? count : g_gpu_manager.gpu_count;
    memcpy(gpus, g_gpu_manager.gpus, to_copy * sizeof(mvgal_gpu_descriptor_t));
    return (int32_t)to_copy;
}

mvgal_error_t mvgal_gpu_get_descriptor(uint32_t index, mvgal_gpu_descriptor_t *gpu) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    
    if (index >= g_gpu_manager.gpu_count || gpu == NULL) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }
    
    *gpu = g_gpu_manager.gpus[index];
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_find_by_pci(
    uint16_t domain,
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    mvgal_gpu_descriptor_t *gpu
) {
    (void)domain;
    (void)bus;
    (void)device;
    (void)function;
    
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    
    if (gpu == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (g_gpu_manager.gpu_count > 0) {
        *gpu = g_gpu_manager.gpus[0];
        return MVGAL_SUCCESS;
    }
    
    return MVGAL_ERROR_GPU_NOT_FOUND;
}

mvgal_error_t mvgal_gpu_enable(uint32_t index, bool enable) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    
    if (index >= g_gpu_manager.gpu_count) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }
    
    g_gpu_manager.gpus[index].enabled = enable;
    MVGAL_LOG_DEBUG("GPU %d %s", index, enable ? "enabled" : "disabled");
    return MVGAL_SUCCESS;
}

bool mvgal_gpu_is_enabled(uint32_t index) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    
    if (index >= g_gpu_manager.gpu_count) {
        return false;
    }
    
    return g_gpu_manager.gpus[index].enabled;
}

mvgal_error_t mvgal_gpu_get_capabilities(uint32_t index, uint64_t *capabilities) {
    (void)index;
    (void)capabilities;
    return MVGAL_ERROR_NOT_SUPPORTED;
}

mvgal_error_t mvgal_gpu_register_callback(mvgal_gpu_callback_t callback, void *user_data) {
    (void)callback;
    (void)user_data;
    return MVGAL_ERROR_NOT_SUPPORTED;
}

mvgal_error_t mvgal_gpu_unregister_callback(mvgal_gpu_callback_t callback) {
    (void)callback;
    return MVGAL_ERROR_NOT_SUPPORTED;
}

mvgal_error_t mvgal_gpu_get_utilization(uint32_t index, float *utilization) {
    (void)index;
    if (utilization) {
        *utilization = 0.0f;
    }
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_temperature(uint32_t index, float *temperature) {
    (void)index;
    if (temperature) {
        *temperature = 0.0f;
    }
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_power(uint32_t index, float *power_w) {
    (void)index;
    if (power_w) {
        *power_w = 0.0f;
    }
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_get_memory_info(uint32_t index, uint64_t *total, uint64_t *used, uint64_t *free) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    
    if (index < g_gpu_manager.gpu_count) {
        if (total) *total = g_gpu_manager.gpus[index].vram_total;
        if (used) *used = g_gpu_manager.gpus[index].vram_used;
        if (free) *free = g_gpu_manager.gpus[index].vram_free;
        return MVGAL_SUCCESS;
    }
    if (total) *total = 0;
    if (used) *used = 0;
    if (free) *free = 0;
    return MVGAL_ERROR_GPU_NOT_FOUND;
}

mvgal_error_t mvgal_gpu_select_best(
    const mvgal_gpu_selection_criteria_t *criteria,
    mvgal_gpu_descriptor_t *selected
) {
    (void)criteria;
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }
    
    if (selected == NULL || g_gpu_manager.gpu_count == 0) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }
    
    *selected = g_gpu_manager.gpus[0];
    return MVGAL_SUCCESS;
}

bool mvgal_gpu_has_feature(uint32_t index, uint64_t feature) {
    (void)index;
    (void)feature;
    return false;
}

mvgal_error_t mvgal_gpu_get_memory_stats(
    uint32_t index,
    uint64_t *total,
    uint64_t *used,
    uint64_t *free
) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }

    if (index >= g_gpu_manager.gpu_count) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    // Return stub values for now
    if (total) *total = g_gpu_manager.gpus[index].vram_total;
    if (used) *used = g_gpu_manager.gpus[index].vram_used;
    if (free) *free = g_gpu_manager.gpus[index].vram_total - g_gpu_manager.gpus[index].vram_used;
    return MVGAL_SUCCESS;
}

// =============================================================================
// GPU Health Monitoring
// =============================================================================

/**
 * @brief Default health thresholds
 */
static const mvgal_gpu_health_thresholds_t DEFAULT_HEALTH_THRESHOLDS = {
    .temp_warning_celsius = 80.0f,    ///< Warning at 80°C
    .temp_critical_celsius = 95.0f,   ///< Critical at 95°C
    .utilization_warning = 80.0f,     ///< Warning at 80% utilization
    .utilization_critical = 95.0f,    ///< Critical at 95% utilization
    .memory_warning = 85.0f,          ///< Warning at 85% memory usage
    .memory_critical = 95.0f          ///< Critical at 95% memory usage
};

/**
 * @brief Health monitoring state
 */
static struct {
    bool enabled;
    uint32_t poll_interval_ms;
    bool running;
    pthread_t thread;
    mvgal_gpu_health_callback_t callback;
    void *callback_user_data;
} g_health_monitor = {0};

/**
 * @brief Check GPU health status
 *
 * @param index GPU index
 * @param status Health status (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
static mvgal_error_t check_gpu_health(uint32_t index, mvgal_gpu_health_status_t *status)
{
    if (index >= g_gpu_manager.gpu_count) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }

    const mvgal_gpu_descriptor_t *gpu = &g_gpu_manager.gpus[index];

    status->gpu_index = index;
    status->temperature_celsius = gpu->temperature_celsius;
    status->temperature_max_celsius = 100.0f; // Default max, can be configured
    status->utilization_percent = gpu->utilization_percent;
    status->memory_used_mb = gpu->vram_used / (1024.0f * 1024.0f);
    status->memory_total_mb = gpu->vram_total / (1024.0f * 1024.0f);
    status->timestamp_ns = get_time_ns();

    // Determine health status
    mvgal_gpu_health_level_t level = MVGAL_HEALTH_OK;

    // Check temperature
    if (status->temperature_celsius >= DEFAULT_HEALTH_THRESHOLDS.temp_critical_celsius) {
        level = MVGAL_HEALTH_CRITICAL;
    } else if (status->temperature_celsius >= DEFAULT_HEALTH_THRESHOLDS.temp_warning_celsius) {
        level = MVGAL_HEALTH_WARNING;
    }

    // Check utilization
    if (level < MVGAL_HEALTH_CRITICAL &&
        status->utilization_percent >= DEFAULT_HEALTH_THRESHOLDS.utilization_critical) {
        level = MVGAL_HEALTH_CRITICAL;
    } else if (level < MVGAL_HEALTH_WARNING &&
               status->utilization_percent >= DEFAULT_HEALTH_THRESHOLDS.utilization_warning) {
        level = MVGAL_HEALTH_WARNING;
    }

    // Check memory usage
    float memory_usage_percent = (status->memory_used_mb / status->memory_total_mb) * 100.0f;
    if (level < MVGAL_HEALTH_CRITICAL &&
        memory_usage_percent >= DEFAULT_HEALTH_THRESHOLDS.memory_critical) {
        level = MVGAL_HEALTH_CRITICAL;
    } else if (level < MVGAL_HEALTH_WARNING &&
               memory_usage_percent >= DEFAULT_HEALTH_THRESHOLDS.memory_warning) {
        level = MVGAL_HEALTH_WARNING;
    }

    status->is_healthy = (level == MVGAL_HEALTH_OK);
    return MVGAL_SUCCESS;
}

/**
 * @brief Health monitoring thread
 */
static void *health_monitor_thread(void *arg) {
    (void)arg;

    while (g_health_monitor.running) {
        if (g_health_monitor.enabled) {
            // Check all GPUs
            for (uint32_t i = 0; i < g_gpu_manager.gpu_count; i++) {
                mvgal_gpu_health_status_t status;
                if (check_gpu_health(i, &status) == MVGAL_SUCCESS) {
                    // Notify callback if registered
                    if (g_health_monitor.callback) {
                        g_health_monitor.callback(i, &status, &DEFAULT_HEALTH_THRESHOLDS,
                                                   g_health_monitor.callback_user_data);
                    }
                }
            }
        }

        // Sleep for the poll interval
        if (g_health_monitor.poll_interval_ms > 0) {
            uint32_t ms = g_health_monitor.poll_interval_ms;
            struct timespec ts = {
                .tv_sec = ms / 1000,
                .tv_nsec = (ms % 1000) * 1000000
            };
            nanosleep(&ts, NULL);
        }
    }

    return NULL;
}

// =============================================================================
// Public Health Monitoring API
// =============================================================================

mvgal_error_t mvgal_gpu_get_health_status(
    uint32_t index,
    mvgal_gpu_health_status_t *status
) {
    if (status == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    return check_gpu_health(index, status);
}

mvgal_error_t mvgal_gpu_get_health_level(
    uint32_t index,
    mvgal_gpu_health_level_t *level
) {
    if (level == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    mvgal_gpu_health_status_t status;
    mvgal_error_t err = check_gpu_health(index, &status);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    *level = status.is_healthy ? MVGAL_HEALTH_OK : MVGAL_HEALTH_WARNING;
    return MVGAL_SUCCESS;
}

bool mvgal_gpu_all_healthy(void) {
    if (!g_gpu_manager.initialized) {
        mvgal_gpu_manager_init();
    }

    for (uint32_t i = 0; i < g_gpu_manager.gpu_count; i++) {
        mvgal_gpu_health_status_t status;
        if (check_gpu_health(i, &status) != MVGAL_SUCCESS) {
            return false;
        }
        if (!status.is_healthy) {
            return false;
        }
    }

    return g_gpu_manager.gpu_count > 0;
}

mvgal_error_t mvgal_gpu_get_health_thresholds(
    uint32_t index,
    mvgal_gpu_health_thresholds_t *thresholds
) {
    (void)index; // Currently using global defaults
    if (thresholds == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    *thresholds = DEFAULT_HEALTH_THRESHOLDS;
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_set_health_thresholds(
    uint32_t index,
    const mvgal_gpu_health_thresholds_t *thresholds
) {
    (void)index;
    (void)thresholds;
    // For now, just accept the thresholds (they're global in this implementation)
    // In a full implementation, we'd store per-GPU thresholds
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_register_health_callback(
    mvgal_gpu_health_callback_t callback,
    void *user_data
) {
    if (g_health_monitor.callback != NULL) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }

    g_health_monitor.callback = callback;
    g_health_monitor.callback_user_data = user_data;
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_unregister_health_callback(
    mvgal_gpu_health_callback_t callback
) {
    if (g_health_monitor.callback != callback) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    g_health_monitor.callback = NULL;
    g_health_monitor.callback_user_data = NULL;
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_gpu_enable_health_monitoring(
    bool enabled,
    uint32_t poll_interval_ms
) {
    if (enabled && !g_health_monitor.running) {
        // Start health monitoring thread
        g_health_monitor.enabled = true;
        g_health_monitor.poll_interval_ms = poll_interval_ms > 0 ? poll_interval_ms : 1000;
        g_health_monitor.running = true;

        if (pthread_create(&g_health_monitor.thread, NULL, health_monitor_thread, NULL) != 0) {
            g_health_monitor.running = false;
            return MVGAL_ERROR_DRIVER;
        }

        MVGAL_LOG_INFO("Health monitoring started (interval=%u ms)", g_health_monitor.poll_interval_ms);
    } else if (!enabled && g_health_monitor.running) {
        // Stop health monitoring thread
        g_health_monitor.running = false;
        pthread_join(g_health_monitor.thread, NULL);
        g_health_monitor.enabled = false;

        MVGAL_LOG_INFO("Health monitoring stopped");
    }

    return MVGAL_SUCCESS;
}
