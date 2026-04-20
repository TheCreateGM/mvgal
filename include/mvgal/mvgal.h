/**
 * @file mvgal.h
 * @brief Main MVGAL API header
 * 
 * Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * This header provides the main API for interacting with MVGAL.
 * It includes initialization, GPU management, and core functionality.
 */

#ifndef MVGAL_H
#define MVGAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Version information
#include "mvgal_version.h"

// Type definitions
#include "mvgal_types.h"

// GPU management
#include "mvgal_gpu.h"

// Memory management
#include "mvgal_memory.h"

// Scheduler
#include "mvgal_scheduler.h"

// Logging
#include "mvgal_log.h"

// Configuration
#include "mvgal_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup CoreAPI
 * @{
 */

/**
 * @brief Initialize MVGAL
 * 
 * This function initializes the MVGAL system, detects available GPUs,
 * and prepares the aggregation layer for use.
 * 
 * @param flags Initialization flags (reserved for future use)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_init(uint32_t flags);

/**
 * @brief Initialize MVGAL with configuration
 * 
 * Initialize MVGAL with a custom configuration file.
 * 
 * @param config_path Path to configuration file (NULL for default)
 * @param flags Initialization flags
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_init_with_config(const char *config_path, uint32_t flags);

/**
 * @brief Shutdown MVGAL
 * 
 * Clean up all resources and shutdown the MVGAL system.
 * This should be called when MVGAL is no longer needed.
 */
void mvgal_shutdown(void);

/**
 * @brief Check if MVGAL is initialized
 * 
 * @return true if MVGAL is initialized, false otherwise
 */
bool mvgal_is_initialized(void);

/**
 * @brief Get MVGAL version information
 * 
 * @return Pointer to version string (static, do not free)
 */
const char *mvgal_get_version(void);

/**
 * @brief Get MVGAL version numbers
 * 
 * @param major Major version (out)
 * @param minor Minor version (out)
 * @param patch Patch version (out)
 */
void mvgal_get_version_numbers(uint32_t *major, uint32_t *minor, uint32_t *patch);

/**
 * @brief Create a new MVGAL context
 * 
 * A context represents a connection to the MVGAL system and manages
 * resources for workload submission.
 * 
 * @param context Pointer to context handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_context_create(mvgal_context_t *context);

/**
 * @brief Destroy an MVGAL context
 * 
 * @param context Context to destroy
 */
void mvgal_context_destroy(mvgal_context_t context);

/**
 * @brief Set the current context
 * 
 * Sets the context that will be used by subsequent MVGAL calls
 * that don't explicitly take a context parameter.
 * 
 * @param context Context to set as current
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_context_set_current(mvgal_context_t context);

/**
 * @brief Get the current context
 * 
 * @return Current context handle, or NULL if none set
 */
mvgal_context_t mvgal_context_get_current(void);

/**
 * @brief Flush all pending operations
 * 
 * Blocks until all submitted workloads have completed.
 * 
 * @param context Context to flush
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_flush(mvgal_context_t context);

/**
 * @brief Finish all operations and release resources
 * 
 * Similar to flush but also releases resources.
 * 
 * @param context Context to finish
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_finish(mvgal_context_t context);

/**
 * @brief Set MVGAL enabled state
 * 
 * Enable or disable MVGAL processing. When disabled, workloads
 * will be passed through to the primary GPU directly.
 * 
 * @param enabled true to enable, false to disable
 */
void mvgal_set_enabled(bool enabled);

/**
 * @brief Check if MVGAL is enabled
 * 
 * @return true if enabled, false otherwise
 */
bool mvgal_is_enabled(void);

/**
 * @brief Set the distribution strategy
 * 
 * @param context Context to configure
 * @param strategy Distribution strategy
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_set_strategy(mvgal_context_t context, mvgal_distribution_strategy_t strategy);

/**
 * @brief Get the current distribution strategy
 * 
 * @param context Context to query
 * @return Current distribution strategy
 */
mvgal_distribution_strategy_t mvgal_get_strategy(mvgal_context_t context);

/**
 * @brief Wait for all GPUs to be idle
 * 
 * @param context Context to wait on
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return MVGAL_SUCCESS on success, MVGAL_ERROR_TIMEOUT if timed out
 */
mvgal_error_t mvgal_wait_idle(mvgal_context_t context, uint32_t timeout_ms);

/**
 * @brief Get statistics for a context
 * 
 * @param context Context to get statistics for
 * @param stats Statistics structure (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_get_stats(mvgal_context_t context, mvgal_stats_t *stats);

/**
 * @brief Reset statistics for a context
 * 
 * @param context Context to reset statistics for
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_reset_stats(mvgal_context_t context);

/**
 * @brief Register a custom workload splitter
 * 
 * Allows applications to provide custom workload splitting logic.
 * 
 * @param context Context to register with
 * @param splitter Workload splitter callbacks
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_register_custom_splitter(
    mvgal_context_t context,
    const mvgal_workload_splitter_t *splitter
);

/**
 * @brief Create a fence for synchronization
 * 
 * @param context Context to create fence in
 * @param fence Pointer to fence handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_fence_create(mvgal_context_t context, mvgal_fence_t *fence);

/**
 * @brief Destroy a fence
 * 
 * @param fence Fence to destroy
 */
void mvgal_fence_destroy(mvgal_fence_t fence);

/**
 * @brief Wait for a fence to be signaled
 * 
 * @param fence Fence to wait for
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return MVGAL_SUCCESS if signaled, MVGAL_ERROR_TIMEOUT if timed out
 */
mvgal_error_t mvgal_fence_wait(mvgal_fence_t fence, uint32_t timeout_ms);

/**
 * @brief Check if a fence is signaled
 * 
 * @param fence Fence to check
 * @return true if signaled, false otherwise
 */
bool mvgal_fence_check(mvgal_fence_t fence);

/**
 * @brief Reset a fence
 * 
 * @param fence Fence to reset
 */
void mvgal_fence_reset(mvgal_fence_t fence);

/**
 * @brief Create a semaphore for synchronization
 * 
 * @param context Context to create semaphore in
 * @param initial_value Initial semaphore value
 * @param semaphore Pointer to semaphore handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_semaphore_create(
    mvgal_context_t context,
    uint64_t initial_value,
    mvgal_semaphore_t *semaphore
);

/**
 * @brief Destroy a semaphore
 * 
 * @param semaphore Semaphore to destroy
 */
void mvgal_semaphore_destroy(mvgal_semaphore_t semaphore);

/**
 * @brief Signal a semaphore
 * 
 * @param semaphore Semaphore to signal
 * @param value Value to add to semaphore
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_semaphore_signal(mvgal_semaphore_t semaphore, uint64_t value);

/**
 * @brief Wait on a semaphore
 * 
 * @param semaphore Semaphore to wait on
 * @param value Value to subtract from semaphore
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return MVGAL_SUCCESS if wait succeeded, MVGAL_ERROR_TIMEOUT if timed out
 */
mvgal_error_t mvgal_semaphore_wait(
    mvgal_semaphore_t semaphore,
    uint64_t value,
    uint32_t timeout_ms
);

/**
 * @brief Get the value of a semaphore
 * 
 * @param semaphore Semaphore to query
 * @param value Pointer to value (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_semaphore_get_value(mvgal_semaphore_t semaphore, uint64_t *value);

/** @} */ // end of CoreAPI

#ifdef __cplusplus
}
#endif

#endif // MVGAL_H
