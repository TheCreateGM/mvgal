/**
 * @file mvgal_scheduler.h
 * @brief Scheduler API
 * 
 * Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * This header provides workload scheduling and distribution functions.
 */

#ifndef MVGAL_SCHEDULER_H
#define MVGAL_SCHEDULER_H

#include <stddef.h>
#include "mvgal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup Scheduler
 * @{
 */

/**
 * @brief Workload handle
 */
typedef struct mvgal_workload *mvgal_workload_t;

/**
 * @brief Workload descriptor
 */
typedef struct {
    uint32_t id;                    ///< Workload ID
    mvgal_workload_type_t type;    ///< Workload type
    uint32_t priority;               ///< Priority (0-100, higher = more important)
    uint64_t submission_time;        ///< Submission timestamp (ns)
    uint64_t deadline;               ///< Deadline timestamp (ns, 0 = none)
    
    // Source information
    void *source;                   ///< Source context/handle
    mvgal_api_type_t api;          ///< API used to submit
    
    // Resource requirements
    uint64_t estimated_duration_ns; ///< Estimated duration in nanoseconds
    size_t memory_required;         ///< Memory required in bytes
    uint32_t gpu_preference;        ///< Preferred GPU index (or 0xFFFFFFFF for any)
    uint64_t gpu_mask;              ///< Bitmask of acceptable GPUs
    
    // Dependencies
    uint32_t dependency_count;      ///< Number of dependencies
    mvgal_fence_t *dependencies;    ///< Fences that must be signaled
    
    // Feedback
    bool completed;                 ///< Whether workload is completed
    mvgal_error_t result;           ///< Result/error code
    uint64_t start_time;             ///< Start timestamp (ns)
    uint64_t end_time;               ///< End timestamp (ns)
    uint32_t assigned_gpu;           ///< GPU assigned to
    
    // Reserved
    void *reserved[4];
} mvgal_workload_descriptor_t;

/**
 * @brief Workload submit info
 */
typedef struct {
    mvgal_workload_type_t type;    ///< Workload type
    uint32_t priority;               ///< Priority (0-100)
    uint64_t deadline;               ///< Deadline (ns, 0 = none)
    uint32_t gpu_mask;               ///< Bitmask of acceptable GPUs
    uint32_t dependency_count;      ///< Number of dependencies
    mvgal_fence_t *dependencies;    ///< Dependency fences
    void *user_data;                ///< User data
} mvgal_workload_submit_info_t;

/**
 * @brief Workload completion callback
 * @param workload Workload handle
 * @param result Result/error code
 * @param user_data User data provided at submission
 */
typedef void (*mvgal_workload_callback_t)(
    mvgal_workload_t workload,
    mvgal_error_t result,
    void *user_data
);

/**
 * @brief Custom workload splitter
 * 
 * Callbacks for custom workload splitting logic.
 */
typedef struct {
    /**
     * @brief Analyze workload and determine distribution
     * @param workload Workload to analyze
     * @param workload_info Workload information
     * @param gpu_count Number of available GPUs
     * @param gpu_indices Array of GPU indices
     * @param split_info Output split information
     * @param user_data User data
     */
    bool (*analyze)(
        mvgal_workload_t workload,
        const mvgal_workload_submit_info_t *workload_info,
        uint32_t gpu_count,
        const uint32_t *gpu_indices,
        void *split_info,
        void *user_data
    );
    
    /**
     * @brief Split workload into parts
     * @param workload Workload to split
     * @param split_info Split information from analyze
     * @param part_count Number of parts to create
     * @param parts Array of workload parts (out)
     * @param user_data User data
     */
    bool (*split)(
        mvgal_workload_t workload,
        void *split_info,
        uint32_t part_count,
        mvgal_workload_t *parts,
        void *user_data
    );
    
    /**
     * @brief Merge results from split workloads
     * @param workload Original workload
     * @param part_count Number of parts
     * @param parts Array of workload parts
     * @param user_data User data
     */
    bool (*merge)(
        mvgal_workload_t workload,
        uint32_t part_count,
        mvgal_workload_t *parts,
        void *user_data
    );
    
    void *user_data;                ///< User data for callbacks
} mvgal_workload_splitter_t;

/**
 * @brief Scheduler statistics
 */
typedef struct {
    uint64_t workloads_submitted;    ///< Total workloads submitted
    uint64_t workloads_completed;   ///< Total workloads completed
    uint64_t workloads_failed;      ///< Total workloads failed
    uint64_t total_execution_time_ns; ///< Total execution time (ns)
    uint64_t total_wait_time_ns;    ///< Total wait time (ns)
    double average_throughput;      ///< Average throughput (workloads/sec)
    double average_latency_ns;      ///< Average latency (ns)
    uint64_t gpu_assignments[16];    ///< Workload count per GPU
    uint64_t gpuutilization[16];      ///< Utilization per GPU (0-100)
} mvgal_scheduler_stats_t;

/**
 * @brief Scheduler configuration
 */
typedef struct {
    mvgal_distribution_strategy_t strategy; ///< Distribution strategy
    bool dynamic_load_balance;       ///< Enable dynamic load balancing
    bool thermal_aware;               ///< Enable thermal-aware scheduling
    bool power_aware;                 ///< Enable power-aware scheduling
    float load_balance_threshold;   ///< Load balance threshold (0.0-1.0)
    uint32_t max_queued_workloads;  ///< Maximum queued workloads
    uint32_t quantum_ns;              ///< Scheduling quantum in ns
} mvgal_scheduler_config_t;

/**
 * @brief Submit a workload for execution
 * 
 * @param context Context to submit to
 * @param info Workload submit info
 * @param workload Workload handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_workload_submit(
    void *context,
    const mvgal_workload_submit_info_t *info,
    mvgal_workload_t *workload
);

/**
 * @brief Submit a workload with callback
 * 
 * @param context Context to submit to
 * @param info Workload submit info
 * @param callback Completion callback
 * @param workload Workload handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_workload_submit_with_callback(
    void *context,
    const mvgal_workload_submit_info_t *info,
    mvgal_workload_callback_t callback,
    mvgal_workload_t *workload
);

/**
 * @brief Wait for workload completion
 * 
 * @param workload Workload to wait for
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return MVGAL_SUCCESS if completed, MVGAL_ERROR_TIMEOUT if timed out
 */
mvgal_error_t mvgal_workload_wait(
    mvgal_workload_t workload,
    uint32_t timeout_ms
);

/**
 * @brief Check if workload is completed
 * 
 * @param workload Workload to check
 * @return true if completed, false otherwise
 */
bool mvgal_workload_is_completed(mvgal_workload_t workload);

/**
 * @brief Get workload result
 * 
 * @param workload Workload to query
 * @return Result/error code
 */
mvgal_error_t mvgal_workload_get_result(mvgal_workload_t workload);

/**
 * @brief Get workload descriptor
 * 
 * @param workload Workload to query
 * @param desc Descriptor (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_workload_get_descriptor(
    mvgal_workload_t workload,
    mvgal_workload_descriptor_t *desc
);

/**
 * @brief Cancel a pending workload
 * 
 * @param workload Workload to cancel
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_workload_cancel(mvgal_workload_t workload);

/**
 * @brief Destroy a workload handle
 * 
 * @param workload Workload to destroy
 */
void mvgal_workload_destroy(mvgal_workload_t workload);

/**
 * @brief Set workload priority
 * 
 * @param workload Workload to modify
 * @param priority New priority (0-100)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_workload_set_priority(
    mvgal_workload_t workload,
    uint32_t priority
);

/**
 * @brief Configure the scheduler
 * 
 * @param context Context
 * @param config Scheduler configuration
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_configure(
    void *context,
    const mvgal_scheduler_config_t *config
);

/**
 * @brief Get scheduler configuration
 * 
 * @param context Context
 * @param config Configuration (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_get_config(
    void *context,
    mvgal_scheduler_config_t *config
);

/**
 * @brief Get scheduler statistics
 * 
 * @param context Context
 * @param stats Statistics (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_get_stats(
    void *context,
    mvgal_scheduler_stats_t *stats
);

/**
 * @brief Reset scheduler statistics
 * 
 * @param context Context
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_reset_stats(void *context);

/**
 * @brief Set the distribution strategy
 * 
 * @param context Context
 * @param strategy Distribution strategy
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_set_strategy(
    void *context,
    mvgal_distribution_strategy_t strategy
);

/**
 * @brief Get the current distribution strategy
 * 
 * @param context Context
 * @return Current distribution strategy
 */
mvgal_distribution_strategy_t mvgal_scheduler_get_strategy(void *context);

/**
 * @brief Manually assign workload to specific GPUs
 * 
 * Override automatic scheduling for specific workloads.
 * 
 * @param workload Workload to assign
 * @param gpu_count Number of GPUs to assign to
 * @param gpu_indices Array of GPU indices
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_workload_assign_gpus(
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices
);

/**
 * @brief Register a custom workload splitter
 * 
 * @param context Context
 * @param splitter Workload splitter
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_register_splitter(
    void *context,
    const mvgal_workload_splitter_t *splitter
);

/**
 * @brief Unregister a custom workload splitter
 * 
 * @param context Context
 * @param splitter Workload splitter to unregister
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_unregister_splitter(
    void *context,
    const mvgal_workload_splitter_t *splitter
);

/**
 * @brief Set GPU priority
 * 
 * @param context Context
 * @param gpu_index GPU index
 * @param priority Priority (0-100, higher = more preferred)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_set_gpu_priority(
    void *context,
    uint32_t gpu_index,
    uint32_t priority
);

/**
 * @brief Get GPU priority
 * 
 * @param context Context
 * @param gpu_index GPU index
 * @param priority Priority (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_get_gpu_priority(
    void *context,
    uint32_t gpu_index,
    uint32_t *priority
);

/**
 * @brief Pause scheduling
 * 
 * @param context Context
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_pause(void *context);

/**
 * @brief Resume scheduling
 * 
 * @param context Context
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_resume(void *context);

/**
 * @brief Check if scheduler is paused
 * 
 * @param context Context
 * @return true if paused, false otherwise
 */
bool mvgal_scheduler_is_paused(void *context);

/**
 * @brief Process pending workloads
 * 
 * Manually trigger scheduling (useful for debugging).
 * 
 * @param context Context
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_scheduler_process(void *context);

/**
 * @brief Distribute workload across GPUs using AFR strategy
 * 
 * @param context Context
 * @param workload Workload to distribute
 * @param gpu_count Number of GPUs
 * @param gpu_indices Array of GPU indices
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_distribute_afr(
    void *context,
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices
);

/**
 * @brief Distribute workload across GPUs using SFR strategy
 * 
 * @param context Context
 * @param workload Workload to distribute
 * @param gpu_count Number of GPUs
 * @param gpu_indices Array of GPU indices
 * @param regions Split regions (out)
 * @param region_count Number of regions (in/out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_distribute_sfr(
    void *context,
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_rect_t *regions,
    uint32_t *region_count
);

/**
 * @brief Distribute workload based on task type
 * 
 * @param context Context
 * @param workload Workload to distribute
 * @param gpu_count Number of GPUs
 * @param gpu_indices Array of GPU indices
 * @param capabilities GPU capabilities
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_distribute_task(
    void *context,
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    uint64_t *capabilities
);

/** @} */ // end of Scheduler

#ifdef __cplusplus
}
#endif

#endif // MVGAL_SCHEDULER_H
