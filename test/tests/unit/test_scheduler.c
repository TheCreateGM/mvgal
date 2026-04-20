/**
 * @file test_scheduler.c
 * @brief Scheduler Unit Tests
 */

#include "mvgal.h"
#include "mvgal_scheduler.h"
#include "mvgal_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { if (condition) { test_passed++; } else { test_failed++; \
        MVGAL_LOG_ERROR("TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
    } } while (0)

static void test_scheduler_init(void)
{
    MVGAL_LOG_INFO("TEST: Scheduler Initialization");
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to initialize");
    
    // Scheduler is initialized as part of mvgal_init
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Scheduler Initialization - PASSED");
}

static void test_workload_creation(void)
{
    MVGAL_LOG_INFO("TEST: Workload Creation");
    mvgal_init(0);
    void *context = NULL; // Using void* as per scheduler API
    
    // Create and submit a simple workload
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
    mvgal_error_t err = mvgal_workload_submit(context, &info, &workload);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to submit workload");
    TEST_ASSERT(workload != NULL, "Workload is NULL");
    
    // Wait for completion
    err = mvgal_workload_wait(workload, 1000);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to wait for workload");
    
    // Destroy workload
    mvgal_workload_destroy(workload);
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Workload Creation - PASSED");
}

static void test_strategy_selection(void)
{
    MVGAL_LOG_INFO("TEST: Strategy Selection");
    mvgal_init(0);
    mvgal_context_t context;
    mvgal_context_create(&context);
    
    // Test all strategies
    mvgal_distribution_strategy_t strategies[] = {
        MVGAL_STRATEGY_AFR,
        MVGAL_STRATEGY_SFR,
        MVGAL_STRATEGY_TASK,
        MVGAL_STRATEGY_COMPUTE_OFFLOAD,
        MVGAL_STRATEGY_HYBRID,
        MVGAL_STRATEGY_SINGLE_GPU,
        MVGAL_STRATEGY_AFR
    };
    
    for (size_t i = 0; i < sizeof(strategies)/sizeof(strategies[0]); i++) {
        mvgal_error_t err = mvgal_scheduler_set_strategy(context, strategies[i]);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to set strategy");
        
        mvgal_distribution_strategy_t current = mvgal_scheduler_get_strategy(context);
        TEST_ASSERT(current == strategies[i], "Strategy not set correctly");
    }
    
    mvgal_context_destroy(context);
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Strategy Selection - PASSED");
}

static void test_stats_tracking(void)
{
    MVGAL_LOG_INFO("TEST: Statistics Tracking");
    mvgal_init(0);
    mvgal_context_t context;
    mvgal_context_create(&context);
    
    // Get initial stats
    mvgal_stats_t stats;
    mvgal_error_t err = mvgal_get_stats(context, &stats);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to get stats");
    
    MVGAL_LOG_INFO("  Initial frames: %" PRIu64, stats.frames_submitted);
    
    mvgal_context_destroy(context);
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Statistics Tracking - PASSED");
}

int main(void)
{
    printf("\n============================================================\n");
    printf("  MVGAL Scheduler Unit Tests\n");
    printf("============================================================\n\n");
    
    test_scheduler_init();
    test_workload_creation();
    test_strategy_selection();
    test_stats_tracking();
    
    printf("\n============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n\n");
    
    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
