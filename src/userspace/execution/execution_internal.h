/**
 * @file execution_internal.h
 * @brief Internal execution engine declarations
 */

#ifndef MVGAL_EXECUTION_INTERNAL_H
#define MVGAL_EXECUTION_INTERNAL_H

#include "mvgal/mvgal_execution.h"
#include "../scheduler/scheduler_internal.h"
#include "../memory/memory_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MVGAL_EXECUTION_MAX_FRAMES 64

typedef struct {
    bool in_use;
    uint64_t frame_id;
    mvgal_context_t context;
    mvgal_api_type_t api;
    mvgal_distribution_strategy_t requested_strategy;
    mvgal_distribution_strategy_t applied_strategy;
    uint32_t submit_count;
    uint32_t present_count;
    uint32_t selected_gpu_count;
    uint32_t selected_gpus[MVGAL_EXECUTION_MAX_GPUS];
    uint64_t bytes_scheduled;
    uint64_t bytes_migrated;
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    bool active;
    bool steam_mode;
    bool proton_mode;
    bool low_latency;
    char application_name[128];
    mvgal_execution_plan_t last_plan;
} mvgal_execution_frame_state_t;

typedef struct {
    pthread_mutex_t lock;
    bool initialized;
    uint64_t next_frame_id;
    mvgal_execution_frame_state_t frames[MVGAL_EXECUTION_MAX_FRAMES];
    mvgal_stats_t stats;
} mvgal_execution_state_t;

mvgal_execution_state_t *mvgal_execution_get_state(void);
mvgal_error_t mvgal_execution_module_init(void);
void mvgal_execution_module_shutdown(void);
mvgal_error_t mvgal_execution_get_stats_internal(mvgal_stats_t *stats);
void mvgal_execution_reset_stats_internal(void);

#ifdef __cplusplus
}
#endif

#endif // MVGAL_EXECUTION_INTERNAL_H
