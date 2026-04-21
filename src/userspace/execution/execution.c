/**
 * @file execution.c
 * @brief Unified execution planning and frame orchestration
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 */

#include "execution_internal.h"

#include "mvgal.h"
#include "mvgal_gpu.h"
#include "mvgal_log.h"
#include "mvgal_scheduler.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static mvgal_execution_state_t g_execution_state = {0};

static uint64_t execution_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

mvgal_execution_state_t *mvgal_execution_get_state(void) {
    return &g_execution_state;
}

static bool env_enabled(const char *name) {
    const char *value = getenv(name);
    if (value == NULL) {
        return false;
    }
    return strcmp(value, "0") != 0 && value[0] != '\0';
}

static bool detect_steam_mode(void) {
    return env_enabled("STEAM_GAME_ID") ||
           env_enabled("SteamGameId") ||
           env_enabled("STEAM_COMPAT_CLIENT_INSTALL_PATH");
}

static bool detect_proton_mode(void) {
    return env_enabled("STEAM_COMPAT_DATA_PATH") ||
           env_enabled("PROTON_VERSION") ||
           env_enabled("WINEPREFIX");
}

static const char *strategy_to_string(mvgal_distribution_strategy_t strategy) {
    switch (strategy) {
        case MVGAL_STRATEGY_AFR:
            return "afr";
        case MVGAL_STRATEGY_SFR:
            return "sfr";
        case MVGAL_STRATEGY_TASK:
            return "task";
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD:
            return "compute_offload";
        case MVGAL_STRATEGY_HYBRID:
            return "hybrid";
        case MVGAL_STRATEGY_SINGLE_GPU:
            return "single";
        case MVGAL_STRATEGY_ROUND_ROBIN:
            return "round_robin";
        default:
            return "hybrid";
    }
}

static uint32_t build_enabled_gpu_mask(uint32_t *gpu_list, uint32_t *gpu_count_out) {
    uint32_t gpu_count = 0;
    uint32_t gpu_mask = 0;
    int32_t count = mvgal_gpu_get_count();

    if (count <= 0) {
        if (gpu_count_out != NULL) {
            *gpu_count_out = 0;
        }
        return 0;
    }

    for (int32_t i = 0; i < count && gpu_count < MVGAL_EXECUTION_MAX_GPUS; i++) {
        if (!mvgal_gpu_is_enabled((uint32_t)i)) {
            continue;
        }
        gpu_mask |= (1U << (uint32_t)i);
        if (gpu_list != NULL) {
            gpu_list[gpu_count] = (uint32_t)i;
        }
        gpu_count++;
    }

    if (gpu_count_out != NULL) {
        *gpu_count_out = gpu_count;
    }
    return gpu_mask;
}

static mvgal_error_t ensure_execution_initialized(void) {
    if (!mvgal_is_initialized()) {
        mvgal_error_t err = mvgal_init(0);
        if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
            return err;
        }
    }

    if (!g_execution_state.initialized) {
        return mvgal_execution_module_init();
    }

    return MVGAL_SUCCESS;
}

static mvgal_error_t resolve_context(mvgal_context_t *context) {
    if (context == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (*context != NULL) {
        return MVGAL_SUCCESS;
    }

    *context = mvgal_context_get_current();
    if (*context != NULL) {
        return MVGAL_SUCCESS;
    }

    mvgal_error_t err = mvgal_context_create(context);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    return mvgal_context_set_current(*context);
}

static mvgal_execution_frame_state_t *find_frame_state(uint64_t frame_id) {
    for (uint32_t i = 0; i < MVGAL_EXECUTION_MAX_FRAMES; i++) {
        if (g_execution_state.frames[i].in_use &&
            g_execution_state.frames[i].frame_id == frame_id) {
            return &g_execution_state.frames[i];
        }
    }
    return NULL;
}

static mvgal_execution_frame_state_t *allocate_frame_state(void) {
    mvgal_execution_frame_state_t *replacement = &g_execution_state.frames[0];

    for (uint32_t i = 0; i < MVGAL_EXECUTION_MAX_FRAMES; i++) {
        if (!g_execution_state.frames[i].in_use) {
            return &g_execution_state.frames[i];
        }
        if (!g_execution_state.frames[i].active) {
            replacement = &g_execution_state.frames[i];
        }
    }

    return replacement;
}

static mvgal_workload_type_t normalize_workload_type(
    const mvgal_execution_submit_info_t *info
) {
    if (info->telemetry.type != MVGAL_WORKLOAD_UNKNOWN) {
        return info->telemetry.type;
    }

    if ((info->queue_family_flags & 0x1U) != 0U ||
        info->telemetry.flags.is_present) {
        return MVGAL_WORKLOAD_GRAPHICS;
    }

    if ((info->queue_family_flags & 0x2U) != 0U) {
        return MVGAL_WORKLOAD_COMPUTE;
    }

    if (info->telemetry.data_size > 0) {
        return MVGAL_WORKLOAD_TRANSFER;
    }

    return MVGAL_WORKLOAD_GRAPHICS;
}

static uint64_t estimate_duration_ns(
    mvgal_workload_type_t type,
    size_t resource_bytes,
    uint32_t command_buffer_count
) {
    uint64_t base_ns = 500000ULL;

    switch (type) {
        case MVGAL_WORKLOAD_GRAPHICS:
        case MVGAL_WORKLOAD_VULKAN:
        case MVGAL_WORKLOAD_VULKAN_CMD:
        case MVGAL_WORKLOAD_D3D_QUEUE:
        case MVGAL_WORKLOAD_D3D_PIPELINE:
            base_ns = 2000000ULL;
            break;
        case MVGAL_WORKLOAD_COMPUTE:
        case MVGAL_WORKLOAD_AI:
        case MVGAL_WORKLOAD_CUDA_KERNEL:
        case MVGAL_WORKLOAD_OPENCL_KERNEL:
            base_ns = 1500000ULL;
            break;
        case MVGAL_WORKLOAD_TRANSFER:
            base_ns = 800000ULL;
            break;
        default:
            break;
    }

    base_ns += (uint64_t)command_buffer_count * 250000ULL;
    base_ns += ((uint64_t)resource_bytes / (1024ULL * 1024ULL)) * 150000ULL;

    return base_ns;
}

static mvgal_distribution_strategy_t choose_strategy(
    mvgal_context_t context,
    mvgal_execution_frame_state_t *frame,
    const mvgal_execution_submit_info_t *info
) {
    uint32_t enabled_gpu_count = 0;
    mvgal_distribution_strategy_t requested = info->requested_strategy;
    mvgal_workload_type_t workload_type = normalize_workload_type(info);

    (void)build_enabled_gpu_mask(NULL, &enabled_gpu_count);

    if (requested == MVGAL_STRATEGY_AUTO) {
        requested = (frame != NULL) ? frame->requested_strategy : mvgal_get_strategy(context);
    }

    if (requested == MVGAL_STRATEGY_AUTO) {
        requested = MVGAL_STRATEGY_HYBRID;
    }

    if (enabled_gpu_count <= 1U &&
        (requested == MVGAL_STRATEGY_AFR ||
         requested == MVGAL_STRATEGY_SFR ||
         requested == MVGAL_STRATEGY_TASK ||
         requested == MVGAL_STRATEGY_HYBRID ||
         requested == MVGAL_STRATEGY_COMPUTE_OFFLOAD)) {
        return MVGAL_STRATEGY_SINGLE_GPU;
    }

    if (requested != MVGAL_STRATEGY_HYBRID) {
        return requested;
    }

    if (frame != NULL &&
        (frame->steam_mode || frame->proton_mode) &&
        workload_type == MVGAL_WORKLOAD_GRAPHICS &&
        enabled_gpu_count > 1U) {
        return MVGAL_STRATEGY_AFR;
    }

    if ((workload_type == MVGAL_WORKLOAD_COMPUTE ||
         workload_type == MVGAL_WORKLOAD_AI) &&
        enabled_gpu_count > 1U) {
        return MVGAL_STRATEGY_COMPUTE_OFFLOAD;
    }

    if (workload_type == MVGAL_WORKLOAD_TRANSFER && enabled_gpu_count > 1U) {
        return MVGAL_STRATEGY_TASK;
    }

    if (workload_type == MVGAL_WORKLOAD_GRAPHICS &&
        info->resource_bytes >= 64U * 1024U * 1024U &&
        enabled_gpu_count > 1U) {
        return MVGAL_STRATEGY_SFR;
    }

    if (enabled_gpu_count > 1U) {
        return MVGAL_STRATEGY_AFR;
    }

    return MVGAL_STRATEGY_SINGLE_GPU;
}

static bool detect_cross_vendor(uint32_t gpu_count, const uint32_t *gpu_indices) {
    mvgal_vendor_t first_vendor = MVGAL_VENDOR_UNKNOWN;

    for (uint32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t gpu;
        if (mvgal_gpu_get_descriptor(gpu_indices[i], &gpu) != MVGAL_SUCCESS) {
            continue;
        }

        if (first_vendor == MVGAL_VENDOR_UNKNOWN) {
            first_vendor = gpu.vendor;
            continue;
        }

        if (gpu.vendor != first_vendor) {
            return true;
        }
    }

    return false;
}

static void populate_plan_gpu_selection(
    mvgal_execution_plan_t *plan,
    struct mvgal_workload *workload
) {
    plan->selected_gpu_count = 0;
    plan->selected_gpu_mask = 0;

    if (workload->assigned_gpu_count > 0) {
        plan->selected_gpu_count = workload->assigned_gpu_count;
        for (uint32_t i = 0; i < workload->assigned_gpu_count; i++) {
            plan->selected_gpus[i] = workload->assigned_gpus[i];
            plan->selected_gpu_mask |= (1ULL << workload->assigned_gpus[i]);
        }
        return;
    }

    if (workload->descriptor.assigned_gpu != 0xFFFFFFFFU) {
        plan->selected_gpu_count = 1;
        plan->selected_gpus[0] = workload->descriptor.assigned_gpu;
        plan->selected_gpu_mask = (1ULL << workload->descriptor.assigned_gpu);
        return;
    }

    uint32_t enabled_gpu_count = 0;
    uint32_t enabled_gpus[MVGAL_EXECUTION_MAX_GPUS] = {0};
    build_enabled_gpu_mask(enabled_gpus, &enabled_gpu_count);
    if (enabled_gpu_count > 0U) {
        plan->selected_gpu_count = 1;
        plan->selected_gpus[0] = enabled_gpus[0];
        plan->selected_gpu_mask = (1ULL << enabled_gpus[0]);
    }
}

static mvgal_error_t submit_scheduler_workload(
    mvgal_context_t context,
    const mvgal_execution_submit_info_t *info,
    mvgal_execution_plan_t *plan
) {
    mvgal_workload_submit_info_t submit_info;
    mvgal_workload_t workload = NULL;
    mvgal_distribution_strategy_t previous_strategy;
    mvgal_error_t err;
    struct mvgal_workload *internal_workload;

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.type = plan->workload_type;
    submit_info.priority = info->telemetry.flags.is_present ? 95U :
                           (info->telemetry.flags.is_frame_end ? 90U : 70U);
    submit_info.deadline = info->telemetry.flags.is_present ? execution_get_time_ns() + 16000000ULL : 0;
    submit_info.gpu_mask = (info->gpu_mask != 0U) ? info->gpu_mask : build_enabled_gpu_mask(NULL, NULL);
    submit_info.dependency_count = 0;
    submit_info.dependencies = NULL;
    submit_info.user_data = NULL;

    previous_strategy = mvgal_scheduler_get_strategy(context);
    mvgal_scheduler_set_strategy(context, plan->applied_strategy);
    err = mvgal_workload_submit(context, &submit_info, &workload);
    mvgal_scheduler_set_strategy(context, previous_strategy);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    internal_workload = (struct mvgal_workload *)workload;
    plan->workload_id = internal_workload->descriptor.id;
    populate_plan_gpu_selection(plan, internal_workload);
    plan->cross_vendor = detect_cross_vendor(plan->selected_gpu_count, plan->selected_gpus);
    mvgal_workload_destroy(workload);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_execution_module_init(void) {
    if (g_execution_state.initialized) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }

    pthread_mutex_init(&g_execution_state.lock, NULL);
    memset(&g_execution_state.stats, 0, sizeof(g_execution_state.stats));
    memset(g_execution_state.frames, 0, sizeof(g_execution_state.frames));
    g_execution_state.next_frame_id = 1;
    g_execution_state.initialized = true;

    MVGAL_LOG_INFO("Execution module initialized");
    return MVGAL_SUCCESS;
}

void mvgal_execution_module_shutdown(void) {
    if (!g_execution_state.initialized) {
        return;
    }

    pthread_mutex_destroy(&g_execution_state.lock);
    memset(&g_execution_state, 0, sizeof(g_execution_state));
}

mvgal_error_t mvgal_execution_get_stats_internal(mvgal_stats_t *stats) {
    if (stats == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (!g_execution_state.initialized) {
        memset(stats, 0, sizeof(*stats));
        return MVGAL_SUCCESS;
    }

    pthread_mutex_lock(&g_execution_state.lock);
    *stats = g_execution_state.stats;
    pthread_mutex_unlock(&g_execution_state.lock);

    return MVGAL_SUCCESS;
}

void mvgal_execution_reset_stats_internal(void) {
    if (!g_execution_state.initialized) {
        return;
    }

    pthread_mutex_lock(&g_execution_state.lock);
    memset(&g_execution_state.stats, 0, sizeof(g_execution_state.stats));
    pthread_mutex_unlock(&g_execution_state.lock);
}

mvgal_error_t mvgal_execution_begin_frame(
    mvgal_context_t context,
    const mvgal_execution_frame_begin_info_t *info,
    uint64_t *frame_id
) {
    mvgal_execution_frame_state_t *frame;
    mvgal_error_t err;

    if (info == NULL || frame_id == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    err = ensure_execution_initialized();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        return err;
    }

    err = resolve_context(&context);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    pthread_mutex_lock(&g_execution_state.lock);

    frame = allocate_frame_state();
    memset(frame, 0, sizeof(*frame));
    frame->in_use = true;
    frame->active = true;
    frame->frame_id = g_execution_state.next_frame_id++;
    frame->context = context;
    frame->api = info->api;
    frame->requested_strategy = (info->requested_strategy == MVGAL_STRATEGY_AUTO) ?
        mvgal_get_strategy(context) : info->requested_strategy;
    frame->applied_strategy = frame->requested_strategy;
    frame->steam_mode = info->steam_mode || detect_steam_mode();
    frame->proton_mode = info->proton_mode || detect_proton_mode();
    frame->low_latency = info->low_latency || frame->steam_mode || frame->proton_mode;
    frame->start_time_ns = execution_get_time_ns();
    frame->end_time_ns = 0;
    if (info->application_name != NULL) {
        snprintf(frame->application_name, sizeof(frame->application_name), "%s", info->application_name);
    } else if (frame->proton_mode) {
        snprintf(frame->application_name, sizeof(frame->application_name), "proton");
    } else if (frame->steam_mode) {
        snprintf(frame->application_name, sizeof(frame->application_name), "steam");
    } else {
        snprintf(frame->application_name, sizeof(frame->application_name), "mvgal");
    }

    g_execution_state.stats.frames_submitted++;
    *frame_id = frame->frame_id;

    pthread_mutex_unlock(&g_execution_state.lock);

    MVGAL_LOG_DEBUG("Execution frame %" PRIu64 " started for %s", frame->frame_id, frame->application_name);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_execution_submit(
    mvgal_context_t context,
    const mvgal_execution_submit_info_t *info,
    mvgal_execution_plan_t *plan
) {
    mvgal_execution_frame_state_t *frame;
    mvgal_execution_submit_info_t local_info;
    mvgal_error_t err;
    uint64_t frame_id = 0;

    if (info == NULL || plan == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    err = ensure_execution_initialized();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        return err;
    }

    err = resolve_context(&context);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    local_info = *info;
    if (local_info.frame_id == 0) {
        mvgal_execution_frame_begin_info_t begin_info = {
            .api = local_info.api,
            .requested_strategy = local_info.requested_strategy,
            .application_name = local_info.telemetry.step_name,
            .steam_mode = detect_steam_mode(),
            .proton_mode = detect_proton_mode(),
            .low_latency = local_info.telemetry.flags.is_present
        };

        err = mvgal_execution_begin_frame(context, &begin_info, &frame_id);
        if (err != MVGAL_SUCCESS) {
            return err;
        }
        local_info.frame_id = frame_id;
    }

    pthread_mutex_lock(&g_execution_state.lock);
    frame = find_frame_state(local_info.frame_id);
    if (frame == NULL) {
        pthread_mutex_unlock(&g_execution_state.lock);
        return MVGAL_ERROR_NOT_FOUND;
    }

    memset(plan, 0, sizeof(*plan));
    plan->frame_id = frame->frame_id;
    plan->api = local_info.api;
    plan->workload_type = normalize_workload_type(&local_info);
    plan->requested_strategy = (local_info.requested_strategy == MVGAL_STRATEGY_AUTO) ?
        frame->requested_strategy : local_info.requested_strategy;
    plan->applied_strategy = choose_strategy(context, frame, &local_info);
    plan->estimated_bytes = local_info.resource_bytes + local_info.telemetry.data_size;
    plan->estimated_duration_ns = estimate_duration_ns(
        plan->workload_type,
        plan->estimated_bytes,
        local_info.command_buffer_count
    );
    plan->steam_mode = frame->steam_mode;
    plan->proton_mode = frame->proton_mode;
    plan->frame_pacing_enabled = frame->low_latency &&
        (plan->applied_strategy == MVGAL_STRATEGY_AFR ||
         plan->applied_strategy == MVGAL_STRATEGY_SFR);
    pthread_mutex_unlock(&g_execution_state.lock);

    err = submit_scheduler_workload(context, &local_info, plan);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    if (plan->selected_gpu_count > 1U) {
        plan->migration_method = mvgal_dmabuf_get_copy_method(
            plan->selected_gpus[0], plan->selected_gpus[1]
        );
    } else {
        plan->migration_method = MVGAL_MEMORY_COPY_CPU;
    }

    pthread_mutex_lock(&g_execution_state.lock);
    frame = find_frame_state(local_info.frame_id);
    if (frame != NULL) {
        frame->submit_count++;
        frame->bytes_scheduled += plan->estimated_bytes;
        frame->applied_strategy = plan->applied_strategy;
        frame->selected_gpu_count = plan->selected_gpu_count;
        memcpy(frame->selected_gpus, plan->selected_gpus, sizeof(frame->selected_gpus));
        frame->last_plan = *plan;
    }

    g_execution_state.stats.workloads_distributed++;
    switch (local_info.api) {
        case MVGAL_API_VULKAN:
            g_execution_state.stats.vulkan_workloads++;
            break;
        case MVGAL_API_CUDA:
            g_execution_state.stats.cuda_kernels++;
            break;
        case MVGAL_API_OPENCL:
            g_execution_state.stats.opencl_kernels++;
            break;
        case MVGAL_API_D3D11:
        case MVGAL_API_D3D12:
            g_execution_state.stats.d3d_workloads++;
            break;
        case MVGAL_API_METAL:
            g_execution_state.stats.metal_workloads++;
            break;
        case MVGAL_API_WEBGPU:
            g_execution_state.stats.webgpu_workloads++;
            break;
        default:
            break;
    }
    pthread_mutex_unlock(&g_execution_state.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_execution_present(
    mvgal_context_t context,
    uint64_t frame_id,
    mvgal_api_type_t api,
    mvgal_execution_plan_t *plan
) {
    mvgal_execution_frame_state_t *frame;
    mvgal_error_t err;

    if (plan == NULL || frame_id == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    err = ensure_execution_initialized();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        return err;
    }

    err = resolve_context(&context);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    pthread_mutex_lock(&g_execution_state.lock);
    frame = find_frame_state(frame_id);
    if (frame == NULL) {
        pthread_mutex_unlock(&g_execution_state.lock);
        return MVGAL_ERROR_NOT_FOUND;
    }

    memset(plan, 0, sizeof(*plan));
    plan->frame_id = frame->frame_id;
    plan->api = api;
    plan->workload_type = MVGAL_WORKLOAD_GRAPHICS;
    plan->requested_strategy = frame->requested_strategy;
    plan->applied_strategy = frame->applied_strategy;
    plan->estimated_bytes = (size_t)frame->bytes_scheduled;
    plan->estimated_duration_ns = estimate_duration_ns(
        MVGAL_WORKLOAD_GRAPHICS,
        plan->estimated_bytes,
        frame->submit_count
    );
    plan->steam_mode = frame->steam_mode;
    plan->proton_mode = frame->proton_mode;
    plan->frame_pacing_enabled = frame->low_latency &&
        frame->selected_gpu_count > 1U &&
        (frame->applied_strategy == MVGAL_STRATEGY_AFR ||
         frame->applied_strategy == MVGAL_STRATEGY_SFR);
    plan->selected_gpu_count = frame->selected_gpu_count;
    memcpy(plan->selected_gpus, frame->selected_gpus, sizeof(plan->selected_gpus));
    for (uint32_t i = 0; i < plan->selected_gpu_count; i++) {
        plan->selected_gpu_mask |= (1ULL << plan->selected_gpus[i]);
    }
    plan->cross_vendor = detect_cross_vendor(plan->selected_gpu_count, plan->selected_gpus);
    if (plan->selected_gpu_count > 1U) {
        plan->migration_method = mvgal_dmabuf_get_copy_method(
            plan->selected_gpus[0], plan->selected_gpus[1]
        );
    } else {
        plan->migration_method = MVGAL_MEMORY_COPY_CPU;
    }

    frame->present_count++;
    frame->active = false;
    frame->end_time_ns = execution_get_time_ns();
    frame->last_plan = *plan;
    g_execution_state.stats.frames_completed++;
    pthread_mutex_unlock(&g_execution_state.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_execution_get_frame_stats(
    uint64_t frame_id,
    mvgal_execution_frame_stats_t *stats
) {
    mvgal_execution_frame_state_t *frame;

    if (stats == NULL || frame_id == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (!g_execution_state.initialized) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }

    pthread_mutex_lock(&g_execution_state.lock);
    frame = find_frame_state(frame_id);
    if (frame == NULL) {
        pthread_mutex_unlock(&g_execution_state.lock);
        return MVGAL_ERROR_NOT_FOUND;
    }

    memset(stats, 0, sizeof(*stats));
    stats->frame_id = frame->frame_id;
    stats->api = frame->api;
    stats->applied_strategy = frame->applied_strategy;
    stats->submit_count = frame->submit_count;
    stats->present_count = frame->present_count;
    stats->selected_gpu_count = frame->selected_gpu_count;
    memcpy(stats->selected_gpus, frame->selected_gpus, sizeof(stats->selected_gpus));
    stats->bytes_scheduled = frame->bytes_scheduled;
    stats->bytes_migrated = frame->bytes_migrated;
    stats->start_time_ns = frame->start_time_ns;
    stats->end_time_ns = frame->end_time_ns;
    stats->active = frame->active;
    stats->steam_mode = frame->steam_mode;
    stats->proton_mode = frame->proton_mode;
    stats->low_latency = frame->low_latency;
    memcpy(stats->application_name, frame->application_name, sizeof(stats->application_name));
    pthread_mutex_unlock(&g_execution_state.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_execution_migrate_memory(
    mvgal_context_t context,
    const mvgal_execution_migration_info_t *info,
    mvgal_execution_migration_result_t *result
) {
    mvgal_error_t err;
    mvgal_memory_copy_method_t method;
    mvgal_gpu_descriptor_t src_gpu;
    mvgal_gpu_descriptor_t dst_gpu;
    bool have_src_gpu = false;
    bool have_dst_gpu = false;

    if (info == NULL || result == NULL ||
        info->src_buffer == NULL || info->dst_buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    err = ensure_execution_initialized();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        return err;
    }

    err = resolve_context(&context);
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    method = mvgal_dmabuf_get_copy_method(info->src_gpu_index, info->dst_gpu_index);
    if (info->prefer_zero_copy &&
        !info->allow_cpu_fallback &&
        method == MVGAL_MEMORY_COPY_CPU &&
        info->src_gpu_index != info->dst_gpu_index) {
        return MVGAL_ERROR_NOT_SUPPORTED;
    }

    err = mvgal_memory_copy_gpu(
        context,
        info->src_buffer,
        info->src_offset,
        info->dst_buffer,
        info->dst_offset,
        info->size,
        info->src_gpu_index,
        info->dst_gpu_index,
        NULL
    );
    if (err != MVGAL_SUCCESS) {
        return err;
    }

    memset(result, 0, sizeof(*result));
    result->method = method;
    result->bytes_migrated = info->size;
    result->src_gpu_index = info->src_gpu_index;
    result->dst_gpu_index = info->dst_gpu_index;
    result->zero_copy = method != MVGAL_MEMORY_COPY_CPU;
    have_src_gpu = mvgal_gpu_get_descriptor(info->src_gpu_index, &src_gpu) == MVGAL_SUCCESS;
    have_dst_gpu = mvgal_gpu_get_descriptor(info->dst_gpu_index, &dst_gpu) == MVGAL_SUCCESS;
    result->cross_vendor = have_src_gpu && have_dst_gpu && src_gpu.vendor != dst_gpu.vendor;

    pthread_mutex_lock(&g_execution_state.lock);
    g_execution_state.stats.bytes_transferred += info->size;
    pthread_mutex_unlock(&g_execution_state.lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_execution_get_steam_profile(
    const mvgal_steam_profile_request_t *request,
    mvgal_steam_profile_t *profile
) {
    uint32_t gpu_indices[MVGAL_EXECUTION_MAX_GPUS] = {0};
    uint32_t gpu_count = 0;
    char gpu_list[128] = {0};
    size_t offset = 0;
    mvgal_distribution_strategy_t strategy;
    mvgal_error_t err;

    if (request == NULL || profile == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    err = ensure_execution_initialized();
    if (err != MVGAL_SUCCESS && err != MVGAL_ERROR_ALREADY_INITIALIZED) {
        return err;
    }

    memset(profile, 0, sizeof(*profile));
    build_enabled_gpu_mask(gpu_indices, &gpu_count);
    if (gpu_count == 0U) {
        return MVGAL_ERROR_NO_GPUS;
    }

    strategy = request->preferred_strategy;
    if (strategy == MVGAL_STRATEGY_AUTO) {
        strategy = (gpu_count > 1U) ? MVGAL_STRATEGY_AFR : MVGAL_STRATEGY_SINGLE_GPU;
    }

    profile->strategy = strategy;
    profile->gpu_count = gpu_count;
    profile->steam_mode = request->steam_mode || detect_steam_mode();
    profile->proton_mode = request->proton_mode || detect_proton_mode();
    profile->low_latency = request->low_latency || profile->steam_mode || profile->proton_mode;
    memcpy(profile->gpu_indices, gpu_indices, gpu_count * sizeof(uint32_t));

    for (uint32_t i = 0; i < gpu_count; i++) {
        int written;
        written = snprintf(
            gpu_list + offset,
            sizeof(gpu_list) - offset,
            "%s%u",
            (i == 0U) ? "" : ",",
            gpu_indices[i]
        );
        if (written < 0) {
            return MVGAL_ERROR_UNKNOWN;
        }
        offset += (size_t)written;
        if (offset >= sizeof(gpu_list)) {
            break;
        }
    }

    snprintf(
        profile->env_block,
        sizeof(profile->env_block),
        "MVGAL_ENABLED=1\n"
        "MVGAL_VULKAN_ENABLE=%d\n"
        "MVGAL_STRATEGY=%s\n"
        "MVGAL_GPUS=%s\n"
        "MVGAL_LOW_LATENCY=%d\n"
        "MVGAL_STEAM_MODE=%d\n"
        "MVGAL_PROTON_MODE=%d\n"
        "%s"
        "%s",
        request->enable_vulkan_layer ? 1 : 0,
        strategy_to_string(strategy),
        gpu_list,
        profile->low_latency ? 1 : 0,
        profile->steam_mode ? 1 : 0,
        profile->proton_mode ? 1 : 0,
        request->enable_vulkan_layer ? "MVGAL_VULKAN_DEBUG=0\n" : "",
        request->enable_d3d_wrapper ? "MVGAL_D3D_ENABLED=1\n" : ""
    );

    snprintf(
        profile->launch_options,
        sizeof(profile->launch_options),
        "MVGAL_ENABLED=1 MVGAL_VULKAN_ENABLE=%d MVGAL_STRATEGY=%s MVGAL_GPUS=%s MVGAL_LOW_LATENCY=%d%s %%command%%",
        request->enable_vulkan_layer ? 1 : 0,
        strategy_to_string(strategy),
        gpu_list,
        profile->low_latency ? 1 : 0,
        request->enable_d3d_wrapper ? " MVGAL_D3D_ENABLED=1" : ""
    );

    return MVGAL_SUCCESS;
}
