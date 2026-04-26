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
    
    // Free any allocated strings
    free(g_current_config.gpus.devices);
    free(g_current_config.vulkan.layer_path);
    free(g_current_config.opencl.library_path);
    free(g_current_config.performance.profile_file);
    
    for (int i = 0; i < 8; i++) {
        free(g_current_config.reserved[i]);
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
    for (size_t i = 0; lower[i]; i++) lower[i] = tolower(lower[i]);
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
    for (size_t i = 0; lower[i]; i++) lower[i] = tolower(lower[i]);
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
    for (size_t i = 0; lower[i]; i++) lower[i] = tolower(lower[i]);
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
        for (size_t i = 0; key_lower[i]; i++) key_lower[i] = tolower(key_lower[i]);
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
        char *buffer = malloc(file_size + 1);
        if (buffer) {
            size_t bytes_read = fread(buffer, 1, file_size, f);
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
    if (written > 0) *offset += written;
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
    if (config) {
        memcpy(&g_current_config, config, sizeof(g_current_config));
    }
    return MVGAL_SUCCESS;
}

/**
 * @brief Get a configuration value by name (stub)
 * 
 * @param name Configuration name (e.g., "gpus.enabled", "scheduler.strategy")
 * @param value Value (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_get_by_name(const char *name, mvgal_config_value_u *value) {
    (void)name;
    (void)value;
    MVGAL_LOG_WARN("Config get by name not yet implemented");
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Set a configuration value by name (stub)
 * 
 * @param name Configuration name
 * @param value Value
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_set_by_name(const char *name, const mvgal_config_value_u *value) {
    (void)name;
    (void)value;
    MVGAL_LOG_WARN("Config set by name not yet implemented");
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Register configuration change callback (stub)
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
    (void)name;
    (void)callback;
    (void)user_data;
    MVGAL_LOG_WARN("Config callback registration not yet implemented");
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Unregister configuration change callback (stub)
 * 
 * @param name Configuration name
 * @param callback Callback function
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_unregister_callback(
    const char *name,
    mvgal_config_callback_t callback
) {
    (void)name;
    (void)callback;
    MVGAL_LOG_WARN("Config callback unregistration not yet implemented");
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Print configuration (stub)
 * 
 * @param file Output file
 */
void mvgal_config_print(FILE *file) {
    (void)file;
    MVGAL_LOG_WARN("Config printing not yet implemented");
}

/**
 * @brief Reset configuration to defaults
 */
void mvgal_config_reset(void) {
    if (!g_config_initialized) {
        return;
    }
    memcpy(&g_current_config, &g_default_config, sizeof(g_current_config));
    MVGAL_LOG_DEBUG("Configuration reset to defaults");
}

/**
 * @brief Set configuration file path
 * 
 * @param path Path to configuration file
 */
void mvgal_config_set_path(const char *path) {
    (void)path;
    MVGAL_LOG_WARN("Config path setting not yet implemented");
}

/**
 * @brief Get a configuration value by name
 * 
 * @param name Configuration name (e.g., "gpus.enabled", "scheduler.strategy")
 * @param value Value (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_get_value(const char *name, mvgal_config_value_u *value) {
    (void)name;
    (void)value;
    MVGAL_LOG_WARN("Config get_value not yet implemented");
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Set a configuration value by name
 * 
 * @param name Configuration name
 * @param value Value
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_set_value(const char *name, const mvgal_config_value_u *value) {
    (void)name;
    (void)value;
    MVGAL_LOG_WARN("Config set_value not yet implemented");
    return MVGAL_ERROR_NOT_SUPPORTED;
}
