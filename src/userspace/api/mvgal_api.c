/**
 * @file mvgal_api.c
 * @brief Core MVGAL API implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements the core public API functions declared in mvgal.h.
 */

#include "mvgal.h"
#include "mvgal_gpu.h"
#include "mvgal_memory.h"
#include "mvgal_scheduler.h"
#include "mvgal_config.h"
#include "mvgal_log.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Global MVGAL state
 */
typedef struct {
    uint32_t flags;               ///< Initialization flags
    bool initialized;             ///< Whether MVGAL is initialized
    uint32_t refcount;            ///< Reference count
    mvgal_context_t current_context; ///< Current context
} mvgal_state_t;

/**
 * @brief Global state
 */
static mvgal_state_t g_mvgal_state = {0};

// Internal subsystem entry points owned by daemon/gpu_manager.c.
mvgal_error_t mvgal_gpu_manager_init(void);
void mvgal_gpu_manager_shutdown(void);
mvgal_error_t mvgal_memory_module_init(void);
void mvgal_memory_module_shutdown(void);
mvgal_error_t mvgal_scheduler_module_init(void);
void mvgal_scheduler_module_shutdown(void);
mvgal_error_t mvgal_execution_module_init(void);
void mvgal_execution_module_shutdown(void);
mvgal_error_t mvgal_execution_get_stats_internal(mvgal_stats_t *stats);
void mvgal_execution_reset_stats_internal(void);

/**
 * @brief Version numbers
 */
#define MVGAL_VERSION_MAJOR 0
#define MVGAL_VERSION_MINOR 1
#define MVGAL_VERSION_PATCH 0

/**
 * @brief Magic number for context validation
 */
#define MVGAL_CONTEXT_MAGIC 0x4D56474C  // "MVGL"

/**
 * @brief Internal context structure
 */
typedef struct mvgal_context_internal {
    uint32_t magic;                   ///< Magic number for validation
    uint64_t id;                     ///< Context ID
    bool enabled;                    ///< Whether context is enabled
    mvgal_distribution_strategy_t strategy; ///< Current strategy
    // Additional fields as needed
} mvgal_context_internal_t;

/**
 * @brief Initialize MVGAL
 */
mvgal_error_t mvgal_init(uint32_t flags) {
    if (g_mvgal_state.initialized) {
        g_mvgal_state.refcount++;
        return MVGAL_SUCCESS;
    }
    
    // Initialize logging first
    mvgal_log_init(MVGAL_LOG_LEVEL_INFO, NULL, NULL);
    
    MVGAL_LOG_INFO("Initializing MVGAL (flags=0x%X)", flags);
    
    // Initialize configuration
    mvgal_error_t err = mvgal_config_init();
    if (err != MVGAL_SUCCESS) {
        MVGAL_LOG_ERROR("Failed to initialize configuration: %d", err);
        return err;
    }

    err = mvgal_gpu_manager_init();
    if (err != MVGAL_SUCCESS) {
        MVGAL_LOG_ERROR("Failed to initialize GPU manager: %d", err);
        mvgal_config_shutdown();
        mvgal_log_shutdown();
        return err;
    }

    err = mvgal_memory_module_init();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        MVGAL_LOG_ERROR("Failed to initialize memory manager: %d", err);
        mvgal_gpu_manager_shutdown();
        mvgal_config_shutdown();
        mvgal_log_shutdown();
        return err;
    }

    err = mvgal_scheduler_module_init();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        MVGAL_LOG_ERROR("Failed to initialize scheduler: %d", err);
        mvgal_memory_module_shutdown();
        mvgal_gpu_manager_shutdown();
        mvgal_config_shutdown();
        mvgal_log_shutdown();
        return err;
    }

    err = mvgal_execution_module_init();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        MVGAL_LOG_ERROR("Failed to initialize execution module: %d", err);
        mvgal_scheduler_module_shutdown();
        mvgal_memory_module_shutdown();
        mvgal_gpu_manager_shutdown();
        mvgal_config_shutdown();
        mvgal_log_shutdown();
        return err;
    }
    
    g_mvgal_state.flags = flags;
    g_mvgal_state.initialized = true;
    g_mvgal_state.refcount = 1;
    g_mvgal_state.current_context = NULL;
    
    MVGAL_LOG_INFO("MVGAL initialized successfully");
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Initialize MVGAL with configuration file
 */
mvgal_error_t mvgal_init_with_config(const char *config_path, uint32_t flags) {
    if (config_path == NULL) {
        return mvgal_init(flags);
    }
    
    // Initialize configuration from file
    mvgal_error_t err = mvgal_config_load(config_path);
    if (err != MVGAL_SUCCESS) {
        MVGAL_LOG_WARN("Failed to load config from %s, using defaults", config_path);
    }
    
    return mvgal_init(flags);
}

/**
 * @brief Shutdown MVGAL
 */
void mvgal_shutdown(void) {
    if (!g_mvgal_state.initialized) {
        return;
    }
    
    g_mvgal_state.refcount--;
    if (g_mvgal_state.refcount > 0) {
        return;
    }
    
    MVGAL_LOG_INFO("Shutting down MVGAL");
    
    // Set current context to NULL
    if (g_mvgal_state.current_context != NULL) {
        mvgal_context_destroy(g_mvgal_state.current_context);
        g_mvgal_state.current_context = NULL;
    }
    
    // Shutdown subsystems in reverse initialization order.
    mvgal_execution_module_shutdown();
    mvgal_scheduler_module_shutdown();
    mvgal_memory_module_shutdown();
    mvgal_gpu_manager_shutdown();
    mvgal_config_shutdown();
    mvgal_log_shutdown();
    
    g_mvgal_state.initialized = false;
    g_mvgal_state.flags = 0;
    
    MVGAL_LOG_INFO("MVGAL shutdown complete");
}

/**
 * @brief Get version numbers
 */
void mvgal_get_version_numbers(uint32_t *major, uint32_t *minor, uint32_t *patch) {
    if (major != NULL) {
        *major = MVGAL_VERSION_MAJOR;
    }
    if (minor != NULL) {
        *minor = MVGAL_VERSION_MINOR;
    }
    if (patch != NULL) {
        *patch = MVGAL_VERSION_PATCH;
    }
}

/**
 * @brief Check if MVGAL is initialized
 */
bool mvgal_is_initialized(void) {
    return g_mvgal_state.initialized;
}

/**
 * @brief Get MVGAL version string
 */
const char *mvgal_get_version(void) {
    static char version_str[64];
    snprintf(version_str, sizeof(version_str), "%d.%d.%d",
             MVGAL_VERSION_MAJOR, MVGAL_VERSION_MINOR, MVGAL_VERSION_PATCH);
    return version_str;
}

/**
 * @brief Check if MVGAL is enabled
 */
bool mvgal_is_enabled(void) {
    if (!g_mvgal_state.initialized) {
        return false;
    }
    return (g_mvgal_state.flags & 1) != 0;
}

// Note: Fence and semaphore functions are implemented in memory/sync.c
// They are declared in mvgal.h and this file does not re-implement them.

/**
 * @brief Create a new context
 */
mvgal_error_t mvgal_context_create(mvgal_context_t *context) {
    if (context == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (!g_mvgal_state.initialized) {
        mvgal_error_t err = mvgal_init(0);
        if (err != MVGAL_SUCCESS) {
            return err;
        }
    }
    
    mvgal_context_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize context
    ctx->magic = MVGAL_CONTEXT_MAGIC;
    ctx->id = 1; // Simple ID for now
    ctx->enabled = true;
    ctx->strategy = MVGAL_STRATEGY_HYBRID;
    
    *context = (mvgal_context_t)ctx;
    
    MVGAL_LOG_DEBUG("Context created: %p", (void *)ctx);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Destroy a context
 */
void mvgal_context_destroy(mvgal_context_t context) {
    if (context == NULL) {
        return;
    }
    
    mvgal_context_internal_t *ctx = (mvgal_context_internal_t *)context;
    
    // Validate magic
    if (ctx->magic != MVGAL_CONTEXT_MAGIC) {
        MVGAL_LOG_ERROR("Invalid context magic number");
        return;
    }
    
    // If this is the current context, clear it
    if (g_mvgal_state.current_context == context) {
        g_mvgal_state.current_context = NULL;
    }
    
    // Free context
    ctx->magic = 0; // Invalidate
    free(ctx);
    
    MVGAL_LOG_DEBUG("Context destroyed");
}

/**
 * @brief Set the current context
 */
mvgal_error_t mvgal_context_set_current(mvgal_context_t context) {
    if (context != NULL) {
        mvgal_context_internal_t *ctx = (mvgal_context_internal_t *)context;
        if (ctx->magic != MVGAL_CONTEXT_MAGIC) {
            return MVGAL_ERROR_INVALID_ARGUMENT;
        }
    }
    
    g_mvgal_state.current_context = context;
    
    MVGAL_LOG_DEBUG("Current context set to %p", (void *)context);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get the current context
 */
mvgal_context_t mvgal_context_get_current(void) {
    return g_mvgal_state.current_context;
}

/**
 * @brief Flush pending operations
 */
mvgal_error_t mvgal_flush(mvgal_context_t context) {
    (void)context;
    
    if (!g_mvgal_state.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    // In a real implementation, this would flush all pending operations
    // For now, just return success
    
    MVGAL_LOG_DEBUG("Flush called");
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Wait for completion
 */
mvgal_error_t mvgal_finish(mvgal_context_t context) {
    (void)context;
    
    if (!g_mvgal_state.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    // In a real implementation, this would wait for all operations to complete
    // For now, just return success
    
    MVGAL_LOG_DEBUG("Finish called");
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Set whether MVGAL is enabled
 */
void mvgal_set_enabled(bool enabled) {
    if (!g_mvgal_state.initialized) {
        return;
    }
    
    g_mvgal_state.flags = enabled ? (g_mvgal_state.flags | 1) : (g_mvgal_state.flags & ~1U);
    
    MVGAL_LOG_DEBUG("MVGAL %s", enabled ? "enabled" : "disabled");
}

/**
 * @brief Set the distribution strategy for a context
 */
mvgal_error_t mvgal_set_strategy(mvgal_context_t context, mvgal_distribution_strategy_t strategy) {
    if (context == NULL) {
        if (!g_mvgal_state.initialized) {
            return MVGAL_ERROR_NOT_INITIALIZED;
        }
        context = g_mvgal_state.current_context;
    }
    
    if (context == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_context_internal_t *ctx = (mvgal_context_internal_t *)context;
    if (ctx->magic != MVGAL_CONTEXT_MAGIC) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    ctx->strategy = strategy;
    
    // Also set in scheduler (if we have a current context)
    if (context != NULL) {
        mvgal_scheduler_set_strategy(context, strategy);
    }
    
    MVGAL_LOG_DEBUG("Strategy set to %d for context %p", strategy, (void *)context);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get the current distribution strategy for a context
 */
mvgal_distribution_strategy_t mvgal_get_strategy(mvgal_context_t context) {
    if (context == NULL) {
        if (!g_mvgal_state.initialized) {
            return MVGAL_STRATEGY_HYBRID; // Default
        }
        context = g_mvgal_state.current_context;
    }
    
    if (context == NULL) {
        return MVGAL_STRATEGY_HYBRID; // Default
    }
    
    mvgal_context_internal_t *ctx = (mvgal_context_internal_t *)context;
    if (ctx->magic != MVGAL_CONTEXT_MAGIC) {
        return MVGAL_STRATEGY_HYBRID; // Default
    }
    
    return ctx->strategy;
}

/**
 * @brief Wait for idle
 */
mvgal_error_t mvgal_wait_idle(mvgal_context_t context, uint32_t timeout_ms) {
    (void)context;
    (void)timeout_ms;
    
    if (!g_mvgal_state.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    // In a real implementation, this would wait until all GPUs are idle
    // For now, just return success
    
    MVGAL_LOG_DEBUG("Wait idle called (timeout=%u ms)", timeout_ms);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get statistics
 */
mvgal_error_t mvgal_get_stats(mvgal_context_t context, mvgal_stats_t *stats) {
    mvgal_scheduler_stats_t scheduler_stats;
    mvgal_stats_t execution_stats;

    if (!g_mvgal_state.initialized || stats == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    memset(stats, 0, sizeof(*stats));

    if (mvgal_execution_get_stats_internal(&execution_stats) == MVGAL_SUCCESS) {
        *stats = execution_stats;
    }

    if (mvgal_scheduler_get_stats(context, &scheduler_stats) == MVGAL_SUCCESS) {
        if (stats->frames_submitted == 0) {
            stats->frames_submitted = scheduler_stats.workloads_submitted;
        }
        if (stats->frames_completed == 0) {
            stats->frames_completed = scheduler_stats.workloads_completed;
        }
        if (stats->workloads_distributed == 0) {
            stats->workloads_distributed = scheduler_stats.workloads_submitted;
        } else if (scheduler_stats.workloads_submitted > stats->workloads_distributed) {
            stats->workloads_distributed = scheduler_stats.workloads_submitted;
        }
        stats->errors += scheduler_stats.workloads_failed;
    }

    MVGAL_LOG_DEBUG("Get stats called");

    return MVGAL_SUCCESS;
}

/**
 * @brief Reset statistics
 */
mvgal_error_t mvgal_reset_stats(mvgal_context_t context) {
    (void)context;
    
    if (!g_mvgal_state.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    // Reset scheduler stats
    mvgal_scheduler_reset_stats(context);
    mvgal_execution_reset_stats_internal();
    
    // In a real implementation, this would reset stats in all subsystems
    
    MVGAL_LOG_DEBUG("Reset stats called");
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Register a custom splitter
 */
mvgal_error_t mvgal_register_custom_splitter(
    mvgal_context_t context,
    const mvgal_workload_splitter_t *splitter
) {
    (void)context;
    (void)splitter;
    
    MVGAL_LOG_WARN("Custom splitter registration not yet implemented");
    
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Unregister a custom splitter
 */
mvgal_error_t mvgal_unregister_custom_splitter(
    mvgal_context_t context,
    const mvgal_workload_splitter_t *splitter
) {
    (void)context;
    (void)splitter;
    
    MVGAL_LOG_WARN("Custom splitter unregistration not yet implemented");
    
    return MVGAL_ERROR_NOT_SUPPORTED;
}
