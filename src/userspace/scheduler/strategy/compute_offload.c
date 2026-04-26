/**
 * @file compute_offload.c
 * @brief Compute offload strategy implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This strategy offloads compute-intensive workloads to GPUs with
 * the best compute capabilities.
 */

#include "../scheduler_internal.h"
#include "mvgal/mvgal_log.h"

/**
 * @brief Distribute workload using compute offload strategy
 *
 * Assigns compute workloads to GPUs with the highest compute scores.
 */
mvgal_error_t mvgal_scheduler_distribute_compute_offload(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    // If workload has specific GPU assignments, use them
    if (workload->assigned_gpu_count > 0) {
        workload->descriptor.assigned_gpu = workload->assigned_gpus[0];
        MVGAL_LOG_DEBUG("Compute offload: Workload %u using pre-assigned GPU %u",
                       workload->descriptor.id, workload->descriptor.assigned_gpu);
        return MVGAL_SUCCESS;
    }
    
    // Find GPU with best compute score
    float best_compute_score = -1.0f;
    uint32_t best_gpu = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < state->gpu_count; i++) {
        if (!state->gpus[i].available || !state->gpus[i].enabled) {
            continue;
        }
        
        // Check if GPU has compute capability
        if (!(state->gpus[i].features & MVGAL_FEATURE_COMPUTE)) {
            continue;
        }
        
        // Adjust score based on utilization
        float adjusted_score = state->gpus[i].compute_score * (1.0f - state->gpus[i].utilization);
        
        if (adjusted_score > best_compute_score) {
            best_compute_score = adjusted_score;
            best_gpu = i;
        }
    }
    
    // If no GPU with compute feature, fall back to any GPU
    if (best_gpu == 0xFFFFFFFF) {
        for (uint32_t i = 0; i < state->gpu_count; i++) {
            if (state->gpus[i].available && state->gpus[i].enabled) {
                float adjusted_score = state->gpus[i].compute_score * (1.0f - state->gpus[i].utilization);
                if (adjusted_score > best_compute_score) {
                    best_compute_score = adjusted_score;
                    best_gpu = i;
                }
            }
        }
    }
    
    if (best_gpu == 0xFFFFFFFF) {
        return MVGAL_ERROR_NO_GPUS;
    }
    
    workload->descriptor.assigned_gpu = best_gpu;
    workload->assigned_gpu_count = 1;
    workload->assigned_gpus[0] = best_gpu;
    
    MVGAL_LOG_DEBUG("Compute offload: Workload %u assigned to GPU %u (score=%.1f)",
                   workload->descriptor.id, best_gpu, best_compute_score);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get compute score for a GPU
 */
float compute_offload_get_score(uint32_t gpu_index) {
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    if (gpu_index >= state->gpu_count) {
        return -1.0f;
    }
    
    return state->gpus[gpu_index].compute_score;
}

/**
 * @brief Estimate compute workload duration
 */
uint64_t compute_offload_estimate_duration(
    uint32_t gpu_index,
    uint64_t workload_size_bytes
) {
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    if (gpu_index >= state->gpu_count) {
        return 0;
    }
    
    // Simple estimation: bandwidth-based
    // In a real implementation, this would use GPU performance characteristics
    float bandwidth_gbps = 500.0f; // Default assumption
    
    // Estimate is workload_size / bandwidth
    uint64_t duration_ns = (workload_size_bytes * 8 * 1000000000ULL) / 
                          (uint64_t)(bandwidth_gbps * 1000000000.0f);
    
    // Adjust based on GPU compute score
    float score_factor = state->gpus[gpu_index].compute_score / 100.0f;
    if (score_factor > 0.0f) {
        duration_ns = (uint64_t)((float)duration_ns / score_factor);
    }
    
    return duration_ns;
}
