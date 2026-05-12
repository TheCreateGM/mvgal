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
#include "mvgal/mvgal_log.h"

static void update_gpu_utilization(mvgal_gpu_state_t *gpu, uint32_t index) {
    if (gpu == NULL) {
        return;
    }
    
    float util = 0.0f;
    float temp = 0.0f;
    
    /* Query real GPU utilization and temperature from the driver */
    if (mvgal_gpu_get_utilization(index, &util) == MVGAL_SUCCESS) {
        /* Convert 0-100 to 0.0-1.0 */
        float real_util = util / 100.0f;
        gpu->utilization = gpu->utilization * 0.7f + real_util * 0.3f;
    } else {
        /* Fallback to queue-based simulation if driver query fails */
        float queue_util = (float)gpu->queue.count / 10.0f;
        gpu->utilization = gpu->utilization * 0.7f + queue_util * 0.3f;
    }
    
    if (mvgal_gpu_get_temperature(index, &temp) == MVGAL_SUCCESS) {
        gpu->temperature = temp;
    }
    
    /* Clamp to 0-1 range */
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
        update_gpu_utilization(&state->gpus[i], i);
    }
    
    // Check if we need to balance
    float max_util = 0.0f;
    float min_util = 1.0f;
    
    for (uint32_t i = 0; i < state->gpu_count; i++) {
        if (state->gpus[i].available && state->gpus[i].enabled) {
            if (state->gpus[i].utilization > max_util) max_util = state->gpus[i].utilization;
            if (state->gpus[i].utilization < min_util) min_util = state->gpus[i].utilization;
        }
    }
    
    // Calculate imbalance
    float imbalance = max_util - min_util;
    
    /*
     * Dynamic rebalancing: steer new work toward cooler GPUs by nudging per-GPU
     * scheduler priority (kept in sync with config.gpu_priorities). Vulkan
     * command-buffer rewrite / in-flight migration is out of scope here.
     */
    if (imbalance > state->config.load_balance_threshold) {
        MVGAL_LOG_INFO("Load imbalance detected: %.2f%% (threshold %.2f%%), adjusting scheduler weights",
                       imbalance * 100.0f, state->config.load_balance_threshold * 100.0f);

        uint32_t idx_hot = 0, idx_cool = 0;
        float util_hot = -1.0f;
        float util_cool = 2.0f;
        uint32_t active = 0;

        for (uint32_t i = 0; i < state->gpu_count; i++) {
            if (!state->gpus[i].available || !state->gpus[i].enabled) {
                continue;
            }
            active++;
            float u = state->gpus[i].utilization;
            if (u > util_hot) {
                util_hot = u;
                idx_hot = i;
            }
            if (u < util_cool) {
                util_cool = u;
                idx_cool = i;
            }
        }

        if (active >= 2 && idx_hot != idx_cool && (util_hot - util_cool) > 0.05f) {
            const uint32_t step = 3;
            uint32_t pri_hot = state->gpus[idx_hot].priority;
            uint32_t pri_cool = state->gpus[idx_cool].priority;

            pri_hot = pri_hot > step + 10u ? pri_hot - step : 10u;
            pri_cool = pri_cool + step < 100u ? pri_cool + step : 100u;

            state->gpus[idx_hot].priority = pri_hot;
            state->gpus[idx_cool].priority = pri_cool;
            state->config.gpu_priorities[idx_hot] = pri_hot;
            state->config.gpu_priorities[idx_cool] = pri_cool;

            MVGAL_LOG_DEBUG("Rebalance: GPU %u pri=%u (util %.1f%%), GPU %u pri=%u (util %.1f%%)",
                            idx_hot, pri_hot, util_hot * 100.0f,
                            idx_cool, pri_cool, util_cool * 100.0f);
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
        
        /* 
         * Heterogeneity-aware Load Factor calculation (ML-like heuristic).
         * We consider:
         * 1. Current utilization (higher = lower factor)
         * 2. Compute capacity (normalized compute units)
         * 3. Thermal headroom (higher temp = lower factor)
         * 4. Memory availability
         */
        
        float base_util = 1.0f - gpu->utilization;
        
        /* Get GPU descriptor for capacity info */
        mvgal_gpu_descriptor_t desc;
        float capacity_weight = 1.0f;
        if (mvgal_gpu_get_descriptor(gpu_index, &desc) == MVGAL_SUCCESS) {
             /* Normalize capacity against a "standard" GPU (score 50.0) */
             capacity_weight = desc.compute_score / 50.0f;
             if (capacity_weight < 0.1f) capacity_weight = 0.1f;
         }
        
        float load_factor = base_util * capacity_weight;
        
        // Adjust based on thermal conditions
        if (state->config.thermal_aware) {
            /* Non-linear thermal penalty as we approach 85C */
            if (gpu->temperature > 60.0f) {
                float thermal_penalty = (gpu->temperature - 60.0f) / 25.0f; /* 0.0 at 60C, 1.0 at 85C */
                if (thermal_penalty > 0.9f) thermal_penalty = 0.9f;
                load_factor *= (1.0f - thermal_penalty);
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
