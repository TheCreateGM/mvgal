/**
 * @file cl_intercept.h
 * @brief OpenCL Interception Layer Internal Header
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header provides internal definitions for the OpenCL interception layer.
 * It intercepts OpenCL API calls using LD_PRELOAD and distributes workloads
 * across multiple GPUs.
 */

#ifndef CL_INTERCEPT_MVGAL_H
#define CL_INTERCEPT_MVGAL_H

// MVGAL headers
#include "mvgal.h"
#include "mvgal_gpu.h"
#include "mvgal_log.h"
#include "mvgal_scheduler.h"
#include "mvgal_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup OpenCLIntercept
 * @{
 */

// Layer configuration
#define MVGAL_CL_LAYER_NAME "MVGAL_OpenCL"
#define MVGAL_CL_LAYER_DESCRIPTION "Multi-Vendor GPU Aggregation Layer - OpenCL"
#define MVGAL_CL_LAYER_VERSION 1

// Maximum number of devices
#define MVGAL_CL_MAX_DEVICES 16
#define MVGAL_CL_MAX_PLATFORMS 8

/**
 * @brief Environment variable names
 */
#define ENV_MVGAL_OPENCL_ENABLED "MVGAL_OPENCL_ENABLED"
#define ENV_MVGAL_OPENCL_DEBUG "MVGAL_OPENCL_DEBUG"
#define ENV_MVGAL_STRATEGY "MVGAL_STRATEGY"

/**
 * @brief OpenCL vendor types
 */
typedef enum {
    MVGAL_CL_VENDOR_UNKNOWN = 0,
    MVGAL_CL_VENDOR_NVIDIA,
    MVGAL_CL_VENDOR_AMD,
    MVGAL_CL_VENDOR_INTEL,
    MVGAL_CL_VENDOR_ARM,
    MVGAL_CL_VENDOR_QUALCOMM,
    MVGAL_CL_VENDOR_MESA,
    MVGAL_CL_VENDOR_POCL
} mvgal_cl_vendor_t;

/**
 * @brief MVGAL OpenCL layer state
 */
typedef struct {
    // Layer information
    bool initialized;
    bool enabled;
    bool debug;
    
    // Original OpenCL function pointers (void* to avoid header dependency)
    void *original_clGetPlatformIDs;
    void *original_clGetPlatformInfo;
    void *original_clGetDeviceIDs;
    void *original_clGetDeviceInfo;
    void *original_clCreateContext;
    void *original_clCreateContextFromType;
    void *original_clReleaseContext;
    void *original_clRetainContext;
    void *original_clGetContextInfo;
    void *original_clCreateCommandQueue;
    void *original_clReleaseCommandQueue;
    void *original_clRetainCommandQueue;
    void *original_clGetCommandQueueInfo;
    void *original_clCreateBuffer;
    void *original_clCreateSubBuffer;
    void *original_clRetainMemObject;
    void *original_clReleaseMemObject;
    void *original_clGetMemObjectInfo;
    void *original_clCreateProgramWithSource;
    void *original_clCreateProgramWithBinary;
    void *original_clRetainProgram;
    void *original_clReleaseProgram;
    void *original_clBuildProgram;
    void *original_clCreateKernel;
    void *original_clCreateKernelsInProgram;
    void *original_clRetainKernel;
    void *original_clReleaseKernel;
    void *original_clSetKernelArg;
    void *original_clGetKernelInfo;
    void *original_clGetKernelWorkGroupInfo;
    void *original_clEnqueueNDRangeKernel;
    void *original_clEnqueueTask;
    void *original_clEnqueueReadBuffer;
    void *original_clEnqueueWriteBuffer;
    void *original_clEnqueueCopyBuffer;
    void *original_clEnqueueMapBuffer;
    void *original_clEnqueueUnmapMemObject;
    void *original_clWaitForEvents;
    void *original_clFlush;
    void *original_clFinish;
    void *original_clCreateUserEvent;
    void *original_clRetainEvent;
    void *original_clReleaseEvent;
    void *original_clGetEventInfo;
    
    // MVGAL context
    mvgal_context_t mvgal_context;
    
    // Distribution strategy
    mvgal_distribution_strategy_t strategy;
    
    // Synchronization
    pthread_mutex_t mutex;
    
    // Statistics
    uint64_t kernels_submitted;
    uint64_t kernels_completed;
    uint64_t buffers_created;
    uint64_t memory_allocated;
    
    // GPU information
    mvgal_gpu_descriptor_t mvgal_gpus[32];
    uint32_t mvgal_gpu_count;
    
    // Memory tracking
    pthread_mutex_t mem_mutex;
} mvgal_cl_layer_state_t;

// Global layer state
extern mvgal_cl_layer_state_t g_cl_layer_state;

// =============================================================================
// Function declarations
// =============================================================================

// Initialization and cleanup
void mvgal_cl_layer_init_global(void);
void mvgal_cl_layer_shutdown_global(void);
mvgal_cl_vendor_t mvgal_cl_get_vendor(void *device);
uint32_t mvgal_cl_map_device_to_gpu(void *device);

// Logging
extern inline void mvgal_cl_log_call(const char *func) {
    if (g_cl_layer_state.debug) {
        MVGAL_LOG_TRACE("CL_LAYER: %s called", func);
    }
}

extern inline bool mvgal_cl_layer_is_enabled(void) {
    return g_cl_layer_state.enabled;
}

/** @} */ // end of OpenCLIntercept

#ifdef __cplusplus
}
#endif

#endif // CL_INTERCEPT_MVGAL_H
