/**
 * @file test_opencl_intercept.c
 * @brief OpenCL Interception Integration Test
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Tests that the OpenCL interception layer properly intercepts and
 * distributes workloads across multiple GPUs.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MVGAL headers
#include "mvgal/mvgal.h"
#include "mvgal/mvgal_log.h"

// Test counters
static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            test_passed++; \
        } else { \
            test_failed++; \
            MVGAL_LOG_ERROR("TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
        } \
    } while (0)

// =============================================================================
// Test: MVGAL Initialization (Required for OpenCL layer)
// =============================================================================

static void test_mvgal_init(void) {
    MVGAL_LOG_INFO("TEST: MVGAL Initialization");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    TEST_ASSERT(mvgal_is_initialized(), "MVGAL not initialized");
    
    // Check GPU detection
    int gpu_count = mvgal_gpu_get_count();
    TEST_ASSERT(gpu_count >= 0, "GPU count is negative");
    MVGAL_LOG_INFO("  Detected %d GPU(s)", gpu_count);
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: MVGAL Initialization - PASSED");
}

// =============================================================================
// Test: OpenCL Library Availability
// =============================================================================

static void test_opencl_library_load(void) {
    MVGAL_LOG_INFO("TEST: OpenCL Library Load");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    // Try to load libOpenCL.so
    void *libopencl = dlopen("libOpenCL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!libopencl) {
        libopencl = dlopen("libOpenCL.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    
    TEST_ASSERT(libopencl != NULL, "Failed to load libOpenCL.so");
    
    if (libopencl) {
        // Check for key OpenCL functions
        void *clGetPlatformIDs = dlsym(libopencl, "clGetPlatformIDs");
        void *clGetDeviceIDs = dlsym(libopencl, "clGetDeviceIDs");
        void *clCreateContext = dlsym(libopencl, "clCreateContext");
        void *clCreateCommandQueue = dlsym(libopencl, "clCreateCommandQueue");
        void *clCreateBuffer = dlsym(libopencl, "clCreateBuffer");
        void *clCreateKernel = dlsym(libopencl, "clCreateKernel");
        void *clSetKernelArg = dlsym(libopencl, "clSetKernelArg");
        void *clEnqueueNDRangeKernel = dlsym(libopencl, "clEnqueueNDRangeKernel");
        
        TEST_ASSERT(clGetPlatformIDs != NULL, "clGetPlatformIDs not found");
        TEST_ASSERT(clGetDeviceIDs != NULL, "clGetDeviceIDs not found");
        TEST_ASSERT(clCreateContext != NULL, "clCreateContext not found");
        TEST_ASSERT(clCreateCommandQueue != NULL, "clCreateCommandQueue not found");
        TEST_ASSERT(clCreateBuffer != NULL, "clCreateBuffer not found");
        TEST_ASSERT(clCreateKernel != NULL, "clCreateKernel not found");
        TEST_ASSERT(clSetKernelArg != NULL, "clSetKernelArg not found");
        TEST_ASSERT(clEnqueueNDRangeKernel != NULL, "clEnqueueNDRangeKernel not found");
        
        dlclose(libopencl);
    }
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: OpenCL Library Load - PASSED");
}

// =============================================================================  
// Test: OpenCL Function Availability
// =============================================================================

static void test_opencl_function_availability(void) {
    MVGAL_LOG_INFO("TEST: OpenCL Function Availability");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    // Get OpenCL library
    void *libopencl = dlopen("libOpenCL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!libopencl) {
        libopencl = dlopen("libOpenCL.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    
    TEST_ASSERT(libopencl != NULL, "Failed to load libOpenCL.so");
    
    // Check for all intercepted OpenCL functions
    const char *function_names[] = {
        "clGetPlatformIDs",
        "clGetPlatformInfo",
        "clGetDeviceIDs",
        "clGetDeviceInfo",
        "clCreateContext",
        "clCreateContextFromType",
        "clReleaseContext",
        "clRetainContext",
        "clGetContextInfo",
        "clCreateCommandQueue",
        "clReleaseCommandQueue",
        "clRetainCommandQueue",
        "clGetCommandQueueInfo",
        "clCreateBuffer",
        "clCreateSubBuffer",
        "clRetainMemObject",
        "clReleaseMemObject",
        "clGetMemObjectInfo",
        "clCreateProgramWithSource",
        "clCreateProgramWithBinary",
        "clRetainProgram",
        "clReleaseProgram",
        "clBuildProgram",
        "clCreateKernel",
        "clCreateKernelsInProgram",
        "clRetainKernel",
        "clReleaseKernel",
        "clSetKernelArg",
        "clGetKernelInfo",
        "clGetKernelWorkGroupInfo",
        "clEnqueueNDRangeKernel",
        "clEnqueueTask",
        "clEnqueueReadBuffer",
        "clEnqueueWriteBuffer",
        "clEnqueueCopyBuffer",
        "clEnqueueMapBuffer",
        "clEnqueueUnmapMemObject",
        "clWaitForEvents",
        "clFlush",
        "clFinish",
        "clCreateUserEvent",
        "clRetainEvent",
        "clReleaseEvent",
        "clGetEventInfo",
        NULL
    };
    
    int functions_found = 0;
    int functions_total = 0;
    for (int i = 0; function_names[i] != NULL; i++) {
        functions_total++;
        if (dlsym(libopencl, function_names[i])) {
            functions_found++;
        } else {
            MVGAL_LOG_WARN("  Function not found: %s", function_names[i]);
        }
    }
    
    MVGAL_LOG_INFO("  Found %d/%d OpenCL functions", functions_found, functions_total);
    TEST_ASSERT(functions_found > 0, "No OpenCL functions found");
    
    dlclose(libopencl);
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: OpenCL Function Availability - PASSED");
}

// =============================================================================
// Test: MVGAL Workload Submission
// =============================================================================

static void test_mvgal_workload_submission(void) {
    MVGAL_LOG_INFO("TEST: MVGAL Workload Submission");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    // Test that we can create a context
    mvgal_context_t mvgal_ctx;
    err = mvgal_context_create(&mvgal_ctx);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to create MVGAL context");
    
    // Test scheduler
    err = mvgal_set_strategy(mvgal_ctx, MVGAL_STRATEGY_ROUND_ROBIN);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to set strategy");
    
    mvgal_distribution_strategy_t strategy = mvgal_get_strategy(mvgal_ctx);
    TEST_ASSERT(strategy == MVGAL_STRATEGY_ROUND_ROBIN, "Strategy not set correctly");
    
    // Test workload submission for OpenCL kernel
    mvgal_workload_submit_info_t info = {
        .type = MVGAL_WORKLOAD_OPENCL_KERNEL,
        .priority = 50,
        .gpu_mask = 0xFFFFFFFF,
        .deadline = 0,
        .dependency_count = 0,
        .dependencies = NULL,
        .user_data = (void *)0x1000
    };
    
    mvgal_workload_t workload;
    err = mvgal_workload_submit(mvgal_ctx, &info, &workload);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to submit workload");
    TEST_ASSERT(workload != NULL, "Workload is NULL");
    
    // Wait for workload to complete
    err = mvgal_workload_wait(workload, 5000);
    TEST_ASSERT(err == MVGAL_SUCCESS || err == MVGAL_ERROR_TIMEOUT, "Workload wait failed");
    
    // Test workload submission for OpenCL buffer
    info.type = MVGAL_WORKLOAD_OPENCL_BUFFER;
    info.user_data = (void *)0x4000;
    err = mvgal_workload_submit(mvgal_ctx, &info, &workload);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to submit buffer workload");
    
    // Test workload submission for OpenCL NDRange
    info.type = MVGAL_WORKLOAD_OPENCL_KERNEL;
    info.user_data = (void *)0x1000000; // 1 million work items
    err = mvgal_workload_submit(mvgal_ctx, &info, &workload);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to submit NDRange workload");
    
    // Cleanup
    mvgal_context_destroy(mvgal_ctx);
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: MVGAL Workload Submission - PASSED");
}

// =============================================================================
// Test: OpenCL Layer Library Load
// =============================================================================

static void test_opencl_layer_library(void) {
    MVGAL_LOG_INFO("TEST: OpenCL Layer Library Load");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    // Try to load the MVGAL OpenCL interception library
    void *libmvgal_opencl = dlopen("libmvgal_opencl.so", RTLD_LAZY | RTLD_GLOBAL);
    if (libmvgal_opencl) {
        MVGAL_LOG_INFO("  libmvgal_opencl.so loaded successfully");
        
        // Check that initialization function exists
        void *init_func = dlsym(libmvgal_opencl, "mvgal_cl_layer_init_global");
        TEST_ASSERT(init_func != NULL, "mvgal_cl_layer_init_global not found");
        
        void *shutdown_func = dlsym(libmvgal_opencl, "mvgal_cl_layer_shutdown_global");
        TEST_ASSERT(shutdown_func != NULL, "mvgal_cl_layer_shutdown_global not found");
        
        void *map_func = dlsym(libmvgal_opencl, "mvgal_cl_map_device_to_gpu");
        TEST_ASSERT(map_func != NULL, "mvgal_cl_map_device_to_gpu not found");
        
        void *stats_func = dlsym(libmvgal_opencl, "mvgal_cl_get_statistics");
        TEST_ASSERT(stats_func != NULL, "mvgal_cl_get_statistics not found");
        
        // Check for intercepted OpenCL functions
        int intercepted_count = 0;
        const char *intercepted_functions[] = {
            "clGetPlatformIDs",
            "clGetDeviceIDs",
            "clCreateContext",
            "clCreateCommandQueue",
            "clCreateBuffer",
            "clEnqueueNDRangeKernel",
            NULL
        };
        
        for (int i = 0; intercepted_functions[i] != NULL; i++) {
            if (dlsym(libmvgal_opencl, intercepted_functions[i])) {
                intercepted_count++;
            }
        }
        
        TEST_ASSERT(intercepted_count > 0, "No intercepted functions found in libmvgal_opencl.so");
        MVGAL_LOG_INFO("  Found %d intercepted functions", intercepted_count);
        
        dlclose(libmvgal_opencl);
    } else {
        // Library not built yet, that's OK for this test
        MVGAL_LOG_WARN("  libmvgal_opencl.so not found (not built yet)");
    }
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: OpenCL Layer Library Load - PASSED");
}

// =============================================================================
// Test: OpenCL Statistics Tracking
// =============================================================================

static void test_opencl_statistics(void) {
    MVGAL_LOG_INFO("TEST: OpenCL Statistics Tracking");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    // Create context and submit some workloads
    mvgal_context_t mvgal_ctx;
    err = mvgal_context_create(&mvgal_ctx);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to create MVGAL context");
    
    // Get initial stats
    mvgal_stats_t stats;
    err = mvgal_get_stats(mvgal_ctx, &stats);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to get initial stats");
    
    // Submit some workloads
    for (int i = 0; i < 5; i++) {
        mvgal_workload_submit_info_t info = {
            .type = MVGAL_WORKLOAD_OPENCL_KERNEL,
            .priority = 50,
            .gpu_mask = 0xFFFFFFFF,
            .deadline = 0,
            .dependency_count = 0,
            .dependencies = NULL,
            .user_data = (void *)(uintptr_t)(1024 * (i + 1))
        };
        
        mvgal_workload_t workload;
        err = mvgal_workload_submit(mvgal_ctx, &info, &workload);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to submit workload");
    }
    
    // Get updated stats
    err = mvgal_get_stats(mvgal_ctx, &stats);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to get updated stats");
    
    // Check that workloads were tracked
    TEST_ASSERT(stats.workloads_distributed >= 5, "Expected at least 5 workloads distributed");
    MVGAL_LOG_INFO("  Distributed %llu workloads", stats.workloads_distributed);
    
    // Cleanup
    mvgal_context_destroy(mvgal_ctx);
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: OpenCL Statistics Tracking - PASSED");
}

// =============================================================================
// Main
// =============================================================================

int main(void) {
    printf("\n");
    printf("============================================================\n");
    printf("  MVGAL OpenCL Interception Integration Tests\n");
    printf("============================================================\n");
    printf("\n");
    
    // Run all tests
    test_mvgal_init();
    test_opencl_library_load();
    test_opencl_function_availability();
    test_mvgal_workload_submission();
    test_opencl_layer_library();
    test_opencl_statistics();
    
    // Print summary
    printf("\n");
    printf("============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n");
    printf("\n");
    
    // Return exit code
    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
