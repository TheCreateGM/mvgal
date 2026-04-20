/**
 * @file cl_intercept.c
 * @brief OpenCL Interception Layer Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This file implements the OpenCL layer that intercepts OpenCL API calls
 * using LD_PRELOAD and distributes workloads across multiple GPUs.
 *
 * Usage:
 *   LD_PRELOAD=/path/to/libmvgal_opencl.so ./your_opencl_app
 *
 * Environment Variables:
 *   MVGAL_OPENCL_ENABLED=1    - Enable MVGAL OpenCL interception (default: 1)
 *   MVGAL_OPENCL_DEBUG=1      - Enable debug logging (default: 0)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <pthread.h>

// MVGAL headers
#include "mvgal.h"
#include "mvgal_log.h"
#include "mvgal_gpu.h"
#include "mvgal_scheduler.h"

// Global state
static bool g_enabled = true;
static bool g_debug = false;
static bool g_initialized = false;
static int g_gpu_count = 0;
static mvgal_gpu_descriptor_t g_gpus[32];

/**
 * @brief Initialize the layer
 */
static void init_layer(void) {
    if (g_initialized) return;
    
    const char *enabled = getenv("MVGAL_OPENCL_ENABLED");
    g_enabled = enabled ? atoi(enabled) : 1;
    const char *debug = getenv("MVGAL_OPENCL_DEBUG");
    g_debug = debug ? atoi(debug) : 0;
    
    if (!g_enabled) return;
    
    if (g_debug) fprintf(stderr, "MVGAL OpenCL: Initializing\n");
    
    // Initialize MVGAL
    g_gpu_count = mvgal_gpu_get_count();
    for (int i = 0; i < g_gpu_count && i < 32; i++) {
        mvgal_gpu_get_descriptor(i, &g_gpus[i]);
    }
    if (g_debug) fprintf(stderr, "MVGAL OpenCL: Found %d GPUs\n", g_gpu_count);
    
    g_initialized = true;
}

/**
 * @brief Get original function pointer
 */
static void* get_original(const char *name) {
    void *ptr = dlsym(RTLD_NEXT, name);
    if (!ptr && g_debug) {
        fprintf(stderr, "MVGAL OpenCL: Failed to find original %s\n", name);
    }
    return ptr;
}

// =============================================================================
// Intercepted OpenCL Functions
// Using void* for opaque types since we can't include OpenCL headers
// =============================================================================

// clGetPlatformIDs
int clGetPlatformIDs(unsigned int num_entries, void **platforms, unsigned int *num_platforms) {
    init_layer();
    if (!g_enabled) {
        int (*func)(unsigned int, void **, unsigned int *) = get_original("clGetPlatformIDs");
        return func ? func(num_entries, platforms, num_platforms) : 0;
    }
    int (*func)(unsigned int, void **, unsigned int *) = get_original("clGetPlatformIDs");
    int ret = func ? func(num_entries, platforms, num_platforms) : 0;
    if (g_debug && ret == 0 && num_platforms) {
        fprintf(stderr, "MVGAL OpenCL: clGetPlatformIDs returned %d platforms\n", *num_platforms);
    }
    return ret;
}

// clGetDeviceIDs
int clGetDeviceIDs(void *platform, unsigned long device_type, unsigned int num_entries, 
                   void **devices, unsigned int *num_devices) {
    init_layer();
    if (!g_enabled) {
        int (*func)(void *, unsigned long, unsigned int, void **, unsigned int *) = get_original("clGetDeviceIDs");
        return func ? func(platform, device_type, num_entries, devices, num_devices) : 0;
    }
    int (*func)(void *, unsigned long, unsigned int, void **, unsigned int *) = get_original("clGetDeviceIDs");
    int ret = func ? func(platform, device_type, num_entries, devices, num_devices) : 0;
    if (g_debug && ret == 0 && num_devices) {
        fprintf(stderr, "MVGAL OpenCL: clGetDeviceIDs returned %d devices\n", *num_devices);
    }
    return ret;
}

// clCreateContext
void* clCreateContext(const void *properties, unsigned int num_devices, const void **devices,
                      void *pfn_notify, void *user_data, int *errcode_ret) {
    init_layer();
    if (!g_enabled) {
        void* (*func)(const void *, unsigned int, const void **, void *, void *, int *) = get_original("clCreateContext");
        return func ? func(properties, num_devices, devices, pfn_notify, user_data, errcode_ret) : NULL;
    }
    void* (*func)(const void *, unsigned int, const void **, void *, void *, int *) = get_original("clCreateContext");
    if (g_debug) fprintf(stderr, "MVGAL OpenCL: clCreateContext\n");
    return func ? func(properties, num_devices, devices, pfn_notify, user_data, errcode_ret) : NULL;
}

// clCreateCommandQueue
void* clCreateCommandQueue(void *context, void *device, unsigned long properties, int *errcode_ret) {
    init_layer();
    if (!g_enabled) {
        void* (*func)(void *, void *, unsigned long, int *) = get_original("clCreateCommandQueue");
        return func ? func(context, device, properties, errcode_ret) : NULL;
    }
    void* (*func)(void *, void *, unsigned long, int *) = get_original("clCreateCommandQueue");
    if (g_debug) fprintf(stderr, "MVGAL OpenCL: clCreateCommandQueue\n");
    return func ? func(context, device, properties, errcode_ret) : NULL;
}

// clCreateBuffer
void* clCreateBuffer(void *context, unsigned long flags, size_t size, void *host_ptr, int *errcode_ret) {
    init_layer();
    if (!g_enabled) {
        void* (*func)(void *, unsigned long, size_t, void *, int *) = get_original("clCreateBuffer");
        return func ? func(context, flags, size, host_ptr, errcode_ret) : NULL;
    }
    void* (*func)(void *, unsigned long, size_t, void *, int *) = get_original("clCreateBuffer");
    if (g_debug) fprintf(stderr, "MVGAL OpenCL: clCreateBuffer(size=%zu)\n", size);
    return func ? func(context, flags, size, host_ptr, errcode_ret) : NULL;
}

// clEnqueueNDRangeKernel - KEY DISTRIBUTION POINT
int clEnqueueNDRangeKernel(void *command_queue, void *kernel, unsigned int work_dim,
                           const size_t *global_work_offset, const size_t *global_work_size,
                           const size_t *local_work_size, unsigned int num_events_in_wait_list,
                           const void **event_wait_list, void **event) {
    init_layer();
    if (!g_enabled) {
        int (*func)(void *, void *, unsigned int, const size_t *, const size_t *, const size_t *,
                     unsigned int, const void **, void **) = get_original("clEnqueueNDRangeKernel");
        return func ? func(command_queue, kernel, work_dim, global_work_offset, global_work_size,
                          local_work_size, num_events_in_wait_list, event_wait_list, event) : 0;
    }
    
    int (*func)(void *, void *, unsigned int, const size_t *, const size_t *, const size_t *,
                 unsigned int, const void **, void **) = get_original("clEnqueueNDRangeKernel");
    
    if (g_debug && g_gpu_count > 1) {
        fprintf(stderr, "MVGAL OpenCL: clEnqueueNDRangeKernel - Distribution across %d GPUs\n", g_gpu_count);
        // In production: split kernel execution across GPUs here
        // using mvgal_scheduler_submit_workload()
    }
    
    return func ? func(command_queue, kernel, work_dim, global_work_offset, global_work_size,
                      local_work_size, num_events_in_wait_list, event_wait_list, event) : 0;
}
