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
 *   MVGAL_STRATEGY=strategy  - Distribution strategy (round_robin, afr, hybrid, etc.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <dlfcn.h>
#include <pthread.h>

// MVGAL headers
#include "mvgal/mvgal.h"
#include "mvgal/mvgal_log.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_scheduler.h"
#include "mvgal/mvgal_memory.h"
#include "mvgal/mvgal_execution.h"

#include "cl_intercept.h"

// =============================================================================
// Global State
// =============================================================================

// Global layer state (defined in cl_intercept.h)
mvgal_cl_layer_state_t g_cl_layer_state = {0};

// =============================================================================
// Initialization and Cleanup
// =============================================================================

/**
 * @brief Initialize the global layer state
 */
void mvgal_cl_layer_init_global(void) {
    if (g_cl_layer_state.initialized) return;
    
    pthread_mutex_init(&g_cl_layer_state.mutex, NULL);
    pthread_mutex_init(&g_cl_layer_state.mem_mutex, NULL);
    
    // Load configuration from environment
    const char *enabled = getenv(ENV_MVGAL_OPENCL_ENABLED);
    g_cl_layer_state.enabled = enabled ? atoi(enabled) : 1;
    
    const char *debug = getenv(ENV_MVGAL_OPENCL_DEBUG);
    g_cl_layer_state.debug = debug ? atoi(debug) : 0;
    
    // Load MVGAL - this initializes GPU manager, scheduler, etc.
    if (g_cl_layer_state.enabled) {
        mvgal_error_t err = mvgal_init(0);
        if (err != MVGAL_SUCCESS) {
            MVGAL_LOG_WARN("MVGAL OpenCL: Failed to initialize MVGAL: %d", err);
            g_cl_layer_state.enabled = false;
        } else {
            // Get GPU information
            g_cl_layer_state.mvgal_gpu_count = (uint32_t)mvgal_gpu_get_count();
            for (uint32_t i = 0; i < g_cl_layer_state.mvgal_gpu_count && i < 32; i++) {
                mvgal_gpu_get_descriptor(i, &g_cl_layer_state.mvgal_gpus[i]);
            }
            
            // Set default strategy
            const char *strategy_env = getenv(ENV_MVGAL_STRATEGY);
            if (strategy_env) {
                if (strcmp(strategy_env, "round_robin") == 0) {
                    g_cl_layer_state.strategy = MVGAL_STRATEGY_ROUND_ROBIN;
                } else if (strcmp(strategy_env, "afr") == 0) {
                    g_cl_layer_state.strategy = MVGAL_STRATEGY_AFR;
                } else if (strcmp(strategy_env, "sfr") == 0) {
                    g_cl_layer_state.strategy = MVGAL_STRATEGY_SFR;
                } else if (strcmp(strategy_env, "hybrid") == 0) {
                    g_cl_layer_state.strategy = MVGAL_STRATEGY_HYBRID;
                } else if (strcmp(strategy_env, "task") == 0) {
                    g_cl_layer_state.strategy = MVGAL_STRATEGY_TASK;
                } else if (strcmp(strategy_env, "compute_offload") == 0) {
                    g_cl_layer_state.strategy = MVGAL_STRATEGY_COMPUTE_OFFLOAD;
                }
            } else {
                g_cl_layer_state.strategy = MVGAL_STRATEGY_ROUND_ROBIN;
            }
            
            // Create MVGAL context
            mvgal_context_create(&g_cl_layer_state.mvgal_context);
            mvgal_scheduler_set_strategy(g_cl_layer_state.mvgal_context, g_cl_layer_state.strategy);
            
            MVGAL_LOG_INFO("MVGAL OpenCL Layer: Initialized with %d GPUs, strategy=%d",
                          g_cl_layer_state.mvgal_gpu_count, g_cl_layer_state.strategy);
        }
    }
    
    // Cache original function pointers
    g_cl_layer_state.original_clGetPlatformIDs = dlsym(RTLD_NEXT, "clGetPlatformIDs");
    g_cl_layer_state.original_clGetPlatformInfo = dlsym(RTLD_NEXT, "clGetPlatformInfo");
    g_cl_layer_state.original_clGetDeviceIDs = dlsym(RTLD_NEXT, "clGetDeviceIDs");
    g_cl_layer_state.original_clGetDeviceInfo = dlsym(RTLD_NEXT, "clGetDeviceInfo");
    g_cl_layer_state.original_clCreateContext = dlsym(RTLD_NEXT, "clCreateContext");
    g_cl_layer_state.original_clCreateContextFromType = dlsym(RTLD_NEXT, "clCreateContextFromType");
    g_cl_layer_state.original_clReleaseContext = dlsym(RTLD_NEXT, "clReleaseContext");
    g_cl_layer_state.original_clRetainContext = dlsym(RTLD_NEXT, "clRetainContext");
    g_cl_layer_state.original_clGetContextInfo = dlsym(RTLD_NEXT, "clGetContextInfo");
    g_cl_layer_state.original_clCreateCommandQueue = dlsym(RTLD_NEXT, "clCreateCommandQueue");
    g_cl_layer_state.original_clReleaseCommandQueue = dlsym(RTLD_NEXT, "clReleaseCommandQueue");
    g_cl_layer_state.original_clRetainCommandQueue = dlsym(RTLD_NEXT, "clRetainCommandQueue");
    g_cl_layer_state.original_clGetCommandQueueInfo = dlsym(RTLD_NEXT, "clGetCommandQueueInfo");
    g_cl_layer_state.original_clCreateBuffer = dlsym(RTLD_NEXT, "clCreateBuffer");
    g_cl_layer_state.original_clCreateSubBuffer = dlsym(RTLD_NEXT, "clCreateSubBuffer");
    g_cl_layer_state.original_clRetainMemObject = dlsym(RTLD_NEXT, "clRetainMemObject");
    g_cl_layer_state.original_clReleaseMemObject = dlsym(RTLD_NEXT, "clReleaseMemObject");
    g_cl_layer_state.original_clGetMemObjectInfo = dlsym(RTLD_NEXT, "clGetMemObjectInfo");
    g_cl_layer_state.original_clCreateProgramWithSource = dlsym(RTLD_NEXT, "clCreateProgramWithSource");
    g_cl_layer_state.original_clCreateProgramWithBinary = dlsym(RTLD_NEXT, "clCreateProgramWithBinary");
    g_cl_layer_state.original_clRetainProgram = dlsym(RTLD_NEXT, "clRetainProgram");
    g_cl_layer_state.original_clReleaseProgram = dlsym(RTLD_NEXT, "clReleaseProgram");
    g_cl_layer_state.original_clBuildProgram = dlsym(RTLD_NEXT, "clBuildProgram");
    g_cl_layer_state.original_clCreateKernel = dlsym(RTLD_NEXT, "clCreateKernel");
    g_cl_layer_state.original_clCreateKernelsInProgram = dlsym(RTLD_NEXT, "clCreateKernelsInProgram");
    g_cl_layer_state.original_clRetainKernel = dlsym(RTLD_NEXT, "clRetainKernel");
    g_cl_layer_state.original_clReleaseKernel = dlsym(RTLD_NEXT, "clReleaseKernel");
    g_cl_layer_state.original_clSetKernelArg = dlsym(RTLD_NEXT, "clSetKernelArg");
    g_cl_layer_state.original_clGetKernelInfo = dlsym(RTLD_NEXT, "clGetKernelInfo");
    g_cl_layer_state.original_clGetKernelWorkGroupInfo = dlsym(RTLD_NEXT, "clGetKernelWorkGroupInfo");
    g_cl_layer_state.original_clEnqueueNDRangeKernel = dlsym(RTLD_NEXT, "clEnqueueNDRangeKernel");
    g_cl_layer_state.original_clEnqueueTask = dlsym(RTLD_NEXT, "clEnqueueTask");
    g_cl_layer_state.original_clEnqueueNativeKernel = dlsym(RTLD_NEXT, "clEnqueueNativeKernel");
    g_cl_layer_state.original_clEnqueueReadBuffer = dlsym(RTLD_NEXT, "clEnqueueReadBuffer");
    g_cl_layer_state.original_clEnqueueWriteBuffer = dlsym(RTLD_NEXT, "clEnqueueWriteBuffer");
    g_cl_layer_state.original_clEnqueueCopyBuffer = dlsym(RTLD_NEXT, "clEnqueueCopyBuffer");
    g_cl_layer_state.original_clEnqueueMapBuffer = dlsym(RTLD_NEXT, "clEnqueueMapBuffer");
    g_cl_layer_state.original_clEnqueueUnmapMemObject = dlsym(RTLD_NEXT, "clEnqueueUnmapMemObject");
    g_cl_layer_state.original_clCreateUserEvent = dlsym(RTLD_NEXT, "clCreateUserEvent");
    g_cl_layer_state.original_clRetainEvent = dlsym(RTLD_NEXT, "clRetainEvent");
    g_cl_layer_state.original_clReleaseEvent = dlsym(RTLD_NEXT, "clReleaseEvent");
    g_cl_layer_state.original_clGetEventInfo = dlsym(RTLD_NEXT, "clGetEventInfo");
    g_cl_layer_state.original_clWaitForEvents = dlsym(RTLD_NEXT, "clWaitForEvents");
    g_cl_layer_state.original_clFlush = dlsym(RTLD_NEXT, "clFlush");
    g_cl_layer_state.original_clFinish = dlsym(RTLD_NEXT, "clFinish");
    
    g_cl_layer_state.initialized = true;
    mvgal_cl_log_call("mvgal_cl_layer_init_global");
}

/**
 * @brief Shutdown the global layer state
 */
void mvgal_cl_layer_shutdown_global(void) {
    if (!g_cl_layer_state.initialized) return;
    
    if (g_cl_layer_state.mvgal_context) {
        mvgal_context_destroy(g_cl_layer_state.mvgal_context);
        g_cl_layer_state.mvgal_context = NULL;
    }
    
    mvgal_shutdown();
    
    pthread_mutex_destroy(&g_cl_layer_state.mutex);
    pthread_mutex_destroy(&g_cl_layer_state.mem_mutex);
    
    memset(&g_cl_layer_state, 0, sizeof(g_cl_layer_state));
    mvgal_cl_log_call("mvgal_cl_layer_shutdown_global");
}

/**
 * @brief Ensure layer is initialized
 */
static void ensure_initialized(void) {
    if (!g_cl_layer_state.initialized) {
        mvgal_cl_layer_init_global();
    }
}

/**
 * @brief Map OpenCL device to GPU index
 */
uint32_t mvgal_cl_map_device_to_gpu(void *device) {
    ensure_initialized();
    (void)device;
    
    if (!g_cl_layer_state.enabled || g_cl_layer_state.mvgal_gpu_count == 0) {
        return 0; // Default to first GPU
    }
    
    // Simple round-robin mapping for now
    // In production, this would use device properties to find the best match
    static uint32_t current_gpu = 0;
    uint32_t selected = current_gpu;
    current_gpu = (current_gpu + 1) % g_cl_layer_state.mvgal_gpu_count;
    return selected;
}

/**
 * @brief Get vendor from OpenCL device
 */
mvgal_cl_vendor_t mvgal_cl_get_vendor(void *device) {
    (void)device;
    // This would query device info to determine vendor
    // For now, return unknown
    return MVGAL_CL_VENDOR_UNKNOWN;
}

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Submit workload to MVGAL scheduler
 */
static void submit_workload(const char *kernel_name, size_t global_size) {
    (void)kernel_name;

    if (!g_cl_layer_state.enabled || !g_cl_layer_state.mvgal_context) {
        return;
    }
    
    // Store kernel name and size in user_data for tracking
    // For now, we'll just use the size as a simple metric
    mvgal_workload_submit_info_t info = {
        .type = MVGAL_WORKLOAD_OPENCL_KERNEL,
        .priority = 50,
        .gpu_mask = 0xFFFFFFFF, // All GPUs
        .deadline = 0,
        .dependency_count = 0,
        .dependencies = NULL,
        .user_data = (void *)(uintptr_t)global_size,
    };
    
    mvgal_workload_t workload;
    mvgal_error_t err = mvgal_workload_submit(g_cl_layer_state.mvgal_context, &info, &workload);
    if (err == MVGAL_SUCCESS) {
        g_cl_layer_state.kernels_submitted++;
        mvgal_workload_wait(workload, 5000); // Wait up to 5 seconds
        g_cl_layer_state.kernels_completed++;
    }
}

// =============================================================================
// Intercepted OpenCL Functions
// =============================================================================

// ---- Platform Functions ----

int clGetPlatformIDs(unsigned int num_entries, void **platforms, unsigned int *num_platforms) {
    ensure_initialized();
    mvgal_cl_log_call("clGetPlatformIDs");
    
    if (!g_cl_layer_state.original_clGetPlatformIDs) {
        if (num_platforms) *num_platforms = 0;
        return 0; // CL_INVALID_VALUE
    }
    
    typedef int (*func_t)(unsigned int, void **, unsigned int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetPlatformIDs;
    int ret = func(num_entries, platforms, num_platforms);
    
    if (g_cl_layer_state.debug && ret == 0 && num_platforms) {
        fprintf(stderr, "[MVGAL OpenCL] clGetPlatformIDs: %d platforms\n", *num_platforms);
    }
    
    return ret;
}

int clGetPlatformInfo(void *platform, unsigned int param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetPlatformInfo");
    
    if (!g_cl_layer_state.original_clGetPlatformInfo) {
        return -1; // CL_INVALID_VALUE
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetPlatformInfo;
    return func(platform, param_name, param_value_size, param_value, param_value_size_ret);
}

// ---- Device Functions ----

int clGetDeviceIDs(void *platform, unsigned long device_type, unsigned int num_entries,
                   void **devices, unsigned int *num_devices) {
    ensure_initialized();
    mvgal_cl_log_call("clGetDeviceIDs");
    
    if (!g_cl_layer_state.original_clGetDeviceIDs) {
        if (num_devices) *num_devices = 0;
        return -1; // CL_INVALID_VALUE
    }
    
    typedef int (*func_t)(void *, unsigned long, unsigned int, void **, unsigned int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetDeviceIDs;
    int ret = func(platform, device_type, num_entries, devices, num_devices);
    
    if (g_cl_layer_state.debug && ret == 0 && num_devices) {
        fprintf(stderr, "[MVGAL OpenCL] clGetDeviceIDs: %d devices\n", *num_devices);
    }
    
    return ret;
}

int clGetDeviceInfo(void *device, unsigned int param_name, size_t param_value_size,
                    void *param_value, size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetDeviceInfo");
    
    if (!g_cl_layer_state.original_clGetDeviceInfo) {
        return -1; // CL_INVALID_VALUE
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetDeviceInfo;
    return func(device, param_name, param_value_size, param_value, param_value_size_ret);
}

// ---- Context Functions ----

void* clCreateContext(const void *properties, unsigned int num_devices, const void **devices,
                      void *pfn_notify, void *user_data, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateContext");
    
    if (!g_cl_layer_state.original_clCreateContext) {
        if (errcode_ret) *errcode_ret = -1; // CL_INVALID_VALUE
        return NULL;
    }
    
    typedef void* (*func_t)(const void *, unsigned int, const void **, void *, void *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateContext;
    
    if (g_cl_layer_state.debug) {
        fprintf(stderr, "[MVGAL OpenCL] clCreateContext with %d devices\n", num_devices);
    }
    
    return func(properties, num_devices, devices, pfn_notify, user_data, errcode_ret);
}

void* clCreateContextFromType(const void *properties, unsigned int device_type,
                               void *pfn_notify, void *user_data, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateContextFromType");
    
    if (!g_cl_layer_state.original_clCreateContextFromType) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(const void *, unsigned int, void *, void *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateContextFromType;
    return func(properties, device_type, pfn_notify, user_data, errcode_ret);
}

int clRetainContext(void *context) {
    ensure_initialized();
    mvgal_cl_log_call("clRetainContext");
    
    if (!g_cl_layer_state.original_clRetainContext) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clRetainContext;
    return func(context);
}

int clReleaseContext(void *context) {
    ensure_initialized();
    mvgal_cl_log_call("clReleaseContext");
    
    if (!g_cl_layer_state.original_clReleaseContext) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clReleaseContext;
    return func(context);
}

int clGetContextInfo(void *context, unsigned int param_name, size_t param_value_size,
                    void *param_value, size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetContextInfo");
    
    if (!g_cl_layer_state.original_clGetContextInfo) {
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetContextInfo;
    return func(context, param_name, param_value_size, param_value, param_value_size_ret);
}

// ---- Command Queue Functions ----

void* clCreateCommandQueue(void *context, void *device, unsigned long properties, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateCommandQueue");
    
    if (!g_cl_layer_state.original_clCreateCommandQueue) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, void *, unsigned long, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateCommandQueue;
    
    if (g_cl_layer_state.debug) {
        uint32_t gpu_index = mvgal_cl_map_device_to_gpu(device);
        fprintf(stderr, "[MVGAL OpenCL] clCreateCommandQueue: device mapped to GPU %d\n", gpu_index);
    }
    
    return func(context, device, properties, errcode_ret);
}

int clRetainCommandQueue(void *command_queue) {
    ensure_initialized();
    mvgal_cl_log_call("clRetainCommandQueue");
    
    if (!g_cl_layer_state.original_clRetainCommandQueue) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clRetainCommandQueue;
    return func(command_queue);
}

int clReleaseCommandQueue(void *command_queue) {
    ensure_initialized();
    mvgal_cl_log_call("clReleaseCommandQueue");
    
    if (!g_cl_layer_state.original_clReleaseCommandQueue) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clReleaseCommandQueue;
    return func(command_queue);
}

int clGetCommandQueueInfo(void *command_queue, unsigned int param_name, size_t param_value_size,
                         void *param_value, size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetCommandQueueInfo");
    
    if (!g_cl_layer_state.original_clGetCommandQueueInfo) {
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetCommandQueueInfo;
    return func(command_queue, param_name, param_value_size, param_value, param_value_size_ret);
}

// ---- Memory Object Functions ----

void* clCreateBuffer(void *context, unsigned long flags, size_t size, void *host_ptr, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateBuffer");
    
    if (!g_cl_layer_state.original_clCreateBuffer) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, unsigned long, size_t, void *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateBuffer;
    
    void *buffer = func(context, flags, size, host_ptr, errcode_ret);
    if (buffer && g_cl_layer_state.enabled) {
        pthread_mutex_lock(&g_cl_layer_state.mem_mutex);
        g_cl_layer_state.buffers_created++;
        g_cl_layer_state.memory_allocated += size;
        pthread_mutex_unlock(&g_cl_layer_state.mem_mutex);
        
        if (g_cl_layer_state.debug) {
            fprintf(stderr, "[MVGAL OpenCL] clCreateBuffer: size=%zu bytes\n", size);
        }
    }
    
    return buffer;
}

void* clCreateSubBuffer(void *buffer, unsigned long flags, unsigned int buffer_create_type,
                        const void *buffer_create_info, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateSubBuffer");
    
    if (!g_cl_layer_state.original_clCreateSubBuffer) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, unsigned long, unsigned int, const void *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateSubBuffer;
    return func(buffer, flags, buffer_create_type, buffer_create_info, errcode_ret);
}

int clRetainMemObject(void *memobj) {
    ensure_initialized();
    mvgal_cl_log_call("clRetainMemObject");
    
    if (!g_cl_layer_state.original_clRetainMemObject) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clRetainMemObject;
    return func(memobj);
}

int clReleaseMemObject(void *memobj) {
    ensure_initialized();
    mvgal_cl_log_call("clReleaseMemObject");
    
    if (!g_cl_layer_state.original_clReleaseMemObject) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clReleaseMemObject;
    return func(memobj);
}

int clGetMemObjectInfo(void *memobj, unsigned int param_name, size_t param_value_size,
                       void *param_value, size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetMemObjectInfo");
    
    if (!g_cl_layer_state.original_clGetMemObjectInfo) {
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetMemObjectInfo;
    return func(memobj, param_name, param_value_size, param_value, param_value_size_ret);
}

// ---- Program Functions ----

void* clCreateProgramWithSource(void *context, unsigned int count, const char **strings,
                                const size_t *lengths, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateProgramWithSource");
    
    if (!g_cl_layer_state.original_clCreateProgramWithSource) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, unsigned int, const char **, const size_t *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateProgramWithSource;
    
    if (g_cl_layer_state.debug) {
        fprintf(stderr, "[MVGAL OpenCL] clCreateProgramWithSource: %d source strings\n", count);
    }
    
    return func(context, count, strings, lengths, errcode_ret);
}

void* clCreateProgramWithBinary(void *context, unsigned int num_devices, const void **device_list,
                                 const size_t *lengths, const unsigned char **binaries,
                                 int *binary_status, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateProgramWithBinary");
    
    if (!g_cl_layer_state.original_clCreateProgramWithBinary) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, unsigned int, const void **, const size_t *, const unsigned char **, int *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateProgramWithBinary;
    return func(context, num_devices, device_list, lengths, binaries, binary_status, errcode_ret);
}

int clRetainProgram(void *program) {
    ensure_initialized();
    mvgal_cl_log_call("clRetainProgram");
    
    if (!g_cl_layer_state.original_clRetainProgram) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clRetainProgram;
    return func(program);
}

int clReleaseProgram(void *program) {
    ensure_initialized();
    mvgal_cl_log_call("clReleaseProgram");
    
    if (!g_cl_layer_state.original_clReleaseProgram) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clReleaseProgram;
    return func(program);
}

int clBuildProgram(void *program, unsigned int num_devices, const void **device_list,
                    const char *options, void *pfn_notify, void *user_data) {
    ensure_initialized();
    mvgal_cl_log_call("clBuildProgram");
    
    if (!g_cl_layer_state.original_clBuildProgram) {
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, const void **, const char *, void *, void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clBuildProgram;
    return func(program, num_devices, device_list, options, pfn_notify, user_data);
}

// ---- Kernel Functions ----

void* clCreateKernel(void *program, const char *kernel_name, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateKernel");
    
    if (!g_cl_layer_state.original_clCreateKernel) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, const char *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateKernel;
    
    if (g_cl_layer_state.debug && kernel_name) {
        fprintf(stderr, "[MVGAL OpenCL] clCreateKernel: %s\n", kernel_name);
    }
    
    return func(program, kernel_name, errcode_ret);
}

int clCreateKernelsInProgram(void *program, unsigned int num_kernels, void **kernels,
                              unsigned int *num_kernels_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateKernelsInProgram");
    
    if (!g_cl_layer_state.original_clCreateKernelsInProgram) {
        if (num_kernels_ret) *num_kernels_ret = 0;
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, void **, unsigned int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateKernelsInProgram;
    return func(program, num_kernels, kernels, num_kernels_ret);
}

int clRetainKernel(void *kernel) {
    ensure_initialized();
    mvgal_cl_log_call("clRetainKernel");
    
    if (!g_cl_layer_state.original_clRetainKernel) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clRetainKernel;
    return func(kernel);
}

int clReleaseKernel(void *kernel) {
    ensure_initialized();
    mvgal_cl_log_call("clReleaseKernel");
    
    if (!g_cl_layer_state.original_clReleaseKernel) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clReleaseKernel;
    return func(kernel);
}

int clSetKernelArg(void *kernel, unsigned int arg_index, size_t arg_size, const void *arg_value) {
    ensure_initialized();
    mvgal_cl_log_call("clSetKernelArg");
    
    if (!g_cl_layer_state.original_clSetKernelArg) {
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, const void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clSetKernelArg;
    return func(kernel, arg_index, arg_size, arg_value);
}

int clGetKernelInfo(void *kernel, unsigned int param_name, size_t param_value_size,
                    void *param_value, size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetKernelInfo");
    
    if (!g_cl_layer_state.original_clGetKernelInfo) {
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetKernelInfo;
    return func(kernel, param_name, param_value_size, param_value, param_value_size_ret);
}

int clGetKernelWorkGroupInfo(void *kernel, void *device, unsigned int param_name,
                              size_t param_value_size, void *param_value,
                              size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetKernelWorkGroupInfo");
    
    if (!g_cl_layer_state.original_clGetKernelWorkGroupInfo) {
        return -1;
    }
    
    typedef int (*func_t)(void *, void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetKernelWorkGroupInfo;
    return func(kernel, device, param_name, param_value_size, param_value, param_value_size_ret);
}

// ---- Execution Functions (Key Distribution Points) ----

int clEnqueueNDRangeKernel(void *command_queue, void *kernel, unsigned int work_dim,
                           const size_t *global_work_offset, const size_t *global_work_size,
                           const size_t *local_work_size, unsigned int num_events_in_wait_list,
                           const void **event_wait_list, void **event) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueNDRangeKernel");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueNDRangeKernel) {
        if (g_cl_layer_state.original_clEnqueueNDRangeKernel) {
            typedef int (*func_t)(void *, void *, unsigned int, const size_t *, const size_t *, const size_t *,
                                  unsigned int, const void **, void **);
            func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueNDRangeKernel;
            return func(command_queue, kernel, work_dim, global_work_offset, global_work_size,
                       local_work_size, num_events_in_wait_list, event_wait_list, event);
        }
        return -1;
    }
    
    // =========================================================================
    // WORKLOAD DISTRIBUTION LOGIC
    // =========================================================================
    
    if (g_cl_layer_state.enabled && g_cl_layer_state.mvgal_gpu_count > 1) {
        // Calculate total work size
        size_t total_work_size = 1;
        if (global_work_size) {
            for (unsigned int i = 0; i < work_dim; i++) {
                total_work_size *= global_work_size[i];
            }
        }
        
        // Get kernel name for logging
        char kernel_name[256] = "unknown";
        if (kernel) {
            size_t name_size = 0;
            typedef int (*getInfo_func)(void *, unsigned int, size_t, void *, size_t *);
            getInfo_func getKernelInfo = (getInfo_func)g_cl_layer_state.original_clGetKernelInfo;
            if (getKernelInfo) {
                getKernelInfo(kernel, 0x103F, 256, kernel_name, &name_size);
            }
        }
        
        // Submit to MVGAL scheduler
        submit_workload(kernel_name, total_work_size);
        
        if (g_cl_layer_state.debug) {
            fprintf(stderr, "[MVGAL OpenCL] Distributed kernel '%s' with work size %zu\n",
                   kernel_name, total_work_size);
        }
    }
    
    // Call original function
    typedef int (*func_t)(void *, void *, unsigned int, const size_t *, const size_t *, const size_t *,
                          unsigned int, const void **, void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueNDRangeKernel;
    return func(command_queue, kernel, work_dim, global_work_offset, global_work_size,
               local_work_size, num_events_in_wait_list, event_wait_list, event);
}

int clEnqueueTask(void *command_queue, void *kernel, unsigned int num_events_in_wait_list,
                  const void **event_wait_list, void **event) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueTask");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueTask) {
        if (g_cl_layer_state.original_clEnqueueTask) {
            typedef int (*func_t)(void *, void *, unsigned int, const void **, void **);
            func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueTask;
            return func(command_queue, kernel, num_events_in_wait_list, event_wait_list, event);
        }
        return -1;
    }
    
    // Submit task workload to MVGAL
    if (g_cl_layer_state.enabled) {
        submit_workload("task_kernel", 1);
    }
    
    typedef int (*func_t)(void *, void *, unsigned int, const void **, void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueTask;
    return func(command_queue, kernel, num_events_in_wait_list, event_wait_list, event);
}

int clEnqueueNativeKernel(void *command_queue, void *user_func, void *args, size_t cb_args,
                           unsigned int num_mem_objects, const void **mem_list,
                           const void **args_mem_loc, unsigned int num_events_in_wait_list,
                           const void **event_wait_list, void **event) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueNativeKernel");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueNativeKernel) {
        return -1;
    }
    
    typedef int (*func_t)(void *, void *, void *, size_t, unsigned int, const void **,
                          const void **, unsigned int, const void **, void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueNativeKernel;
    return func(command_queue, user_func, args, cb_args, num_mem_objects, mem_list,
               args_mem_loc, num_events_in_wait_list, event_wait_list, event);
}

// ---- Memory Transfer Functions ----

int clEnqueueReadBuffer(void *command_queue, void *buffer, bool blocking_read,
                       size_t offset, size_t size, void *ptr,
                       unsigned int num_events_in_wait_list, const void **event_wait_list,
                       void **event) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueReadBuffer");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueReadBuffer) {
        return -1;
    }
    
    typedef int (*func_t)(void *, void *, bool, size_t, size_t, void *,
                          unsigned int, const void **, void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueReadBuffer;
    return func(command_queue, buffer, blocking_read, offset, size, ptr,
               num_events_in_wait_list, event_wait_list, event);
}

int clEnqueueWriteBuffer(void *command_queue, void *buffer, bool blocking_write,
                        size_t offset, size_t size, const void *ptr,
                        unsigned int num_events_in_wait_list, const void **event_wait_list,
                        void **event) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueWriteBuffer");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueWriteBuffer) {
        return -1;
    }
    
    typedef int (*func_t)(void *, void *, bool, size_t, size_t, const void *,
                          unsigned int, const void **, void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueWriteBuffer;
    return func(command_queue, buffer, blocking_write, offset, size, ptr,
               num_events_in_wait_list, event_wait_list, event);
}

int clEnqueueCopyBuffer(void *command_queue, void *src_buffer, void *dst_buffer,
                       size_t src_offset, size_t dst_offset, size_t size,
                       unsigned int num_events_in_wait_list, const void **event_wait_list,
                       void **event) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueCopyBuffer");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueCopyBuffer) {
        return -1;
    }
    
    typedef int (*func_t)(void *, void *, void *, size_t, size_t, size_t,
                          unsigned int, const void **, void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueCopyBuffer;
    return func(command_queue, src_buffer, dst_buffer, src_offset, dst_offset, size,
               num_events_in_wait_list, event_wait_list, event);
}

void* clEnqueueMapBuffer(void *command_queue, void *buffer, bool blocking_map,
                        unsigned long map_flags, size_t offset, size_t size,
                        unsigned int num_events_in_wait_list, const void **event_wait_list,
                        void **event, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueMapBuffer");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueMapBuffer) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, void *, bool, unsigned long, size_t, size_t,
                          unsigned int, const void **, void **, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueMapBuffer;
    return func(command_queue, buffer, blocking_map, map_flags, offset, size,
               num_events_in_wait_list, event_wait_list, event, errcode_ret);
}

int clEnqueueUnmapMemObject(void *command_queue, void *memobj, void *mapped_ptr,
                            unsigned int num_events_in_wait_list, const void **event_wait_list,
                            void **event) {
    ensure_initialized();
    mvgal_cl_log_call("clEnqueueUnmapMemObject");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clEnqueueUnmapMemObject) {
        return -1;
    }
    
    typedef int (*func_t)(void *, void *, void *,
                          unsigned int, const void **, void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clEnqueueUnmapMemObject;
    return func(command_queue, memobj, mapped_ptr,
               num_events_in_wait_list, event_wait_list, event);
}

// ---- Synchronization Functions ----

int clWaitForEvents(unsigned int num_events, const void **event_list) {
    ensure_initialized();
    mvgal_cl_log_call("clWaitForEvents");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clWaitForEvents) {
        return -1;
    }
    
    typedef int (*func_t)(unsigned int, const void **);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clWaitForEvents;
    return func(num_events, event_list);
}

int clFlush(void *command_queue) {
    ensure_initialized();
    mvgal_cl_log_call("clFlush");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clFlush) {
        return -1;
    }
    
    // Flush MVGAL context
    if (g_cl_layer_state.mvgal_context) {
        mvgal_flush(g_cl_layer_state.mvgal_context);
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clFlush;
    return func(command_queue);
}

int clFinish(void *command_queue) {
    ensure_initialized();
    mvgal_cl_log_call("clFinish");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clFinish) {
        return -1;
    }
    
    // Finish MVGAL context
    if (g_cl_layer_state.mvgal_context) {
        mvgal_finish(g_cl_layer_state.mvgal_context);
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clFinish;
    return func(command_queue);
}

// ---- Event Functions ----

void* clCreateUserEvent(void *context, int *errcode_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clCreateUserEvent");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clCreateUserEvent) {
        if (errcode_ret) *errcode_ret = -1;
        return NULL;
    }
    
    typedef void* (*func_t)(void *, int *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clCreateUserEvent;
    return func(context, errcode_ret);
}

int clRetainEvent(void *event) {
    ensure_initialized();
    mvgal_cl_log_call("clRetainEvent");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clRetainEvent) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clRetainEvent;
    return func(event);
}

int clReleaseEvent(void *event) {
    ensure_initialized();
    mvgal_cl_log_call("clReleaseEvent");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clReleaseEvent) {
        return -1;
    }
    
    typedef int (*func_t)(void *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clReleaseEvent;
    return func(event);
}

int clGetEventInfo(void *event, unsigned int param_name, size_t param_value_size,
                   void *param_value, size_t *param_value_size_ret) {
    ensure_initialized();
    mvgal_cl_log_call("clGetEventInfo");
    
    if (!g_cl_layer_state.enabled || !g_cl_layer_state.original_clGetEventInfo) {
        return -1;
    }
    
    typedef int (*func_t)(void *, unsigned int, size_t, void *, size_t *);
    func_t func = (func_t)(void *)g_cl_layer_state.original_clGetEventInfo;
    return func(event, param_name, param_value_size, param_value, param_value_size_ret);
}

// =============================================================================
// Statistics Functions (MVGAL-specific)
// =============================================================================

/**
 * @brief Get MVGAL OpenCL layer statistics
 */
void mvgal_cl_get_statistics(uint64_t *kernels_submitted, uint64_t *kernels_completed,
                              uint64_t *buffers_created, uint64_t *memory_allocated) {
    ensure_initialized();
    
    pthread_mutex_lock(&g_cl_layer_state.mem_mutex);
    if (kernels_submitted) *kernels_submitted = g_cl_layer_state.kernels_submitted;
    if (kernels_completed) *kernels_completed = g_cl_layer_state.kernels_completed;
    if (buffers_created) *buffers_created = g_cl_layer_state.buffers_created;
    if (memory_allocated) *memory_allocated = g_cl_layer_state.memory_allocated;
    pthread_mutex_unlock(&g_cl_layer_state.mem_mutex);
}
