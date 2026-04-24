/**
 * @file scheduler_internal.h
 * @brief Internal scheduler types and declarations
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header defines internal structures for the scheduler module.
 * Not part of the public API.
 */

#ifndef MVGAL_SCHEDULER_INTERNAL_H
#define MVGAL_SCHEDULER_INTERNAL_H

#include "mvgal_scheduler.h"
#include "mvgal_gpu.h"
#include "mvgal_memory.h"
#include <pthread.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of GPUs supported by scheduler
 */
#define MVGAL_SCHEDULER_MAX_GPUS 16

/**
 * @brief Maximum number of queued workloads
 */
#define MVGAL_SCHEDULER_MAX_QUEUED 1024

/**
 * @brief Workload state
 */
typedef enum {
    MVGAL_WORKLOAD_STATE_PENDING,     ///< Waiting to be scheduled
    MVGAL_WORKLOAD_STATE_QUEUED,      ///< Queued for execution
    MVGAL_WORKLOAD_STATE_RUNNING,     ///< Currently executing
    MVGAL_WORKLOAD_STATE_COMPLETED,   ///< Completed successfully
    MVGAL_WORKLOAD_STATE_FAILED,      ///< Failed
    MVGAL_WORKLOAD_STATE_CANCELLED,   ///< Cancelled before execution
} mvgal_workload_state_t;

/**
 * @brief Internal workload structure
 */
struct mvgal_workload {
    // Reference counting
    atomic_uint refcount;
    
    // State
    mvgal_workload_state_t state;
    
    // Descriptor (public info)
    mvgal_workload_descriptor_t descriptor;
    
    // Context (from submission)
    void *context;
    mvgal_workload_callback_t callback;
    void *user_data;
    
    // GPU assignment
    uint32_t assigned_gpu_count;
    uint32_t assigned_gpus[MVGAL_SCHEDULER_MAX_GPUS];
    
    // Scheduling info
    uint64_t queue_time;           ///< When it was queued (ns)
    uint64_t start_time;           ///< When it started (ns)
    uint64_t end_time;             ///< When it completed (ns)
    
    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool completion_signaled;
    mvgal_error_t completion_status;
    
    // Statistics tracking
    uint64_t gpu_execution_time_ns[MVGAL_SCHEDULER_MAX_GPUS];
    
    // Next/prev for queue management
    struct mvgal_workload *next;
    struct mvgal_workload *prev;
    
    // Strategy-specific data (opaque)
    void *strategy_data;
};

/**
 * @brief Workload queue
 */
typedef struct {
    pthread_mutex_t lock;
    struct mvgal_workload *head;
    struct mvgal_workload *tail;
    uint32_t count;
    uint64_t total_priority;
} mvgal_workload_queue_t;

/**
 * @brief GPU state for scheduling
 */
typedef struct {
    bool available;
    bool enabled;
    float utilization;           ///< 0.0-1.0
    float temperature;           ///< Celsius
    float power_usage_w;         ///< Watts
    uint64_t estimated_free_time_ns; ///< When GPU will be free
    
    // Capabilities
    uint64_t features;
    float compute_score;
    float graphics_score;
    mvgal_gpu_descriptor_t gpu_info;
    
    // Workload queue for this GPU
    mvgal_workload_queue_t queue;
    
    // Statistics
    uint64_t workloads_completed;
    uint64_t total_execution_time_ns;
    
    // Priority
    uint32_t priority;            ///< 0-100 for scheduling preference
} mvgal_gpu_state_t;

/**
 * @brief Scheduler configuration (extract from public config)
 */
typedef struct {
    mvgal_distribution_strategy_t strategy;
    bool dynamic_load_balance;
    bool thermal_aware;
    bool power_aware;
    float load_balance_threshold;
    uint32_t max_queued_workloads;
    uint64_t quantum_ns;              ///< Scheduling quantum in ns
    
    // GPU priorities
    uint32_t gpu_priorities[MVGAL_SCHEDULER_MAX_GPUS];
} mvgal_scheduler_config_internal_t;

/**
 * @brief Scheduler global state
 */
typedef struct {
    pthread_mutex_t lock;
    bool initialized;
    bool paused;
    bool running;
    
    // Configuration
    mvgal_scheduler_config_internal_t config;
    
    // GPU state
    mvgal_gpu_state_t gpus[MVGAL_SCHEDULER_MAX_GPUS];
    uint32_t gpu_count;
    
    // Workload queues
    mvgal_workload_queue_t global_queue;     ///< All workloads
    mvgal_workload_queue_t ready_queue;      ///< Ready to schedule
    mvgal_workload_queue_t running_queue;    ///< Currently running
    mvgal_workload_queue_t completed_queue;  ///< Completed
    
    // Statistics
    mvgal_scheduler_stats_t stats;
    
    // Custom splitters
    mvgal_workload_splitter_t *splitters;
    uint32_t splitter_count;
    pthread_mutex_t splitters_lock;
    
    // Thread management
    pthread_t scheduler_thread;
    bool thread_running;
    pthread_cond_t work_cond;
    
    // Workload ID counter
    atomic_ullong next_workload_id;
} mvgal_scheduler_state_t;

/**
 * @brief Get the global scheduler state
 */
mvgal_scheduler_state_t *mvgal_scheduler_get_state(void);

/**
 * @brief Initialize scheduler module
 */
mvgal_error_t mvgal_scheduler_module_init(void);

/**
 * @brief Shutdown scheduler module
 */
void mvgal_scheduler_module_shutdown(void);

/**
 * @brief Create a workload structure
 */
struct mvgal_workload *mvgal_workload_create_internal(
    const mvgal_workload_submit_info_t *info
);

/**
 * @brief Destroy a workload structure
 */
void mvgal_workload_destroy_internal(struct mvgal_workload *workload);

/**
 * @brief Increment workload reference count
 */
void mvgal_workload_retain(struct mvgal_workload *workload);

/**
 * @brief Decrement workload reference count and possibly free
 */
void mvgal_workload_release(struct mvgal_workload *workload);

/**
 * @brief Queue a workload
 */
mvgal_error_t mvgal_workload_queue(
    struct mvgal_workload *workload,
    mvgal_workload_queue_t *queue
);

/**
 * @brief Dequeue a workload
 */
struct mvgal_workload *mvgal_workload_dequeue(
    mvgal_workload_queue_t *queue
);

/**
 * @brief Dequeue the highest priority workload
 */
struct mvgal_workload *mvgal_workload_dequeue_priority(
    mvgal_workload_queue_t *queue
);

/**
 * @brief Find the best GPU for a workload
 */
mvgal_error_t mvgal_scheduler_find_best_gpu(
    struct mvgal_workload *workload,
    uint32_t *gpu_index
);

/**
 * @brief Apply distribution strategy
 */
mvgal_error_t mvgal_scheduler_apply_strategy(
    struct mvgal_workload *workload
);

/**
 * @brief Distribute workload using AFR strategy
 */
mvgal_error_t mvgal_scheduler_distribute_afr(
    struct mvgal_workload *workload
);

/**
 * @brief Distribute workload using SFR strategy
 */
mvgal_error_t mvgal_scheduler_distribute_sfr(
    struct mvgal_workload *workload
);

/**
 * @brief Distribute workload using task-based strategy
 */
mvgal_error_t mvgal_scheduler_distribute_task(
    struct mvgal_workload *workload
);

/**
 * @brief Distribute workload using compute offload strategy
 */
mvgal_error_t mvgal_scheduler_distribute_compute_offload(
    struct mvgal_workload *workload
);

/**
 * @brief Mark workload as completed
 */
void mvgal_workload_complete(
    struct mvgal_workload *workload,
    mvgal_error_t result
);

/**
 * @brief Scheduler main loop (runs in separate thread)
 */
void *mvgal_scheduler_thread(void *arg);

/**
 * @brief Process pending workloads (for non-threaded mode)
 */
mvgal_error_t mvgal_scheduler_process_internal(void);

/**
 * @brief Balance load across GPUs
 */
void mvgal_scheduler_balance_load(void);

/**
 * @brief Check if GPU is suitable for workload
 */
bool mvgal_scheduler_gpu_suitable(
    uint32_t gpu_index,
    struct mvgal_workload *workload
);

/**
 * @brief Calculate workload score for GPU
 */
float mvgal_scheduler_calculate_score(
    uint32_t gpu_index,
    struct mvgal_workload *workload
);

/**
 * @brief Notify workload completion
 */
void mvgal_workload_signal_completion(
    struct mvgal_workload *workload,
    mvgal_error_t result
);

#ifdef __cplusplus
}
#endif

#endif // MVGAL_SCHEDULER_INTERNAL_H
