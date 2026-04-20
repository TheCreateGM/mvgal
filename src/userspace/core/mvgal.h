/**
 * @file mvgal.h
 * @brief MVGAL Core Public API
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Public API for the MVGAL core library
 */

#ifndef MVGAL_H
#define MVGAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Workload distribution strategies
 */
typedef enum {
    MVGAL_STRATEGY_ROUND_ROBIN = 0,  ///< Round-robin distribution
    MVGAL_STRATEGY_AFR,              ///< Alternate Frame Rendering
    MVGAL_STRATEGY_SFR,              ///< Split Frame Rendering
    MVGAL_STRATEGY_SINGLE,           ///< Single GPU only
    MVGAL_STRATEGY_HYBRID,           ///< Hybrid distribution
    MVGAL_STRATEGY_CUSTOM            ///< Custom strategy
} mvgal_strategy_t;

/**
 * @brief GPU information
 */
typedef struct {
    int id;                     ///< GPU index
    char name[256];            ///< GPU name
    char vendor[64];           ///< Vendor name
    uint64_t memory_total;     ///< Total memory in bytes
    uint64_t memory_used;      ///< Used memory in bytes
    int priority;              ///< Priority (lower = higher)
    bool enabled;              ///< Whether GPU is enabled
    bool active;               ///< Whether GPU is currently active
} mvgal_gpu_info_t;

/**
 * @brief Workload statistics
 */
typedef struct {
    uint64_t total_workloads;       ///< Total workloads processed
    uint64_t workloads_per_gpu[8];   ///< Workloads per GPU
    double load_balance;            ///< Load balance percentage (0-100)
    uint64_t memory_allocated;       ///< Memory allocated in bytes
    uint64_t memory_used;           ///< Memory used in bytes
    double avg_latency_ms;          ///< Average latency in milliseconds
} mvgal_stats_t;

/**
 * @brief Configuration options
 */
typedef struct {
    bool enabled;                        ///< Whether MVGAL is enabled
    char debug_level[16];               ///< Debug level string
    int gpu_count;                      ///< Number of GPUs to use
    mvgal_strategy_t default_strategy;  ///< Default distribution strategy
    bool enable_memory_migration;       ///< Enable memory migration
    bool enable_dmabuf;                 ///< Enable DMA-BUF support
    bool enable_kernel_names;          ///< Enable kernel name resolution
    int stats_interval;                ///< Statistics collection interval
    bool enable_stats;                  ///< Enable statistics collection
} mvgal_config_t;

/**
 * @brief Initialize MVGAL
 * @param config_file Path to configuration file (NULL for default)
 * @return 0 on success, -1 on error
 */
int mvgal_init(const char *config_file);

/**
 * @brief Shutdown MVGAL
 */
void mvgal_shutdown(void);

/**
 * @brief Get number of detected GPUs
 * @return Number of GPUs
 */
int mvgal_get_gpu_count(void);

/**
 * @brief Get GPU information
 * @param index GPU index
 * @param info Pointer to mvgal_gpu_info_t to fill
 * @return 0 on success, -1 on error
 */
int mvgal_get_gpu_info(int index, mvgal_gpu_info_t *info);

/**
 * @brief Set workload distribution strategy
 * @param strategy Strategy to use
 * @return 0 on success, -1 on error
 */
int mvgal_set_strategy(mvgal_strategy_t strategy);

/**
 * @brief Get current strategy
 * @return Current strategy
 */
mvgal_strategy_t mvgal_get_strategy(void);

/**
 * @brief Enable/disable a GPU
 * @param index GPU index
 * @param enabled Whether to enable
 * @return 0 on success, -1 on error
 */
int mvgal_set_gpu_enabled(int index, bool enabled);

/**
 * @brief Set GPU priority
 * @param index GPU index
 * @param priority Priority (lower = higher)
 * @return 0 on success, -1 on error
 */
int mvgal_set_gpu_priority(int index, int priority);

/**
 * @brief Get current statistics
 * @param stats Pointer to mvgal_stats_t to fill
 * @return 0 on success, -1 on error
 */
int mvgal_get_stats(mvgal_stats_t *stats);

/**
 * @brief Reset statistics
 * @return 0 on success, -1 on error
 */
int mvgal_reset_stats(void);

/**
 * @brief Reload configuration from file
 * @return 0 on success, -1 on error
 */
int mvgal_reload_config(void);

/**
 * @brief Get current configuration
 * @param config Pointer to mvgal_config_t to fill
 * @return 0 on success, -1 on error
 */
int mvgal_get_config(mvgal_config_t *config);

/**
 * @brief Set configuration option
 * @param key Configuration key
 * @param value Configuration value
 * @return 0 on success, -1 on error
 */
int mvgal_set_config(const char *key, const char *value);

/**
 * @brief Log a message through MVGAL's logging system
 * @param level Log level
 * @param format Format string
 * @param ... Arguments
 */
void mvgal_log(const char *level, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif // MVGAL_H
