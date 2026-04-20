/**
 * @file scheduler.c
 * @brief Scheduler implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements the workload scheduling and distribution system.
 * It manages workload queues, GPU selection, and strategy application.
 */

#include "scheduler_internal.h"
#include "mvgal_log.h"
#include "mvgal_gpu.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Global scheduler state
static mvgal_scheduler_state_t g_scheduler_state = {0};

/**
 * @brief Get current time in nanoseconds
 */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/**
 * @brief Initialize workload queue
 */
static void workload_queue_init(mvgal_workload_queue_t *queue) {
    pthread_mutex_init(&queue->lock, NULL);
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->total_priority = 0;
}

/**
 * @brief Get the global scheduler state
 */
mvgal_scheduler_state_t *mvgal_scheduler_get_state(void) {
    return &g_scheduler_state;
}

/**
 * @brief Initialize scheduler module
 */
mvgal_error_t mvgal_scheduler_module_init(void) {
    if (g_scheduler_state.initialized) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }
    
    pthread_mutex_init(&g_scheduler_state.lock, NULL);
    pthread_mutex_init(&g_scheduler_state.splitters_lock, NULL);
    pthread_cond_init(&g_scheduler_state.work_cond, NULL);
    
    g_scheduler_state.paused = false;
    g_scheduler_state.running = false;
    g_scheduler_state.thread_running = false;
    
    // Initialize configuration
    g_scheduler_state.config.strategy = MVGAL_STRATEGY_AUTO;
    g_scheduler_state.config.dynamic_load_balance = true;
    g_scheduler_state.config.thermal_aware = true;
    g_scheduler_state.config.power_aware = true;
    g_scheduler_state.config.load_balance_threshold = 0.8f;
    g_scheduler_state.config.max_queued_workloads = MVGAL_SCHEDULER_MAX_QUEUED;
    g_scheduler_state.config.quantum_ns = 16000000; // 16ms quantum
    for (uint32_t i = 0; i < MVGAL_SCHEDULER_MAX_GPUS; i++) {
        g_scheduler_state.config.gpu_priorities[i] = 50; // Default priority
    }
    
    // Initialize workload queues
    workload_queue_init(&g_scheduler_state.global_queue);
    workload_queue_init(&g_scheduler_state.ready_queue);
    workload_queue_init(&g_scheduler_state.running_queue);
    workload_queue_init(&g_scheduler_state.completed_queue);
    
    // Initialize GPU states
    g_scheduler_state.gpu_count = 0;
    for (uint32_t i = 0; i < MVGAL_SCHEDULER_MAX_GPUS; i++) {
        g_scheduler_state.gpus[i].available = false;
        g_scheduler_state.gpus[i].enabled = true;
        g_scheduler_state.gpus[i].utilization = 0.0f;
        g_scheduler_state.gpus[i].temperature = 0.0f;
        g_scheduler_state.gpus[i].power_usage_w = 0.0f;
        g_scheduler_state.gpus[i].estimated_free_time_ns = 0;
        g_scheduler_state.gpus[i].features = 0;
        g_scheduler_state.gpus[i].compute_score = 0.0f;
        g_scheduler_state.gpus[i].graphics_score = 0.0f;
        g_scheduler_state.gpus[i].priority = 50;
        workload_queue_init(&g_scheduler_state.gpus[i].queue);
        g_scheduler_state.gpus[i].workloads_completed = 0;
        g_scheduler_state.gpus[i].total_execution_time_ns = 0;
    }
    
    // Initialize statistics
    memset(&g_scheduler_state.stats, 0, sizeof(g_scheduler_state.stats));
    
    // Initialize custom splitters
    g_scheduler_state.splitters = NULL;
    g_scheduler_state.splitter_count = 0;
    
    // Initialize workload ID counter
    atomic_init(&g_scheduler_state.next_workload_id, 1);
    
    // Scan for GPUs
    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count > 0) {
        mvgal_gpu_descriptor_t gpus[MVGAL_SCHEDULER_MAX_GPUS];
        int32_t count = mvgal_gpu_enumerate(gpus, MVGAL_SCHEDULER_MAX_GPUS);
        g_scheduler_state.gpu_count = (uint32_t)count;
        
        for (int32_t i = 0; i < count; i++) {
            g_scheduler_state.gpus[i].available = gpus[i].enabled && gpus[i].available;
            g_scheduler_state.gpus[i].gpu_info = gpus[i];
            g_scheduler_state.gpus[i].features = gpus[i].features;
            g_scheduler_state.gpus[i].compute_score = gpus[i].compute_score;
            g_scheduler_state.gpus[i].graphics_score = gpus[i].graphics_score;
            g_scheduler_state.gpus[i].priority = 50;
        }
    }
    
    g_scheduler_state.initialized = true;
    MVGAL_LOG_INFO("Scheduler module initialized with %d GPUs", g_scheduler_state.gpu_count);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Shutdown scheduler module
 */
void mvgal_scheduler_module_shutdown(void) {
    if (!g_scheduler_state.initialized) {
        return;
    }
    
    // Stop scheduler thread
    if (g_scheduler_state.thread_running) {
        pthread_mutex_lock(&g_scheduler_state.lock);
        g_scheduler_state.running = false;
        pthread_cond_signal(&g_scheduler_state.work_cond);
        pthread_mutex_unlock(&g_scheduler_state.lock);
        
        pthread_join(g_scheduler_state.scheduler_thread, NULL);
        g_scheduler_state.thread_running = false;
    }
    
    // Clean up queues
    // In a real implementation, we would wait for all workloads to complete
    
    // Free custom splitters
    if (g_scheduler_state.splitters != NULL) {
        free(g_scheduler_state.splitters);
        g_scheduler_state.splitters = NULL;
    }
    
    // Destroy mutexes and conditions
    pthread_mutex_destroy(&g_scheduler_state.lock);
    pthread_mutex_destroy(&g_scheduler_state.splitters_lock);
    pthread_cond_destroy(&g_scheduler_state.work_cond);
    
    g_scheduler_state.initialized = false;
    MVGAL_LOG_INFO("Scheduler module shut down");
}

/**
 * @brief Create a workload structure
 */
struct mvgal_workload *mvgal_workload_create_internal(
    const mvgal_workload_submit_info_t *info
) {
    struct mvgal_workload *workload = calloc(1, sizeof(struct mvgal_workload));
    if (workload == NULL) {
        return NULL;
    }
    
    // Initialize reference counting
    atomic_init(&workload->refcount, 1);
    
    // Set state
    workload->state = MVGAL_WORKLOAD_STATE_PENDING;
    
    // Initialize descriptor
    workload->descriptor.id = atomic_fetch_add(&g_scheduler_state.next_workload_id, 1);
    workload->descriptor.type = info->type;
    workload->descriptor.priority = info->priority;
    workload->descriptor.submission_time = get_time_ns();
    workload->descriptor.deadline = info->deadline;
    workload->descriptor.source = NULL; // Will be set by context
    workload->descriptor.api = MVGAL_API_UNKNOWN; // Will be set by context
    workload->descriptor.estimated_duration_ns = 0; // Could be estimated based on type
    workload->descriptor.memory_required = 0; // Could be estimated
    workload->descriptor.gpu_preference = 0xFFFFFFFF; // Any GPU
    workload->descriptor.gpu_mask = info->gpu_mask;
    workload->descriptor.dependency_count = info->dependency_count;
    workload->descriptor.dependencies = info->dependencies;
    workload->descriptor.completed = false;
    workload->descriptor.result = MVGAL_SUCCESS;
    workload->descriptor.start_time = 0;
    workload->descriptor.end_time = 0;
    workload->descriptor.assigned_gpu = 0xFFFFFFFF; // Not assigned yet
    memset(workload->descriptor.reserved, 0, sizeof(workload->descriptor.reserved));
    
    // Set context and callback
    workload->context = NULL; // Will be set by submission
    workload->callback = NULL; // No callback by default
    workload->user_data = info->user_data;
    
    // GPU assignment
    workload->assigned_gpu_count = 0;
    
    // Scheduling info
    workload->queue_time = 0;
    workload->start_time = 0;
    workload->end_time = 0;
    
    // Synchronization
    pthread_mutex_init(&workload->mutex, NULL);
    pthread_cond_init(&workload->cond, NULL);
    workload->completion_signaled = false;
    workload->completion_status = MVGAL_SUCCESS;
    
    // Statistics
    memset(workload->gpu_execution_time_ns, 0, sizeof(workload->gpu_execution_time_ns));
    
    // Queue links
    workload->next = NULL;
    workload->prev = NULL;
    
    // Strategy-specific data
    workload->strategy_data = NULL;
    
    MVGAL_LOG_DEBUG("Workload %u created", workload->descriptor.id);
    
    return workload;
}

/**
 * @brief Destroy a workload structure
 */
void mvgal_workload_destroy_internal(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return;
    }
    
    pthread_mutex_destroy(&workload->mutex);
    pthread_cond_destroy(&workload->cond);
    
    // Free strategy-specific data
    if (workload->strategy_data != NULL) {
        free(workload->strategy_data);
    }
    
    uint32_t id = workload->descriptor.id;
    free(workload);
    MVGAL_LOG_DEBUG("Workload %u destroyed", id);
}

/**
 * @brief Increment workload reference count
 */
void mvgal_workload_retain(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return;
    }
    atomic_fetch_add(&workload->refcount, 1);
}

/**
 * @brief Decrement workload reference count and possibly free
 */
void mvgal_workload_release(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return;
    }
    
    uint32_t old_count = atomic_fetch_sub(&workload->refcount, 1);
    if (old_count == 1) {
        mvgal_workload_destroy_internal(workload);
    }
}

/**
 * @brief Queue a workload
 */
mvgal_error_t mvgal_workload_queue(
    struct mvgal_workload *workload,
    mvgal_workload_queue_t *queue
) {
    if (workload == NULL || queue == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&queue->lock);
    
    // Check max queue size
    if (queue->count >= MVGAL_SCHEDULER_MAX_QUEUED) {
        pthread_mutex_unlock(&queue->lock);
        return MVGAL_ERROR_MEMORY;
    }
    
    // Add to tail
    workload->next = NULL;
    workload->prev = queue->tail;
    if (queue->tail != NULL) {
        queue->tail->next = workload;
    }
    queue->tail = workload;
    if (queue->head == NULL) {
        queue->head = workload;
    }
    queue->count++;
    queue->total_priority += workload->descriptor.priority;
    
    pthread_mutex_unlock(&queue->lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Dequeue a workload (FIFO)
 */
struct mvgal_workload *mvgal_workload_dequeue(
    mvgal_workload_queue_t *queue
) {
    if (queue == NULL) {
        return NULL;
    }
    
    pthread_mutex_lock(&queue->lock);
    
    struct mvgal_workload *workload = queue->head;
    if (workload != NULL) {
        queue->head = workload->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        } else {
            queue->head->prev = NULL;
        }
        workload->next = NULL;
        workload->prev = NULL;
        queue->count--;
        queue->total_priority -= workload->descriptor.priority;
    }
    
    pthread_mutex_unlock(&queue->lock);
    return workload;
}

/**
 * @brief Dequeue the highest priority workload
 */
struct mvgal_workload *mvgal_workload_dequeue_priority(
    mvgal_workload_queue_t *queue
) {
    if (queue == NULL) {
        return NULL;
    }
    
    pthread_mutex_lock(&queue->lock);
    
    struct mvgal_workload *workload = NULL;
    struct mvgal_workload *current = queue->head;
    struct mvgal_workload *best = NULL;
    uint32_t best_priority = 0;
    
    while (current != NULL) {
        if (current->descriptor.priority > best_priority) {
            best = current;
            best_priority = current->descriptor.priority;
        }
        current = current->next;
    }
    
    if (best != NULL) {
        // Remove from queue
        if (best->prev != NULL) {
            best->prev->next = best->next;
        } else {
            queue->head = best->next;
        }
        if (best->next != NULL) {
            best->next->prev = best->prev;
        } else {
            queue->tail = best->prev;
        }
        best->next = NULL;
        best->prev = NULL;
        queue->count--;
        queue->total_priority -= best_priority;
        workload = best;
    }
    
    pthread_mutex_unlock(&queue->lock);
    return workload;
}

/**
 * @brief Check if GPU is suitable for workload
 */
bool mvgal_scheduler_gpu_suitable(
    uint32_t gpu_index,
    struct mvgal_workload *workload
) {
    if (gpu_index >= g_scheduler_state.gpu_count) {
        return false;
    }
    
    mvgal_gpu_state_t *gpu = &g_scheduler_state.gpus[gpu_index];
    
    // Check if enabled and available
    if (!gpu->enabled || !gpu->available) {
        return false;
    }
    
    // Check if workload has GPU mask restriction
    if (workload->descriptor.gpu_mask != 0xFFFFFFFF) {
        if (!(workload->descriptor.gpu_mask & (1 << gpu_index))) {
            return false;
        }
    }
    
    // Check feature requirements based on workload type
    // In a real implementation, this would check if GPU has required features
    // For now, assume all GPUs can handle all workload types
    
    return true;
}

/**
 * @brief Calculate workload score for GPU
 */
float mvgal_scheduler_calculate_score(
    uint32_t gpu_index,
    struct mvgal_workload *workload
) {
    if (gpu_index >= g_scheduler_state.gpu_count) {
        return -1.0f; // Invalid
    }
    
    mvgal_gpu_state_t *gpu = &g_scheduler_state.gpus[gpu_index];
    float score = 0.0f;
    
    // Base score on GPU priority
    score += gpu->priority * 0.1f;
    
    // Add compute/graphics score based on workload type
    switch (workload->descriptor.type) {
        case MVGAL_WORKLOAD_COMPUTE:
        case MVGAL_WORKLOAD_AI:
            score += gpu->compute_score * 0.01f;
            break;
        case MVGAL_WORKLOAD_GRAPHICS:
        case MVGAL_WORKLOAD_TRACE:
            score += gpu->graphics_score * 0.01f;
            break;
        case MVGAL_WORKLOAD_VIDEO:
        case MVGAL_WORKLOAD_TRANSFER:
            // Use average
            score += (gpu->compute_score + gpu->graphics_score) * 0.005f;
            break;
        default:
            score += (gpu->compute_score + gpu->graphics_score) * 0.005f;
            break;
    }
    
    // Penalize based on utilization
    score -= gpu->utilization * 100.0f;
    
    // Penalize based on temperature (if thermal-aware)
    if (g_scheduler_state.config.thermal_aware && gpu->temperature > 80.0f) {
        score -= (gpu->temperature - 80.0f) * 0.5f;
    }
    
    return score;
}

/**
 * @brief Find the best GPU for a workload
 */
mvgal_error_t mvgal_scheduler_find_best_gpu(
    struct mvgal_workload *workload,
    uint32_t *gpu_index
) {
    if (workload == NULL || gpu_index == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    float best_score = -1000.0f;
    uint32_t best_idx = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < g_scheduler_state.gpu_count; i++) {
        if (mvgal_scheduler_gpu_suitable(i, workload)) {
            float score = mvgal_scheduler_calculate_score(i, workload);
            if (score > best_score) {
                best_score = score;
                best_idx = i;
            }
        }
    }
    
    if (best_idx == 0xFFFFFFFF) {
        return MVGAL_ERROR_NO_GPUS;
    }
    
    *gpu_index = best_idx;
    return MVGAL_SUCCESS;
}

/**
 * @brief Mark workload as completed
 */
void mvgal_workload_complete(
    struct mvgal_workload *workload,
    mvgal_error_t result
) {
    if (workload == NULL) {
        return;
    }
    
    workload->end_time = get_time_ns();
    workload->state = (result == MVGAL_SUCCESS) ? 
                     MVGAL_WORKLOAD_STATE_COMPLETED : 
                     MVGAL_WORKLOAD_STATE_FAILED;
    workload->descriptor.completed = true;
    workload->descriptor.result = result;
    workload->descriptor.end_time = workload->end_time;
    
    // Signal completion
    pthread_mutex_lock(&workload->mutex);
    workload->completion_signaled = true;
    workload->completion_status = result;
    pthread_cond_broadcast(&workload->cond);
    pthread_mutex_unlock(&workload->mutex);
    
    // Update statistics
    pthread_mutex_lock(&g_scheduler_state.lock);
    g_scheduler_state.stats.workloads_completed++;
    if (result != MVGAL_SUCCESS) {
        g_scheduler_state.stats.workloads_failed++;
    }
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    // Notify callback if registered
    if (workload->callback != NULL) {
        workload->callback(workload, result, workload->user_data);
    }
    
    MVGAL_LOG_DEBUG("Workload %u completed with result %d", 
                   workload->descriptor.id, result);
}

/**
 * @brief Scheduler main loop
 */
void *mvgal_scheduler_thread(void *arg) {
    (void)arg;
    
    MVGAL_LOG_INFO("Scheduler thread started");
    
    while (g_scheduler_state.running) {
        pthread_mutex_lock(&g_scheduler_state.lock);
        
        // Wait for work if paused or queues are empty
        if (g_scheduler_state.paused || 
            g_scheduler_state.ready_queue.count == 0) {
            pthread_cond_wait(&g_scheduler_state.work_cond, &g_scheduler_state.lock);
            pthread_mutex_unlock(&g_scheduler_state.lock);
            continue;
        }
        
        // Get next workload
        struct mvgal_workload *workload = mvgal_workload_dequeue_priority(
            &g_scheduler_state.ready_queue);
        
        pthread_mutex_unlock(&g_scheduler_state.lock);
        
        if (workload != NULL) {
            // Process workload
            mvgal_scheduler_apply_strategy(workload);
            
            // Mark as completed (in real impl, this would be async)
            mvgal_workload_complete(workload, MVGAL_SUCCESS);
            mvgal_workload_release(workload);
        }
    }
    
    MVGAL_LOG_INFO("Scheduler thread stopped");
    return NULL;
}

/**
 * @brief Process pending workloads
 */
mvgal_error_t mvgal_scheduler_process_internal(void) {
    if (!g_scheduler_state.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    
    while (g_scheduler_state.ready_queue.count > 0) {
        struct mvgal_workload *workload = mvgal_workload_dequeue_priority(
            &g_scheduler_state.ready_queue);
        
        if (workload != NULL) {
            pthread_mutex_unlock(&g_scheduler_state.lock);
            
            mvgal_scheduler_apply_strategy(workload);
            
            // Mark as completed
            mvgal_workload_complete(workload, MVGAL_SUCCESS);
            mvgal_workload_release(workload);
            
            pthread_mutex_lock(&g_scheduler_state.lock);
        }
    }
    
    pthread_mutex_unlock(&g_scheduler_state.lock);
    return MVGAL_SUCCESS;
}

/**
 * @brief Apply distribution strategy
 */
mvgal_error_t mvgal_scheduler_apply_strategy(
    struct mvgal_workload *workload
) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Use configured strategy
    mvgal_distribution_strategy_t strategy = g_scheduler_state.config.strategy;
    
    if (strategy == MVGAL_STRATEGY_AUTO) {
        // Auto-select based on workload type
        switch (workload->descriptor.type) {
            case MVGAL_WORKLOAD_GRAPHICS:
                strategy = MVGAL_STRATEGY_AFR;
                break;
            case MVGAL_WORKLOAD_COMPUTE:
                strategy = MVGAL_STRATEGY_COMPUTE_OFFLOAD;
                break;
            case MVGAL_WORKLOAD_VIDEO:
                strategy = MVGAL_STRATEGY_SFR;
                break;
            default:
                strategy = MVGAL_STRATEGY_SINGLE_GPU;
                break;
        }
    }
    
    switch (strategy) {
        case MVGAL_STRATEGY_AFR:
            return mvgal_scheduler_distribute_afr(workload);
        case MVGAL_STRATEGY_SFR:
            return mvgal_scheduler_distribute_sfr(workload);
        case MVGAL_STRATEGY_TASK:
            return mvgal_scheduler_distribute_task(workload);
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD:
        case MVGAL_STRATEGY_HYBRID:
        case MVGAL_STRATEGY_SINGLE_GPU:
            return mvgal_scheduler_find_best_gpu(workload, 
                &workload->descriptor.assigned_gpu);
        case MVGAL_STRATEGY_CUSTOM:
        default:
            return MVGAL_ERROR_NOT_SUPPORTED;
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

/**
 * @brief Submit a workload for execution
 */
mvgal_error_t mvgal_workload_submit(
    void *context,
    const mvgal_workload_submit_info_t *info,
    mvgal_workload_t *workload
) {
    if (info == NULL || workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (!g_scheduler_state.initialized) {
        mvgal_error_t err = mvgal_scheduler_module_init();
        if (err != MVGAL_SUCCESS) {
            return err;
        }
    }
    
    // Create workload
    struct mvgal_workload *w = mvgal_workload_create_internal(info);
    if (w == NULL) {
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    w->context = context;
    w->descriptor.api = MVGAL_API_UNKNOWN; // Will be determined from context
    
    // Set state to queued
    w->state = MVGAL_WORKLOAD_STATE_QUEUED;
    w->queue_time = get_time_ns();
    w->descriptor.submission_time = w->queue_time;
    
    // Queue the workload
    mvgal_error_t err = mvgal_workload_queue(w, &g_scheduler_state.ready_queue);
    if (err != MVGAL_SUCCESS) {
        mvgal_workload_destroy_internal(w);
        return err;
    }
    
    // Update statistics
    g_scheduler_state.stats.workloads_submitted++;
    
    // Signal scheduler thread
    pthread_cond_signal(&g_scheduler_state.work_cond);
    
    *workload = w;
    MVGAL_LOG_DEBUG("Workload %u submitted", w->descriptor.id);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Submit a workload with callback
 */
mvgal_error_t mvgal_workload_submit_with_callback(
    void *context,
    const mvgal_workload_submit_info_t *info,
    mvgal_workload_callback_t callback,
    mvgal_workload_t *workload
) {
    if (callback == NULL) {
        return mvgal_workload_submit(context, info, workload);
    }
    
    if (!g_scheduler_state.initialized) {
        mvgal_error_t err = mvgal_scheduler_module_init();
        if (err != MVGAL_SUCCESS) {
            return err;
        }
    }
    
    // Create workload
    struct mvgal_workload *w = mvgal_workload_create_internal(info);
    if (w == NULL) {
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    w->context = context;
    w->callback = callback;
    
    // Set state to queued
    w->state = MVGAL_WORKLOAD_STATE_QUEUED;
    w->queue_time = get_time_ns();
    w->descriptor.submission_time = w->queue_time;
    
    // Queue the workload
    mvgal_error_t err = mvgal_workload_queue(w, &g_scheduler_state.ready_queue);
    if (err != MVGAL_SUCCESS) {
        mvgal_workload_destroy_internal(w);
        return err;
    }
    
    // Update statistics
    g_scheduler_state.stats.workloads_submitted++;
    
    // Signal scheduler thread
    pthread_cond_signal(&g_scheduler_state.work_cond);
    
    *workload = w;
    MVGAL_LOG_DEBUG("Workload %u submitted with callback", w->descriptor.id);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Wait for workload completion
 */
mvgal_error_t mvgal_workload_wait(
    mvgal_workload_t workload,
    uint32_t timeout_ms
) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    
    pthread_mutex_lock(&w->mutex);
    
    if (w->completion_signaled) {
        mvgal_error_t result = w->completion_status;
        pthread_mutex_unlock(&w->mutex);
        return result;
    }
    
    if (timeout_ms == 0) {
        // Wait indefinitely
        while (!w->completion_signaled) {
            pthread_cond_wait(&w->cond, &w->mutex);
        }
    } else {
        // Wait with timeout
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        ns += (uint64_t)timeout_ms * 1000000ULL;
        
        ts.tv_sec = ns / 1000000000ULL;
        ts.tv_nsec = ns % 1000000000ULL;
        
        while (!w->completion_signaled) {
            int ret = pthread_cond_timedwait(&w->cond, &w->mutex, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&w->mutex);
                return MVGAL_ERROR_TIMEOUT;
            }
        }
    }
    
    mvgal_error_t result = w->completion_status;
    pthread_mutex_unlock(&w->mutex);
    return result;
}

/**
 * @brief Check if workload is completed
 */
bool mvgal_workload_is_completed(mvgal_workload_t workload) {
    if (workload == NULL) {
        return true; // NULL workload is "complete"
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    pthread_mutex_lock(&w->mutex);
    bool completed = w->completion_signaled;
    pthread_mutex_unlock(&w->mutex);
    return completed;
}

/**
 * @brief Get workload result
 */
mvgal_error_t mvgal_workload_get_result(mvgal_workload_t workload) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    pthread_mutex_lock(&w->mutex);
    mvgal_error_t result = w->completion_status;
    pthread_mutex_unlock(&w->mutex);
    return result;
}

/**
 * @brief Get workload descriptor
 */
mvgal_error_t mvgal_workload_get_descriptor(
    mvgal_workload_t workload,
    mvgal_workload_descriptor_t *desc
) {
    if (workload == NULL || desc == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    pthread_mutex_lock(&w->mutex);
    *desc = w->descriptor;
    pthread_mutex_unlock(&w->mutex);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Cancel a pending workload
 */
mvgal_error_t mvgal_workload_cancel(mvgal_workload_t workload) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    pthread_mutex_lock(&w->mutex);
    
    // Can only cancel pending or queued workloads
    if (w->state == MVGAL_WORKLOAD_STATE_PENDING || 
        w->state == MVGAL_WORKLOAD_STATE_QUEUED) {
        w->state = MVGAL_WORKLOAD_STATE_CANCELLED;
        w->completion_signaled = true;
        w->completion_status = MVGAL_ERROR_INTERRUPTED;
        pthread_cond_broadcast(&w->cond);
        
        // Remove from queue
        // In a real implementation, we would remove from the queue
        g_scheduler_state.stats.workloads_failed++;
        
        pthread_mutex_unlock(&w->mutex);
        pthread_mutex_unlock(&g_scheduler_state.lock);
        
        return MVGAL_SUCCESS;
    }
    
    pthread_mutex_unlock(&w->mutex);
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return MVGAL_ERROR_NOT_SUPPORTED; // Cannot cancel running/completed workloads
}

/**
 * @brief Destroy a workload handle
 */
void mvgal_workload_destroy(mvgal_workload_t workload) {
    if (workload == NULL) {
        return;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    mvgal_workload_release(w);
}

/**
 * @brief Set workload priority
 */
mvgal_error_t mvgal_workload_set_priority(
    mvgal_workload_t workload,
    uint32_t priority
) {
    if (workload == NULL || priority > 100) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    pthread_mutex_lock(&w->mutex);
    
    // Update global queue priority
    if (w->state == MVGAL_WORKLOAD_STATE_QUEUED) {
        g_scheduler_state.ready_queue.total_priority -= w->descriptor.priority;
        w->descriptor.priority = priority;
        g_scheduler_state.ready_queue.total_priority += priority;
    } else {
        w->descriptor.priority = priority;
    }
    
    pthread_mutex_unlock(&w->mutex);
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Configure the scheduler
 */
mvgal_error_t mvgal_scheduler_configure(
    void *context,
    const mvgal_scheduler_config_t *config
) {
    (void)context;
    
    if (config == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    
    g_scheduler_state.config.strategy = config->strategy;
    g_scheduler_state.config.dynamic_load_balance = config->dynamic_load_balance;
    g_scheduler_state.config.thermal_aware = config->thermal_aware;
    g_scheduler_state.config.power_aware = config->power_aware;
    g_scheduler_state.config.load_balance_threshold = config->load_balance_threshold;
    g_scheduler_state.config.max_queued_workloads = config->max_queued_workloads;
    g_scheduler_state.config.quantum_ns = config->quantum_ns;
    
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    MVGAL_LOG_INFO("Scheduler configured: strategy=%d, load_balance=%d",
                   config->strategy, config->dynamic_load_balance);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get scheduler configuration
 */
mvgal_error_t mvgal_scheduler_get_config(
    void *context,
    mvgal_scheduler_config_t *config
) {
    (void)context;
    
    if (config == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    
    *config = (mvgal_scheduler_config_t){
        .strategy = g_scheduler_state.config.strategy,
        .dynamic_load_balance = g_scheduler_state.config.dynamic_load_balance,
        .thermal_aware = g_scheduler_state.config.thermal_aware,
        .power_aware = g_scheduler_state.config.power_aware,
        .load_balance_threshold = g_scheduler_state.config.load_balance_threshold,
        .max_queued_workloads = g_scheduler_state.config.max_queued_workloads,
        .quantum_ns = g_scheduler_state.config.quantum_ns,
    };
    
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get scheduler statistics
 */
mvgal_error_t mvgal_scheduler_get_stats(
    void *context,
    mvgal_scheduler_stats_t *stats
) {
    (void)context;
    
    if (stats == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    *stats = g_scheduler_state.stats;
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Reset scheduler statistics
 */
mvgal_error_t mvgal_scheduler_reset_stats(void *context) {
    (void)context;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    memset(&g_scheduler_state.stats, 0, sizeof(g_scheduler_state.stats));
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Set the distribution strategy
 */
mvgal_error_t mvgal_scheduler_set_strategy(
    void *context,
    mvgal_distribution_strategy_t strategy
) {
    (void)context;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    g_scheduler_state.config.strategy = strategy;
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    MVGAL_LOG_INFO("Scheduler strategy set to %d", strategy);
    return MVGAL_SUCCESS;
}

/**
 * @brief Get the current distribution strategy
 */
mvgal_distribution_strategy_t mvgal_scheduler_get_strategy(void *context) {
    (void)context;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    mvgal_distribution_strategy_t strategy = g_scheduler_state.config.strategy;
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return strategy;
}

/**
 * @brief Manually assign workload to specific GPUs
 */
mvgal_error_t mvgal_workload_assign_gpus(
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices
) {
    if (workload == NULL || gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    
    if (gpu_count > MVGAL_SCHEDULER_MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&w->mutex);
    
    w->assigned_gpu_count = gpu_count;
    for (uint32_t i = 0; i < gpu_count; i++) {
        if (gpu_indices[i] < MVGAL_SCHEDULER_MAX_GPUS) {
            w->assigned_gpus[i] = gpu_indices[i];
            w->descriptor.gpu_mask |= (1 << gpu_indices[i]);
        }
    }
    
    // If only one GPU, set as assigned
    if (gpu_count == 1) {
        w->descriptor.assigned_gpu = gpu_indices[0];
    }
    
    pthread_mutex_unlock(&w->mutex);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Register a custom workload splitter
 */
mvgal_error_t mvgal_scheduler_register_splitter(
    void *context,
    const mvgal_workload_splitter_t *splitter
) {
    (void)context;
    
    if (splitter == NULL || splitter->analyze == NULL || 
        splitter->split == NULL || splitter->merge == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_scheduler_state.splitters_lock);
    
    // Add to splitters list
    mvgal_workload_splitter_t *new_splitters = realloc(
        g_scheduler_state.splitters,
        (g_scheduler_state.splitter_count + 1) * sizeof(mvgal_workload_splitter_t));
    
    if (new_splitters == NULL) {
        pthread_mutex_unlock(&g_scheduler_state.splitters_lock);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    g_scheduler_state.splitters = new_splitters;
    g_scheduler_state.splitters[g_scheduler_state.splitter_count] = *splitter;
    g_scheduler_state.splitter_count++;
    
    pthread_mutex_unlock(&g_scheduler_state.splitters_lock);
    
    MVGAL_LOG_INFO("Custom splitter registered");
    return MVGAL_SUCCESS;
}

/**
 * @brief Unregister a custom workload splitter
 */
mvgal_error_t mvgal_scheduler_unregister_splitter(
    void *context,
    const mvgal_workload_splitter_t *splitter
) {
    (void)context;
    
    if (splitter == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_scheduler_state.splitters_lock);
    
    for (uint32_t i = 0; i < g_scheduler_state.splitter_count; i++) {
        if (&g_scheduler_state.splitters[i] == splitter) {
            // Remove this splitter
            for (uint32_t j = i; j < g_scheduler_state.splitter_count - 1; j++) {
                g_scheduler_state.splitters[j] = g_scheduler_state.splitters[j + 1];
            }
            g_scheduler_state.splitter_count--;
            
            if (g_scheduler_state.splitter_count == 0) {
                free(g_scheduler_state.splitters);
                g_scheduler_state.splitters = NULL;
            }
            
            pthread_mutex_unlock(&g_scheduler_state.splitters_lock);
            MVGAL_LOG_INFO("Custom splitter unregistered");
            return MVGAL_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&g_scheduler_state.splitters_lock);
    return MVGAL_ERROR_INVALID_ARGUMENT; // Splitter not found
}

/**
 * @brief Set GPU priority
 */
mvgal_error_t mvgal_scheduler_set_gpu_priority(
    void *context,
    uint32_t gpu_index,
    uint32_t priority
) {
    (void)context;
    
    if (gpu_index >= MVGAL_SCHEDULER_MAX_GPUS || priority > 100) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    g_scheduler_state.config.gpu_priorities[gpu_index] = priority;
    g_scheduler_state.gpus[gpu_index].priority = priority;
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    MVGAL_LOG_INFO("GPU %u priority set to %u", gpu_index, priority);
    return MVGAL_SUCCESS;
}

/**
 * @brief Get GPU priority
 */
mvgal_error_t mvgal_scheduler_get_gpu_priority(
    void *context,
    uint32_t gpu_index,
    uint32_t *priority
) {
    (void)context;
    
    if (priority == NULL || gpu_index >= MVGAL_SCHEDULER_MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    *priority = g_scheduler_state.config.gpu_priorities[gpu_index];
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Pause scheduling
 */
mvgal_error_t mvgal_scheduler_pause(void *context) {
    (void)context;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    g_scheduler_state.paused = true;
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    MVGAL_LOG_INFO("Scheduler paused");
    return MVGAL_SUCCESS;
}

/**
 * @brief Resume scheduling
 */
mvgal_error_t mvgal_scheduler_resume(void *context) {
    (void)context;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    g_scheduler_state.paused = false;
    pthread_cond_signal(&g_scheduler_state.work_cond);
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    MVGAL_LOG_INFO("Scheduler resumed");
    return MVGAL_SUCCESS;
}

/**
 * @brief Check if scheduler is paused
 */
bool mvgal_scheduler_is_paused(void *context) {
    (void)context;
    
    pthread_mutex_lock(&g_scheduler_state.lock);
    bool paused = g_scheduler_state.paused;
    pthread_mutex_unlock(&g_scheduler_state.lock);
    
    return paused;
}

/**
 * @brief Process pending workloads
 */
mvgal_error_t mvgal_scheduler_process(void *context) {
    (void)context;
    return mvgal_scheduler_process_internal();
}

/**
 * @brief Distribute workload across GPUs using AFR strategy
 */
mvgal_error_t mvgal_distribute_afr(
    void *context,
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices
) {
    (void)context;
    
    if (workload == NULL || gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    
    // Store GPU indices for AFR
    pthread_mutex_lock(&w->mutex);
    w->assigned_gpu_count = gpu_count;
    for (uint32_t i = 0; i < gpu_count; i++) {
        w->assigned_gpus[i] = gpu_indices[i];
    }
    pthread_mutex_unlock(&w->mutex);
    
    return mvgal_scheduler_distribute_afr(w);
}

/**
 * @brief Distribute workload across GPUs using SFR strategy
 */
mvgal_error_t mvgal_distribute_sfr(
    void *context,
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_rect_t *regions,
    uint32_t *region_count
) {
    (void)context;
    (void)regions;
    (void)region_count;
    
    if (workload == NULL || gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    
    // Store GPU indices for SFR
    pthread_mutex_lock(&w->mutex);
    w->assigned_gpu_count = gpu_count;
    for (uint32_t i = 0; i < gpu_count; i++) {
        w->assigned_gpus[i] = gpu_indices[i];
    }
    pthread_mutex_unlock(&w->mutex);
    
    return mvgal_scheduler_distribute_sfr(w);
}

/**
 * @brief Distribute workload based on task type
 */
mvgal_error_t mvgal_distribute_task(
    void *context,
    mvgal_workload_t workload,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    uint64_t *capabilities
) {
    (void)context;
    (void)capabilities;
    
    if (workload == NULL || gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_workload *w = (struct mvgal_workload *)workload;
    
    // Store GPU indices
    pthread_mutex_lock(&w->mutex);
    w->assigned_gpu_count = gpu_count;
    for (uint32_t i = 0; i < gpu_count; i++) {
        w->assigned_gpus[i] = gpu_indices[i];
    }
    pthread_mutex_unlock(&w->mutex);
    
    return mvgal_scheduler_distribute_task(w);
}
