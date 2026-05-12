/**
 * @file config.c
 * @brief Configuration management for MVGAL daemon
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module handles configuration initialization and management.
 * It provides default values and basic get/set operations.
 */

#include "mvgal/mvgal_config.h"
#include "mvgal/mvgal_log.h"
#include "mvgal/mvgal_types.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

/**
 * @brief Default configuration
 */
static mvgal_config_t g_default_config = {0};

/**
 * @brief Current configuration
 */
static mvgal_config_t g_current_config = {0};

/**
 * @brief Check if configuration is initialized
 */
static bool g_config_initialized = false;

static char *g_config_path = NULL;

typedef struct {
    char *name;
    mvgal_config_callback_t callback;
    void *user_data;
} config_callback_entry_t;

#define MVGAL_MAX_CONFIG_CALLBACKS 32
static config_callback_entry_t g_config_callbacks[MVGAL_MAX_CONFIG_CALLBACKS];

typedef enum {
    CFG_BOOL,
    CFG_INT,
    CFG_FLOAT,
    CFG_STRING,
    CFG_ENUM,
    CFG_SIZE
} config_kind_t;

typedef struct {
    const char *name;
    const char *description;
    config_kind_t kind;
    double min_value;
    double max_value;
} config_descriptor_t;

static const config_descriptor_t g_config_descriptors[] = {
    {"enabled", "Enable MVGAL globally", CFG_BOOL, 0, 1},
    {"log_level", "Logging level", CFG_ENUM, MVGAL_LOG_LEVEL_ERROR, MVGAL_LOG_LEVEL_TRACE},
    {"debug", "Enable debug mode", CFG_BOOL, 0, 1},
    {"gpus.auto_detect", "Auto-detect GPUs", CFG_BOOL, 0, 1},
    {"gpus.devices", "Comma-separated GPU device list", CFG_STRING, 0, 0},
    {"gpus.max_gpus", "Maximum GPUs to use", CFG_INT, 1, 256},
    {"gpus.enable_all", "Enable all detected GPUs", CFG_BOOL, 0, 1},
    {"scheduler.strategy", "Work distribution strategy", CFG_ENUM, MVGAL_STRATEGY_ROUND_ROBIN, MVGAL_STRATEGY_CUSTOM},
    {"scheduler.dynamic_load_balance", "Enable dynamic load balancing", CFG_BOOL, 0, 1},
    {"scheduler.thermal_aware", "Enable thermal-aware scheduling", CFG_BOOL, 0, 1},
    {"scheduler.power_aware", "Enable power-aware scheduling", CFG_BOOL, 0, 1},
    {"scheduler.load_balance_threshold", "Load imbalance threshold", CFG_FLOAT, 0.0, 1.0},
    {"scheduler.max_queued_workloads", "Maximum queued workloads", CFG_INT, 1, 1048576},
    {"memory.use_dmabuf", "Use DMA-BUF sharing", CFG_BOOL, 0, 1},
    {"memory.use_p2p", "Use PCIe peer-to-peer transfers", CFG_BOOL, 0, 1},
    {"memory.replicate_small", "Replicate small buffers", CFG_BOOL, 0, 1},
    {"memory.replicate_threshold", "Small-buffer replication threshold", CFG_SIZE, 0, (double)SIZE_MAX},
    {"memory.max_buffer_size", "Maximum buffer size", CFG_SIZE, 0, (double)SIZE_MAX},
    {"vulkan.enabled", "Enable Vulkan layer", CFG_BOOL, 0, 1},
    {"vulkan.intercept_all", "Intercept all Vulkan calls", CFG_BOOL, 0, 1},
    {"vulkan.layer_path", "Path to Vulkan layer library", CFG_STRING, 0, 0},
    {"opencl.enabled", "Enable OpenCL interception", CFG_BOOL, 0, 1},
    {"opencl.preload", "Use OpenCL LD_PRELOAD", CFG_BOOL, 0, 1},
    {"opencl.library_path", "Path to OpenCL library", CFG_STRING, 0, 0},
    {"performance.profile", "Enable profiling", CFG_BOOL, 0, 1},
    {"performance.profile_file", "Path to profiling output", CFG_STRING, 0, 0},
    {"performance.profile_interval_ms", "Profiling interval in milliseconds", CFG_INT, 10, 3600000},
};

static const size_t g_config_descriptor_count =
    sizeof(g_config_descriptors) / sizeof(g_config_descriptors[0]);

/**
 * @brief Initialize default configuration
 */
static void init_defaults(void) {
    // Global settings
    g_default_config.enabled = true;
    g_default_config.log_level = MVGAL_LOG_LEVEL_INFO;
    g_default_config.debug = false;
    
    // GPU settings
    g_default_config.gpus.auto_detect = true;
    g_default_config.gpus.devices = NULL;
    g_default_config.gpus.max_gpus = 16;
    g_default_config.gpus.enable_all = true;
    
    // Scheduler settings
    g_default_config.scheduler.strategy = MVGAL_STRATEGY_HYBRID;
    g_default_config.scheduler.dynamic_load_balance = true;
    g_default_config.scheduler.thermal_aware = true;
    g_default_config.scheduler.power_aware = true;
    g_default_config.scheduler.load_balance_threshold = 0.75f;
    g_default_config.scheduler.max_queued_workloads = 1024;
    
    // Memory settings
    g_default_config.memory.use_dmabuf = true;
    g_default_config.memory.use_p2p = true;
    g_default_config.memory.replicate_small = true;
    g_default_config.memory.replicate_threshold = 4096; // 4KB
    g_default_config.memory.max_buffer_size = 0; // 0 = unlimited
    
    // Vulkan layer settings
    g_default_config.vulkan.enabled = true;
    g_default_config.vulkan.intercept_all = false;
    g_default_config.vulkan.layer_path = NULL;
    
    // OpenCL settings
    g_default_config.opencl.enabled = true;
    g_default_config.opencl.preload = true;
    g_default_config.opencl.library_path = NULL;
    
    // Performance settings
    g_default_config.performance.profile = false;
    g_default_config.performance.profile_file = NULL;
    g_default_config.performance.profile_interval_ms = 1000;
    
    // Reserved
    for (int i = 0; i < 8; i++) {
        g_default_config.reserved[i] = NULL;
    }
}

static const config_descriptor_t *find_config_descriptor(const char *name) {
    if (name == NULL || *name == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < g_config_descriptor_count; i++) {
        if (strcmp(g_config_descriptors[i].name, name) == 0) {
            return &g_config_descriptors[i];
        }
    }
    return NULL;
}

static mvgal_config_value_type_t public_config_type(config_kind_t kind) {
    switch (kind) {
        case CFG_BOOL: return MVGAL_CONFIG_BOOL;
        case CFG_INT:
        case CFG_SIZE: return MVGAL_CONFIG_INT;
        case CFG_FLOAT: return MVGAL_CONFIG_FLOAT;
        case CFG_STRING: return MVGAL_CONFIG_STRING;
        case CFG_ENUM: return MVGAL_CONFIG_ENUM;
        default: return MVGAL_CONFIG_STRING;
    }
}

static bool config_value_equal(config_kind_t kind,
                               const mvgal_config_value_u *a,
                               const mvgal_config_value_u *b) {
    switch (kind) {
        case CFG_BOOL: return a->b == b->b;
        case CFG_INT:
        case CFG_ENUM:
        case CFG_SIZE: return a->i == b->i;
        case CFG_FLOAT: return a->f == b->f;
        case CFG_STRING:
            if (a->s == NULL || b->s == NULL) {
                return a->s == b->s;
            }
            return strcmp(a->s, b->s) == 0;
        default:
            return false;
    }
}

static char *dup_nullable_string(const char *value) {
    if (value == NULL) {
        return NULL;
    }
    char *copy = strdup(value);
    if (copy == NULL) {
        MVGAL_LOG_ERROR("Failed to allocate configuration string");
    }
    return copy;
}

static void free_owned_config_strings(mvgal_config_t *config) {
    free(config->gpus.devices);
    config->gpus.devices = NULL;
    free(config->vulkan.layer_path);
    config->vulkan.layer_path = NULL;
    free(config->opencl.library_path);
    config->opencl.library_path = NULL;
    free(config->performance.profile_file);
    config->performance.profile_file = NULL;
}

static void free_temp_config_strings(mvgal_config_t *config) {
    free(config->gpus.devices);
    free(config->vulkan.layer_path);
    free(config->opencl.library_path);
    free(config->performance.profile_file);
}

static mvgal_error_t clone_config_strings(mvgal_config_t *dst, const mvgal_config_t *src) {
    dst->gpus.devices = dup_nullable_string(src->gpus.devices);
    dst->vulkan.layer_path = dup_nullable_string(src->vulkan.layer_path);
    dst->opencl.library_path = dup_nullable_string(src->opencl.library_path);
    dst->performance.profile_file = dup_nullable_string(src->performance.profile_file);

    if ((src->gpus.devices != NULL && dst->gpus.devices == NULL) ||
        (src->vulkan.layer_path != NULL && dst->vulkan.layer_path == NULL) ||
        (src->opencl.library_path != NULL && dst->opencl.library_path == NULL) ||
        (src->performance.profile_file != NULL && dst->performance.profile_file == NULL)) {
        free_temp_config_strings(dst);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    return MVGAL_SUCCESS;
}

static void notify_config_callbacks(const char *name,
                                    const mvgal_config_value_u *old_value,
                                    const mvgal_config_value_u *new_value) {
    for (size_t i = 0; i < MVGAL_MAX_CONFIG_CALLBACKS; i++) {
        config_callback_entry_t *entry = &g_config_callbacks[i];
        if (entry->callback == NULL) {
            continue;
        }
        if (entry->name != NULL && strcmp(entry->name, name) != 0) {
            continue;
        }
        entry->callback(name, old_value, new_value, entry->user_data);
    }
}

static mvgal_error_t set_string_field(char **field, const char *value) {
    char *copy = dup_nullable_string(value);
    if (value != NULL && copy == NULL) {
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    free(*field);
    *field = copy;
    return MVGAL_SUCCESS;
}

static mvgal_error_t get_config_value_internal(const char *name, mvgal_config_value_u *value) {
    if (strcmp(name, "enabled") == 0) value->b = g_current_config.enabled;
    else if (strcmp(name, "log_level") == 0) value->i = g_current_config.log_level;
    else if (strcmp(name, "debug") == 0) value->b = g_current_config.debug;
    else if (strcmp(name, "gpus.auto_detect") == 0) value->b = g_current_config.gpus.auto_detect;
    else if (strcmp(name, "gpus.devices") == 0) value->s = g_current_config.gpus.devices;
    else if (strcmp(name, "gpus.max_gpus") == 0) value->i = g_current_config.gpus.max_gpus;
    else if (strcmp(name, "gpus.enable_all") == 0) value->b = g_current_config.gpus.enable_all;
    else if (strcmp(name, "scheduler.strategy") == 0) value->i = g_current_config.scheduler.strategy;
    else if (strcmp(name, "scheduler.dynamic_load_balance") == 0) value->b = g_current_config.scheduler.dynamic_load_balance;
    else if (strcmp(name, "scheduler.thermal_aware") == 0) value->b = g_current_config.scheduler.thermal_aware;
    else if (strcmp(name, "scheduler.power_aware") == 0) value->b = g_current_config.scheduler.power_aware;
    else if (strcmp(name, "scheduler.load_balance_threshold") == 0) value->f = g_current_config.scheduler.load_balance_threshold;
    else if (strcmp(name, "scheduler.max_queued_workloads") == 0) value->i = g_current_config.scheduler.max_queued_workloads;
    else if (strcmp(name, "memory.use_dmabuf") == 0) value->b = g_current_config.memory.use_dmabuf;
    else if (strcmp(name, "memory.use_p2p") == 0) value->b = g_current_config.memory.use_p2p;
    else if (strcmp(name, "memory.replicate_small") == 0) value->b = g_current_config.memory.replicate_small;
    else if (strcmp(name, "memory.replicate_threshold") == 0) value->i = (int64_t)g_current_config.memory.replicate_threshold;
    else if (strcmp(name, "memory.max_buffer_size") == 0) value->i = (int64_t)g_current_config.memory.max_buffer_size;
    else if (strcmp(name, "vulkan.enabled") == 0) value->b = g_current_config.vulkan.enabled;
    else if (strcmp(name, "vulkan.intercept_all") == 0) value->b = g_current_config.vulkan.intercept_all;
    else if (strcmp(name, "vulkan.layer_path") == 0) value->s = g_current_config.vulkan.layer_path;
    else if (strcmp(name, "opencl.enabled") == 0) value->b = g_current_config.opencl.enabled;
    else if (strcmp(name, "opencl.preload") == 0) value->b = g_current_config.opencl.preload;
    else if (strcmp(name, "opencl.library_path") == 0) value->s = g_current_config.opencl.library_path;
    else if (strcmp(name, "performance.profile") == 0) value->b = g_current_config.performance.profile;
    else if (strcmp(name, "performance.profile_file") == 0) value->s = g_current_config.performance.profile_file;
    else if (strcmp(name, "performance.profile_interval_ms") == 0) value->i = g_current_config.performance.profile_interval_ms;
    else return MVGAL_ERROR_NOT_FOUND;
    return MVGAL_SUCCESS;
}

static mvgal_error_t set_config_value_internal(const char *name, const mvgal_config_value_u *value) {
    if (strcmp(name, "enabled") == 0) g_current_config.enabled = value->b;
    else if (strcmp(name, "log_level") == 0) g_current_config.log_level = (mvgal_log_level_t)value->i;
    else if (strcmp(name, "debug") == 0) g_current_config.debug = value->b;
    else if (strcmp(name, "gpus.auto_detect") == 0) g_current_config.gpus.auto_detect = value->b;
    else if (strcmp(name, "gpus.devices") == 0) return set_string_field(&g_current_config.gpus.devices, value->s);
    else if (strcmp(name, "gpus.max_gpus") == 0) g_current_config.gpus.max_gpus = (uint32_t)value->i;
    else if (strcmp(name, "gpus.enable_all") == 0) g_current_config.gpus.enable_all = value->b;
    else if (strcmp(name, "scheduler.strategy") == 0) g_current_config.scheduler.strategy = (mvgal_distribution_strategy_t)value->i;
    else if (strcmp(name, "scheduler.dynamic_load_balance") == 0) g_current_config.scheduler.dynamic_load_balance = value->b;
    else if (strcmp(name, "scheduler.thermal_aware") == 0) g_current_config.scheduler.thermal_aware = value->b;
    else if (strcmp(name, "scheduler.power_aware") == 0) g_current_config.scheduler.power_aware = value->b;
    else if (strcmp(name, "scheduler.load_balance_threshold") == 0) g_current_config.scheduler.load_balance_threshold = (float)value->f;
    else if (strcmp(name, "scheduler.max_queued_workloads") == 0) g_current_config.scheduler.max_queued_workloads = (uint32_t)value->i;
    else if (strcmp(name, "memory.use_dmabuf") == 0) g_current_config.memory.use_dmabuf = value->b;
    else if (strcmp(name, "memory.use_p2p") == 0) g_current_config.memory.use_p2p = value->b;
    else if (strcmp(name, "memory.replicate_small") == 0) g_current_config.memory.replicate_small = value->b;
    else if (strcmp(name, "memory.replicate_threshold") == 0) g_current_config.memory.replicate_threshold = (size_t)value->i;
    else if (strcmp(name, "memory.max_buffer_size") == 0) g_current_config.memory.max_buffer_size = (size_t)value->i;
    else if (strcmp(name, "vulkan.enabled") == 0) g_current_config.vulkan.enabled = value->b;
    else if (strcmp(name, "vulkan.intercept_all") == 0) g_current_config.vulkan.intercept_all = value->b;
    else if (strcmp(name, "vulkan.layer_path") == 0) return set_string_field(&g_current_config.vulkan.layer_path, value->s);
    else if (strcmp(name, "opencl.enabled") == 0) g_current_config.opencl.enabled = value->b;
    else if (strcmp(name, "opencl.preload") == 0) g_current_config.opencl.preload = value->b;
    else if (strcmp(name, "opencl.library_path") == 0) return set_string_field(&g_current_config.opencl.library_path, value->s);
    else if (strcmp(name, "performance.profile") == 0) g_current_config.performance.profile = value->b;
    else if (strcmp(name, "performance.profile_file") == 0) return set_string_field(&g_current_config.performance.profile_file, value->s);
    else if (strcmp(name, "performance.profile_interval_ms") == 0) g_current_config.performance.profile_interval_ms = (uint32_t)value->i;
    else return MVGAL_ERROR_NOT_FOUND;
    return MVGAL_SUCCESS;
}

/**
 * @brief Initialize configuration system
 */
mvgal_error_t mvgal_config_init(void) {
    if (g_config_initialized) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }
    
    init_defaults();
    
    // Copy defaults to current
    memcpy(&g_current_config, &g_default_config, sizeof(g_current_config));
    
    g_config_initialized = true;
    
    MVGAL_LOG_DEBUG("Configuration system initialized");
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Shutdown configuration system
 */
void mvgal_config_shutdown(void) {
    if (!g_config_initialized) {
        return;
    }
    
    free_owned_config_strings(&g_current_config);
    free(g_config_path);
    g_config_path = NULL;
    for (size_t i = 0; i < MVGAL_MAX_CONFIG_CALLBACKS; i++) {
        free(g_config_callbacks[i].name);
        g_config_callbacks[i].name = NULL;
        g_config_callbacks[i].callback = NULL;
        g_config_callbacks[i].user_data = NULL;
    }
    
    g_config_initialized = false;
    MVGAL_LOG_DEBUG("Configuration system shutdown");
}

/**
 * @brief Remove leading and trailing whitespace from a string
 */
static char *trim_whitespace(char *str) {
    if (str == NULL || *str == '\0') return str;
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
    char *end = start + strlen(start) - 1;
    while (end >= start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    return start;
}

/**
 * @brief Parse a boolean value from string
 */
static bool parse_boolean(const char *value) {
    if (value == NULL) return false;
    char lower[32];
    strncpy(lower, value, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    for (size_t i = 0; lower[i]; i++) lower[i] = (char)tolower((unsigned char)lower[i]);
    return (strcmp(lower, "true") == 0 || strcmp(lower, "yes") == 0 ||
            strcmp(lower, "1") == 0 || strcmp(lower, "on") == 0);
}

/**
 * @brief Parse log level from string
 */
static mvgal_log_level_t parse_log_level(const char *value) {
    if (value == NULL) return MVGAL_LOG_LEVEL_INFO;
    char lower[32];
    strncpy(lower, value, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    for (size_t i = 0; lower[i]; i++) lower[i] = (char)tolower((unsigned char)lower[i]);
    if (strcmp(lower, "error") == 0 || strcmp(lower, "0") == 0) return MVGAL_LOG_LEVEL_ERROR;
    if (strcmp(lower, "warn") == 0 || strcmp(lower, "warning") == 0 || strcmp(lower, "1") == 0) return MVGAL_LOG_LEVEL_WARN;
    if (strcmp(lower, "info") == 0 || strcmp(lower, "2") == 0) return MVGAL_LOG_LEVEL_INFO;
    if (strcmp(lower, "debug") == 0 || strcmp(lower, "3") == 0) return MVGAL_LOG_LEVEL_DEBUG;
    if (strcmp(lower, "trace") == 0 || strcmp(lower, "4") == 0) return MVGAL_LOG_LEVEL_TRACE;
    return MVGAL_LOG_LEVEL_INFO;
}

/**
 * @brief Parse distribution strategy from string
 */
static mvgal_distribution_strategy_t parse_strategy(const char *value) {
    if (value == NULL) return MVGAL_STRATEGY_HYBRID;
    char lower[32];
    strncpy(lower, value, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    for (size_t i = 0; lower[i]; i++) lower[i] = (char)tolower((unsigned char)lower[i]);
    if (strcmp(lower, "afr") == 0) return MVGAL_STRATEGY_AFR;
    if (strcmp(lower, "sfr") == 0) return MVGAL_STRATEGY_SFR;
    if (strcmp(lower, "task") == 0) return MVGAL_STRATEGY_TASK;
    if (strcmp(lower, "compute") == 0 || strcmp(lower, "compute_offload") == 0) return MVGAL_STRATEGY_COMPUTE_OFFLOAD;
    if (strcmp(lower, "hybrid") == 0) return MVGAL_STRATEGY_HYBRID;
    if (strcmp(lower, "single") == 0) return MVGAL_STRATEGY_SINGLE_GPU;
    return MVGAL_STRATEGY_HYBRID;
}

/**
 * @brief Parse configuration from INI file data
 */
static void parse_ini_data(const char *data) {
    if (data == NULL) return;
    char line[1024];
    const char *p = data;
    char *section = NULL;
    while (*p) {
        while (*p && (*p == '\n' || *p == '\r')) p++;
        if (*p == '\0') break;
        size_t len = 0;
        while (*p && *p != '\n' && *p != '\r' && len < sizeof(line) - 1) {
            line[len++] = *p++;
        }
        line[len] = '\0';
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#') continue;
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                free(section);
                section = strdup(trimmed + 1);
                continue;
            }
        }
        char *equals = strchr(trimmed, '=');
        if (!equals) continue;
        *equals = '\0';
        char *key = trim_whitespace(trimmed);
        char *value = trim_whitespace(equals + 1);
        if (*key == '\0') continue;
        char key_lower[64];
        strncpy(key_lower, key, sizeof(key_lower) - 1);
        key_lower[sizeof(key_lower) - 1] = '\0';
        for (size_t i = 0; key_lower[i]; i++) key_lower[i] = (char)tolower((unsigned char)key_lower[i]);
        if (section == NULL || strcmp(section, "global") == 0 || strcmp(section, "core") == 0) {
            if (strcmp(key_lower, "enabled") == 0) g_current_config.enabled = parse_boolean(value);
            else if (strcmp(key_lower, "log_level") == 0 || strcmp(key_lower, "loglevel") == 0) g_current_config.log_level = parse_log_level(value);
            else if (strcmp(key_lower, "debug") == 0) g_current_config.debug = parse_boolean(value);
        } else if (strcmp(section, "gpus") == 0 || strcmp(section, "gpu") == 0) {
            if (strcmp(key_lower, "auto_detect") == 0) g_current_config.gpus.auto_detect = parse_boolean(value);
            else if (strcmp(key_lower, "max_gpus") == 0) g_current_config.gpus.max_gpus = (uint32_t)atoi(value);
            else if (strcmp(key_lower, "enable_all") == 0) g_current_config.gpus.enable_all = parse_boolean(value);
        } else if (strcmp(section, "scheduler") == 0) {
            if (strcmp(key_lower, "strategy") == 0) g_current_config.scheduler.strategy = parse_strategy(value);
            else if (strcmp(key_lower, "dynamic_load_balance") == 0) g_current_config.scheduler.dynamic_load_balance = parse_boolean(value);
            else if (strcmp(key_lower, "thermal_aware") == 0) g_current_config.scheduler.thermal_aware = parse_boolean(value);
            else if (strcmp(key_lower, "power_aware") == 0) g_current_config.scheduler.power_aware = parse_boolean(value);
            else if (strcmp(key_lower, "load_balance_threshold") == 0) g_current_config.scheduler.load_balance_threshold = (float)atof(value);
            else if (strcmp(key_lower, "max_queued_workloads") == 0) g_current_config.scheduler.max_queued_workloads = (uint32_t)atoi(value);
        } else if (strcmp(section, "memory") == 0) {
            if (strcmp(key_lower, "use_dmabuf") == 0) g_current_config.memory.use_dmabuf = parse_boolean(value);
            else if (strcmp(key_lower, "use_p2p") == 0) g_current_config.memory.use_p2p = parse_boolean(value);
            else if (strcmp(key_lower, "replicate_small") == 0) g_current_config.memory.replicate_small = parse_boolean(value);
            else if (strcmp(key_lower, "replicate_threshold") == 0) g_current_config.memory.replicate_threshold = (size_t)atoll(value);
            else if (strcmp(key_lower, "max_buffer_size") == 0) g_current_config.memory.max_buffer_size = (size_t)atoll(value);
        } else if (strcmp(section, "vulkan") == 0) {
            if (strcmp(key_lower, "enabled") == 0) g_current_config.vulkan.enabled = parse_boolean(value);
            else if (strcmp(key_lower, "intercept_all") == 0) g_current_config.vulkan.intercept_all = parse_boolean(value);
        } else if (strcmp(section, "opencl") == 0) {
            if (strcmp(key_lower, "enabled") == 0) g_current_config.opencl.enabled = parse_boolean(value);
            else if (strcmp(key_lower, "preload") == 0) g_current_config.opencl.preload = parse_boolean(value);
        } else if (strcmp(section, "performance") == 0) {
            if (strcmp(key_lower, "profile") == 0) g_current_config.performance.profile = parse_boolean(value);
            else if (strcmp(key_lower, "profile_interval_ms") == 0) g_current_config.performance.profile_interval_ms = (uint32_t)atoi(value);
        }
    }
    free(section);
}

/**
 * @brief Load configuration from file
 * 
 * Supports INI format configuration files.
 * 
 * @param filepath Path to configuration file
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_load(const char *filepath) {
    if (!g_config_initialized) {
        mvgal_config_init();
    }
    if (filepath == NULL || *filepath == '\0') {
        MVGAL_LOG_DEBUG("No config file path provided, using defaults");
        return MVGAL_SUCCESS;
    }
    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        MVGAL_LOG_WARN("Failed to open config file %s: %s, using defaults", filepath, strerror(errno));
        return MVGAL_SUCCESS;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size > 0) {
        char *buffer = malloc((size_t)file_size + 1);
        if (buffer) {
            size_t bytes_read = fread(buffer, 1, (size_t)file_size, f);
            buffer[bytes_read] = '\0';
            parse_ini_data(buffer);
            free(buffer);
        }
    }
    fclose(f);
    MVGAL_LOG_INFO("Configuration loaded from %s", filepath);
    return MVGAL_SUCCESS;
}

/**
 * @brief Load configuration from string
 * 
 * Parses INI format configuration from a string.
 * 
 * @param config_string Configuration string (INI format)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_load_string(const char *config_string) {
    if (!g_config_initialized) {
        mvgal_config_init();
    }
    if (config_string == NULL || *config_string == '\0') {
        MVGAL_LOG_DEBUG("Empty config string, using defaults");
        return MVGAL_SUCCESS;
    }
    parse_ini_data(config_string);
    MVGAL_LOG_INFO("Configuration loaded from string");
    return MVGAL_SUCCESS;
}

/**
 * @brief Write config value to buffer
 */
static void write_cfg(char **buf, size_t *size, size_t *offset, const char *fmt, ...) {
    if (*offset + 128 > *size) {
        *size *= 2;
        *buf = realloc(*buf, *size);
        if (*buf == NULL) return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(*buf + *offset, *size - *offset, fmt, args);
    va_end(args);
    if (written > 0) *offset += (size_t)written;
}

/**
 * @brief Save configuration to string
 * 
 * @param config_string Configuration string (out, must be freed by caller)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_save_string(char **config_string) {
    if (config_string == NULL) return MVGAL_ERROR_INVALID_ARGUMENT;
    if (!g_config_initialized) return MVGAL_ERROR_NOT_INITIALIZED;
    size_t buf_size = 4096;
    char *buf = malloc(buf_size);
    if (buf == NULL) {
        *config_string = NULL;
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    size_t offset = 0;
    const char *log_str;
    switch (g_current_config.log_level) {
        case MVGAL_LOG_LEVEL_ERROR: log_str = "error"; break;
        case MVGAL_LOG_LEVEL_WARN: log_str = "warn"; break;
        case MVGAL_LOG_LEVEL_INFO: log_str = "info"; break;
        case MVGAL_LOG_LEVEL_DEBUG: log_str = "debug"; break;
        case MVGAL_LOG_LEVEL_TRACE: log_str = "trace"; break;
        default: log_str = "info"; break;
    }
    const char *strat_str;
    switch (g_current_config.scheduler.strategy) {
        case MVGAL_STRATEGY_AFR: strat_str = "afr"; break;
        case MVGAL_STRATEGY_SFR: strat_str = "sfr"; break;
        case MVGAL_STRATEGY_TASK: strat_str = "task"; break;
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD: strat_str = "compute"; break;
        case MVGAL_STRATEGY_HYBRID: strat_str = "hybrid"; break;
        case MVGAL_STRATEGY_SINGLE_GPU: strat_str = "single"; break;
        default: strat_str = "hybrid"; break;
    }
    write_cfg(&buf, &buf_size, &offset, "[global]\nenabled=%s\nlog_level=%s\ndebug=%s\n\n",
             g_current_config.enabled ? "true" : "false", log_str, g_current_config.debug ? "true" : "false");
    write_cfg(&buf, &buf_size, &offset, "[gpus]\nauto_detect=%s\nmax_gpus=%u\nenable_all=%s\n\n",
             g_current_config.gpus.auto_detect ? "true" : "false", g_current_config.gpus.max_gpus,
             g_current_config.gpus.enable_all ? "true" : "false");
    write_cfg(&buf, &buf_size, &offset, "[scheduler]\nstrategy=%s\ndynamic_load_balance=%s\nthermal_aware=%s\npower_aware=%s\nload_balance_threshold=%.2f\nmax_queued_workloads=%u\n\n",
             strat_str,
             g_current_config.scheduler.dynamic_load_balance ? "true" : "false",
             g_current_config.scheduler.thermal_aware ? "true" : "false",
             g_current_config.scheduler.power_aware ? "true" : "false",
             g_current_config.scheduler.load_balance_threshold,
             g_current_config.scheduler.max_queued_workloads);
    write_cfg(&buf, &buf_size, &offset, "[memory]\nuse_dmabuf=%s\nuse_p2p=%s\nreplicate_small=%s\nreplicate_threshold=%zu\nmax_buffer_size=%zu\n\n",
             g_current_config.memory.use_dmabuf ? "true" : "false",
             g_current_config.memory.use_p2p ? "true" : "false",
             g_current_config.memory.replicate_small ? "true" : "false",
             g_current_config.memory.replicate_threshold,
             g_current_config.memory.max_buffer_size);
    write_cfg(&buf, &buf_size, &offset, "[vulkan]\nenabled=%s\nintercept_all=%s\n\n",
             g_current_config.vulkan.enabled ? "true" : "false",
             g_current_config.vulkan.intercept_all ? "true" : "false");
    write_cfg(&buf, &buf_size, &offset, "[opencl]\nenabled=%s\npreload=%s\n\n",
             g_current_config.opencl.enabled ? "true" : "false",
             g_current_config.opencl.preload ? "true" : "false");
    write_cfg(&buf, &buf_size, &offset, "[performance]\nprofile=%s\nprofile_interval_ms=%u\n",
             g_current_config.performance.profile ? "true" : "false",
             g_current_config.performance.profile_interval_ms);
    *config_string = buf;
    MVGAL_LOG_INFO("Configuration saved to string (%zu bytes)", offset);
    return MVGAL_SUCCESS;
}

/**
 * @brief Save configuration to file
 * 
 * @param filepath Path to configuration file
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_save(const char *filepath) {
    if (filepath == NULL || *filepath == '\0') return MVGAL_ERROR_INVALID_ARGUMENT;
    if (!g_config_initialized) return MVGAL_ERROR_NOT_INITIALIZED;
    char *config_str = NULL;
    mvgal_error_t err = mvgal_config_save_string(&config_str);
    if (err != MVGAL_SUCCESS) return err;
    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        MVGAL_LOG_ERROR("Failed to open config file %s for writing: %s", filepath, strerror(errno));
        free(config_str);
        return MVGAL_ERROR_DRIVER;
    }
    size_t bytes_written = fwrite(config_str, 1, strlen(config_str), f);
    fclose(f);
    free(config_str);
    if (bytes_written == 0) {
        MVGAL_LOG_ERROR("Failed to write config file %s", filepath);
        return MVGAL_ERROR_DRIVER;
    }
    MVGAL_LOG_INFO("Configuration saved to %s", filepath);
    return MVGAL_SUCCESS;
}

/**
 * @brief Get current configuration
 * 
 * @param config Configuration structure (out)
 */
void mvgal_config_get(mvgal_config_t *config) {
    if (!g_config_initialized) {
        mvgal_config_init();
    }
    if (config) {
        memcpy(config, &g_current_config, sizeof(*config));
    }
}

/**
 * @brief Set configuration
 * 
 * @param config Configuration structure
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_set(const mvgal_config_t *config) {
    if (!g_config_initialized) {
        mvgal_config_init();
    }
    if (config == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    mvgal_config_t next = *config;
    next.gpus.devices = NULL;
    next.vulkan.layer_path = NULL;
    next.opencl.library_path = NULL;
    next.performance.profile_file = NULL;
    for (int i = 0; i < 8; i++) {
        next.reserved[i] = NULL;
    }

    mvgal_error_t err = clone_config_strings(&next, config);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    free_owned_config_strings(&g_current_config);
    g_current_config = next;
    return MVGAL_SUCCESS;
}

/**
 * @brief Get a configuration value by name
 * 
 * @param name Configuration name (e.g., "gpus.enabled", "scheduler.strategy")
 * @param value Value (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_get_by_name(const char *name, mvgal_config_value_u *value) {
    return mvgal_config_get_value(name, value);
}

/**
 * @brief Set a configuration value by name
 * 
 * @param name Configuration name
 * @param value Value
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_set_by_name(const char *name, const mvgal_config_value_u *value) {
    return mvgal_config_set_value(name, value);
}

/**
 * @brief Register configuration change callback
 * 
 * @param name Configuration name (NULL for all)
 * @param callback Callback function
 * @param user_data User data
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_register_callback(
    const char *name,
    mvgal_config_callback_t callback,
    void *user_data
) {
    if (callback == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    if (name != NULL && find_config_descriptor(name) == NULL) {
        return MVGAL_ERROR_NOT_FOUND;
    }
    for (size_t i = 0; i < MVGAL_MAX_CONFIG_CALLBACKS; i++) {
        if (g_config_callbacks[i].callback == NULL) {
            g_config_callbacks[i].name = dup_nullable_string(name);
            if (name != NULL && g_config_callbacks[i].name == NULL) {
                return MVGAL_ERROR_OUT_OF_MEMORY;
            }
            g_config_callbacks[i].callback = callback;
            g_config_callbacks[i].user_data = user_data;
            return MVGAL_SUCCESS;
        }
    }
    return MVGAL_ERROR_BUSY;
}

/**
 * @brief Unregister configuration change callback
 * 
 * @param name Configuration name
 * @param callback Callback function
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_unregister_callback(
    const char *name,
    mvgal_config_callback_t callback
) {
    if (callback == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < MVGAL_MAX_CONFIG_CALLBACKS; i++) {
        config_callback_entry_t *entry = &g_config_callbacks[i];
        if (entry->callback != callback) {
            continue;
        }
        if ((name == NULL && entry->name == NULL) ||
            (name != NULL && entry->name != NULL && strcmp(name, entry->name) == 0)) {
            free(entry->name);
            entry->name = NULL;
            entry->callback = NULL;
            entry->user_data = NULL;
            return MVGAL_SUCCESS;
        }
    }
    return MVGAL_ERROR_NOT_FOUND;
}

/**
 * @brief Print configuration
 * 
 * @param file Output file
 */
void mvgal_config_print(FILE *file) {
    if (file == NULL) {
        file = stdout;
    }
    char *config_string = NULL;
    if (mvgal_config_save_string(&config_string) != MVGAL_SUCCESS || config_string == NULL) {
        fprintf(file, "# Failed to render MVGAL configuration\n");
        return;
    }
    fputs(config_string, file);
    free(config_string);
}

/**
 * @brief Reset configuration to defaults
 */
void mvgal_config_reset(void) {
    if (!g_config_initialized) {
        return;
    }
    free_owned_config_strings(&g_current_config);
    memcpy(&g_current_config, &g_default_config, sizeof(g_current_config));
    MVGAL_LOG_DEBUG("Configuration reset to defaults");
}

/**
 * @brief Set configuration file path
 * 
 * @param path Path to configuration file
 */
void mvgal_config_set_path(const char *path) {
    char *copy = dup_nullable_string(path);
    if (path != NULL && copy == NULL) {
        return;
    }
    free(g_config_path);
    g_config_path = copy;
}

/**
 * @brief Get a configuration value by name
 * 
 * @param name Configuration name (e.g., "gpus.enabled", "scheduler.strategy")
 * @param value Value (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_get_value(const char *name, mvgal_config_value_u *value) {
    if (!g_config_initialized) {
        mvgal_config_init();
    }
    if (value == NULL || name == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    if (find_config_descriptor(name) == NULL) {
        return MVGAL_ERROR_NOT_FOUND;
    }
    memset(value, 0, sizeof(*value));
    return get_config_value_internal(name, value);
}

/**
 * @brief Set a configuration value by name
 * 
 * @param name Configuration name
 * @param value Value
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_set_value(const char *name, const mvgal_config_value_u *value) {
    if (!g_config_initialized) {
        mvgal_config_init();
    }
    if (name == NULL || value == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    const config_descriptor_t *desc = find_config_descriptor(name);
    if (desc == NULL) {
        return MVGAL_ERROR_NOT_FOUND;
    }
    if (!mvgal_config_validate(name, value)) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    mvgal_config_value_u old_value = {0};
    mvgal_error_t err = get_config_value_internal(name, &old_value);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    char *old_string_copy = NULL;
    if (desc->kind == CFG_STRING) {
        old_string_copy = dup_nullable_string(old_value.s);
        if (old_value.s != NULL && old_string_copy == NULL) {
            return MVGAL_ERROR_OUT_OF_MEMORY;
        }
        old_value.s = old_string_copy;
    }

    if (config_value_equal(desc->kind, &old_value, value)) {
        free(old_string_copy);
        return MVGAL_SUCCESS;
    }

    err = set_config_value_internal(name, value);
    if (err == MVGAL_SUCCESS) {
        mvgal_config_value_u new_value = {0};
        if (get_config_value_internal(name, &new_value) == MVGAL_SUCCESS) {
            notify_config_callbacks(name, &old_value, &new_value);
        }
    }
    free(old_string_copy);
    return err;
}

const char *mvgal_config_get_default_path(void) {
    return g_config_path != NULL ? g_config_path : "/etc/mvgal/mvgal.conf";
}

bool mvgal_config_exists(const char *path) {
    const char *candidate = (path != NULL && *path != '\0') ? path : mvgal_config_get_default_path();
    FILE *file = fopen(candidate, "r");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

int mvgal_config_parse_args(int argc, char *argv[]) {
    int consumed = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mvgal-config") == 0 && i + 1 < argc) {
            mvgal_config_set_path(argv[i + 1]);
            mvgal_config_load(argv[i + 1]);
            i++;
            consumed += 2;
        } else if (strcmp(argv[i], "--mvgal-disable") == 0) {
            mvgal_config_value_u value = {.b = false};
            mvgal_config_set_value("enabled", &value);
            consumed++;
        } else if (strcmp(argv[i], "--mvgal-debug") == 0) {
            mvgal_config_value_u value = {.b = true};
            mvgal_config_set_value("debug", &value);
            consumed++;
        } else if (strcmp(argv[i], "--mvgal-strategy") == 0 && i + 1 < argc) {
            mvgal_config_value_u value = {.i = parse_strategy(argv[i + 1])};
            mvgal_config_set_value("scheduler.strategy", &value);
            i++;
            consumed += 2;
        }
    }
    return consumed;
}

int mvgal_config_get_entries(mvgal_config_entry_t *entries, int count) {
    if (!g_config_initialized) {
        mvgal_config_init();
    }
    if (entries == NULL || count <= 0) {
        return (int)g_config_descriptor_count;
    }

    int filled = 0;
    for (size_t i = 0; i < g_config_descriptor_count && filled < count; i++, filled++) {
        const config_descriptor_t *desc = &g_config_descriptors[i];
        mvgal_config_entry_t *entry = &entries[filled];
        memset(entry, 0, sizeof(*entry));
        entry->name = desc->name;
        entry->description = desc->description;
        entry->type = public_config_type(desc->kind);
        entry->min.f = desc->min_value;
        entry->max.f = desc->max_value;
        if (desc->kind == CFG_INT || desc->kind == CFG_ENUM || desc->kind == CFG_SIZE) {
            entry->min.i = (int64_t)desc->min_value;
            entry->max.i = (int64_t)desc->max_value;
        }
        get_config_value_internal(desc->name, &entry->value);
    }
    return filled;
}

bool mvgal_config_validate(const char *name, const mvgal_config_value_u *value) {
    const config_descriptor_t *desc = find_config_descriptor(name);
    if (desc == NULL || value == NULL) {
        return false;
    }
    switch (desc->kind) {
        case CFG_BOOL:
            return true;
        case CFG_INT:
        case CFG_ENUM:
        case CFG_SIZE:
            return (double)value->i >= desc->min_value && (double)value->i <= desc->max_value;
        case CFG_FLOAT:
            return value->f >= desc->min_value && value->f <= desc->max_value;
        case CFG_STRING:
            return true;
        default:
            return false;
    }
}
