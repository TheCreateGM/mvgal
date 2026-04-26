/**
 * @file task.c
 * @brief Task-based distribution strategy implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This strategy distributes workloads based on their type, assigning
 * different workload types to different GPUs based on their capabilities.
 */

#include "../scheduler_internal.h"
#include "mvgal/mvgal_log.h"

/**
 * @brief Task type to GPU capability mapping
 */
typedef struct {
    mvgal_workload_type_t task_type;
    uint64_t required_features;
    float compute_weight;
    float graphics_weight;
    float memory_weight;
} task_type_mapping_t;

// Task type mappings
static const task_type_mapping_t g_task_mappings[] = {
    {
        .task_type = MVGAL_WORKLOAD_GRAPHICS,
        .required_features = MVGAL_FEATURE_GRAPHICS,
        .compute_weight = 0.2f,
        .graphics_weight = 0.7f,
        .memory_weight = 0.1f,
    },
    {
        .task_type = MVGAL_WORKLOAD_COMPUTE,
        .required_features = MVGAL_FEATURE_COMPUTE,
        .compute_weight = 0.8f,
        .graphics_weight = 0.1f,
        .memory_weight = 0.1f,
    },
    {
        .task_type = MVGAL_WORKLOAD_VIDEO,
        .required_features = MVGAL_FEATURE_VIDEO_DECODE | MVGAL_FEATURE_VIDEO_ENCODE,
        .compute_weight = 0.5f,
        .graphics_weight = 0.3f,
        .memory_weight = 0.2f,
    },
    {
        .task_type = MVGAL_WORKLOAD_AI,
        .required_features = MVGAL_FEATURE_AI_ACCEL,
        .compute_weight = 0.9f,
        .graphics_weight = 0.0f,
        .memory_weight = 0.1f,
    },
    {
        .task_type = MVGAL_WORKLOAD_TRACE,
        .required_features = MVGAL_FEATURE_RAY_TRACING,
        .compute_weight = 0.6f,
        .graphics_weight = 0.4f,
        .memory_weight = 0.0f,
    },
    {
        .task_type = MVGAL_WORKLOAD_TRANSFER,
        .required_features = 0, // No specific features needed
        .compute_weight = 0.1f,
        .graphics_weight = 0.1f,
        .memory_weight = 0.8f,
    },
};

/**
 * @brief Get task type mapping
 */
static const task_type_mapping_t *get_task_mapping(mvgal_workload_type_t task_type) {
    for (size_t i = 0; i < sizeof(g_task_mappings) / sizeof(g_task_mappings[0]); i++) {
        if (g_task_mappings[i].task_type == task_type) {
            return &g_task_mappings[i];
        }
    }
    return NULL; // Default: use first mapping
}

/**
 * @brief Distribute workload using task-based strategy
 *
 * Assigns workloads to GPUs based on workload type and GPU capabilities.
 */
mvgal_error_t mvgal_scheduler_distribute_task(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    // If workload has specific GPU assignments, use them
    if (workload->assigned_gpu_count > 0) {
        workload->descriptor.assigned_gpu = workload->assigned_gpus[0];
        MVGAL_LOG_DEBUG("Task strategy: Workload %u using pre-assigned GPU %u",
                       workload->descriptor.id, workload->descriptor.assigned_gpu);
        return MVGAL_SUCCESS;
    }
    
    // Get task mapping
    const task_type_mapping_t *mapping = get_task_mapping(workload->descriptor.type);
    if (mapping == NULL) {
        mapping = &g_task_mappings[0]; // Default to graphics
    }
    
    // Score each GPU based on capabilities
    float best_score = -1000.0f;
    uint32_t best_gpu = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < state->gpu_count; i++) {
        if (!state->gpus[i].available || !state->gpus[i].enabled) {
            continue;
        }
        
        // Check if GPU has required features
        if ((state->gpus[i].features & mapping->required_features) != 
            mapping->required_features) {
            // Feature not available, but we can still use this GPU
            // with lower priority
            continue;
        }
        
        // Calculate score based on weights
        float score = 0.0f;
        score += state->gpus[i].compute_score * mapping->compute_weight;
        score += state->gpus[i].graphics_score * mapping->graphics_weight;
        
        // Apply GPU priority
        score += state->gpus[i].priority * 0.01f;
        
        // Penalize based on utilization
        score -= state->gpus[i].utilization * 50.0f;
        
        if (score > best_score) {
            best_score = score;
            best_gpu = i;
        }
    }
    
    // If no GPU found with required features, fall back to any GPU
    if (best_gpu == 0xFFFFFFFF) {
        for (uint32_t i = 0; i < state->gpu_count; i++) {
            if (!state->gpus[i].available || !state->gpus[i].enabled) {
                continue;
            }
            
            float score = state->gpus[i].compute_score * 0.5f + 
                         state->gpus[i].graphics_score * 0.5f;
            score += state->gpus[i].priority * 0.01f;
            score -= state->gpus[i].utilization * 50.0f;
            
            if (score > best_score) {
                best_score = score;
                best_gpu = i;
            }
        }
    }
    
    if (best_gpu == 0xFFFFFFFF) {
        return MVGAL_ERROR_NO_GPUS;
    }
    
    workload->descriptor.assigned_gpu = best_gpu;
    workload->assigned_gpu_count = 1;
    workload->assigned_gpus[0] = best_gpu;
    
    MVGAL_LOG_DEBUG("Task strategy: Workload %u (type=%d) assigned to GPU %u (score=%f)",
                   workload->descriptor.id, workload->descriptor.type,
                   best_gpu, best_score);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get best GPU for a specific task type
 */
mvgal_error_t task_get_best_gpu(
    mvgal_workload_type_t task_type,
    uint32_t *gpu_index
) {
    if (gpu_index == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    const task_type_mapping_t *mapping = get_task_mapping(task_type);
    if (mapping == NULL) {
        mapping = &g_task_mappings[0];
    }
    
    float best_score = -1000.0f;
    *gpu_index = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < state->gpu_count; i++) {
        if (!state->gpus[i].available || !state->gpus[i].enabled) {
            continue;
        }
        
        if ((state->gpus[i].features & mapping->required_features) != 
            mapping->required_features) {
            continue;
        }
        
        float score = state->gpus[i].compute_score * mapping->compute_weight + 
                     state->gpus[i].graphics_score * mapping->graphics_weight +
                     state->gpus[i].priority * 0.01f;
        
        if (score > best_score) {
            best_score = score;
            *gpu_index = i;
        }
    }
    
    // If no GPU with required features, return any available
    if (*gpu_index == 0xFFFFFFFF) {
        for (uint32_t i = 0; i < state->gpu_count; i++) {
            if (state->gpus[i].available && state->gpus[i].enabled) {
                *gpu_index = i;
                break;
            }
        }
    }
    
    return (*gpu_index != 0xFFFFFFFF) ? MVGAL_SUCCESS : MVGAL_ERROR_NO_GPUS;
}

/**
 * @brief Configure task strategy weights
 */
mvgal_error_t task_configure_weights(
    mvgal_workload_type_t task_type,
    float compute_weight,
    float graphics_weight,
    float memory_weight
) {
    for (size_t i = 0; i < sizeof(g_task_mappings) / sizeof(g_task_mappings[0]); i++) {
        if (g_task_mappings[i].task_type == task_type) {
            // In a real implementation, we would allow dynamic weight adjustment
            // For now, just log
            MVGAL_LOG_INFO("Task weights for type %d: compute=%.2f, graphics=%.2f, memory=%.2f",
                          task_type, compute_weight, graphics_weight, memory_weight);
            return MVGAL_SUCCESS;
        }
    }
    
    return MVGAL_ERROR_INVALID_ARGUMENT;
}
