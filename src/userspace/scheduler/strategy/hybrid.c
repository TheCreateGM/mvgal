/**
 * @file hybrid.c
 * @brief Hybrid distribution strategy implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This strategy dynamically adapts between different distribution
 * strategies based on workload characteristics and system conditions.
 */

#include "../scheduler_internal.h"
#include "mvgal/mvgal_log.h"

// Forward declarations for strategy functions
mvgal_error_t mvgal_scheduler_distribute_compute_offload(struct mvgal_workload *workload);

/**
 * @brief Strategy selection criteria
 */
typedef struct {
    mvgal_workload_type_t workload_type;
    uint64_t min_size_bytes;      ///< Minimum workload size for this strategy
    float utilization_threshold;  ///< Max utilization to use this strategy
    mvgal_distribution_strategy_t fallback_strategy;
} hybrid_strategy_criteria_t;

// Default strategy selection criteria
static const hybrid_strategy_criteria_t g_hybrid_criteria[] = {
    {
        .workload_type = MVGAL_WORKLOAD_GRAPHICS,
        .min_size_bytes = 1024 * 1024, // 1MB
        .utilization_threshold = 0.7f,
        .fallback_strategy = MVGAL_STRATEGY_SINGLE_GPU,
    },
    {
        .workload_type = MVGAL_WORKLOAD_COMPUTE,
        .min_size_bytes = 256 * 1024, // 256KB
        .utilization_threshold = 0.8f,
        .fallback_strategy = MVGAL_STRATEGY_COMPUTE_OFFLOAD,
    },
    {
        .workload_type = MVGAL_WORKLOAD_VIDEO,
        .min_size_bytes = 4 * 1024 * 1024, // 4MB
        .utilization_threshold = 0.6f,
        .fallback_strategy = MVGAL_STRATEGY_SINGLE_GPU,
    },
    {
        .workload_type = MVGAL_WORKLOAD_AI,
        .min_size_bytes = 1 * 1024 * 1024, // 1MB
        .utilization_threshold = 0.7f,
        .fallback_strategy = MVGAL_STRATEGY_COMPUTE_OFFLOAD,
    },
};

/**
 * @brief Select the best strategy for a workload
 *
 * This function analyzes the workload and system state to select
 * the optimal distribution strategy.
 */
mvgal_distribution_strategy_t hybrid_select_strategy(
    struct mvgal_workload *workload
) {
    if (workload == NULL) {
        return MVGAL_STRATEGY_SINGLE_GPU;
    }
    
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    // If workload has pre-assigned GPUs, use AFR for multi-GPU
    if (workload->assigned_gpu_count > 1) {
        return MVGAL_STRATEGY_AFR;
    }
    
    // If workload has GPU mask with multiple GPUs, use SFR
    if (workload->descriptor.gpu_mask != 0xFFFFFFFF &&
        __builtin_popcount(workload->descriptor.gpu_mask) > 1) {
        return MVGAL_STRATEGY_SFR;
    }
    
    // Find criteria for this workload type
    const hybrid_strategy_criteria_t *criteria = NULL;
    for (size_t i = 0; i < sizeof(g_hybrid_criteria) / sizeof(g_hybrid_criteria[0]); i++) {
        if (g_hybrid_criteria[i].workload_type == workload->descriptor.type) {
            criteria = &g_hybrid_criteria[i];
            break;
        }
    }
    
    if (criteria == NULL) {
        criteria = &g_hybrid_criteria[0]; // Default to graphics
    }
    
    // Check average utilization
    float avg_utilization = 0.0f;
    uint32_t active_gpus = 0;
    
    for (uint32_t i = 0; i < state->gpu_count; i++) {
        if (state->gpus[i].available && state->gpus[i].enabled) {
            avg_utilization += state->gpus[i].utilization;
            active_gpus++;
        }
    }
    
    if (active_gpus > 0) {
        avg_utilization /= active_gpus;
    }
    
    // Select strategy based on conditions
    if (avg_utilization < criteria->utilization_threshold && active_gpus >= 2) {
        // Low utilization and multiple GPUs: use AFR for graphics, SFR for others
        if (workload->descriptor.type == MVGAL_WORKLOAD_GRAPHICS) {
            return MVGAL_STRATEGY_AFR;
        } else {
            return MVGAL_STRATEGY_SFR;
        }
    } else {
        // High utilization or single GPU: use single GPU or compute offload
        if (workload->descriptor.type == MVGAL_WORKLOAD_COMPUTE ||
            workload->descriptor.type == MVGAL_WORKLOAD_AI) {
            return MVGAL_STRATEGY_COMPUTE_OFFLOAD;
        } else {
            return MVGAL_STRATEGY_SINGLE_GPU;
        }
    }
}

/**
 * @brief Distribute workload using hybrid strategy
 *
 * This function dynamically selects and applies the best strategy
 * for the given workload based on current conditions.
 */
mvgal_error_t mvgal_scheduler_distribute_hybrid(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_distribution_strategy_t strategy = hybrid_select_strategy(workload);
    
    // Apply the selected strategy
    switch (strategy) {
        case MVGAL_STRATEGY_AFR:
            return mvgal_scheduler_distribute_afr(workload);
        case MVGAL_STRATEGY_SFR:
            return mvgal_scheduler_distribute_sfr(workload);
        case MVGAL_STRATEGY_TASK:
            return mvgal_scheduler_distribute_task(workload);
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD:
            return mvgal_scheduler_distribute_compute_offload(workload);
        case MVGAL_STRATEGY_SINGLE_GPU:
        default:
            return mvgal_scheduler_find_best_gpu(workload, 
                &workload->descriptor.assigned_gpu);
    }
}

/**
 * @brief Get current hybrid strategy recommendation
 */
mvgal_error_t hybrid_get_recommendation(
    struct mvgal_workload *workload,
    mvgal_distribution_strategy_t *recommended_strategy,
    const char **reason
) {
    if (workload == NULL || recommended_strategy == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    *recommended_strategy = hybrid_select_strategy(workload);
    
    if (reason != NULL) {
        // Provide reason string
        static char reason_buf[256];
        
        float avg_utilization = 0.0f;
        uint32_t active_gpus = 0;
        
        for (uint32_t i = 0; i < state->gpu_count; i++) {
            if (state->gpus[i].available && state->gpus[i].enabled) {
                avg_utilization += state->gpus[i].utilization;
                active_gpus++;
            }
        }
        
        if (active_gpus > 0) {
            avg_utilization /= active_gpus;
        }
        
        switch (*recommended_strategy) {
            case MVGAL_STRATEGY_AFR:
                snprintf(reason_buf, sizeof(reason_buf),
                        "AFR: Low utilization (%.1f%%), multiple GPUs (%u)",
                        avg_utilization * 100.0f, active_gpus);
                *reason = reason_buf;
                break;
            case MVGAL_STRATEGY_SFR:
                snprintf(reason_buf, sizeof(reason_buf),
                        "SFR: Low utilization (%.1f%%), multi-GPU workload",
                        avg_utilization * 100.0f);
                *reason = reason_buf;
                break;
            case MVGAL_STRATEGY_COMPUTE_OFFLOAD:
                snprintf(reason_buf, sizeof(reason_buf),
                        "Compute offload: High compute workload");
                *reason = reason_buf;
                break;
            case MVGAL_STRATEGY_SINGLE_GPU:
                snprintf(reason_buf, sizeof(reason_buf),
                        "Single GPU: High utilization (%.1f%%) or single GPU",
                        avg_utilization * 100.0f);
                *reason = reason_buf;
                break;
            default:
                *reason = "Unknown strategy";
                break;
        }
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Update hybrid strategy criteria
 */
mvgal_error_t hybrid_update_criteria(
    mvgal_workload_type_t workload_type,
    uint64_t min_size_bytes,
    float utilization_threshold
) {
    // In a real implementation, we would update the selection criteria
    // For now, just log
    MVGAL_LOG_INFO("Hybrid criteria update for type %d: min_size=%lu, util_threshold=%.2f",
                   workload_type, min_size_bytes, utilization_threshold);
    return MVGAL_SUCCESS;
}
