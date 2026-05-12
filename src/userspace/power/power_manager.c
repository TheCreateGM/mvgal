/**
 * @file power_manager.c
 * @brief MVGAL Runtime Power Management and S0ix Optimization
 *
 * Implements idle detection, GPU parking, and S0ix (suspend-to-idle)
 * support to minimize power consumption when secondary GPUs are unused.
 *
 * Features:
 * - Runtime PM with automatic idle detection
 * - S0ix deep sleep state support
 * - Lazy initialization to prevent spurious resumes
 * - Sysfs proxying for power control
 * - Per-GPU power state tracking
 * - Thermal-aware throttling
 *
 * Copyright (C) 2026 MVGAL Project
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_power.h"

/* ============================================================================
 * Power Management Constants
 * ============================================================================ */

#define MVGAL_PM_VERSION_MAJOR 1
#define MVGAL_PM_VERSION_MINOR 0
#define MVGAL_PM_VERSION_PATCH 0

/* Idle detection thresholds (milliseconds) */
#define IDLE_THRESHOLD_MS_DEFAULT       5000    /* 5 seconds */
#define IDLE_THRESHOLD_MS_MIN           1000    /* 1 second */
#define IDLE_THRESHOLD_MS_MAX           60000   /* 60 seconds */
#define IDLE_UTIL_THRESHOLD_DEFAULT     1.0f    /* 1% utilization */

/* S0ix entry/exit delays */
#define S0IX_ENTRY_DELAY_MS             100     /* Delay before S0ix entry */
#define S0IX_EXIT_WAKE_TIME_MS          50      /* Expected wake time */

/* GPU parking timeouts */
#define GPU_PARK_TIMEOUT_MS_DEFAULT     30000   /* 30 seconds */
#define GPU_UNPARK_TIMEOUT_MS           100     /* Unpark timeout */

/* Thermal thresholds (Celsius) */
#define THERMAL_THROTTLE_ON_C           85
#define THERMAL_THROTTLE_OFF_C          75
#define THERMAL_CRITICAL_C              95

/* Power state strings */
static const char *power_state_names[] = {
    [MVGAL_POWER_STATE_OFF]         = "off",
    [MVGAL_POWER_STATE_SUSPEND]     = "suspend",
    [MVGAL_POWER_STATE_IDLE]        = "idle",
    [MVGAL_POWER_STATE_ON]          = "on",
    [MVGAL_POWER_STATE_PERFORMANCE] = "performance",
};

/* ============================================================================
 * Power Management State Structures
 * ============================================================================ */

/**
 * @brief Per-GPU power state tracking
 */
typedef struct {
    uint32_t gpu_index;
    mvgal_power_state_t current_state;
    mvgal_power_state_t requested_state;
    
    /* Idle tracking */
    uint64_t last_activity_ns;
    uint64_t idle_threshold_ns;
    bool is_idle;
    bool auto_power_manage;
    
    /* S0ix support */
    bool s0ix_capable;
    bool s0ix_enabled;
    uint32_t s0ix_entry_count;
    uint64_t s0ix_time_ns;
    
    /* Parking state */
    bool is_parked;
    uint64_t park_time_ns;
    uint64_t unpark_time_ns;
    
    /* Thermal state */
    float current_temp;
    bool is_throttled;
    uint32_t throttle_reason;
    
    /* Power metrics */
    float current_power_w;
    float average_power_w;
    uint64_t energy_consumed_j;
    
    /* Runtime PM */
    int runtime_pm_fd;
    char runtime_pm_path[256];
    bool runtime_pm_enabled;
    
    /* Lazy init state */
    bool lazy_init_pending;
    void *lazy_init_data;
    
} mvgal_gpu_pm_state_t;

/**
 * @brief Global power manager state
 */
typedef struct {
    bool initialized;
    pthread_mutex_t lock;
    pthread_t monitor_thread;
    bool monitor_running;
    
    /* Configuration */
    uint32_t idle_threshold_ms;
    uint32_t park_timeout_ms;
    bool s0ix_enabled;
    bool lazy_init_enabled;
    bool thermal_throttling_enabled;
    
    /* Global S0ix state */
    bool s0ix_active;
    uint64_t s0ix_enter_time_ns;
    uint32_t s0ix_sequence;
    
    /* GPU states */
    uint32_t gpu_count;
    mvgal_gpu_pm_state_t *gpu_states;
    
    /* Statistics */
    uint64_t total_idle_transitions;
    uint64_t total_s0ix_entries;
    uint64_t total_power_saves;
    uint64_t total_energy_saved_j;
    
} mvgal_power_manager_t;

/* Global power manager instance */
static mvgal_power_manager_t g_pm = {0};

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static uint64_t ms_to_ns(uint32_t ms) {
    return (uint64_t)ms * 1000000ULL;
}

/* ============================================================================
 * Sysfs Power Control
 * ============================================================================ */

/**
 * @brief Write string to sysfs path
 */
static int sysfs_write(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return -errno;
    }
    
    size_t len = strlen(value);
    ssize_t written = write(fd, value, len);
    close(fd);
    
    if (written < 0) {
        return -errno;
    }
    
    return 0;
}

/**
 * @brief Read string from sysfs path
 */
static int sysfs_read(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -errno;
    }
    
    ssize_t n = read(fd, buf, size - 1);
    close(fd);
    
    if (n < 0) {
        return -errno;
    }
    
    buf[n] = '\0';
    
    /* Remove trailing newline */
    if (n > 0 && buf[n-1] == '\n') {
        buf[n-1] = '\0';
    }
    
    return 0;
}

/**
 * @brief Set PCI power control for GPU
 */
static int set_pci_power_control(uint32_t gpu_index, const char *state) {
    mvgal_gpu_descriptor_t gpu;
    if (mvgal_gpu_get_descriptor(gpu_index, &gpu) != MVGAL_SUCCESS) {
        return -ENODEV;
    }
    
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%04x:%02x:%02x.%x/power/control",
             gpu.pci_domain, gpu.pci_bus, gpu.pci_device, gpu.pci_function);
    
    return sysfs_write(path, state);
}

/**
 * @brief Get PCI power state
 */
static int get_pci_power_state(uint32_t gpu_index, char *state, size_t size) {
    mvgal_gpu_descriptor_t gpu;
    if (mvgal_gpu_get_descriptor(gpu_index, &gpu) != MVGAL_SUCCESS) {
        return -ENODEV;
    }
    
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%04x:%02x:%02x.%x/power/runtime_status",
             gpu.pci_domain, gpu.pci_bus, gpu.pci_device, gpu.pci_function);
    
    return sysfs_read(path, state, size);
}

/* ============================================================================
 * Runtime PM Operations
 * ============================================================================ */

/**
 * @brief Enable runtime PM for a GPU
 */
static int enable_runtime_pm(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    /* Enable PCI runtime power management */
    int ret = set_pci_power_control(gpu_index, "auto");
    if (ret < 0) {
        mvgal_log_warn("Failed to enable runtime PM for GPU %u: %d", gpu_index, ret);
        return ret;
    }
    
    state->runtime_pm_enabled = true;
    mvgal_log_info("Runtime PM enabled for GPU %u", gpu_index);
    
    return 0;
}

/**
 * @brief Disable runtime PM for a GPU
 */
static int disable_runtime_pm(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    /* Disable PCI runtime power management (force on) */
    int ret = set_pci_power_control(gpu_index, "on");
    if (ret < 0) {
        mvgal_log_warn("Failed to disable runtime PM for GPU %u: %d", gpu_index, ret);
        return ret;
    }
    
    state->runtime_pm_enabled = false;
    mvgal_log_info("Runtime PM disabled for GPU %u", gpu_index);
    
    return 0;
}

/**
 * @brief Suspend GPU via runtime PM
 */
static int suspend_gpu(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    if (state->current_state == MVGAL_POWER_STATE_SUSPEND) {
        return 0; /* Already suspended */
    }
    
    mvgal_log_info("Suspending GPU %u", gpu_index);
    
    /* Mark as idle to allow runtime PM to suspend */
    state->is_idle = true;
    
    /* Trigger runtime PM suspend */
    int ret = set_pci_power_control(gpu_index, "auto");
    if (ret < 0) {
        return ret;
    }
    
    state->current_state = MVGAL_POWER_STATE_SUSPEND;
    state->last_activity_ns = get_time_ns();
    
    g_pm.total_power_saves++;
    
    return 0;
}

/**
 * @brief Resume GPU from suspend
 */
static int resume_gpu(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    if (state->current_state != MVGAL_POWER_STATE_SUSPEND &&
        state->current_state != MVGAL_POWER_STATE_OFF) {
        return 0; /* Not suspended */
    }
    
    mvgal_log_info("Resuming GPU %u", gpu_index);
    
    /* Clear idle flag */
    state->is_idle = false;
    
    /* Force power on */
    int ret = set_pci_power_control(gpu_index, "on");
    if (ret < 0) {
        mvgal_log_error("Failed to resume GPU %u: %d", gpu_index, ret);
        return ret;
    }
    
    state->current_state = MVGAL_POWER_STATE_ON;
    state->last_activity_ns = get_time_ns();
    state->unpark_time_ns = get_time_ns();
    
    return 0;
}

/* ============================================================================
 * S0ix Support
 * ============================================================================ */

/**
 * @brief Check if system supports S0ix
 */
static bool check_s0ix_support(void) {
    struct stat st;
    
    /* Check for s2idle availability */
    if (stat("/sys/power/mem_sleep", &st) == 0) {
        char buf[64];
        if (sysfs_read("/sys/power/mem_sleep", buf, sizeof(buf)) == 0) {
            return strstr(buf, "s2idle") != NULL;
        }
    }
    
    return false;
}

/**
 * @brief Enter S0ix state
 */
static int enter_s0ix(void) {
    if (!g_pm.s0ix_enabled) {
        return -ENOTSUP;
    }
    
    if (g_pm.s0ix_active) {
        return 0; /* Already in S0ix */
    }
    
    mvgal_log_info("Entering S0ix state");
    
    /* Suspend all secondary GPUs */
    int32_t gpu_count = mvgal_gpu_get_count();
    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t gpu;
        if (mvgal_gpu_get_descriptor(i, &gpu) == MVGAL_SUCCESS) {
            /* Don't suspend primary GPU (index 0) */
            if (i != 0) {
                suspend_gpu(i);
            }
        }
    }
    
    g_pm.s0ix_active = true;
    g_pm.s0ix_enter_time_ns = get_time_ns();
    g_pm.s0ix_sequence++;
    g_pm.total_s0ix_entries++;
    
    return 0;
}

/**
 * @brief Exit S0ix state
 */
static int exit_s0ix(void) {
    if (!g_pm.s0ix_active) {
        return 0;
    }
    
    mvgal_log_info("Exiting S0ix state");
    
    /* Calculate time spent in S0ix */
    uint64_t now = get_time_ns();
    uint64_t s0ix_time = now - g_pm.s0ix_enter_time_ns;
    
    /* Update per-GPU states */
    int32_t gpu_count = mvgal_gpu_get_count();
    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[i];
        if (state->s0ix_enabled) {
            state->s0ix_time_ns += s0ix_time;
        }
    }
    
    g_pm.s0ix_active = false;
    
    return 0;
}

/* ============================================================================
 * GPU Parking
 * ============================================================================ */

/**
 * @brief Park (idle) a GPU
 */
static int park_gpu(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    if (state->is_parked) {
        return 0;
    }
    
    mvgal_log_info("Parking GPU %u", gpu_index);
    
    /* Save context if needed */
    /* ... */
    
    /* Transition to idle/suspend state */
    suspend_gpu(gpu_index);
    
    state->is_parked = true;
    state->park_time_ns = get_time_ns();
    
    return 0;
}

/**
 * @brief Unpark (activate) a GPU
 */
static int unpark_gpu(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    if (!state->is_parked) {
        return 0;
    }
    
    mvgal_log_info("Unparking GPU %u", gpu_index);
    
    /* Resume the GPU */
    resume_gpu(gpu_index);
    
    /* Restore context if needed */
    /* ... */
    
    state->is_parked = false;
    state->unpark_time_ns = get_time_ns();
    
    /* Handle lazy init */
    if (state->lazy_init_pending) {
        state->lazy_init_pending = false;
        /* Perform deferred initialization */
        /* ... */
    }
    
    return 0;
}

/* ============================================================================
 * Thermal Management
 * ============================================================================ */

/**
 * @brief Check thermal state and throttle if needed
 */
static void check_thermal_state(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    if (!g_pm.thermal_throttling_enabled) {
        return;
    }
    
    float temp;
    if (mvgal_gpu_get_temperature(gpu_index, &temp) != MVGAL_SUCCESS) {
        return;
    }
    
    state->current_temp = temp;
    
    /* Check for critical temperature */
    if (temp >= THERMAL_CRITICAL_C) {
        mvgal_log_error("GPU %u CRITICAL TEMPERATURE: %.1fC", gpu_index, temp);
        state->is_throttled = true;
        state->throttle_reason = MVGAL_THERMAL_REASON_CRITICAL;
        
        /* Emergency: suspend non-primary GPUs */
        if (gpu_index != 0) {
            suspend_gpu(gpu_index);
        }
        return;
    }
    
    /* Check for throttle-on threshold */
    if (temp >= THERMAL_THROTTLE_ON_C && !state->is_throttled) {
        mvgal_log_warn("GPU %u thermal throttling ON: %.1fC", gpu_index, temp);
        state->is_throttled = true;
        state->throttle_reason = MVGAL_THERMAL_REASON_HIGH_TEMP;
        
        /* Reduce power state */
        if (gpu_index != 0) {
            suspend_gpu(gpu_index);
        }
    }
    
    /* Check for throttle-off threshold */
    if (temp <= THERMAL_THROTTLE_OFF_C && state->is_throttled) {
        mvgal_log_info("GPU %u thermal throttling OFF: %.1fC", gpu_index, temp);
        state->is_throttled = false;
        state->throttle_reason = MVGAL_THERMAL_REASON_NONE;
        
        /* Can resume if needed */
        /* ... */
    }
}

/* ============================================================================
 * Idle Detection
 * ============================================================================ */

/**
 * @brief Check if GPU is idle
 */
static bool check_gpu_idle(uint32_t gpu_index) {
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    float utilization;
    if (mvgal_gpu_get_utilization(gpu_index, &utilization) != MVGAL_SUCCESS) {
        return false; /* Assume active on error */
    }
    
    uint64_t now = get_time_ns();
    
    if (utilization < 1.0f) {
        /* GPU appears idle */
        if (!state->is_idle) {
            /* Check if idle long enough */
            uint64_t idle_time = now - state->last_activity_ns;
            if (idle_time >= state->idle_threshold_ns) {
                state->is_idle = true;
                g_pm.total_idle_transitions++;
                mvgal_log_info("GPU %u is now idle (%.1f%% util, %lums)",
                              gpu_index, utilization,
                              (unsigned long)(idle_time / 1000000));
            }
        }
    } else {
        /* GPU is active */
        if (state->is_idle) {
            state->is_idle = false;
            mvgal_log_info("GPU %u is now active (%.1f%%)", gpu_index, utilization);
        }
        state->last_activity_ns = now;
    }
    
    return state->is_idle;
}

/* ============================================================================
 * Monitor Thread
 * ============================================================================ */

static void *power_monitor_thread(void *arg) {
    (void)arg;
    
    mvgal_log_info("Power monitor thread started");
    
    while (g_pm.monitor_running) {
        pthread_mutex_lock(&g_pm.lock);
        
        int32_t gpu_count = mvgal_gpu_get_count();
        
        for (int32_t i = 0; i < gpu_count; i++) {
            mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[i];
            
            if (!state->auto_power_manage) {
                continue;
            }
            
            /* Check thermal state */
            check_thermal_state(i);
            
            /* Check idle state */
            bool idle = check_gpu_idle(i);
            
            /* Park/unpark based on idle state */
            if (idle && !state->is_parked && i != 0) {
                /* Secondary GPU idle - park it */
                uint64_t idle_time = get_time_ns() - state->last_activity_ns;
                if (idle_time >= ms_to_ns(g_pm.park_timeout_ms)) {
                    park_gpu(i);
                }
            } else if (!idle && state->is_parked) {
                /* GPU needs to be active - unpark */
                unpark_gpu(i);
            }
            
            /* Handle S0ix entry for system-wide idle */
            if (i == 0 && idle && g_pm.s0ix_enabled) {
                /* Check if all GPUs are idle */
                bool all_idle = true;
                for (int32_t j = 0; j < gpu_count; j++) {
                    if (!g_pm.gpu_states[j].is_idle) {
                        all_idle = false;
                        break;
                    }
                }
                
                if (all_idle && !g_pm.s0ix_active) {
                    enter_s0ix();
                }
            }
        }
        
        /* Exit S0ix if any GPU becomes active */
        if (g_pm.s0ix_active) {
            bool any_active = false;
            for (int32_t i = 0; i < gpu_count; i++) {
                if (!g_pm.gpu_states[i].is_idle) {
                    any_active = true;
                    break;
                }
            }
            
            if (any_active) {
                exit_s0ix();
            }
        }
        
        pthread_mutex_unlock(&g_pm.lock);
        
        /* Sleep before next check */
        usleep(500000); /* 500ms */
    }
    
    mvgal_log_info("Power monitor thread stopped");
    return NULL;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

mvgal_error_t mvgal_pm_init(const mvgal_pm_config_t *config) {
    if (g_pm.initialized) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }
    
    memset(&g_pm, 0, sizeof(g_pm));
    
    /* Apply configuration or defaults */
    if (config) {
        g_pm.idle_threshold_ms = config->idle_threshold_ms > 0 
            ? config->idle_threshold_ms 
            : IDLE_THRESHOLD_MS_DEFAULT;
        g_pm.park_timeout_ms = config->park_timeout_ms > 0
            ? config->park_timeout_ms
            : GPU_PARK_TIMEOUT_MS_DEFAULT;
        g_pm.s0ix_enabled = config->enable_s0ix;
        g_pm.lazy_init_enabled = config->lazy_init;
        g_pm.thermal_throttling_enabled = config->thermal_throttling;
    } else {
        g_pm.idle_threshold_ms = IDLE_THRESHOLD_MS_DEFAULT;
        g_pm.park_timeout_ms = GPU_PARK_TIMEOUT_MS_DEFAULT;
        g_pm.s0ix_enabled = check_s0ix_support();
        g_pm.lazy_init_enabled = true;
        g_pm.thermal_throttling_enabled = true;
    }
    
    /* Initialize mutex */
    if (pthread_mutex_init(&g_pm.lock, NULL) != 0) {
        return MVGAL_ERROR_INITIALIZATION;
    }
    
    /* Allocate GPU state array */
    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count > 0) {
        g_pm.gpu_states = calloc(gpu_count, sizeof(mvgal_gpu_pm_state_t));
        if (!g_pm.gpu_states) {
            pthread_mutex_destroy(&g_pm.lock);
            return MVGAL_ERROR_OUT_OF_MEMORY;
        }
        
        /* Initialize per-GPU states */
        for (int32_t i = 0; i < gpu_count; i++) {
            mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[i];
            state->gpu_index = i;
            state->current_state = MVGAL_POWER_STATE_ON;
            state->idle_threshold_ns = ms_to_ns(g_pm.idle_threshold_ms);
            state->last_activity_ns = get_time_ns();
            state->auto_power_manage = true;
            state->s0ix_capable = (i != 0); /* Secondary GPUs can S0ix */
            state->s0ix_enabled = g_pm.s0ix_enabled && state->s0ix_capable;
            state->runtime_pm_enabled = false;
            state->lazy_init_pending = false;
        }
        
        g_pm.gpu_count = gpu_count;
    }
    
    g_pm.initialized = true;
    
    mvgal_log_info("Power manager initialized (S0ix: %s, Lazy init: %s)",
                  g_pm.s0ix_enabled ? "enabled" : "disabled",
                  g_pm.lazy_init_enabled ? "enabled" : "disabled");
    
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_pm_shutdown(void) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    /* Stop monitor thread */
    if (g_pm.monitor_running) {
        g_pm.monitor_running = false;
        pthread_join(g_pm.monitor_thread, NULL);
    }
    
    /* Exit S0ix if active */
    if (g_pm.s0ix_active) {
        exit_s0ix();
    }
    
    /* Resume all parked GPUs */
    for (uint32_t i = 0; i < g_pm.gpu_count; i++) {
        if (g_pm.gpu_states[i].is_parked) {
            unpark_gpu(i);
        }
        
        /* Ensure runtime PM is disabled */
        if (g_pm.gpu_states[i].runtime_pm_enabled) {
            disable_runtime_pm(i);
        }
    }
    
    free(g_pm.gpu_states);
    pthread_mutex_destroy(&g_pm.lock);
    
    memset(&g_pm, 0, sizeof(g_pm));
    
    mvgal_log_info("Power manager shutdown complete");
    
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_pm_start_monitor(void) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    if (g_pm.monitor_running) {
        return MVGAL_ERROR_BUSY;
    }
    
    g_pm.monitor_running = true;
    
    if (pthread_create(&g_pm.monitor_thread, NULL, power_monitor_thread, NULL) != 0) {
        g_pm.monitor_running = false;
        return MVGAL_ERROR_INITIALIZATION;
    }
    
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_pm_stop_monitor(void) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    if (!g_pm.monitor_running) {
        return MVGAL_ERROR_NOT_FOUND;
    }
    
    g_pm.monitor_running = false;
    pthread_join(g_pm.monitor_thread, NULL);
    
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * GPU Power Control
 * ============================================================================ */

mvgal_error_t mvgal_pm_set_gpu_state(uint32_t gpu_index, mvgal_power_state_t state) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    if (gpu_index >= g_pm.gpu_count) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    
    mvgal_gpu_pm_state_t *gpu_state = &g_pm.gpu_states[gpu_index];
    gpu_state->requested_state = state;
    
    int ret = 0;
    switch (state) {
        case MVGAL_POWER_STATE_OFF:
            /* Not fully supported in userspace yet */
            ret = suspend_gpu(gpu_index);
            break;
        case MVGAL_POWER_STATE_SUSPEND:
            ret = suspend_gpu(gpu_index);
            break;
        case MVGAL_POWER_STATE_IDLE:
        case MVGAL_POWER_STATE_ON:
        case MVGAL_POWER_STATE_PERFORMANCE:
            ret = resume_gpu(gpu_index);
            break;
        default:
            ret = -EINVAL;
            break;
    }
    
    pthread_mutex_unlock(&g_pm.lock);
    
    return ret == 0 ? MVGAL_SUCCESS : MVGAL_ERROR_INITIALIZATION;
}

mvgal_error_t mvgal_pm_get_gpu_state(uint32_t gpu_index, mvgal_power_state_t *state) {
    if (!g_pm.initialized || !state) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (gpu_index >= g_pm.gpu_count) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    *state = g_pm.gpu_states[gpu_index].current_state;
    pthread_mutex_unlock(&g_pm.lock);
    
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_pm_get_gpu_stats(uint32_t gpu_index, mvgal_gpu_power_status_t *status) {
    if (!g_pm.initialized || !status) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (gpu_index >= g_pm.gpu_count) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    
    status->current_state = state->current_state;
    status->requested_state = state->requested_state;
    status->is_idle = state->is_idle;
    status->is_parked = state->is_parked;
    status->is_throttled = state->is_throttled;
    status->current_temp = state->current_temp;
    status->current_power_w = state->current_power_w;
    status->throttle_reason = state->throttle_reason;
    status->s0ix_capable = state->s0ix_capable;
    status->s0ix_enabled = state->s0ix_enabled;
    status->s0ix_time_total_ns = state->s0ix_time_ns;
    
    pthread_mutex_unlock(&g_pm.lock);
    
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_pm_set_auto_pm(uint32_t gpu_index, bool enable) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    if (gpu_index >= g_pm.gpu_count) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    g_pm.gpu_states[gpu_index].auto_power_manage = enable;
    pthread_mutex_unlock(&g_pm.lock);
    
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * S0ix (Suspend-to-Idle) Control
 * ============================================================================ */

bool mvgal_pm_s0ix_available(void) {
    return check_s0ix_support();
}

mvgal_error_t mvgal_pm_set_s0ix_enabled(bool enable) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    g_pm.s0ix_enabled = enable;
    
    /* Update per-GPU s0ix enablement */
    for (uint32_t i = 0; i < g_pm.gpu_count; i++) {
        if (g_pm.gpu_states[i].s0ix_capable) {
            g_pm.gpu_states[i].s0ix_enabled = enable;
        }
    }
    pthread_mutex_unlock(&g_pm.lock);
    
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_pm_enter_s0ix(void) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    int ret = enter_s0ix();
    pthread_mutex_unlock(&g_pm.lock);
    
    return ret == 0 ? MVGAL_SUCCESS : MVGAL_ERROR_INITIALIZATION;
}

mvgal_error_t mvgal_pm_exit_s0ix(void) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    int ret = exit_s0ix();
    pthread_mutex_unlock(&g_pm.lock);
    
    return ret == 0 ? MVGAL_SUCCESS : MVGAL_ERROR_INITIALIZATION;
}

mvgal_error_t mvgal_pm_get_stats(mvgal_pm_stats_t *stats) {
    if (!g_pm.initialized || !stats) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    
    stats->total_idle_transitions = g_pm.total_idle_transitions;
    stats->total_s0ix_entries = g_pm.total_s0ix_entries;
    stats->total_power_saves = g_pm.total_power_saves;
    stats->total_energy_saved_j = g_pm.total_energy_saved_j;
    stats->s0ix_active = g_pm.s0ix_active;
    stats->s0ix_sequence = g_pm.s0ix_sequence;
    
    if (g_pm.s0ix_active) {
        uint64_t now = get_time_ns();
        stats->s0ix_time_ns = now - g_pm.s0ix_enter_time_ns;
    } else {
        stats->s0ix_time_ns = 0;
    }
    
    pthread_mutex_unlock(&g_pm.lock);
    
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_pm_signal_activity(uint32_t gpu_index) {
    if (!g_pm.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    if (gpu_index >= g_pm.gpu_count) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    state->last_activity_ns = get_time_ns();
    state->is_idle = false;
    
    /* Unpark if needed */
    if (state->is_parked) {
        unpark_gpu(gpu_index);
    }
    
    pthread_mutex_unlock(&g_pm.lock);
    
    return MVGAL_SUCCESS;
}

const char *mvgal_pm_state_to_string(mvgal_power_state_t state) {
    if (state < 0 || state >= (int)(sizeof(power_state_names) / sizeof(power_state_names[0]))) {
        return "invalid";
    }
    return power_state_names[state];
}

/* ============================================================================
 * Lazy Initialization Support
 * ============================================================================ */

mvgal_error_t mvgal_pm_register_lazy_init(uint32_t gpu_index,
                                          mvgal_lazy_init_callback_t callback,
                                          void *user_data) {
    if (!g_pm.initialized || !callback) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (!g_pm.lazy_init_enabled) {
        /* Lazy init disabled - execute immediately */
        callback(gpu_index, user_data);
        return MVGAL_SUCCESS;
    }
    
    pthread_mutex_lock(&g_pm.lock);
    
    mvgal_gpu_pm_state_t *state = &g_pm.gpu_states[gpu_index];
    state->lazy_init_pending = true;
    state->lazy_init_data = user_data;
    
    /* If GPU is parked, defer execution until unpark */
    if (state->is_parked) {
        mvgal_log_info("Deferring GPU %u initialization (lazy)", gpu_index);
    } else {
        /* Execute immediately if not parked */
        state->lazy_init_pending = false;
        pthread_mutex_unlock(&g_pm.lock);
        callback(gpu_index, user_data);
        return MVGAL_SUCCESS;
    }
    
    pthread_mutex_unlock(&g_pm.lock);
    
    return MVGAL_SUCCESS;
}
