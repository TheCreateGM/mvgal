/**
 * @file mvgal_config.h
 * @brief Configuration API
 * 
 * Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * This header provides configuration management functions.
 */

#ifndef MVGAL_CONFIG_H
#define MVGAL_CONFIG_H

#include "mvgal_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup Configuration
 * @{
 */

/**
 * @brief MVGAL global configuration structure
 */
typedef struct {
    // Global settings
    bool enabled;                  ///< Whether MVGAL is enabled
    mvgal_log_level_t log_level;  ///< Logging level
    bool debug;                    ///< Enable debug mode
    
    // GPU settings
    struct {
        bool auto_detect;          ///< Auto-detect GPUs
        char *devices;              ///< Comma-separated list of device nodes
        uint32_t max_gpus;          ///< Maximum number of GPUs to use
        bool enable_all;            ///< Enable all detected GPUs
    } gpus;
    
    // Scheduler settings
    struct {
        mvgal_distribution_strategy_t strategy; ///< Distribution strategy
        bool dynamic_load_balance;  ///< Enable dynamic load balancing
        bool thermal_aware;          ///< Enable thermal-aware scheduling
        bool power_aware;           ///< Enable power-aware scheduling
        float load_balance_threshold; ///< Load balance threshold (0.0-1.0)
        uint32_t max_queued_workloads; ///< Maximum queued workloads
    } scheduler;
    
    // Memory settings
    struct {
        bool use_dmabuf;            ///< Use DMA-BUF for sharing
        bool use_p2p;               ///< Use PCIe P2P transfers
        bool replicate_small;       ///< Replicate small buffers
        size_t replicate_threshold;  ///< Threshold for replication (bytes)
        size_t max_buffer_size;     ///< Maximum buffer size (0 = unlimited)
    } memory;
    
    // Vulkan layer settings
    struct {
        bool enabled;               ///< Enable Vulkan layer
        bool intercept_all;         //< Intercept all Vulkan calls
        char *layer_path;           ///< Path to layer library
    } vulkan;
    
    // OpenCL settings
    struct {
        bool enabled;               ///< Enable OpenCL interception
        bool preload;               ///< Use LD_PRELOAD
        char *library_path;         ///< Path to OpenCL library
    } opencl;
    
    // Performance settings
    struct {
        bool profile;               ///< Enable profiling
        char *profile_file;         ///< Path to profile output file
        uint32_t profile_interval_ms; ///< Profiling interval (ms)
    } performance;
    
    // Reserved for future use
    void *reserved[8];
} mvgal_config_t;

/**
 * @brief Configuration value type
 */
typedef enum {
    MVGAL_CONFIG_BOOL = 0,        ///< Boolean value
    MVGAL_CONFIG_INT = 1,          ///< Integer value
    MVGAL_CONFIG_FLOAT = 2,        ///< Float value
    MVGAL_CONFIG_STRING = 3,       ///< String value
    MVGAL_CONFIG_ENUM = 4,         ///< Enumeration value
} mvgal_config_value_type_t;

/**
 * @brief Configuration value union
 */
typedef union {
    bool b;
    int64_t i;
    double f;
    char *s;
    void *ptr;
} mvgal_config_value_u;

/**
 * @brief Configuration entry
 */
typedef struct {
    const char *name;              ///< Configuration name
    const char *description;       ///< Description
    mvgal_config_value_type_t type;///< Value type
    mvgal_config_value_u value;    ///< Value
    mvgal_config_value_u def;      ///< Default value
    mvgal_config_value_u min;      ///< Minimum value (for numbers)
    mvgal_config_value_u max;      ///< Maximum value (for numbers)
} mvgal_config_entry_t;

/**
 * @brief Configuration change callback
 * @param name Configuration name
 * @param old_value Old value
 * @param new_value New value
 * @param user_data User data
 */
typedef void (*mvgal_config_callback_t)(
    const char *name,
    const mvgal_config_value_u *old_value,
    const mvgal_config_value_u *new_value,
    void *user_data
);

/**
 * @brief Initialize configuration from default values
 * 
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_init(void);

/**
 * @brief Shutdown configuration system
 */
void mvgal_config_shutdown(void);

/**
 * @brief Load configuration from file
 * 
 * @param filepath Path to configuration file
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_load(const char *filepath);

/**
 * @brief Load configuration from string
 * 
 * @param config_string Configuration string (INI format)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_load_string(const char *config_string);

/**
 * @brief Save configuration to file
 * 
 * @param filepath Path to configuration file
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_save(const char *filepath);

/**
 * @brief Save configuration to string
 * 
 * @param config_string Configuration string (out, must be freed)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_save_string(char **config_string);

/**
 * @brief Get current configuration
 * 
 * @param config Configuration structure (out)
 */
void mvgal_config_get(mvgal_config_t *config);

/**
 * @brief Set configuration
 * 
 * @param config Configuration structure
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_set(const mvgal_config_t *config);

/**
 * @brief Get a configuration value by name
 * 
 * @param name Configuration name (e.g., "gpus.enabled", "scheduler.strategy")
 * @param value Value (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_get_value(const char *name, mvgal_config_value_u *value);

/**
 * @brief Set a configuration value by name
 * 
 * @param name Configuration name
 * @param value Value
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_set_value(const char *name, const mvgal_config_value_u *value);

/**
 * @brief Register a configuration change callback
 * 
 * @param name Configuration name to monitor
 * @param callback Callback function
 * @param user_data User data
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_register_callback(
    const char *name,
    mvgal_config_callback_t callback,
    void *user_data
);

/**
 * @brief Unregister a configuration callback
 * 
 * @param name Configuration name
 * @param callback Callback function to remove
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_config_unregister_callback(
    const char *name,
    mvgal_config_callback_t callback
);

/**
 * @brief Get default configuration path
 * 
 * @return Default configuration file path
 */
const char *mvgal_config_get_default_path(void);

/**
 * @brief Set configuration path
 * 
 * @param path Configuration file path
 */
void mvgal_config_set_path(const char *path);

/**
 * @brief Check if configuration file exists
 * 
 * @param path Path to check (NULL for default)
 * @return true if exists, false otherwise
 */
bool mvgal_config_exists(const char *path);

/**
 * @brief Reset configuration to defaults
 */
void mvgal_config_reset(void);

/**
 * @brief Parse command line arguments
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Number of arguments consumed
 */
int mvgal_config_parse_args(int argc, char *argv[]);

/**
 * @brief Print configuration
 * 
 * @param file FILE pointer to print to (NULL for stdout)
 */
void mvgal_config_print(FILE *file);

/**
 * @brief Get configuration entries
 * 
 * @param entries Array of entries (out)
 * @param count Size of array
 * @return Number of entries filled
 */
int mvgal_config_get_entries(mvgal_config_entry_t *entries, int count);

/**
 * @brief Check if configuration value is valid
 * 
 * @param name Configuration name
 * @param value Value to validate
 * @return true if valid, false otherwise
 */
bool mvgal_config_validate(const char *name, const mvgal_config_value_u *value);

/** @} */ // end of Configuration

#ifdef __cplusplus
}
#endif

#endif // MVGAL_CONFIG_H
