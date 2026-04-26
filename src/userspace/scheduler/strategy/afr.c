/**
 * @file afr.c
 * @brief Alternate Frame Rendering (AFR) strategy implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This strategy distributes frames across GPUs in an alternating fashion.
 * Each frame is rendered completely by a single GPU, with frames
 * alternating between available GPUs.
 */

#include "../scheduler_internal.h"
#include "mvgal/mvgal_log.h"

// Global AFR state
static struct {
    pthread_mutex_t lock;
    uint32_t current_gpu_index;    ///< Next GPU to use
    uint32_t frame_counter;        ///< Frame counter
    uint32_t gpu_count;            ///< Number of GPUs in rotation
    uint32_t gpu_indices[MVGAL_SCHEDULER_MAX_GPUS]; ///< GPU indices in rotation
    bool initialized;
} g_afr_state = {0};

/**
 * @brief Initialize AFR state
 */
static mvgal_error_t afr_init(void) {
    if (g_afr_state.initialized) {
        return MVGAL_SUCCESS;
    }
    
    pthread_mutex_init(&g_afr_state.lock, NULL);
    g_afr_state.current_gpu_index = 0;
    g_afr_state.frame_counter = 0;
    g_afr_state.gpu_count = 0;
    g_afr_state.initialized = true;
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Configure AFR with specific GPUs
 */
mvgal_error_t afr_configure(uint32_t gpu_count, const uint32_t *gpu_indices) {
    if (gpu_count == 0 || gpu_count > MVGAL_SCHEDULER_MAX_GPUS || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    afr_init();
    
    pthread_mutex_lock(&g_afr_state.lock);
    g_afr_state.gpu_count = gpu_count;
    for (uint32_t i = 0; i < gpu_count; i++) {
        g_afr_state.gpu_indices[i] = gpu_indices[i];
    }
    g_afr_state.current_gpu_index = 0;
    g_afr_state.frame_counter = 0;
    pthread_mutex_unlock(&g_afr_state.lock);
    
    MVGAL_LOG_INFO("AFR configured with %u GPUs", gpu_count);
    return MVGAL_SUCCESS;
}

/**
 * @brief Get next GPU for AFR
 */
static uint32_t afr_get_next_gpu(void) {
    pthread_mutex_lock(&g_afr_state.lock);
    
    if (g_afr_state.gpu_count == 0) {
        pthread_mutex_unlock(&g_afr_state.lock);
        return 0xFFFFFFFF; // No GPUs configured
    }
    
    uint32_t gpu_index = g_afr_state.gpu_indices[g_afr_state.current_gpu_index];
    
    // Round-robin to next GPU
    g_afr_state.current_gpu_index = (g_afr_state.current_gpu_index + 1) % g_afr_state.gpu_count;
    g_afr_state.frame_counter++;
    
    pthread_mutex_unlock(&g_afr_state.lock);
    
    MVGAL_LOG_DEBUG("AFR: Frame %u assigned to GPU %u", 
                   g_afr_state.frame_counter, gpu_index);
    
    return gpu_index;
}

/**
 * @brief Distribute workload using AFR strategy
 */
mvgal_error_t mvgal_scheduler_distribute_afr(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    afr_init();
    
    // If workload has specific GPU assignments, use them
    if (workload->assigned_gpu_count > 0) {
        // Use pre-assigned GPUs
        workload->descriptor.assigned_gpu = workload->assigned_gpus[0];
        MVGAL_LOG_DEBUG("AFR: Workload %u using pre-assigned GPU %u",
                       workload->descriptor.id, workload->descriptor.assigned_gpu);
        return MVGAL_SUCCESS;
    }
    
    // If workload has GPU mask, use those GPUs
    if (workload->descriptor.gpu_mask != 0xFFFFFFFF) {
        uint32_t gpu_count = 0;
        uint32_t gpu_indices[MVGAL_SCHEDULER_MAX_GPUS];
        
        for (uint32_t i = 0; i < MVGAL_SCHEDULER_MAX_GPUS; i++) {
            if (workload->descriptor.gpu_mask & (1 << i)) {
                gpu_indices[gpu_count++] = i;
            }
        }
        
        if (gpu_count > 0) {
            afr_configure(gpu_count, gpu_indices);
        }
    }
    
    // Get next GPU from round-robin
    uint32_t gpu_index = afr_get_next_gpu();
    
    if (gpu_index == 0xFFFFFFFF) {
        // No GPUs configured for AFR, use scheduler's find_best_gpu
        return mvgal_scheduler_find_best_gpu(workload, &workload->descriptor.assigned_gpu);
    }
    
    workload->descriptor.assigned_gpu = gpu_index;
    workload->assigned_gpu_count = 1;
    workload->assigned_gpus[0] = gpu_index;
    
    MVGAL_LOG_DEBUG("AFR: Workload %u assigned to GPU %u",
                   workload->descriptor.id, gpu_index);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get AFR statistics
 */
mvgal_error_t afr_get_stats(uint64_t *frames_distributed, uint32_t *gpu_count) {
    afr_init();
    
    pthread_mutex_lock(&g_afr_state.lock);
    *frames_distributed = g_afr_state.frame_counter;
    *gpu_count = g_afr_state.gpu_count;
    pthread_mutex_unlock(&g_afr_state.lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Reset AFR state
 */
void afr_reset(void) {
    pthread_mutex_lock(&g_afr_state.lock);
    g_afr_state.current_gpu_index = 0;
    g_afr_state.frame_counter = 0;
    g_afr_state.gpu_count = 0;
    pthread_mutex_unlock(&g_afr_state.lock);
}
