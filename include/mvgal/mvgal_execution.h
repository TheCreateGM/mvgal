/**
 * @file mvgal_execution.h
 * @brief Unified execution and frame orchestration API
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header exposes the execution-planning layer used by API interceptors
 * and higher-level runtimes. It translates intercepted submits/presents into
 * scheduler workloads, tracks frame state, and selects memory migration paths.
 */

#ifndef MVGAL_EXECUTION_H
#define MVGAL_EXECUTION_H

#include "mvgal_types.h"
#include "mvgal_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of GPUs represented in execution plans.
 */
#define MVGAL_EXECUTION_MAX_GPUS 16

/**
 * @brief Memory migration policy.
 */
typedef enum {
    MVGAL_EXECUTION_MIGRATION_AUTO = 0,
    MVGAL_EXECUTION_MIGRATION_MIRROR = 1,
    MVGAL_EXECUTION_MIGRATION_STREAM = 2,
    MVGAL_EXECUTION_MIGRATION_EVICT = 3,
} mvgal_execution_migration_policy_t;

/**
 * @brief Frame/session creation information.
 */
typedef struct {
    mvgal_api_type_t api;                      ///< Submitting API
    mvgal_distribution_strategy_t requested_strategy; ///< Requested strategy
    const char *application_name;             ///< Optional application label
    bool steam_mode;                          ///< Steam-launched workload
    bool proton_mode;                         ///< Proton/Wine translated workload
    bool low_latency;                         ///< Favor frame pacing/latency
} mvgal_execution_frame_begin_info_t;

/**
 * @brief Execution submission information.
 */
typedef struct {
    uint64_t frame_id;                        ///< Frame returned by begin_frame
    mvgal_api_type_t api;                     ///< Source API
    mvgal_distribution_strategy_t requested_strategy; ///< Requested strategy
    mvgal_workload_telemetry_t telemetry;     ///< Intercept telemetry
    size_t resource_bytes;                    ///< Estimated working-set bytes
    uint32_t command_buffer_count;            ///< Number of command buffers
    uint32_t queue_family_flags;              ///< API-specific queue flags
    uint32_t gpu_mask;                        ///< Allowed GPU mask (0 = all)
} mvgal_execution_submit_info_t;

/**
 * @brief Planned execution result for a submission or present.
 */
typedef struct {
    uint64_t frame_id;                        ///< Owning frame
    uint64_t workload_id;                     ///< Scheduler workload ID
    mvgal_api_type_t api;                     ///< Source API
    mvgal_workload_type_t workload_type;      ///< Normalized workload type
    mvgal_distribution_strategy_t requested_strategy; ///< User/requested strategy
    mvgal_distribution_strategy_t applied_strategy;   ///< Applied strategy
    mvgal_memory_copy_method_t migration_method;      ///< Chosen inter-GPU path
    uint32_t selected_gpu_count;              ///< Number of selected GPUs
    uint32_t selected_gpus[MVGAL_EXECUTION_MAX_GPUS]; ///< Selected GPU list
    uint64_t selected_gpu_mask;               ///< Selected GPU mask
    size_t estimated_bytes;                   ///< Estimated bytes touched
    uint64_t estimated_duration_ns;           ///< Estimated execution time
    bool steam_mode;                          ///< Steam workload
    bool proton_mode;                         ///< Proton workload
    bool frame_pacing_enabled;                ///< Frame pacing/compositing needed
    bool cross_vendor;                        ///< More than one vendor selected
} mvgal_execution_plan_t;

/**
 * @brief Frame state exported for diagnostics and tests.
 */
typedef struct {
    uint64_t frame_id;                        ///< Frame identifier
    mvgal_api_type_t api;                     ///< Source API
    mvgal_distribution_strategy_t applied_strategy; ///< Last applied strategy
    uint32_t submit_count;                    ///< Number of submits in frame
    uint32_t present_count;                   ///< Number of presents
    uint32_t selected_gpu_count;              ///< GPUs participating in frame
    uint32_t selected_gpus[MVGAL_EXECUTION_MAX_GPUS]; ///< GPU list
    uint64_t bytes_scheduled;                 ///< Estimated bytes scheduled
    uint64_t bytes_migrated;                  ///< Bytes migrated between GPUs
    uint64_t start_time_ns;                   ///< Frame start timestamp
    uint64_t end_time_ns;                     ///< Frame end timestamp
    bool active;                              ///< True until frame is presented
    bool steam_mode;                          ///< Steam workload
    bool proton_mode;                         ///< Proton workload
    bool low_latency;                         ///< Low-latency pacing enabled
    char application_name[128];               ///< Associated application label
} mvgal_execution_frame_stats_t;

/**
 * @brief Explicit memory migration request.
 */
typedef struct {
    mvgal_buffer_t src_buffer;                ///< Source buffer
    uint64_t src_offset;                      ///< Source offset
    mvgal_buffer_t dst_buffer;                ///< Destination buffer
    uint64_t dst_offset;                      ///< Destination offset
    size_t size;                              ///< Transfer size
    uint32_t src_gpu_index;                   ///< Source GPU
    uint32_t dst_gpu_index;                   ///< Destination GPU
    mvgal_execution_migration_policy_t policy; ///< Requested migration policy
    bool prefer_zero_copy;                    ///< Prefer DMA-BUF/P2P if possible
    bool allow_cpu_fallback;                  ///< Allow host-mediated fallback
} mvgal_execution_migration_info_t;

/**
 * @brief Explicit memory migration result.
 */
typedef struct {
    mvgal_memory_copy_method_t method;        ///< Copy method used
    size_t bytes_migrated;                    ///< Bytes migrated
    uint32_t src_gpu_index;                   ///< Source GPU
    uint32_t dst_gpu_index;                   ///< Destination GPU
    bool zero_copy;                           ///< Zero-copy capable path selected
    bool cross_vendor;                        ///< Source/destination vendors differ
} mvgal_execution_migration_result_t;

/**
 * @brief Steam/Proton profile request.
 */
typedef struct {
    const char *application_name;             ///< Optional game/application name
    mvgal_distribution_strategy_t preferred_strategy; ///< Preferred strategy
    bool steam_mode;                          ///< Generate Steam profile
    bool proton_mode;                         ///< Generate Proton profile
    bool enable_vulkan_layer;                 ///< Include Vulkan layer variables
    bool enable_d3d_wrapper;                  ///< Include D3D wrapper variables
    bool low_latency;                         ///< Request frame pacing profile
} mvgal_steam_profile_request_t;

/**
 * @brief Steam/Proton launch profile.
 */
typedef struct {
    mvgal_distribution_strategy_t strategy;   ///< Selected strategy
    uint32_t gpu_count;                       ///< GPUs included in profile
    uint32_t gpu_indices[MVGAL_EXECUTION_MAX_GPUS]; ///< GPU list
    bool steam_mode;                          ///< Steam profile generated
    bool proton_mode;                         ///< Proton profile generated
    bool low_latency;                         ///< Low-latency profile
    char env_block[1024];                     ///< Multi-line environment block
    char launch_options[1024];                ///< Single-line Steam launch string
} mvgal_steam_profile_t;

mvgal_error_t mvgal_execution_begin_frame(
    mvgal_context_t context,
    const mvgal_execution_frame_begin_info_t *info,
    uint64_t *frame_id
);

mvgal_error_t mvgal_execution_submit(
    mvgal_context_t context,
    const mvgal_execution_submit_info_t *info,
    mvgal_execution_plan_t *plan
);

mvgal_error_t mvgal_execution_present(
    mvgal_context_t context,
    uint64_t frame_id,
    mvgal_api_type_t api,
    mvgal_execution_plan_t *plan
);

mvgal_error_t mvgal_execution_get_frame_stats(
    uint64_t frame_id,
    mvgal_execution_frame_stats_t *stats
);

mvgal_error_t mvgal_execution_migrate_memory(
    mvgal_context_t context,
    const mvgal_execution_migration_info_t *info,
    mvgal_execution_migration_result_t *result
);

mvgal_error_t mvgal_execution_get_steam_profile(
    const mvgal_steam_profile_request_t *request,
    mvgal_steam_profile_t *profile
);

#ifdef __cplusplus
}
#endif

#endif // MVGAL_EXECUTION_H
