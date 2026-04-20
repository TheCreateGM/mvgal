/**
 * @file load_balancer.c
 * @brief Load balancing implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements dynamic load balancing across GPUs based on
 * utilization, temperature, and workload characteristics.
 */

#include "scheduler_internal.h"
#include "mvgal_log.h"

/**
 * @brief Update GPU utilization
 *
 * In a real implementation, this would query GPU drivers for actual utilization.
 * For now, we simulate it based on queue depth.
 */
static void update_gpu_utilization(mvgal_gpu_state_t *gpu) {
    if (gpu == NULL) {
        return;
    }
    
    // Calculate utilization based on queue depth
    // This is a simplified model - real implementation would use GPU queries
    float queue_util = (float)gpu->queue.count / 10.0f; // Assume 10 is max reasonable queue
    
    // Blend with previous utilization
    gpu->utilization = gpu->utilization * 0.7f + queue_util * 0.3f;
    
    // Clamp to 0-1 range
    if (gpu->utilization < 0.0f) gpu->utilization = 0.0f;
    if (gpu->utilization > 1.0f) gpu->utilization = 1.0f;
}

/**
 * @brief Balance load across GPUs
 *
 * This function adjusts workload distribution based on current GPU load.
 */
void mvgal_scheduler_balance_load(void) {
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    if (!state->config.dynamic_load_balance) {
        return;
    }
    
    pthread_mutex_lock(&state->lock);
    
    // Update utilization for all GPUs
    for (uint32_t i = 0; i < state->gpu_count; i++) {
        update_gpu_utilization(&state->gpus[i]);
    }
    
    // Check if we need to balance
    float max_util = 0.0f;
    float min_util = 1.0f;
    float avg_util = 0.0f;
    
    for (uint32_t i = 0; i < state->gpu_count; i++) {
        if (state->gpus[i].available && state->gpus[i].enabled) {
            if (state->gpus[i].utilization > max_util) max_util = state->gpus[i].utilization;
            if (state->gpus[i].utilization < min_util) min_util = state->gpus[i].utilization;
            avg_util += state->gpus[i].utilization;
        }
    }
    
    if (state->gpu_count > 0) {
        avg_util /= state->gpu_count;
    }
    
    // Calculate imbalance
    float imbalance = max_util - min_util;
    
    // If imbalance exceeds threshold, trigger rebalancing
    if (imbalance > state->config.load_balance_threshold) {
        MVGAL_LOG_INFO("Load imbalance detected: %.2f%%, threshold: %.2f%%",
                       imbalance * 100.0f, state->config.load_balance_threshold * 100.0f);
        
        // In a real implementation, we would:
        // 1. Identify overloaded and underloaded GPUs
        // 2. Migrate workloads from overloaded to underloaded GPUs
        // 3. Adjust scheduling weights
        
        // For now, just log the situation
        for (uint32_t i = 0; i < state->gpu_count; i++) {
            if (state->gpus[i].available && state->gpus[i].enabled) {
                MVGAL_LOG_DEBUG("GPU %u: utilization=%.2f%%, queue=%u, priority=%u",
                               i, state->gpus[i].utilization * 100.0f,
                               state->gpus[i].queue.count, state->gpus[i].priority);
            }
        }
    }
    
    pthread_mutex_unlock(&state->lock);
}

/**
 * @brief Get load balance recommendation
 *
 * @param workload The workload to assign
 * @param gpu_count Number of GPUs to consider
 * @param gpu_indices Array of GPU indices
 * @param recommendation Output: array of GPU load factors (0.0-1.0)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t load_balancer_get_recommendation(
    struct mvgal_workload *workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    float *recommendation
) {
    if (workload == NULL || gpu_count == 0 || gpu_indices == NULL || recommendation == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    for (uint32_t i = 0; i < gpu_count; i++) {
        uint32_t gpu_index = gpu_indices[i];
        if (gpu_index >= state->gpu_count) {
            recommendation[i] = 0.0f;
            continue;
        }
        
        mvgal_gpu_state_t *gpu = &state->gpus[gpu_index];
        
        // Calculate load factor (inverse of utilization)
        // Lower utilization = higher recommendation
        float load_factor = 1.0f - gpu->utilization;
        
        // Adjust based on thermal conditions
        if (state->config.thermal_aware) {
            if (gpu->temperature > 85.0f) {
                load_factor *= 0.5f; // Reduce load on hot GPUs
            } else if (gpu->temperature > 80.0f) {
                load_factor *= 0.8f;
            }
        }
        
        // Adjust based on power (if power-aware)
        if (state->config.power_aware) {
            // In a real implementation, we would consider power efficiency
            // For now, just apply a small adjustment
            if (gpu->power_usage_w > 200.0f) {
                load_factor *= 0.9f;
            }
        }
        
        recommendation[i] = load_factor;
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Select GPU with best load balance
 *
 * @param workload The workload to assign
 * @param gpu_count Number of GPUs to consider
 * @param gpu_indices Array of GPU indices
 * @param selected_gpu Output: selected GPU index
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t load_balancer_select_gpu(
    struct mvgal_workload *workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    uint32_t *selected_gpu
) {
    if (workload == NULL || gpu_count == 0 || gpu_indices == NULL || selected_gpu == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    float recommendations[MVGAL_SCHEDULER_MAX_GPUS];
    mvgal_error_t err = load_balancer_get_recommendation(
        workload, gpu_count, gpu_indices, recommendations);
    
    if (err != MVGAL_SUCCESS) {
        return err;
    }
    
    // Find GPU with highest recommendation
    float best_recommendation = -1.0f;
    *selected_gpu = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < gpu_count; i++) {
        if (recommendations[i] > best_recommendation) {
            best_recommendation = recommendations[i];
            *selected_gpu = gpu_indices[i];
        }
    }
    
    if (*selected_gpu == 0xFFFFFFFF) {
        return MVGAL_ERROR_NO_GPUS;
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Update GPU temperature
 *
 * In a real implementation, this would query GPU sensors.
 */
void load_balancer_update_temperature(uint32_t gpu_index, float temperature_c) {
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    if (gpu_index >= state->gpu_count) {
        return;
    }
    
    pthread_mutex_lock(&state->lock);
    state->gpus[gpu_index].temperature = temperature_c;
    pthread_mutex_unlock(&state->lock);
    
    MVGAL_LOG_DEBUG("GPU %u temperature updated: %.1fC", gpu_index, temperature_c);
}

/**
 * @brief Update GPU power usage
 *
 * In a real implementation, this would query power sensors.
 */
void load_balancer_update_power(uint32_t gpu_index, float power_w) {
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    if (gpu_index >= state->gpu_count) {
        return;
    }
    
    pthread_mutex_lock(&state->lock);
    state->gpus[gpu_index].power_usage_w = power_w;
    pthread_mutex_unlock(&state->lock);
    
    MVGAL_LOG_DEBUG("GPU %u power updated: %.1fW", gpu_index, power_w);
}

/**
 * @brief Get GPU load information
 */
mvgal_error_t load_balancer_get_gpu_load(
    uint32_t gpu_index,
    float *utilization,
    float *temperature,
    float *power_w
) {
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    if (gpu_index >= state->gpu_count || (utilization == NULL && temperature == NULL && power_w == NULL)) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&state->lock);
    
    if (utilization != NULL) {
        *utilization = state->gpus[gpu_index].utilization * 100.0f; // Convert to percentage
    }
    if (temperature != NULL) {
        *temperature = state->gpus[gpu_index].temperature;
    }
    if (power_w != NULL) {
        *power_w = state->gpus[gpu_index].power_usage_w;
    }
    
    pthread_mutex_unlock(&state->lock);
    
    return MVGAL_SUCCESS;
}
