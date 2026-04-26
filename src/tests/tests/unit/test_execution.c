/**
 * @file test_execution.c
 * @brief Execution engine unit tests
 */

#include "mvgal/mvgal.h"
#include "mvgal_execution.h"
#include "mvgal_memory.h"
#include "mvgal/mvgal_log.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { if (condition) { test_passed++; } else { test_failed++; \
        MVGAL_LOG_ERROR("TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
    } } while (0)

static void test_frame_lifecycle(mvgal_context_t context)
{
    mvgal_execution_frame_begin_info_t begin_info = {
        .api = MVGAL_API_VULKAN,
        .requested_strategy = MVGAL_STRATEGY_HYBRID,
        .application_name = "steam-proton-test",
        .steam_mode = false,
        .proton_mode = false,
        .low_latency = false
    };
    mvgal_execution_submit_info_t submit_info;
    mvgal_execution_plan_t submit_plan;
    mvgal_execution_plan_t present_plan;
    mvgal_execution_frame_stats_t frame_stats;
    uint64_t frame_id = 0;

    setenv("STEAM_GAME_ID", "480", 1);
    setenv("STEAM_COMPAT_DATA_PATH", "/tmp/mvgal-test-proton", 1);

    TEST_ASSERT(
        mvgal_execution_begin_frame(context, &begin_info, &frame_id) == MVGAL_SUCCESS,
        "Execution begin frame"
    );
    TEST_ASSERT(frame_id != 0, "Frame ID assigned");

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.frame_id = frame_id;
    submit_info.api = MVGAL_API_VULKAN;
    submit_info.requested_strategy = MVGAL_STRATEGY_HYBRID;
    submit_info.telemetry.type = MVGAL_WORKLOAD_GRAPHICS;
    submit_info.telemetry.step_name = "vkQueueSubmit";
    submit_info.telemetry.data_size = 8U * 1024U * 1024U;
    submit_info.telemetry.flags.is_commit = 1;
    submit_info.telemetry.flags.is_frame_start = 1;
    submit_info.resource_bytes = 16U * 1024U * 1024U;
    submit_info.command_buffer_count = 2;
    submit_info.queue_family_flags = 0x1U;

    TEST_ASSERT(
        mvgal_execution_submit(context, &submit_info, &submit_plan) == MVGAL_SUCCESS,
        "Execution submit"
    );
    TEST_ASSERT(submit_plan.frame_id == frame_id, "Submit plan frame ID");
    TEST_ASSERT(submit_plan.workload_id != 0, "Submit workload ID");
    TEST_ASSERT(submit_plan.selected_gpu_count >= 1, "Submit selected GPU");
    TEST_ASSERT(submit_plan.applied_strategy != MVGAL_STRATEGY_AUTO, "Submit applied strategy");
    TEST_ASSERT(submit_plan.steam_mode, "Submit steam mode detected");
    TEST_ASSERT(submit_plan.proton_mode, "Submit proton mode detected");

    TEST_ASSERT(
        mvgal_execution_present(context, frame_id, MVGAL_API_VULKAN, &present_plan) == MVGAL_SUCCESS,
        "Execution present"
    );
    TEST_ASSERT(present_plan.frame_id == frame_id, "Present plan frame ID");

    TEST_ASSERT(
        mvgal_execution_get_frame_stats(frame_id, &frame_stats) == MVGAL_SUCCESS,
        "Execution frame stats"
    );
    TEST_ASSERT(!frame_stats.active, "Frame inactive after present");
    TEST_ASSERT(frame_stats.submit_count >= 1, "Frame submit count");
    TEST_ASSERT(frame_stats.present_count == 1, "Frame present count");
    TEST_ASSERT(frame_stats.bytes_scheduled > 0, "Frame bytes tracked");
    TEST_ASSERT(frame_stats.steam_mode, "Frame stats steam mode");
    TEST_ASSERT(frame_stats.proton_mode, "Frame stats proton mode");

    unsetenv("STEAM_GAME_ID");
    unsetenv("STEAM_COMPAT_DATA_PATH");
}

static void test_memory_migration(mvgal_context_t context)
{
    mvgal_memory_alloc_info_t alloc_info = {
        .size = 4096,
        .alignment = 0,
        .flags = MVGAL_MEMORY_FLAG_HOST_VALID | MVGAL_MEMORY_FLAG_DMA_BUF | MVGAL_MEMORY_FLAG_SHARED,
        .memory_type = MVGAL_MEMORY_TYPE_SRAM,
        .sharing_mode = MVGAL_MEMORY_SHARING_DMA_BUF,
        .access = MVGAL_MEMORY_ACCESS_READ_WRITE,
        .gpu_mask = 0,
        .priority = 50
    };
    mvgal_buffer_t src_buffer = NULL;
    mvgal_buffer_t dst_buffer = NULL;
    mvgal_execution_migration_info_t migration_info;
    mvgal_execution_migration_result_t migration_result;
    int gpu_count = mvgal_gpu_get_count();
    uint32_t src_gpu = 0;
    uint32_t dst_gpu = gpu_count > 1 ? 1U : 0U;
    char src_data[64];
    char dst_data[64];

    memset(src_data, 0x5A, sizeof(src_data));
    memset(dst_data, 0, sizeof(dst_data));

    TEST_ASSERT(
        mvgal_memory_allocate(context, &alloc_info, &src_buffer) == MVGAL_SUCCESS,
        "Allocate migration source buffer"
    );
    TEST_ASSERT(
        mvgal_memory_allocate(context, &alloc_info, &dst_buffer) == MVGAL_SUCCESS,
        "Allocate migration destination buffer"
    );

    TEST_ASSERT(
        mvgal_memory_write(src_buffer, 0, sizeof(src_data), src_data) == MVGAL_SUCCESS,
        "Write source buffer"
    );

    memset(&migration_info, 0, sizeof(migration_info));
    migration_info.src_buffer = src_buffer;
    migration_info.dst_buffer = dst_buffer;
    migration_info.size = sizeof(src_data);
    migration_info.src_gpu_index = src_gpu;
    migration_info.dst_gpu_index = dst_gpu;
    migration_info.policy = MVGAL_EXECUTION_MIGRATION_AUTO;
    migration_info.prefer_zero_copy = true;
    migration_info.allow_cpu_fallback = true;

    TEST_ASSERT(
        mvgal_execution_migrate_memory(context, &migration_info, &migration_result) == MVGAL_SUCCESS,
        "Execution memory migration"
    );
    TEST_ASSERT(migration_result.bytes_migrated == sizeof(src_data), "Migration byte count");

    TEST_ASSERT(
        mvgal_memory_read(dst_buffer, 0, sizeof(dst_data), dst_data) == MVGAL_SUCCESS,
        "Read migrated data"
    );
    TEST_ASSERT(memcmp(src_data, dst_data, sizeof(src_data)) == 0, "Migrated data matches");

    mvgal_memory_free(src_buffer);
    mvgal_memory_free(dst_buffer);
}

static void test_steam_profile(void)
{
    mvgal_steam_profile_request_t request = {
        .application_name = "Test Game",
        .preferred_strategy = MVGAL_STRATEGY_AFR,
        .steam_mode = true,
        .proton_mode = true,
        .enable_vulkan_layer = true,
        .enable_d3d_wrapper = true,
        .low_latency = true
    };
    mvgal_steam_profile_t profile;

    TEST_ASSERT(
        mvgal_execution_get_steam_profile(&request, &profile) == MVGAL_SUCCESS,
        "Steam profile generation"
    );
    TEST_ASSERT(profile.gpu_count >= 1, "Steam profile GPU count");
    TEST_ASSERT(strstr(profile.env_block, "MVGAL_ENABLED=1") != NULL, "Steam profile env block");
    TEST_ASSERT(strstr(profile.launch_options, "%command%") != NULL, "Steam profile launch options");
    TEST_ASSERT(strstr(profile.launch_options, "MVGAL_STRATEGY=afr") != NULL, "Steam profile strategy");
}

static void test_execution_stats(mvgal_context_t context)
{
    mvgal_stats_t stats;

    TEST_ASSERT(mvgal_get_stats(context, &stats) == MVGAL_SUCCESS, "Get execution stats");
    TEST_ASSERT(stats.frames_submitted >= 1, "Stats frames submitted");
    TEST_ASSERT(stats.frames_completed >= 1, "Stats frames completed");
    TEST_ASSERT(stats.workloads_distributed >= 1, "Stats workloads distributed");
    TEST_ASSERT(stats.bytes_transferred >= 64, "Stats bytes transferred");
}

int main(void)
{
    mvgal_context_t context = NULL;

    printf("\n============================================================\n");
    printf("  MVGAL Execution Engine Unit Tests\n");
    printf("============================================================\n\n");

    TEST_ASSERT(mvgal_init(1) == MVGAL_SUCCESS, "MVGAL initialization");
    TEST_ASSERT(mvgal_context_create(&context) == MVGAL_SUCCESS, "Context creation");
    TEST_ASSERT(mvgal_context_set_current(context) == MVGAL_SUCCESS, "Set current context");

    test_frame_lifecycle(context);
    test_memory_migration(context);
    test_steam_profile();
    test_execution_stats(context);

    mvgal_context_destroy(context);
    mvgal_shutdown();

    printf("\n============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n\n");

    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
