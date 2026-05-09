/**
 * @file mvgal_power.h
 * @brief MVGAL Power Management API
 *
 * Runtime PM, S0ix idle states, and thermal management for multi-GPU systems.
 *
 * Copyright (C) 2026 MVGAL Project
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_POWER_H
#define MVGAL_POWER_H

#include "mvgal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup PowerManagement
 * @{
 */

/* ============================================================================
 * Power State Definitions
 * ============================================================================ */

/**
 * @brief GPU power states
 */
typedef enum {
    MVGAL_POWER_STATE_UNKNOWN = -1,
    MVGAL_POWER_STATE_OFF = 0,
    MVGAL_POWER_STATE_SUSPEND,
    MVGAL_POWER_STATE_IDLE,
    MVGAL_POWER_STATE_ON,
    MVGAL_POWER_STATE_PERFORMANCE,
} mvgal_power_state_t;

/**
 * @brief Thermal throttle reasons
 */
typedef enum {
    MVGAL_THERMAL_REASON_NONE = 0,
    MVGAL_THERMAL_REASON_HIGH_TEMP = 1,
    MVGAL_THERMAL_REASON_CRITICAL = 2,
    MVGAL_THERMAL_REASON_POWER_LIMIT = 3,
    MVGAL_THERMAL_REASON_THROTTLE_REQUEST = 4,
} mvgal_thermal_reason_t;

/* ============================================================================
 * Configuration Structures
 * ============================================================================ */

/**
 * @brief Power management configuration
 */
typedef struct {
    uint32_t idle_threshold_ms;      /**< Time before marking GPU idle (ms) */
    uint32_t park_timeout_ms;        /**< Time before parking idle GPU (ms) */
    bool enable_s0ix;                /**< Enable S0ix (suspend-to-idle) */
    bool lazy_init;                  /**< Enable lazy initialization */
    bool thermal_throttling;         /**< Enable thermal-based throttling */
    bool auto_pm;                    /**< Enable automatic power management */
} mvgal_pm_config_t;

/**
 * @brief Power management statistics
 */
typedef struct {
    uint64_t total_idle_transitions; /**< Number of idle state transitions */
    uint64_t total_s0ix_entries;   /**< Number of S0ix entries */
    uint64_t total_power_saves;     /**< Number of power save operations */
    uint64_t total_energy_saved_j;  /**< Total energy saved (joules) */
    uint64_t s0ix_time_ns;          /**< Time in S0ix (nanoseconds) */
    bool s0ix_active;               /**< Currently in S0ix */
    uint32_t s0ix_sequence;         /**< S0ix entry sequence number */
} mvgal_pm_stats_t;

/**
 * @brief GPU power status
 */
typedef struct {
    mvgal_power_state_t current_state;  /**< Current power state */
    mvgal_power_state_t requested_state; /**< Requested power state */
    bool is_idle;                       /**< GPU is idle */
    bool is_parked;                     /**< GPU is parked */
    bool is_throttled;                  /**< GPU is thermally throttled */
    float current_temp;                 /**< Current temperature (C) */
    float current_power_w;              /**< Current power consumption (W) */
    uint32_t throttle_reason;           /**< Thermal throttle reason */
    bool s0ix_capable;                  /**< GPU supports S0ix */
    bool s0ix_enabled;                  /**< S0ix enabled for this GPU */
    uint64_t s0ix_time_total_ns;        /**< Total time in S0ix */
} mvgal_gpu_power_status_t;

/**
 * @brief Lazy initialization callback
 * @param gpu_index GPU index
 * @param user_data User-provided data
 */
typedef void (*mvgal_lazy_init_callback_t)(uint32_t gpu_index, void *user_data);

/* ============================================================================
 * Initialization and Control
 * ============================================================================ */

/**
 * @brief Initialize power management
 * @param config Power management configuration (NULL for defaults)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_init(const mvgal_pm_config_t *config);

/**
 * @brief Shutdown power management
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_shutdown(void);

/**
 * @brief Start power monitoring thread
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_start_monitor(void);

/**
 * @brief Stop power monitoring thread
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_stop_monitor(void);

/* ============================================================================
 * GPU Power Control
 * ============================================================================ */

/**
 * @brief Set GPU power state
 * @param gpu_index GPU index
 * @param state Desired power state
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_set_gpu_state(uint32_t gpu_index, mvgal_power_state_t state);

/**
 * @brief Get GPU power state
 * @param gpu_index GPU index
 * @param state Power state (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_get_gpu_state(uint32_t gpu_index, mvgal_power_state_t *state);

/**
 * @brief Get detailed GPU power status
 * @param gpu_index GPU index
 * @param status Power status (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_get_gpu_status(uint32_t gpu_index, mvgal_gpu_power_status_t *status);

/**
 * @brief Enable automatic power management for GPU
 * @param gpu_index GPU index
 * @param enable true to enable, false to disable
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_set_auto_pm(uint32_t gpu_index, bool enable);

/* ============================================================================
 * S0ix (Suspend-to-Idle) Control
 * ============================================================================ */

/**
 * @brief Check if system supports S0ix
 * @return true if S0ix is available
 */
bool mvgal_pm_s0ix_available(void);

/**
 * @brief Enable/disable S0ix globally
 * @param enable true to enable, false to disable
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_set_s0ix_enabled(bool enable);

/**
 * @brief Force entry into S0ix state
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_enter_s0ix(void);

/**
 * @brief Force exit from S0ix state
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_exit_s0ix(void);

/**
 * @brief Check if currently in S0ix
 * @return true if in S0ix state
 */
bool mvgal_pm_is_s0ix_active(void);

/* ============================================================================
 * GPU Parking
 * ============================================================================ */

/**
 * @brief Park (idle) a GPU
 * @param gpu_index GPU index
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_park_gpu(uint32_t gpu_index);

/**
 * @brief Unpark (activate) a GPU
 * @param gpu_index GPU index
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_unpark_gpu(uint32_t gpu_index);

/**
 * @brief Check if GPU is parked
 * @param gpu_index GPU index
 * @return true if parked, false otherwise
 */
bool mvgal_pm_is_gpu_parked(uint32_t gpu_index);

/* ============================================================================
 * Activity and Idle Management
 * ============================================================================ */

/**
 * @brief Signal activity on a GPU
 * @param gpu_index GPU index
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_signal_activity(uint32_t gpu_index);

/**
 * @brief Check if GPU is idle
 * @param gpu_index GPU index
 * @return true if idle, false otherwise
 */
bool mvgal_pm_is_gpu_idle(uint32_t gpu_index);

/**
 * @brief Set idle threshold for a GPU
 * @param gpu_index GPU index
 * @param threshold_ms Idle threshold in milliseconds
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_set_idle_threshold(uint32_t gpu_index, uint32_t threshold_ms);

/**
 * @brief Request lazy initialization
 * @param gpu_index GPU index
 * @param callback Callback function to execute
 * @param user_data User data for callback
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_request_lazy_init(uint32_t gpu_index,
                                          mvgal_lazy_init_callback_t callback,
                                          void *user_data);

/* ============================================================================
 * Runtime PM
 * ============================================================================ */

/**
 * @brief Enable runtime PM for a GPU
 * @param gpu_index GPU index
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_enable_runtime_pm(uint32_t gpu_index);

/**
 * @brief Disable runtime PM for a GPU
 * @param gpu_index GPU index
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_disable_runtime_pm(uint32_t gpu_index);

/**
 * @brief Check if runtime PM is enabled for GPU
 * @param gpu_index GPU index
 * @return true if enabled, false otherwise
 */
bool mvgal_pm_runtime_pm_enabled(uint32_t gpu_index);

/* ============================================================================
 * Statistics and Information
 * ============================================================================ */

/**
 * @brief Get power management statistics
 * @param stats Statistics (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_get_stats(mvgal_pm_stats_t *stats);

/**
 * @brief Convert power state to string
 * @param state Power state
 * @return String representation
 */
const char *mvgal_pm_state_to_string(mvgal_power_state_t state);

/**
 * @brief Get estimated power savings
 * @param savings_j Energy saved in joules (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_get_energy_savings(uint64_t *savings_j);

/**
 * @brief Reset power statistics
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_pm_reset_stats(void);

/** @} */ // end of PowerManagement

#ifdef __cplusplus
}
#endif

#endif // MVGAL_POWER_H
