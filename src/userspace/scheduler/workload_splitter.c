/**
 * @file workload_splitter.c
 * @brief Custom Workload Splitter Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements the custom workload splitter functionality
 * declared in mvgal_scheduler.h.
 */

#include "mvgal/mvgal_scheduler.h"
#include "mvgal/mvgal_log.h"
#include "mvgal/mvgal_types.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Maximum number of registered splitters
 */
#define MAX_SPLITTERS 16

/**
 * @brief Custom splitter registration state
 */
typedef struct {
    mvgal_workload_splitter_t splitter;
    bool used;
} registered_splitter_t;

static registered_splitter_t g_splitters[MAX_SPLITTERS] = {0};

/**
 * @brief Register a custom workload splitter
 *
 * This function is called from mvgal_api.c's mvgal_register_custom_splitter.
 * It stores the splitter callbacks for later use by the scheduler.
 */
mvgal_error_t mvgal_scheduler_register_custom_splitter(
    const mvgal_workload_splitter_t *splitter
) {
    if (splitter == NULL) {
        MVGAL_LOG_ERROR("Null splitter provided");
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Find an empty slot
    for (int i = 0; i < MAX_SPLITTERS; i++) {
        if (!g_splitters[i].used) {
            g_splitters[i].splitter = *splitter;
            g_splitters[i].used = true;
            MVGAL_LOG_INFO("Custom workload splitter registered at index %d", i);
            return MVGAL_SUCCESS;
        }
    }
    
    MVGAL_LOG_ERROR("No available slots for custom splitter");
    return MVGAL_ERROR_OUT_OF_MEMORY;
}

/**
 * @brief Unregister a custom workload splitter
 *
 * This function is called from mvgal_api.c's mvgal_unregister_custom_splitter.
 * It removes a previously registered splitter.
 */
mvgal_error_t mvgal_scheduler_unregister_custom_splitter(
    const mvgal_workload_splitter_t *splitter
) {
    if (splitter == NULL) {
        MVGAL_LOG_ERROR("Null splitter provided");
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Find and remove the splitter
    for (int i = 0; i < MAX_SPLITTERS; i++) {
        if (g_splitters[i].used) {
            // Compare function pointers (simple approach)
            if (g_splitters[i].splitter.split == splitter->split &&
                g_splitters[i].splitter.merge == splitter->merge) {
                g_splitters[i].used = false;
                memset(&g_splitters[i].splitter, 0, sizeof(mvgal_workload_splitter_t));
                MVGAL_LOG_INFO("Custom workload splitter unregistered from index %d", i);
                return MVGAL_SUCCESS;
            }
        }
    }
    
    MVGAL_LOG_WARN("Custom splitter not found");
    return MVGAL_ERROR_INVALID_ARGUMENT;
}

/**
 * @brief Get the number of registered custom splitters
 */
uint32_t mvgal_scheduler_get_custom_splitter_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_SPLITTERS; i++) {
        if (g_splitters[i].used) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Apply a custom splitter to a workload
 *
 * This is called internally by the scheduler when a custom splitter is registered.
 */
mvgal_error_t mvgal_scheduler_apply_custom_splitter(
    mvgal_workload_t workload,
    uint32_t Splitter_index,
    uint32_t part_count,
    mvgal_workload_t *parts
) {
    if (Splitter_index >= MAX_SPLITTERS) {
        MVGAL_LOG_ERROR("Invalid splitter index: %d", Splitter_index);
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (!g_splitters[Splitter_index].used) {
        MVGAL_LOG_ERROR("Splitter at index %d is not registered", Splitter_index);
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (g_splitters[Splitter_index].splitter.split == NULL) {
        MVGAL_LOG_ERROR("Splitter at index %d has null split function", Splitter_index);
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Call the custom split function
    bool success = g_splitters[Splitter_index].splitter.split(
        workload, NULL, part_count, parts, g_splitters[Splitter_index].splitter.user_data
    );
    
    if (!success) {
        MVGAL_LOG_WARN("Custom splitter %d failed to split workload", Splitter_index);
        return MVGAL_ERROR_SCHEDULER;
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Apply all registered custom splitters
 *
 * This tries each registered custom splitter in order until one succeeds.
 */
mvgal_error_t mvgal_scheduler_apply_all_custom_splitters(
    mvgal_workload_t workload,
    uint32_t part_count,
    mvgal_workload_t *parts
) {
    for (int i = 0; i < MAX_SPLITTERS; i++) {
        if (g_splitters[i].used) {
            mvgal_error_t err = mvgal_scheduler_apply_custom_splitter(
                workload, i, part_count, parts
            );
            if (err == MVGAL_SUCCESS) {
                MVGAL_LOG_DEBUG("Custom splitter %d successfully split workload", i);
                return MVGAL_SUCCESS;
            }
        }
    }
    
    MVGAL_LOG_DEBUG("No custom splitters available or all failed");
    return MVGAL_ERROR_NOT_SUPPORTED;
}
