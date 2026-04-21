/**
 * @file test_multi_gpu_validation.c
 * @brief Multi-GPU Integration Test
 * 
 * Validates that multiple GPUs can be detected and used together.
 */

#include "mvgal.h"
#include "mvgal_execution.h"
#include "mvgal_gpu.h"
#include "mvgal_scheduler.h"
#include <inttypes.h>
#include "mvgal_memory.h"
#include "mvgal_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { if (condition) { test_passed++; MVGAL_LOG_INFO("  ✓ %s", message); } \
         else { test_failed++; MVGAL_LOG_ERROR("  ✗ %s", message); } \
    } while (0)

int main(void)
{
    printf("\n============================================================\n");
    printf("  MVGAL Multi-GPU Integration Test\n");
    printf("============================================================\n\n");
    
    MVGAL_LOG_INFO("Initializing MVGAL...");
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "MVGAL initialization");
    
    int gpu_count = mvgal_gpu_get_count();
    MVGAL_LOG_INFO("Detected %d GPU(s)", gpu_count);
    TEST_ASSERT(gpu_count >= 1, "At least 1 GPU detected");
    
    // Test GPU enumeration
    if (gpu_count > 0) {
        mvgal_gpu_descriptor_t *gpus = malloc(gpu_count * sizeof(mvgal_gpu_descriptor_t));
        int enumerated = mvgal_gpu_enumerate(gpus, gpu_count);
        TEST_ASSERT(enumerated > 0, "GPU enumeration");
        MVGAL_LOG_INFO("Enumerated %d GPU(s):", enumerated);
        
        for (int i = 0; i < enumerated; i++) {
            MVGAL_LOG_INFO("  GPU %d: %s (Vendor: %d, VRAM: %" PRIu64 " MB, Type: %d)",
                          gpus[i].id, gpus[i].name, gpus[i].vendor,
                          gpus[i].vram_total / (1024 * 1024), gpus[i].type);
        }
        
        free(gpus);
    }
    
    // Test context creation
    mvgal_context_t context;
    err = mvgal_context_create(&context);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Context creation");
    TEST_ASSERT(context != NULL, "Context is not NULL");
    
    // Test workload creation via submit
    mvgal_workload_submit_info_t info = {
        .type = MVGAL_WORKLOAD_COMPUTE,
        .priority = 50,
        .deadline = 0,
        .gpu_mask = 0xFFFFFFFF,
        .dependency_count = 0,
        .dependencies = NULL,
        .user_data = NULL
    };
    mvgal_workload_t workload;
    err = mvgal_workload_submit(context, &info, &workload);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Workload submission");
    TEST_ASSERT(workload != NULL, "Workload is not NULL");
    
    // Test strategy setting
    err = mvgal_scheduler_set_strategy(context, MVGAL_STRATEGY_HYBRID);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Set hybrid strategy");
    mvgal_distribution_strategy_t strategy = mvgal_scheduler_get_strategy(context);
    TEST_ASSERT(strategy == MVGAL_STRATEGY_HYBRID, "Get hybrid strategy");
    
    // Test memory allocation
    mvgal_buffer_t buffer;
    err = mvgal_memory_allocate_simple(context, 1024, 0, &buffer);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Memory allocation");
    TEST_ASSERT(buffer != NULL, "Buffer is not NULL");
    mvgal_memory_free(buffer);
    
    // Test GPU selection
    if (gpu_count >= 2) {
        mvgal_gpu_selection_criteria_t criteria = {
            .use_compute_score = true,
            .use_graphics_score = true,
            .use_memory = false,
            .use_features = false,
            .required_features = MVGAL_FEATURE_GRAPHICS,
            .preferred_features = 0,
            .min_vram = 0,
            .preferred_vendor = MVGAL_VENDOR_UNKNOWN,
            .required_api = MVGAL_API_VULKAN
        };
        mvgal_gpu_descriptor_t selected;
        err = mvgal_gpu_select_best(&criteria, &selected);
        TEST_ASSERT(err == MVGAL_SUCCESS, "GPU selection");
        MVGAL_LOG_INFO("Selected GPU: %s (ID: %d)", selected.name, selected.id);
    }

    // Test logical device aggregation
    if (gpu_count >= 1) {
        uint32_t logical_gpu_count = gpu_count >= 2 ? 2U : 1U;
        uint32_t gpu_indices[2] = {0, 1};
        void *logical_device = NULL;
        mvgal_logical_device_descriptor_t logical_desc;

        err = mvgal_device_create(logical_gpu_count, gpu_indices, &logical_device);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Logical device creation");
        TEST_ASSERT(logical_device != NULL, "Logical device is not NULL");

        err = mvgal_device_get_descriptor(logical_device, &logical_desc);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Logical device descriptor");
        TEST_ASSERT(logical_desc.gpu_count == logical_gpu_count, "Logical device GPU count");
        TEST_ASSERT(logical_desc.descriptor.type == MVGAL_GPU_TYPE_VIRTUAL,
                    "Logical device type");

        mvgal_device_destroy(logical_device);
    }
    
    // Test stats
    mvgal_stats_t stats;
    err = mvgal_get_stats(context, &stats);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Get stats");
    MVGAL_LOG_INFO("Stats: frames=%" PRIu64 ", workloads=%" PRIu64,
                  stats.frames_submitted, stats.workloads_distributed);

    // Test execution plan generation
    {
        mvgal_execution_frame_begin_info_t begin_info = {
            .api = MVGAL_API_VULKAN,
            .requested_strategy = MVGAL_STRATEGY_HYBRID,
            .application_name = "integration-vulkan",
            .steam_mode = false,
            .proton_mode = false,
            .low_latency = false
        };
        mvgal_execution_submit_info_t submit_info;
        mvgal_execution_plan_t exec_plan;
        uint64_t frame_id = 0;

        err = mvgal_execution_begin_frame(context, &begin_info, &frame_id);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Execution frame begin");

        memset(&submit_info, 0, sizeof(submit_info));
        submit_info.frame_id = frame_id;
        submit_info.api = MVGAL_API_VULKAN;
        submit_info.requested_strategy = MVGAL_STRATEGY_HYBRID;
        submit_info.telemetry.type = MVGAL_WORKLOAD_GRAPHICS;
        submit_info.telemetry.step_name = "vkQueueSubmit";
        submit_info.telemetry.data_size = 4U * 1024U * 1024U;
        submit_info.resource_bytes = 8U * 1024U * 1024U;
        submit_info.command_buffer_count = 1;
        submit_info.queue_family_flags = 0x1U;

        err = mvgal_execution_submit(context, &submit_info, &exec_plan);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Execution workload submit");
        TEST_ASSERT(exec_plan.selected_gpu_count >= 1, "Execution selected GPU");
    }
    
    // Cleanup
    mvgal_workload_destroy(workload);
    mvgal_context_destroy(context);
    mvgal_shutdown();
    
    // Summary
    printf("\n============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n");
    
    if (test_failed == 0) {
        printf("\n✓ Multi-GPU integration test PASSED\n\n");
    } else {
        printf("\n✗ Multi-GPU integration test FAILED\n\n");
    }
    
    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
