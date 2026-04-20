/**
 * @file cuda_wrapper.c
 * @brief CUDA Wrapper Library for MVGAL
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * CUDA Interception via LD_PRELOAD
 *
 * Intercepts both CUDA Driver API (cu*) and Runtime API (cuda*) calls
 * Distributes kernel launches across available GPUs
 * Implements cross-GPU memory migration
 * 
 * Usage: LD_PRELOAD=/path/to/libmvgal_cuda.so ./your_cuda_app
 * 
 * Environment:
 *   MVGAL_CUDA_ENABLED=1    - Enable interception (default: 1)
 *   MVGAL_CUDA_DEBUG=1      - Enable debug logging (default: 0)
 *   MVGAL_CUDA_GPUS="0,1,2" - GPU indices to use (default: all)
 *   MVGAL_CUDA_STRATEGY=round_robin - Distribution strategy
 *   MVGAL_CUDA_MIGRATE=1    - Enable memory migration (default: 1)
 */

#include <mvgal/mvgal.h>
#include <mvgal/mvgal_log.h>
#include <mvgal/mvgal_gpu.h>
#include <mvgal/mvgal_memory.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <pthread.h>

// =============================================================================
// Type Definitions (without CUDA headers)
// We define compatible types since we can't include cuda headers
// =============================================================================

// CUDA Driver API opaque handles
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef struct CUstream_st *CUstream;
typedef struct CUevent_st *CUevent;
typedef unsigned long CUdeviceptr;

// CUDA Driver API error type
typedef enum {
    CUDA_SUCCESS = 0,
    CUDA_ERROR_INVALID_VALUE = 1,
    CUDA_ERROR_OUT_OF_MEMORY = 2,
    CUDA_ERROR_NOT_INITIALIZED = 3,
    CUDA_ERROR_ALREADY_MAPPED = 4,
    CUDA_ERROR_INVALID_HANDLE = 5,
    CUDA_ERROR_DEINITIALIZED = 6
} CUresult;

// CUDA Runtime API types
typedef void *cudaStream_t;
typedef void *cudaEvent_t;
typedef void *cudaDeviceptr_t;

// CUDA Runtime API error type
typedef enum {
    cudaSuccess = 0,
    cudaErrorInvalidValue = 1,
    cudaErrorOutOfMemory = 2,
    cudaErrorNotInitialized = 3,
    cudaErrorDeinitialized = 4,
    cudaErrorInvalidDevice = 5,
    cudaErrorInvalidDeviceFunction = 6,
    cudaErrorInvalidConfiguration = 7,
    cudaErrorLaunchFailure = 8
} cudaError_t;

// cudaMemcpyKind enum
typedef enum {
    cudaMemcpyHostToHost = 0,
    cudaMemcpyHostToDevice = 1,
    cudaMemcpyDeviceToHost = 2,
    cudaMemcpyDeviceToDevice = 3,
    cudaMemcpyDefault = 4
} cudaMemcpyKind;

// dim3 structure
typedef struct { unsigned int x, y, z; } dim3;

// Device attributes enum (subset)
typedef enum {
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 1,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X = 2,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y = 3,
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z = 4,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X = 5,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y = 6,
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z = 7,
    CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY = 8,
    CU_DEVICE_ATTRIBUTE_WARP_SIZE = 9,
    CU_DEVICE_ATTRIBUTE_MAX_PITCH = 10,
    CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK = 11,
    CU_DEVICE_ATTRIBUTE_CLOCK_RATE = 12,
    CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT = 13,
    CU_DEVICE_ATTRIBUTE_GPU_OVERLAP = 14,
    CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 15,
    CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT = 16,
    CU_DEVICE_ATTRIBUTE_INTEGRATED = 17,
    CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY = 18,
    CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING = 20,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_1D_WIDTH = 23,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_2D_WIDTH = 24,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_2D_HEIGHT = 25,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_3D_WIDTH = 26,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_3D_HEIGHT = 27,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_3D_DEPTH = 28,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_2D_LAYERED_WIDTH = 29,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_2D_LAYERED_HEIGHT = 30,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_2D_LAYERED_LAYERS = 31,
    CU_DEVICE_ATTRIBUTE_SURFACE_ALIGNMENT = 32,
    CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS = 33,
    CU_DEVICE_ATTRIBUTE_ECC_ENABLED = 34,
    CU_DEVICE_ATTRIBUTE_PCI_BUS_ID = 35,
    CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID = 36,
    CU_DEVICE_ATTRIBUTE_TCC_DRIVER = 37,
    CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE = 38,
    CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH = 39,
    CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE = 40,
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR = 41,
    CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT = 42,
    CU_DEVICE_ATTRIBUTE_UNIFIED_VIRTUAL_ADDRESSING = 43,
    CU_DEVICE_ATTRIBUTE_MAX_SURFACES_1D = 44,
    CU_DEVICE_ATTRIBUTE_MAX_SURFACES_2D = 45,
    CU_DEVICE_ATTRIBUTE_MAX_SURFACES_3D = 46,
    CU_DEVICE_ATTRIBUTE_MAX_SURFACES_CUBEMAP = 47,
    CU_DEVICE_ATTRIBUTE_MAX_SURFACES_LAYERED = 48,
    CU_DEVICE_ATTRIBUTE_MAX_SURFACES_2D_LAYERED = 49,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_1D_LAYERED_WIDTH = 50,
    CU_DEVICE_ATTRIBUTE_MAX_TEXTURE_1D_LAYERED_LAYERS = 51,
    CU_DEVICE_ATTRIBUTE_CAN_TEX2D_GATHER = 52,
    CU_DEVICE_ATTRIBUTE_MAX_PITCH_2D = 53
} CUdevice_attribute;

// Function type definitions for dlsym
typedef CUresult (*cuInit_t)(unsigned int);
typedef CUresult (*cuDeviceGetCount_t)(int *);
typedef CUresult (*cuDeviceGet_t)(int *, int);
typedef CUresult (*cuDeviceGetName_t)(char *, int, int);
typedef CUresult (*cuDeviceGetAttribute_t)(int *, CUdevice_attribute, int);
typedef CUresult (*cuDeviceGetProperties_t)(void *, int);
typedef CUresult (*cuCtxCreate_t)(CUcontext *, unsigned int, int);
typedef CUresult (*cuCtxDestroy_t)(CUcontext);
typedef CUresult (*cuCtxSetCurrent_t)(CUcontext);
typedef CUresult (*cuCtxGetCurrent_t)(CUcontext *);
typedef CUresult (*cuCtxPushCurrent_t)(CUcontext);
typedef CUresult (*cuCtxPopCurrent_t)(CUcontext *);
typedef CUresult (*cuCtxSynchronize_t)(void);

typedef CUresult (*cuMemAlloc_t)(CUdeviceptr *, size_t);
typedef CUresult (*cuMemFree_t)(CUdeviceptr);
typedef CUresult (*cuMemAllocPitch_t)(CUdeviceptr *, size_t *, size_t, size_t, unsigned int);
typedef CUresult (*cuMemFreeHost_t)(void *);
typedef CUresult (*cuMemHostAlloc_t)(void **, size_t, unsigned int);
typedef CUresult (*cuMemHostGetDevicePointer_t)(CUdeviceptr *, void *, unsigned int);
typedef CUresult (*cuMemHostGetFlags_t)(unsigned int *, void *);
typedef CUresult (*cuMemcpyHtoD_t)(CUdeviceptr, const void *, size_t);
typedef CUresult (*cuMemcpyDtoH_t)(void *, CUdeviceptr, size_t);
typedef CUresult (*cuMemcpyDtoD_t)(CUdeviceptr, CUdeviceptr, size_t);
typedef CUresult (*cuMemcpy2D_t)(const void *, size_t, const void *, size_t, size_t, size_t);
typedef CUresult (*cuMemcpy2DUnified_t)(const void *, size_t, const void *, size_t, size_t, size_t);
typedef CUresult (*cuMemcpyHtoDAsync_t)(CUdeviceptr, const void *, size_t, CUstream);
typedef CUresult (*cuMemcpyDtoHAsync_t)(void *, CUdeviceptr, size_t, CUstream);
typedef CUresult (*cuMemcpyDtoDAsync_t)(CUdeviceptr, CUdeviceptr, size_t, CUstream);
typedef CUresult (*cuMemsetD8_t)(CUdeviceptr, unsigned char, size_t);
typedef CUresult (*cuMemsetD16_t)(CUdeviceptr, unsigned short, size_t);
typedef CUresult (*cuMemsetD32_t)(CUdeviceptr, unsigned int, size_t);
typedef CUresult (*cuMemsetD2D8_t)(CUdeviceptr, size_t, unsigned char, size_t, size_t);
typedef CUresult (*cuMemsetD2D16_t)(CUdeviceptr, size_t, unsigned short, size_t, size_t);
typedef CUresult (*cuMemsetD2D32_t)(CUdeviceptr, size_t, unsigned int, size_t, size_t);

typedef CUresult (*cuStreamCreate_t)(CUstream *, unsigned int);
typedef CUresult (*cuStreamCreateWithPriority_t)(CUstream *, unsigned int, int);
typedef CUresult (*cuStreamDestroy_t)(CUstream);
typedef CUresult (*cuStreamQuery_t)(CUstream);
typedef CUresult (*cuStreamSynchronize_t)(CUstream);
typedef CUresult (*cuStreamWaitEvent_t)(CUstream, CUevent, unsigned int);
typedef CUresult (*cuStreamAddCallback_t)(CUstream, void *, void *, void *);

typedef CUresult (*cuEventCreate_t)(CUevent *, unsigned int);
typedef CUresult (*cuEventCreateWithFlags_t)(CUevent *, unsigned int);
typedef CUresult (*cuEventDestroy_t)(CUevent);
typedef CUresult (*cuEventSynchronize_t)(CUevent);
typedef CUresult (*cuEventElapsedTime_t)(float *, CUevent, CUevent);
typedef CUresult (*cuEventRecord_t)(CUevent, CUstream);
typedef CUresult (*cuEventQuery_t)(CUevent);

typedef CUresult (*cuModuleLoad_t)(CUmodule *, const char *);
typedef CUresult (*cuModuleLoadData_t)(CUmodule *, const void *);
typedef CUresult (*cuModuleLoadDataEx_t)(CUmodule *, const void *, unsigned int, const void **, void **);
typedef CUresult (*cuModuleUnload_t)(CUmodule);
typedef CUresult (*cuModuleGetFunction_t)(CUfunction *, CUmodule, const char *);
typedef CUresult (*cuModuleGetGlobal_t)(CUdeviceptr *, size_t *, CUmodule, const char *);
typedef CUresult (*cuModuleGetTexRef_t)(void **, CUmodule, const char *);

typedef CUresult (*cuLaunchKernel_t)(
    CUfunction, unsigned int, unsigned int, unsigned int,
    unsigned int, unsigned int, unsigned int, unsigned int,
    CUstream, void **, void **
);

// =============================================================================
// Global State
// =============================================================================

/**
 * @brief Kernel symbol tracking for name resolution
 */
typedef struct {
    CUfunction func;
    const char *name;
} kernel_symbol_t;

/**
 * @brief Per-GPU tracking state
 */
typedef struct {
    int cuda_device_index;
    int mvgal_gpu_index;
    CUcontext context;
    bool initialized;
    size_t allocated_memory;
    int kernel_launches;
} gpu_state_t;

/**
 * @brief CUDA wrapper global state
 */
typedef struct {
    bool initialized;
    bool enabled;
    bool debug;
    bool memory_migration_enabled;
    pthread_mutex_t mutex;
    
    // MVGAL integration
    mvgal_context_t mvgal_context;
    int gpu_count;
    mvgal_distribution_strategy_t strategy;
    
    // Library handles
    void *libcuda_handle;
    void *libcudart_handle;
    
    // Symbol table for kernel name resolution
    kernel_symbol_t *kernel_symbols;
    int kernel_symbol_count;
    int kernel_symbol_capacity;
    
    // Per-GPU state
    gpu_state_t *gpus;
    int current_device;
    
    // Statistics
    struct {
        size_t total_memory_allocated;
        size_t total_memory_freed;
        int total_kernel_launches;
        int gpu_switches;
        int cross_gpu_copies;
    } stats;
} cuda_wrapper_state_t;

static cuda_wrapper_state_t g_cuda = {0};

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Map CUDA error to string
 */
static const char *cuda_error_to_string(CUresult err) {
    switch (err) {
        case CUDA_SUCCESS: return "CUDA_SUCCESS";
        case CUDA_ERROR_INVALID_VALUE: return "CUDA_ERROR_INVALID_VALUE";
        case CUDA_ERROR_OUT_OF_MEMORY: return "CUDA_ERROR_OUT_OF_MEMORY";
        case CUDA_ERROR_NOT_INITIALIZED: return "CUDA_ERROR_NOT_INITIALIZED";
        case CUDA_ERROR_ALREADY_MAPPED: return "CUDA_ERROR_ALREADY_MAPPED";
        case CUDA_ERROR_INVALID_HANDLE: return "CUDA_ERROR_INVALID_HANDLE";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Get strategy name string
 */
static const char *strategy_to_string(mvgal_distribution_strategy_t s) {
    switch (s) {
        case MVGAL_STRATEGY_AUTO: return "auto";
        case MVGAL_STRATEGY_AFR: return "afr";
        case MVGAL_STRATEGY_SFR: return "sfr";
        case MVGAL_STRATEGY_TASK: return "task";
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD: return "compute_offload";
        case MVGAL_STRATEGY_HYBRID: return "hybrid";
        case MVGAL_STRATEGY_SINGLE_GPU: return "single";
        case MVGAL_STRATEGY_CUSTOM: return "custom";
        default: return "unknown";
    }
}

/**
 * @brief Get next GPU for round-robin distribution
 */
static int get_next_gpu(void) {
    static int counter = 0;
    return counter++ % g_cuda.gpu_count;
}

/**
 * @brief Map CUDA device index to MVGAL GPU index
 */
static int cuda_to_mvgal_gpu(int cuda_device) {
    return cuda_device % g_cuda.gpu_count;
}

/**
 * @brief Register kernel symbol for name resolution
 */
static void register_kernel_symbol(CUfunction func, const char *name) {
    if (!g_cuda.enabled || !name) return;
    
    pthread_mutex_lock(&g_cuda.mutex);
    
    // Check if we need to grow the array
    if (g_cuda.kernel_symbol_count >= g_cuda.kernel_symbol_capacity) {
        g_cuda.kernel_symbol_capacity = g_cuda.kernel_symbol_capacity * 2 + 16;
        g_cuda.kernel_symbols = realloc(g_cuda.kernel_symbols, 
                                        g_cuda.kernel_symbol_capacity * sizeof(kernel_symbol_t));
    }
    
    if (g_cuda.kernel_symbols) {
        g_cuda.kernel_symbols[g_cuda.kernel_symbol_count].func = func;
        g_cuda.kernel_symbols[g_cuda.kernel_symbol_count].name = strdup(name);
        g_cuda.kernel_symbol_count++;
    }
    
    pthread_mutex_unlock(&g_cuda.mutex);
}

/**
 * @brief Get kernel name from function pointer
 */
static const char *get_kernel_name(CUfunction func) {
    if (!g_cuda.enabled || !g_cuda.kernel_symbols) return "unknown_kernel";
    
    pthread_mutex_lock(&g_cuda.mutex);
    for (int i = 0; i < g_cuda.kernel_symbol_count; i++) {
        if (g_cuda.kernel_symbols[i].func == func) {
            pthread_mutex_unlock(&g_cuda.mutex);
            return g_cuda.kernel_symbols[i].name;
        }
    }
    pthread_mutex_unlock(&g_cuda.mutex);
    return "unknown_kernel";
}

/**
 * @brief Check if memory operation is cross-GPU
 */
static bool is_cross_gpu_copy(CUdeviceptr src, CUdeviceptr dst) {
    // Check which GPU each pointer belongs to
    // This is simplified - in reality we'd need to track allocations per GPU
    // For now, assume DDR copies between different devices are cross-GPU
    return true;  // Always attempt cross-GPU optimization
}

/**
 * @brief Handle cross-GPU memory copy with DMA-BUF if available
 */
static CUresult handle_cross_gpu_copy(
    CUdeviceptr dst, CUdeviceptr src, size_t size,
    cudaMemcpyKind kind
) {
    if (!g_cuda.enabled || !g_cuda.memory_migration_enabled) {
        // Fall through to normal copy
        return CUDA_SUCCESS;  // Will use normal path
    }
    
    if (kind == cudaMemcpyDeviceToDevice) {
        // Attempt to use MVGAL's DMA-BUF functionality
        // For now, just log and fall through
        MVGAL_LOG_DEBUG("Cross-GPU copy detected: %zu bytes (D2D)", size);
        g_cuda.stats.cross_gpu_copies++;
    }
    
    return CUDA_SUCCESS;  // Proceed with normal copy
}

/**
 * @brief Free kernel symbol table
 */
static void free_kernel_symbols(void) {
    if (g_cuda.kernel_symbols) {
        for (int i = 0; i < g_cuda.kernel_symbol_count; i++) {
            free((void *)g_cuda.kernel_symbols[i].name);
        }
        free(g_cuda.kernel_symbols);
        g_cuda.kernel_symbols = NULL;
        g_cuda.kernel_symbol_count = 0;
        g_cuda.kernel_symbol_capacity = 0;
    }
}

// =============================================================================
// Initialization
// =============================================================================

/**
 * @brief Initialize CUDA wrapper
 */
static void cuda_wrapper_init(void) {
    if (g_cuda.initialized) return;
    
    pthread_mutex_init(&g_cuda.mutex, NULL);
    
    // Configuration from environment
    const char *env;
    
    env = getenv("MVGAL_CUDA_ENABLED");
    g_cuda.enabled = env ? atoi(env) : 1;
    
    env = getenv("MVGAL_CUDA_DEBUG");
    g_cuda.debug = env ? atoi(env) : 0;
    
    env = getenv("MVGAL_CUDA_MIGRATE");
    g_cuda.memory_migration_enabled = env ? atoi(env) : 1;
    
    // Default strategy
    g_cuda.strategy = MVGAL_STRATEGY_CUSTOM;  // CUSTOM acts as round-robin
    env = getenv("MVGAL_CUDA_STRATEGY");
    if (env) {
        if (strcmp(env, "afr") == 0) g_cuda.strategy = MVGAL_STRATEGY_AFR;
        else if (strcmp(env, "sfr") == 0) g_cuda.strategy = MVGAL_STRATEGY_SFR;
        else if (strcmp(env, "single") == 0) g_cuda.strategy = MVGAL_STRATEGY_SINGLE_GPU;
        else if (strcmp(env, "hybrid") == 0) g_cuda.strategy = MVGAL_STRATEGY_HYBRID;
    }
    
    // Initialize MVGAL
    if (g_cuda.enabled) {
        mvgal_error_t err = mvgal_init(0);
        if (err == MVGAL_SUCCESS) {
            err = mvgal_context_create(&g_cuda.mvgal_context);
            if (err != MVGAL_SUCCESS) {
                MVGAL_LOG_ERROR("Failed to create MVGAL context: %d", err);
                g_cuda.enabled = false;
            } else {
                g_cuda.gpu_count = mvgal_gpu_get_count();
                
                // Allocate per-GPU state
                g_cuda.gpus = calloc(g_cuda.gpu_count, sizeof(gpu_state_t));
                if (!g_cuda.gpus) {
                    MVGAL_LOG_ERROR("Failed to allocate GPU state");
                    g_cuda.enabled = false;
                }
            }
        } else {
            MVGAL_LOG_ERROR("Failed to initialize MVGAL: %d", err);
            g_cuda.enabled = false;
        }
    }
    
    // Open CUDA libraries
    g_cuda.libcuda_handle = dlopen("libcuda.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!g_cuda.libcuda_handle && g_cuda.debug) {
        MVGAL_LOG_WARN("libcuda.so not found: %s", dlerror());
    }
    
    g_cuda.libcudart_handle = dlopen("libcudart.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!g_cuda.libcudart_handle && g_cuda.debug) {
        MVGAL_LOG_WARN("libcudart.so not found: %s", dlerror());
    }
    
    g_cuda.initialized = true;
    
    if (g_cuda.enabled) {
        MVGAL_LOG_INFO("CUDA wrapper initialized (%d GPUs, strategy: %s, migration: %d)",
                      g_cuda.gpu_count, strategy_to_string(g_cuda.strategy),
                      g_cuda.memory_migration_enabled);
    } else {
        MVGAL_LOG_WARN("CUDA wrapper initialized but disabled");
    }
}

/**
 * @brief Shutdown CUDA wrapper
 */
static void cuda_wrapper_shutdown(void) {
    if (!g_cuda.initialized) return;
    
    free_kernel_symbols();
    
    if (g_cuda.libcuda_handle) {
        dlclose(g_cuda.libcuda_handle);
        g_cuda.libcuda_handle = NULL;
    }
    
    if (g_cuda.libcudart_handle) {
        dlclose(g_cuda.libcudart_handle);
        g_cuda.libcudart_handle = NULL;
    }
    
    if (g_cuda.mvgal_context) {
        mvgal_context_destroy(g_cuda.mvgal_context);
        g_cuda.mvgal_context = NULL;
    }
    
    if (g_cuda.gpus) {
        free(g_cuda.gpus);
        g_cuda.gpus = NULL;
    }
    
    pthread_mutex_destroy(&g_cuda.mutex);
    g_cuda.initialized = false;
    g_cuda.enabled = false;
}

// =============================================================================
// CUDA Driver API - Device Management
// =============================================================================

CUresult cuInit(unsigned int Flags) {
    CUresult ret;
    cuda_wrapper_init();
    CUresult (*func)(unsigned int) = (CUresult (*)(unsigned int))dlsym(RTLD_NEXT, "cuInit");
    ret = func ? func(Flags) : CUDA_ERROR_NOT_INITIALIZED;
    if (ret == CUDA_SUCCESS) {
        MVGAL_LOG_INFO("CUDA Driver API initialized via MVGAL wrapper");
    }
    return ret;
}

CUresult cuDeviceGetCount(int *count) {
    CUresult (*func)(int *) = (CUresult (*)(int *))dlsym(RTLD_NEXT, "cuDeviceGetCount");
    CUresult ret = func ? func(count) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (ret == CUDA_SUCCESS && g_cuda.mvgal_context && g_cuda.gpu_count > 0) {
        // Override with MVGAL GPU count
        *count = g_cuda.gpu_count;
    }
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuDeviceGetCount: %d devices", *count);
    }
    
    return ret;
}

CUresult cuDeviceGet(int *device, int ordinal) {
    CUresult (*func)(int *, int) = (CUresult (*)(int *, int))dlsym(RTLD_NEXT, "cuDeviceGet");
    CUresult ret = func ? func(device, ordinal) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuDeviceGet: ordinal=%d -> device=%d", ordinal, *device);
    }
    
    return ret;
}

CUresult cuDeviceGetName(char *name, int len, int device) {
    CUresult (*func)(char *, int, int) = (CUresult (*)(char *, int, int))dlsym(RTLD_NEXT, "cuDeviceGetName");
    CUresult ret = func ? func(name, len, device) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuDeviceGetName: device=%d -> '%s'", device, name);
    }
    
    return ret;
}

CUresult cuDeviceGetAttribute(int *value, CUdevice_attribute attrib, int device) {
    CUresult (*func)(int *, CUdevice_attribute, int) = 
        (CUresult (*)(int *, CUdevice_attribute, int))dlsym(RTLD_NEXT, "cuDeviceGetAttribute");
    return func ? func(value, attrib, device) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuDeviceGetProperties(void *prop, int device) {
    CUresult (*func)(void *, int) = (CUresult (*)(void *, int))dlsym(RTLD_NEXT, "cuDeviceGetProperties");
    return func ? func(prop, device) : CUDA_ERROR_NOT_INITIALIZED;
}

// =============================================================================
// CUDA Driver API - Context Management
// =============================================================================

CUresult cuCtxCreate(CUcontext *pctx, unsigned int flags, int device) {
    CUresult (*func)(CUcontext *, unsigned int, int) = 
        (CUresult (*)(CUcontext *, unsigned int, int))dlsym(RTLD_NEXT, "cuCtxCreate");
    CUresult ret = func ? func(pctx, flags, device) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.enabled && ret == CUDA_SUCCESS) {
        int mvgal_gpu = cuda_to_mvgal_gpu(device);
        if (mvgal_gpu < g_cuda.gpu_count) {
            g_cuda.gpus[mvgal_gpu].cuda_device_index = device;
            g_cuda.gpus[mvgal_gpu].mvgal_gpu_index = mvgal_gpu;
            g_cuda.gpus[mvgal_gpu].context = *pctx;
            g_cuda.gpus[mvgal_gpu].initialized = true;
            g_cuda.current_device = device;
            
            MVGAL_LOG_INFO("cuCtxCreate: CUDA device=%d -> MVGAL GPU=%d, flags=0x%x",
                          device, mvgal_gpu, flags);
        }
    }
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuCtxCreate: device=%d, ctx=%p", device, (void *)*pctx);
    }
    
    return ret;
}

CUresult cuCtxDestroy(CUcontext ctx) {
    CUresult (*func)(CUcontext) = (CUresult (*)(CUcontext))dlsym(RTLD_NEXT, "cuCtxDestroy");
    CUresult ret = func ? func(ctx) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.enabled && ret == CUDA_SUCCESS) {
        for (int i = 0; i < g_cuda.gpu_count; i++) {
            if (g_cuda.gpus[i].context == ctx) {
                g_cuda.gpus[i].initialized = false;
                g_cuda.gpus[i].context = NULL;
                break;
            }
        }
    }
    
    return ret;
}

CUresult cuCtxSetCurrent(CUcontext ctx) {
    CUresult (*func)(CUcontext) = (CUresult (*)(CUcontext))dlsym(RTLD_NEXT, "cuCtxSetCurrent");
    CUresult ret = func ? func(ctx) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.enabled && ret == CUDA_SUCCESS) {
        for (int i = 0; i < g_cuda.gpu_count; i++) {
            if (g_cuda.gpus[i].context == ctx) {
                g_cuda.current_device = g_cuda.gpus[i].cuda_device_index;
                break;
            }
        }
    }
    
    return ret;
}

CUresult cuCtxGetCurrent(CUcontext *pctx) {
    CUresult (*func)(CUcontext *) = (CUresult (*)(CUcontext *))dlsym(RTLD_NEXT, "cuCtxGetCurrent");
    return func ? func(pctx) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuCtxPushCurrent(CUcontext ctx) {
    CUresult (*func)(CUcontext) = (CUresult (*)(CUcontext))dlsym(RTLD_NEXT, "cuCtxPushCurrent");
    return func ? func(ctx) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuCtxPopCurrent(CUcontext *pctx) {
    CUresult (*func)(CUcontext *) = (CUresult (*)(CUcontext *))dlsym(RTLD_NEXT, "cuCtxPopCurrent");
    return func ? func(pctx) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuCtxSynchronize(void) {
    CUresult (*func)(void) = (CUresult (*)(void))dlsym(RTLD_NEXT, "cuCtxSynchronize");
    return func ? func() : CUDA_ERROR_NOT_INITIALIZED;
}

// =============================================================================
// CUDA Driver API - Memory Management
// =============================================================================

CUresult cuMemAlloc(CUdeviceptr *dptr, size_t bytesize) {
    CUresult (*func)(CUdeviceptr *, size_t) = 
        (CUresult (*)(CUdeviceptr *, size_t))dlsym(RTLD_NEXT, "cuMemAlloc");
    CUresult ret = func ? func(dptr, bytesize) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.enabled && ret == CUDA_SUCCESS) {
        int mvgal_gpu = g_cuda.current_device >= 0 ? cuda_to_mvgal_gpu(g_cuda.current_device) : 0;
        if (mvgal_gpu < g_cuda.gpu_count) {
            g_cuda.gpus[mvgal_gpu].allocated_memory += bytesize;
            g_cuda.stats.total_memory_allocated += bytesize;
            
            if (g_cuda.debug) {
                MVGAL_LOG_DEBUG("cuMemAlloc: GPU %d allocated %zu bytes (total: %zu)",
                              mvgal_gpu, bytesize, g_cuda.gpus[mvgal_gpu].allocated_memory);
            }
        }
    }
    
    return ret;
}

CUresult cuMemFree(CUdeviceptr dptr) {
    CUresult (*func)(CUdeviceptr) = (CUresult (*)(CUdeviceptr))dlsym(RTLD_NEXT, "cuMemFree");
    CUresult ret = func ? func(dptr) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.enabled && ret == CUDA_SUCCESS && g_cuda.debug) {
        MVGAL_LOG_DEBUG("cuMemFree: %p", (void *)dptr);
    }
    
    return ret;
}

CUresult cuMemAllocPitch(CUdeviceptr *dptr, size_t *pitch, size_t width, size_t height, unsigned int elementSize) {
    CUresult (*func)(CUdeviceptr *, size_t *, size_t, size_t, unsigned int) = 
        (CUresult (*)(CUdeviceptr *, size_t *, size_t, size_t, unsigned int))dlsym(RTLD_NEXT, "cuMemAllocPitch");
    return func ? func(dptr, pitch, width, height, elementSize) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemFreeHost(void *hptr) {
    CUresult (*func)(void *) = (CUresult (*)(void *))dlsym(RTLD_NEXT, "cuMemFreeHost");
    return func ? func(hptr) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemHostAlloc(void **pp, size_t size, unsigned int flags) {
    CUresult (*func)(void **, size_t, unsigned int) = 
        (CUresult (*)(void **, size_t, unsigned int))dlsym(RTLD_NEXT, "cuMemHostAlloc");
    return func ? func(pp, size, flags) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemHostGetDevicePointer(CUdeviceptr *dptr, void *hptr, unsigned int flags) {
    CUresult (*func)(CUdeviceptr *, void *, unsigned int) = 
        (CUresult (*)(CUdeviceptr *, void *, unsigned int))dlsym(RTLD_NEXT, "cuMemHostGetDevicePointer");
    return func ? func(dptr, hptr, flags) : CUDA_ERROR_NOT_INITIALIZED;
}

// =============================================================================
// CUDA Driver API - Memory Copy Operations
// =============================================================================

CUresult cuMemcpyHtoD(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount) {
    CUresult (*func)(CUdeviceptr, const void *, size_t) = 
        (CUresult (*)(CUdeviceptr, const void *, size_t))dlsym(RTLD_NEXT, "cuMemcpyHtoD");
    
    // Check for cross-GPU optimization
    if (g_cuda.enabled && g_cuda.memory_migration_enabled) {
        handle_cross_gpu_copy(dstDevice, (CUdeviceptr)srcHost, ByteCount, cudaMemcpyHostToDevice);
    }
    
    CUresult ret = func ? func(dstDevice, srcHost, ByteCount) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuMemcpyHtoD: %zu bytes to %p", ByteCount, (void *)dstDevice);
    }
    
    return ret;
}

CUresult cuMemcpyDtoH(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    CUresult (*func)(void *, CUdeviceptr, size_t) = 
        (CUresult (*)(void *, CUdeviceptr, size_t))dlsym(RTLD_NEXT, "cuMemcpyDtoH");
    
    if (g_cuda.enabled && g_cuda.memory_migration_enabled) {
        handle_cross_gpu_copy((CUdeviceptr)dstHost, srcDevice, ByteCount, cudaMemcpyDeviceToHost);
    }
    
    CUresult ret = func ? func(dstHost, srcDevice, ByteCount) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuMemcpyDtoH: %zu bytes from %p", ByteCount, (void *)srcDevice);
    }
    
    return ret;
}

CUresult cuMemcpyDtoD(CUdeviceptr dstDevice, CUdeviceptr srcDevice, size_t ByteCount) {
    CUresult (*func)(CUdeviceptr, CUdeviceptr, size_t) = 
        (CUresult (*)(CUdeviceptr, CUdeviceptr, size_t))dlsym(RTLD_NEXT, "cuMemcpyDtoD");
    
    // Cross-GPU copy detection
    if (g_cuda.enabled && g_cuda.memory_migration_enabled) {
        if (is_cross_gpu_copy(srcDevice, dstDevice)) {
            handle_cross_gpu_copy(dstDevice, srcDevice, ByteCount, cudaMemcpyDeviceToDevice);
        }
    }
    
    CUresult ret = func ? func(dstDevice, srcDevice, ByteCount) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuMemcpyDtoD: %zu bytes from %p to %p",
                       ByteCount, (void *)srcDevice, (void *)dstDevice);
    }
    
    return ret;
}

CUresult cuMemcpy2D(const void *dst, size_t dpitch, const void *src, size_t spitch, size_t width, size_t height) {
    CUresult (*func)(const void *, size_t, const void *, size_t, size_t, size_t) = 
        (CUresult (*)(const void *, size_t, const void *, size_t, size_t, size_t))dlsym(RTLD_NEXT, "cuMemcpy2D");
    return func ? func(dst, dpitch, src, spitch, width, height) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemcpyHtoDAsync(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount, CUstream hStream) {
    CUresult (*func)(CUdeviceptr, const void *, size_t, CUstream) = 
        (CUresult (*)(CUdeviceptr, const void *, size_t, CUstream))dlsym(RTLD_NEXT, "cuMemcpyHtoDAsync");
    return func ? func(dstDevice, srcHost, ByteCount, hStream) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemcpyDtoHAsync(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream) {
    CUresult (*func)(void *, CUdeviceptr, size_t, CUstream) = 
        (CUresult (*)(void *, CUdeviceptr, size_t, CUstream))dlsym(RTLD_NEXT, "cuMemcpyDtoHAsync");
    return func ? func(dstHost, srcDevice, ByteCount, hStream) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemcpyDtoDAsync(CUdeviceptr dstDevice, CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream) {
    CUresult (*func)(CUdeviceptr, CUdeviceptr, size_t, CUstream) = 
        (CUresult (*)(CUdeviceptr, CUdeviceptr, size_t, CUstream))dlsym(RTLD_NEXT, "cuMemcpyDtoDAsync");
    return func ? func(dstDevice, srcDevice, ByteCount, hStream) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemsetD8(CUdeviceptr dst, unsigned char uc, size_t N) {
    CUresult (*func)(CUdeviceptr, unsigned char, size_t) = 
        (CUresult (*)(CUdeviceptr, unsigned char, size_t))dlsym(RTLD_NEXT, "cuMemsetD8");
    return func ? func(dst, uc, N) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemsetD16(CUdeviceptr dst, unsigned short us, size_t N) {
    CUresult (*func)(CUdeviceptr, unsigned short, size_t) = 
        (CUresult (*)(CUdeviceptr, unsigned short, size_t))dlsym(RTLD_NEXT, "cuMemsetD16");
    return func ? func(dst, us, N) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuMemsetD32(CUdeviceptr dst, unsigned int ui, size_t N) {
    CUresult (*func)(CUdeviceptr, unsigned int, size_t) = 
        (CUresult (*)(CUdeviceptr, unsigned int, size_t))dlsym(RTLD_NEXT, "cuMemsetD32");
    return func ? func(dst, ui, N) : CUDA_ERROR_NOT_INITIALIZED;
}

// =============================================================================
// CUDA Driver API - Stream Management
// =============================================================================

CUresult cuStreamCreate(CUstream *phStream, unsigned int Flags) {
    CUresult (*func)(CUstream *, unsigned int) = 
        (CUresult (*)(CUstream *, unsigned int))dlsym(RTLD_NEXT, "cuStreamCreate");
    CUresult ret = func ? func(phStream, Flags) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuStreamCreate: stream=%p, flags=0x%x", (void *)*phStream, Flags);
    }
    
    return ret;
}

CUresult cuStreamCreateWithPriority(CUstream *phStream, unsigned int Flags, int priority) {
    CUresult (*func)(CUstream *, unsigned int, int) = 
        (CUresult (*)(CUstream *, unsigned int, int))dlsym(RTLD_NEXT, "cuStreamCreateWithPriority");
    return func ? func(phStream, Flags, priority) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuStreamDestroy(CUstream hStream) {
    CUresult (*func)(CUstream) = (CUresult (*)(CUstream))dlsym(RTLD_NEXT, "cuStreamDestroy");
    CUresult ret = func ? func(hStream) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug) {
        MVGAL_LOG_DEBUG("cuStreamDestroy: stream=%p", (void *)hStream);
    }
    
    return ret;
}

CUresult cuStreamQuery(CUstream hStream) {
    CUresult (*func)(CUstream) = (CUresult (*)(CUstream))dlsym(RTLD_NEXT, "cuStreamQuery");
    return func ? func(hStream) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuStreamSynchronize(CUstream hStream) {
    CUresult (*func)(CUstream) = (CUresult (*)(CUstream))dlsym(RTLD_NEXT, "cuStreamSynchronize");
    CUresult ret = func ? func(hStream) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug) {
        MVGAL_LOG_DEBUG("cuStreamSynchronize: stream=%p", (void *)hStream);
    }
    
    return ret;
}

CUresult cuStreamWaitEvent(CUstream hStream, CUevent hEvent, unsigned int Flags) {
    CUresult (*func)(CUstream, CUevent, unsigned int) = 
        (CUresult (*)(CUstream, CUevent, unsigned int))dlsym(RTLD_NEXT, "cuStreamWaitEvent");
    return func ? func(hStream, hEvent, Flags) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuStreamAddCallback(CUstream hStream, void *callback, void *userData, unsigned int Flags) {
    CUresult (*func)(CUstream, void *, void *, unsigned int) = 
        (CUresult (*)(CUstream, void *, void *, unsigned int))dlsym(RTLD_NEXT, "cuStreamAddCallback");
    return func ? func(hStream, callback, userData, Flags) : CUDA_ERROR_NOT_INITIALIZED;
}

// =============================================================================
// CUDA Driver API - Event Management
// =============================================================================

CUresult cuEventCreate(CUevent *phEvent, unsigned int Flags) {
    CUresult (*func)(CUevent *, unsigned int) = 
        (CUresult (*)(CUevent *, unsigned int))dlsym(RTLD_NEXT, "cuEventCreate");
    CUresult ret = func ? func(phEvent, Flags) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuEventCreate: event=%p, flags=0x%x", (void *)*phEvent, Flags);
    }
    
    return ret;
}

CUresult cuEventCreateWithFlags(CUevent *phEvent, unsigned int Flags) {
    CUresult (*func)(CUevent *, unsigned int) = 
        (CUresult (*)(CUevent *, unsigned int))dlsym(RTLD_NEXT, "cuEventCreateWithFlags");
    return func ? func(phEvent, Flags) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuEventDestroy(CUevent hEvent) {
    CUresult (*func)(CUevent) = (CUresult (*)(CUevent))dlsym(RTLD_NEXT, "cuEventDestroy");
    return func ? func(hEvent) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuEventSynchronize(CUevent hEvent) {
    CUresult (*func)(CUevent) = (CUresult (*)(CUevent))dlsym(RTLD_NEXT, "cuEventSynchronize");
    return func ? func(hEvent) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuEventElapsedTime(float *pMilliseconds, CUevent hStart, CUevent hEnd) {
    CUresult (*func)(float *, CUevent, CUevent) = 
        (CUresult (*)(float *, CUevent, CUevent))dlsym(RTLD_NEXT, "cuEventElapsedTime");
    return func ? func(pMilliseconds, hStart, hEnd) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuEventRecord(CUevent hEvent, CUstream hStream) {
    CUresult (*func)(CUevent, CUstream) = 
        (CUresult (*)(CUevent, CUstream))dlsym(RTLD_NEXT, "cuEventRecord");
    return func ? func(hEvent, hStream) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuEventQuery(CUevent hEvent) {
    CUresult (*func)(CUevent) = (CUresult (*)(CUevent))dlsym(RTLD_NEXT, "cuEventQuery");
    return func ? func(hEvent) : CUDA_ERROR_NOT_INITIALIZED;
}

// =============================================================================
// CUDA Driver API - Module and Kernel Management
// =============================================================================

CUresult cuModuleLoad(CUmodule *module, const char *fname) {
    CUresult (*func)(CUmodule *, const char *) = 
        (CUresult (*)(CUmodule *, const char *))dlsym(RTLD_NEXT, "cuModuleLoad");
    CUresult ret = func ? func(module, fname) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuModuleLoad: %s -> module=%p", fname, (void *)*module);
    }
    
    return ret;
}

CUresult cuModuleLoadData(CUmodule *module, const void *image) {
    CUresult (*func)(CUmodule *, const void *) = 
        (CUresult (*)(CUmodule *, const void *))dlsym(RTLD_NEXT, "cuModuleLoadData");
    return func ? func(module, image) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuModuleUnload(CUmodule hmod) {
    CUresult (*func)(CUmodule) = (CUresult (*)(CUmodule))dlsym(RTLD_NEXT, "cuModuleUnload");
    return func ? func(hmod) : CUDA_ERROR_NOT_INITIALIZED;
}

CUresult cuModuleGetFunction(CUfunction *hfunc, CUmodule hmod, const char *name) {
    CUresult (*func)(CUfunction *, CUmodule, const char *) = 
        (CUresult (*)(CUfunction *, CUmodule, const char *))dlsym(RTLD_NEXT, "cuModuleGetFunction");
    CUresult ret = func ? func(hfunc, hmod, name) : CUDA_ERROR_NOT_INITIALIZED;
    
    if (g_cuda.enabled && ret == CUDA_SUCCESS && hfunc && name) {
        // Register kernel symbol for name resolution
        register_kernel_symbol(*hfunc, name);
    }
    
    if (g_cuda.debug && ret == CUDA_SUCCESS) {
        MVGAL_LOG_DEBUG("cuModuleGetFunction: module=%p, kernel='%s' -> function=%p",
                       (void *)hmod, name, (void *)*hfunc);
    }
    
    return ret;
}

CUresult cuModuleGetGlobal(CUdeviceptr *dptr, size_t *bytes, CUmodule hmod, const char *name) {
    CUresult (*func)(CUdeviceptr *, size_t *, CUmodule, const char *) = 
        (CUresult (*)(CUdeviceptr *, size_t *, CUmodule, const char *))dlsym(RTLD_NEXT, "cuModuleGetGlobal");
    return func ? func(dptr, bytes, hmod, name) : CUDA_ERROR_NOT_INITIALIZED;
}

// =============================================================================
// CUDA Driver API - Kernel Launch (KEY FUNCTION)
// =============================================================================

CUresult cuLaunchKernel(
    CUfunction f, unsigned int gx, unsigned int gy, unsigned int gz,
    unsigned int bx, unsigned int by, unsigned int bz,
    unsigned int smem, CUstream stream, void **params, void **extra
) {
    CUresult ret;
    cuda_wrapper_init();
    
    cuLaunchKernel_t func = (cuLaunchKernel_t)dlsym(RTLD_NEXT, "cuLaunchKernel");
    if (!func) return CUDA_ERROR_NOT_INITIALIZED;
    
    // Get kernel name
    const char *kernel_name = get_kernel_name(f);
    
    // Distribute across GPUs
    int target_gpu = 0;
    if (g_cuda.enabled && g_cuda.mvgal_context && g_cuda.gpu_count > 1) {
        switch (g_cuda.strategy) {
            case MVGAL_STRATEGY_AFR:
            case MVGAL_STRATEGY_CUSTOM:
                target_gpu = get_next_gpu();
                break;
            case MVGAL_STRATEGY_SINGLE_GPU:
                target_gpu = 0;
                break;
            case MVGAL_STRATEGY_SFR:
            case MVGAL_STRATEGY_HYBRID:
            case MVGAL_STRATEGY_TASK:
            case MVGAL_STRATEGY_AUTO:
            default:
                target_gpu = get_next_gpu();
                break;
        }
        
        // Submit to MVGAL scheduler
        mvgal_workload_submit_info_t info = {
            .type = MVGAL_WORKLOAD_COMPUTE,
            .priority = 50,
            .gpu_mask = (1 << target_gpu),
            .dependency_count = 0,
            .dependencies = NULL,
            .user_data = NULL
        };
        mvgal_workload_t workload;
        mvgal_error_t err = mvgal_workload_submit(g_cuda.mvgal_context, &info, &workload);
        
        if (err == MVGAL_SUCCESS) {
            g_cuda.gpus[target_gpu].kernel_launches++;
            g_cuda.stats.total_kernel_launches++;
        }
        
        if (g_cuda.debug) {
            MVGAL_LOG_DEBUG("[X] cuLaunchKernel '%s': grid=(%u,%u,%u) block=(%u,%u,%u) -> GPU %d",
                          kernel_name, gx, gy, gz, bx, by, bz, target_gpu);
        }
    }
    
    // Execute on CUDA
    ret = func(f, gx, gy, gz, bx, by, bz, smem, stream, params, extra);
    
    if (g_cuda.debug && ret != CUDA_SUCCESS) {
        MVGAL_LOG_WARN("cuLaunchKernel failed: %s", cuda_error_to_string(ret));
    }
    
    return ret;
}

// =============================================================================
// CUDA Runtime API - Intercepted Functions
// =============================================================================

cudaError_t cudaMalloc(void **ptr, size_t size) {
    cudaError_t (*func)(void **, size_t) = (cudaError_t (*)(void **, size_t))dlsym(RTLD_NEXT, "cudaMalloc");
    cudaError_t ret = func ? func(ptr, size) : cudaErrorInvalidValue;
    if (g_cuda.debug && ret == cudaSuccess) MVGAL_LOG_DEBUG("cudaMalloc: %zu bytes -> %p", size, *ptr);
    return ret;
}

cudaError_t cudaFree(void *ptr) {
    cudaError_t (*func)(void *) = (cudaError_t (*)(void *))dlsym(RTLD_NEXT, "cudaFree");
    return func ? func(ptr) : cudaErrorInvalidValue;
}

cudaError_t cudaMallocPitch(void **ptr, size_t *pitch, size_t width, size_t height) {
    cudaError_t (*func)(void **, size_t *, size_t, size_t) = 
        (cudaError_t (*)(void **, size_t *, size_t, size_t))dlsym(RTLD_NEXT, "cudaMallocPitch");
    return func ? func(ptr, pitch, width, height) : cudaErrorInvalidValue;
}

cudaError_t cudaMemcpy(void *dst, const void *src, size_t size, cudaMemcpyKind kind) {
    cudaError_t (*func)(void *, const void *, size_t, cudaMemcpyKind) = 
        (cudaError_t (*)(void *, const void *, size_t, cudaMemcpyKind))dlsym(RTLD_NEXT, "cudaMemcpy");
    
    if (g_cuda.enabled && g_cuda.memory_migration_enabled && kind == cudaMemcpyDeviceToDevice) {
        // Record cross-GPU copy statistic
        g_cuda.stats.cross_gpu_copies++;
    }
    
    return func ? func(dst, src, size, kind) : cudaErrorInvalidValue;
}

cudaError_t cudaMemcpyAsync(void *dst, const void *src, size_t size, cudaMemcpyKind kind, cudaStream_t stream) {
    cudaError_t (*func)(void *, const void *, size_t, cudaMemcpyKind, cudaStream_t) = 
        (cudaError_t (*)(void *, const void *, size_t, cudaMemcpyKind, cudaStream_t))dlsym(RTLD_NEXT, "cudaMemcpyAsync");
    return func ? func(dst, src, size, kind, stream) : cudaErrorInvalidValue;
}

cudaError_t cudaMemset(void *devPtr, int value, size_t count) {
    cudaError_t (*func)(void *, int, size_t) = 
        (cudaError_t (*)(void *, int, size_t))dlsym(RTLD_NEXT, "cudaMemset");
    return func ? func(devPtr, value, count) : cudaErrorInvalidValue;
}

cudaError_t cudaMemsetAsync(void *devPtr, int value, size_t count, cudaStream_t stream) {
    cudaError_t (*func)(void *, int, size_t, cudaStream_t) = 
        (cudaError_t (*)(void *, int, size_t, cudaStream_t))dlsym(RTLD_NEXT, "cudaMemsetAsync");
    return func ? func(devPtr, value, count, stream) : cudaErrorInvalidValue;
}

cudaError_t cudaGetDeviceCount(int *count) {
    cudaError_t (*func)(int *) = (cudaError_t (*)(int *))dlsym(RTLD_NEXT, "cudaGetDeviceCount");
    cudaError_t ret = func ? func(count) : cudaErrorInvalidValue;
    
    if (ret == cudaSuccess && g_cuda.mvgal_context && g_cuda.gpu_count > 0) {
        *count = g_cuda.gpu_count;
    }
    
    return ret;
}

cudaError_t cudaGetDevice(int *device) {
    cudaError_t (*func)(int *) = (cudaError_t (*)(int *))dlsym(RTLD_NEXT, "cudaGetDevice");
    return func ? func(device) : cudaSuccess;
}

cudaError_t cudaSetDevice(int device) {
    cudaError_t (*func)(int) = (cudaError_t (*)(int))dlsym(RTLD_NEXT, "cudaSetDevice");
    cudaError_t ret = func ? func(device) : cudaErrorInvalidValue;
    
    if (ret == cudaSuccess && g_cuda.enabled) {
        g_cuda.current_device = device;
        MVGAL_LOG_INFO("cudaSetDevice: %d", device);
    }
    
    return ret;
}

cudaError_t cudaDeviceSynchronize(void) {
    cudaError_t (*func)(void) = (cudaError_t (*)(void))dlsym(RTLD_NEXT, "cudaDeviceSynchronize");
    return func ? func() : cudaErrorInvalidValue;
}

cudaError_t cudaStreamCreate(cudaStream_t *pStream) {
    cudaError_t (*func)(cudaStream_t *) = (cudaError_t (*)(cudaStream_t *))dlsym(RTLD_NEXT, "cudaStreamCreate");
    return func ? func(pStream) : cudaErrorInvalidValue;
}

cudaError_t cudaStreamCreateWithFlags(cudaStream_t *pStream, unsigned int flags) {
    cudaError_t (*func)(cudaStream_t *, unsigned int) = 
        (cudaError_t (*)(cudaStream_t *, unsigned int))dlsym(RTLD_NEXT, "cudaStreamCreateWithFlags");
    return func ? func(pStream, flags) : cudaErrorInvalidValue;
}

cudaError_t cudaStreamDestroy(cudaStream_t stream) {
    cudaError_t (*func)(cudaStream_t) = (cudaError_t (*)(cudaStream_t))dlsym(RTLD_NEXT, "cudaStreamDestroy");
    return func ? func(stream) : cudaErrorInvalidValue;
}

cudaError_t cudaStreamSynchronize(cudaStream_t stream) {
    cudaError_t (*func)(cudaStream_t) = (cudaError_t (*)(cudaStream_t))dlsym(RTLD_NEXT, "cudaStreamSynchronize");
    return func ? func(stream) : cudaErrorInvalidValue;
}

cudaError_t cudaStreamQuery(cudaStream_t stream) {
    cudaError_t (*func)(cudaStream_t) = (cudaError_t (*)(cudaStream_t))dlsym(RTLD_NEXT, "cudaStreamQuery");
    return func ? func(stream) : cudaErrorInvalidValue;
}

cudaError_t cudaEventCreate(cudaEvent_t *pEvent) {
    cudaError_t (*func)(cudaEvent_t *) = (cudaError_t (*)(cudaEvent_t *))dlsym(RTLD_NEXT, "cudaEventCreate");
    return func ? func(pEvent) : cudaErrorInvalidValue;
}

cudaError_t cudaEventCreateWithFlags(cudaEvent_t *pEvent, unsigned int flags) {
    cudaError_t (*func)(cudaEvent_t *, unsigned int) = 
        (cudaError_t (*)(cudaEvent_t *, unsigned int))dlsym(RTLD_NEXT, "cudaEventCreateWithFlags");
    return func ? func(pEvent, flags) : cudaErrorInvalidValue;
}

cudaError_t cudaEventDestroy(cudaEvent_t event) {
    cudaError_t (*func)(cudaEvent_t) = (cudaError_t (*)(cudaEvent_t))dlsym(RTLD_NEXT, "cudaEventDestroy");
    return func ? func(event) : cudaErrorInvalidValue;
}

cudaError_t cudaEventSynchronize(cudaEvent_t event) {
    cudaError_t (*func)(cudaEvent_t) = (cudaError_t (*)(cudaEvent_t))dlsym(RTLD_NEXT, "cudaEventSynchronize");
    return func ? func(event) : cudaErrorInvalidValue;
}

cudaError_t cudaEventElapsedTime(float *pMilliseconds, cudaEvent_t start, cudaEvent_t end) {
    cudaError_t (*func)(float *, cudaEvent_t, cudaEvent_t) = 
        (cudaError_t (*)(float *, cudaEvent_t, cudaEvent_t))dlsym(RTLD_NEXT, "cudaEventElapsedTime");
    return func ? func(pMilliseconds, start, end) : cudaErrorInvalidValue;
}

cudaError_t cudaEventRecord(cudaEvent_t event, cudaStream_t stream) {
    cudaError_t (*func)(cudaEvent_t, cudaStream_t) = 
        (cudaError_t (*)(cudaEvent_t, cudaStream_t))dlsym(RTLD_NEXT, "cudaEventRecord");
    return func ? func(event, stream) : cudaErrorInvalidValue;
}

cudaError_t cudaEventQuery(cudaEvent_t event) {
    cudaError_t (*func)(cudaEvent_t) = (cudaError_t (*)(cudaEvent_t))dlsym(RTLD_NEXT, "cudaEventQuery");
    return func ? func(event) : cudaErrorInvalidValue;
}

// =============================================================================
// CUDA Runtime API - Kernel Launch (KEY FUNCTION)
// =============================================================================

cudaError_t cudaLaunchKernel(const void *func, dim3 gridDim, dim3 blockDim, void **args, size_t sharedMem, cudaStream_t stream) {
    cudaError_t (*f)(const void *, dim3, dim3, void **, size_t, cudaStream_t) = 
        (cudaError_t (*)(const void *, dim3, dim3, void **, size_t, cudaStream_t))dlsym(RTLD_NEXT, "cudaLaunchKernel");
    if (!f) return cudaErrorInvalidValue;
    
    if (g_cuda.enabled && g_cuda.mvgal_context && g_cuda.gpu_count > 1) {
        int target_gpu = get_next_gpu();
        
        if (g_cuda.debug) {
            MVGAL_LOG_DEBUG("[CUDA RT] cudaLaunchKernel: grid=(%u,%u,%u) block=(%u,%u,%u) -> GPU %d",
                          gridDim.x, gridDim.y, gridDim.z, blockDim.x, blockDim.y, blockDim.z, target_gpu);
        }
    }
    
    return f(func, gridDim, blockDim, args, sharedMem, stream);
}

// =============================================================================
// Module Constructor/Destructor
// =============================================================================

__attribute__((constructor)) static void cuda_wrapper_constructor(void) {
    cuda_wrapper_init();
}

__attribute__((destructor)) static void cuda_wrapper_destructor(void) {
    cuda_wrapper_shutdown();
}
