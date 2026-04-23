/**
 * @file mvgal_intercept.h
 * @brief Minimal MVGAL header for interception wrappers
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header provides the minimal types and declarations needed by
 * API interception wrappers (CUDA, D3D, Metal, WebGPU, etc.)
 * without pulling in the full MVGAL implementation headers.
 */

#ifndef MVGAL_INTERCEPT_H
#define MVGAL_INTERCEPT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * Basic Types
 ******************************************************************************/

typedef int32_t mvgal_gpu_index_t;
typedef uint64_t mvgal_size_t;
typedef uint64_t mvgal_timestamp_t;

/******************************************************************************
 * Error Types
 ******************************************************************************/

typedef enum {
    MVGAL_SUCCESS = 0,
    MVGAL_ERROR_INVALID_ARGUMENT = 1,
    MVGAL_ERROR_OUT_OF_MEMORY = 2,
    MVGAL_ERROR_NOT_FOUND = 3,
    MVGAL_ERROR_TIMEOUT = 4,
    MVGAL_ERROR_UNSUPPORTED = 5,
    MVGAL_ERROR_BUSY = 6,
    MVGAL_ERROR_DEVICE_LOST = 7,
    MVGAL_ERROR_CONTEXT_LOST = 8,
    MVGAL_ERROR_NOT_INITIALIZED = 9,
    MVGAL_ERROR_ALREADY_INITIALIZED = 10,
    MVGAL_ERROR_INCOMPATIBLE = 11,
    MVGAL_ERROR_GPU_NOT_FOUND = 12,
    MVGAL_ERROR_NOT_SUPPORTED = 13,
    MVGAL_ERROR_DRIVER = 14,
    MVGAL_ERROR_MEMORY = 15,
    MVGAL_ERROR_INITIALIZATION = 16,
    MVGAL_ERROR_IPC = 17,
    MVGAL_ERROR_NO_GPUS = 18,
    MVGAL_ERROR_UNKNOWN = 19,
    MVGAL_ERROR_INTERRUPTED = 20,
    MVGAL_ERROR_SCHEDULER = 21,
} mvgal_error_t;

/******************************************************************************
 * Vendor Types
 ******************************************************************************/

typedef enum {
    MVGAL_VENDOR_UNKNOWN = 0,
    MVGAL_VENDOR_AMD = 0x1002,
    MVGAL_VENDOR_NVIDIA = 0x10DE,
    MVGAL_VENDOR_INTEL = 0x8086,
    MVGAL_VENDOR_MOORE_THREADS = 0x1ED5,
    MVGAL_VENDOR_QUALCOMM = 0x5143,
    MVGAL_VENDOR_ARM = 0x13B5,
    MVGAL_VENDOR_BROADCOM = 0x14E4,
} mvgal_vendor_t;

/******************************************************************************
 * GPU Type
 ******************************************************************************/

typedef enum {
    MVGAL_GPU_TYPE_DISCRETE = 1,
    MVGAL_GPU_TYPE_INTEGRATED = 2,
    MVGAL_GPU_TYPE_VIRTUAL = 3,
    MVGAL_GPU_TYPE_COPROCESSOR = 4,
} mvgal_gpu_type_t;

/******************************************************************************
 * Distribution Strategy Types
 ******************************************************************************/

typedef enum {
    MVGAL_STRATEGY_ROUND_ROBIN = 0,
    MVGAL_STRATEGY_AFR = 1,
    MVGAL_STRATEGY_SFR = 2,
    MVGAL_STRATEGY_COMPUTE_OFFLOAD = 4,
    MVGAL_STRATEGY_HYBRID = 5,
    MVGAL_STRATEGY_SINGLE_GPU = 6,
    MVGAL_STRATEGY_CUSTOM = 100,
} mvgal_distribution_strategy_t;

/******************************************************************************
 * API Types
 ******************************************************************************/

typedef uint64_t mvgal_api_type_t;
#define MVGAL_API_NONE      (0)
#define MVGAL_API_UNKNOWN   (0)
#define MVGAL_API_VULKAN    (1ULL << 0)
#define MVGAL_API_OPENGL    (1ULL << 1)
#define MVGAL_API_OPENCL    (1ULL << 2)
#define MVGAL_API_CUDA      (1ULL << 3)
#define MVGAL_API_D3D11     (1ULL << 4)
#define MVGAL_API_D3D12     (1ULL << 5)
#define MVGAL_API_METAL     (1ULL << 6)
#define MVGAL_API_WEBGPU    (1ULL << 7)
#define MVGAL_API_VA_API    (1ULL << 8)

/******************************************************************************
 * Memory Types
 ******************************************************************************/

typedef enum {
    MVGAL_MEMORY_TYPE_HBM = 0,
    MVGAL_MEMORY_TYPE_GDDR = 1,
    MVGAL_MEMORY_TYPE_DDR = 2,
    MVGAL_MEMORY_TYPE_LPDDR = 3,
    MVGAL_MEMORY_TYPE_SRAM = 4,
    MVGAL_MEMORY_TYPE_SHARED = 5,
    MVGAL_MEMORY_TYPE_UNIFIED = 6,
    MVGAL_MEMORY_TYPE_UNKNOWN = 255,
} mvgal_memory_type_t;

typedef enum {
    MVGAL_MEMORY_SHARING_NONE = 0,
    MVGAL_MEMORY_SHARING_EXCLUSIVE = 1,
    MVGAL_MEMORY_SHARING_CONCURRENT = 2,
    MVGAL_MEMORY_SHARING_CROSS_VENDOR = 3,
    MVGAL_MEMORY_SHARING_DMA_BUF = 4,
} mvgal_memory_sharing_mode_t;

typedef uint32_t mvgal_memory_access_flags_t;
#define MVGAL_MEMORY_ACCESS_READ    (1 << 0)
#define MVGAL_MEMORY_ACCESS_WRITE   (1 << 1)
#define MVGAL_MEMORY_ACCESS_RW      (MVGAL_MEMORY_ACCESS_READ | MVGAL_MEMORY_ACCESS_WRITE)
#define MVGAL_MEMORY_ACCESS_READ_WRITE MVGAL_MEMORY_ACCESS_RW

/**
 * @brief GPU feature flags
 */
typedef uint64_t mvgal_feature_flags_t;
#define MVGAL_FEATURE_DMA_BUF        (1ULL << 0)
#define MVGAL_FEATURE_CROSS_VENDOR   (1ULL << 1)
#define MVGAL_FEATURE_UNIFIED_MEMORY (1ULL << 2)
#define MVGAL_FEATURE_P2P_TRANSFER   (1ULL << 3)
#define MVGAL_FEATURE_GRAPHICS      (1ULL << 4)
#define MVGAL_FEATURE_COMPUTE       (1ULL << 5)
#define MVGAL_FEATURE_VIDEO_DECODE  (1ULL << 6)
#define MVGAL_FEATURE_VIDEO_ENCODE  (1ULL << 7)
#define MVGAL_FEATURE_AI_ACCEL      (1ULL << 8)
#define MVGAL_FEATURE_RAY_TRACING   (1ULL << 9)

/**
 * @brief Memory copy method
 */
typedef enum {
    MVGAL_MEMORY_COPY_CPU = 0,
    MVGAL_MEMORY_COPY_DMA_BUF = 1,
    MVGAL_MEMORY_COPY_NVLINK = 2,
    MVGAL_MEMORY_COPY_P2P = 3,
} mvgal_memory_copy_method_t;

/******************************************************************************
 * PCIe Types
 ******************************************************************************/

typedef enum {
    MVGAL_PCIE_GEN1 = 1,
    MVGAL_PCIE_GEN2 = 2,
    MVGAL_PCIE_GEN3 = 3,
    MVGAL_PCIE_GEN4 = 4,
    MVGAL_PCIE_GEN5 = 5,
    MVGAL_PCIE_GEN6 = 6,
    MVGAL_PCIE_UNKNOWN = 0,
} mvgal_pcie_generation_t;

/******************************************************************************
 * Workload Types
 ******************************************************************************/

typedef enum {
    // Generic workloads
    MVGAL_WORKLOAD_GRAPHICS = 0,
    MVGAL_WORKLOAD_COMPUTE = 1,
    MVGAL_WORKLOAD_VIDEO = 2,
    MVGAL_WORKLOAD_AI = 3,
    MVGAL_WORKLOAD_TRACE = 4,
    MVGAL_WORKLOAD_TRANSFER = 5,
    
    // Vulkan-specific
    MVGAL_WORKLOAD_VULKAN = 10,
    MVGAL_WORKLOAD_VULKAN_CMD = 11,
    
    // CUDA-specific
    MVGAL_WORKLOAD_CUDA_KERNEL = 20,
    MVGAL_WORKLOAD_CUDA_MEMORY = 21,
    MVGAL_WORKLOAD_CUDA_STREAM = 22,
    
    // Direct3D-specific
    MVGAL_WORKLOAD_D3D_CONTEXT = 30,
    MVGAL_WORKLOAD_D3D_QUEUE = 31,
    MVGAL_WORKLOAD_D3D_PIPELINE = 32,
    MVGAL_WORKLOAD_D3D_BUFFER = 33,
    MVGAL_WORKLOAD_D3D_TEXTURE = 34,
    
    // Metal-specific
    MVGAL_WORKLOAD_METAL_QUEUE = 40,
    MVGAL_WORKLOAD_METAL_BUFFER = 41,
    MVGAL_WORKLOAD_METAL_TEXTURE = 42,
    MVGAL_WORKLOAD_METAL_RENDER = 43,
    MVGAL_WORKLOAD_METAL_COMPUTE = 44,
    MVGAL_WORKLOAD_METAL_COMMAND = 45,
    MVGAL_WORKLOAD_METAL_COMMIT = 46,
    MVGAL_WORKLOAD_METAL_PRESENT = 47,
    
    // WebGPU-specific
    MVGAL_WORKLOAD_WEBGPU_QUEUE = 50,
    MVGAL_WORKLOAD_WEBGPU_BUFFER = 51,
    MVGAL_WORKLOAD_WEBGPU_TEXTURE = 52,
    MVGAL_WORKLOAD_WEBGPU_SHADER = 53,
    MVGAL_WORKLOAD_WEBGPU_BINDGROUP_LAYOUT = 54,
    MVGAL_WORKLOAD_WEBGPU_PIPELINE_LAYOUT = 55,
    MVGAL_WORKLOAD_WEBGPU_RENDER = 56,
    MVGAL_WORKLOAD_WEBGPU_COMPUTE = 57,
    MVGAL_WORKLOAD_WEBGPU_COMMAND = 58,
    MVGAL_WORKLOAD_WEBGPU_RENDER_PASS = 59,
    MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS = 60,
    MVGAL_WORKLOAD_WEBGPU_SUBMIT = 61,
    
    // OpenCL-specific
    MVGAL_WORKLOAD_OPENCL_KERNEL = 70,
    MVGAL_WORKLOAD_OPENCL_BUFFER = 71,
    MVGAL_WORKLOAD_OPENCL_QUEUE = 72,
    
    MVGAL_WORKLOAD_UNKNOWN = 100,
} mvgal_workload_type_t;

/******************************************************************************
 * Workload Structure for Interception Logging
 ******************************************************************************/

/**
 * @brief Simple workload descriptor for interception telemetry
 *
 * This is used by API wrappers to log/dispatch intercepted calls.
 * It's intentionally simple to avoid dependencies on the full MVGAL core.
 * Note: This is different from the mvgal_workload_t typedef in mvgal_scheduler.h
 * which is an opaque pointer handle type.
 */
typedef struct {
    mvgal_workload_type_t type;
    mvgal_gpu_index_t gpu_index;
    const char *step_name;
    mvgal_size_t data_size;
    mvgal_timestamp_t timestamp;
    
    // Flags
    struct {
        uint64_t is_commit : 1;
        uint64_t is_frame_start : 1;
        uint64_t is_frame_end : 1;
        uint64_t is_present : 1;
        uint64_t is_async : 1;
        uint64_t reserved : 59;
    } flags;
    
    // Dimensions
    struct {
        uint32_t x, y, z;
    } dims;
    
    void *user_data;
} mvgal_workload_telemetry_t;

// For compatibility with existing wrappers, define as the telemetry type
typedef mvgal_workload_telemetry_t mvgal_workload_t;

/******************************************************************************
 * Logging Types
 ******************************************************************************/

typedef enum {
    MVGAL_LOG_LEVEL_ERROR = 0,
    MVGAL_LOG_LEVEL_WARN = 1,
    MVGAL_LOG_LEVEL_INFO = 2,
    MVGAL_LOG_LEVEL_DEBUG = 3,
    MVGAL_LOG_LEVEL_TRACE = 4,
} mvgal_log_level_t;

/******************************************************************************
 * Opaque Handle Types
 ******************************************************************************/

typedef struct mvgal_context *mvgal_context_t;
typedef struct mvgal_fence *mvgal_fence_t;
typedef struct mvgal_semaphore *mvgal_semaphore_t;

/******************************************************************************
 * Statistics
 ******************************************************************************/

typedef struct {
    uint64_t frames_submitted;
    uint64_t frames_completed;
    uint64_t workloads_distributed;
    uint64_t bytes_transferred;
    uint64_t gpu_switches;
    uint64_t errors;
    uint64_t vulkan_workloads;
    uint64_t cuda_kernels;
    uint64_t opencl_kernels;
    uint64_t d3d_workloads;
    uint64_t metal_workloads;
    uint64_t webgpu_workloads;
    uint64_t dmabuf_exports;
    uint64_t dmabuf_imports;
    uint64_t cross_vendor_allocations;
    uint64_t cross_vendor_frees;
} mvgal_statistics_t;

typedef mvgal_statistics_t mvgal_stats_t;

/******************************************************************************
 * Rectangle Type
 ******************************************************************************/

typedef struct {
    int32_t x, y;
    uint32_t width, height;
} mvgal_rect_t;

/******************************************************************************
 * Simple Logging for Wrappers
 *
 * These are lightweight logging macros that don't require the full
 * MVGAL logging subsystem. Wrappers can override these if needed.
 ******************************************************************************/

#ifndef MVGAL_WRAPPER_SILENT
#define WRAPPER_LOG_ERROR(tag, ...)   fprintf(stderr, "[ERROR] [%s] " __VA_ARGS__, tag)
#define WRAPPER_LOG_WARN(tag, ...)    fprintf(stderr, "[WARN]  [%s] " __VA_ARGS__, tag)
#define WRAPPER_LOG_INFO(tag, ...)    fprintf(stderr, "[INFO]  [%s] " __VA_ARGS__, tag)
#define WRAPPER_LOG_DEBUG(tag, ...)   if (0) fprintf(stderr, "[DEBUG] [%s] " __VA_ARGS__, tag)
#else
#define WRAPPER_LOG_ERROR(tag, ...)
#define WRAPPER_LOG_WARN(tag, ...)
#define WRAPPER_LOG_INFO(tag, ...)
#define WRAPPER_LOG_DEBUG(tag, ...)
#endif

#ifdef __cplusplus
}
#endif

#endif // MVGAL_INTERCEPT_H
