/**
 * @file metal_wrapper.c
 * @brief Metal API Interception Layer for macOS/Linux Compatibility
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * Intercepts Apple Metal API calls when running applications that use
 * Metal through compatibility layers (like Wine with Metal support, or
 * GPU-accelerated applications on Linux with Metal-like APIs).
 *
 * Note: Metal is primarily an Apple API, but this interceptor provides
 * compatibility for:
 *   - Wine applications using Metal via translation layers
 *   - GPU-accelerated applications on Linux with Metal-like APIs
 *   - Future Metal on Linux implementations
 *
 * Usage:
 *   export MVGAL_METAL_ENABLED=1
 *   export LD_PRELOAD=/path/to/libmvgal_metal.so
 *   ./your_metal_application
 *
 * Environment Variables:
 *   MVGAL_METAL_ENABLED=1      - Enable Metal interception (default: 1)
 *   MVGAL_METAL_DEBUG=1        - Enable debug logging (default: 0)
 *   MVGAL_METAL_STRATEGY=round_robin - Distribution strategy
 *   MVGAL_METAL_GPUS="0,1"     - GPU indices to use
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <pthread.h>

// MVGAL headers
#include <mvgal/mvgal.h>
#include <mvgal/mvgal_scheduler.h>
#include <mvgal/mvgal_execution.h>

/******************************************************************************
 * Metal Type Definitions
 *
 * Simplified versions of Metal API types for Linux compatibility.
 * The real Metal API uses Objective-C objects on macOS.
 * Here we use opaque pointers.
 ******************************************************************************/

typedef void* MetalDeviceRef;
typedef void* MetalCommandQueueRef;
typedef void* MetalBufferRef;
typedef void* MetalTextureRef;
typedef void* MetalRenderPipelineRef;
typedef void* MetalComputePipelineRef;
typedef void* MetalCommandBufferRef;

/******************************************************************************
 * Configuration and State
 ******************************************************************************/

#define MVGAL_METAL_VERSION "0.2.0"

typedef struct {
    bool enabled;
    bool debug;
    int gpu_count;
    int current_gpu;
    char strategy[64];
    pthread_mutex_t lock;
    mvgal_context_t context;
} metal_wrapper_state_t;

static metal_wrapper_state_t wrapper_state = {
    .enabled = true,
    .debug = false,
    .gpu_count = 0,
    .current_gpu = 0,
    .strategy = "round_robin",
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .context = NULL
};

/******************************************************************************
 * Function Pointer Types
 ******************************************************************************/

// MTLCreateSystemDefaultDevice
typedef MetalDeviceRef (*MTLCreateSystemDefaultDevice_t)(void);

// MTLDeviceNewBuffer
typedef MetalBufferRef (*MTLDeviceNewBuffer_t)(MetalDeviceRef device, size_t length, uint32_t options);

// MTLDeviceNewTexture
typedef MetalTextureRef (*MTLDeviceNewTexture_t)(MetalDeviceRef device, uint32_t descriptor, uint32_t options);

// MTLDeviceMakeCommandQueue
typedef MetalCommandQueueRef (*MTLDeviceMakeCommandQueue_t)(MetalDeviceRef device, uint32_t maxCommandBufferCount);

// MTLDeviceNewRenderPipelineState
typedef MetalRenderPipelineRef (*MTLDeviceNewRenderPipelineState_t)(MetalDeviceRef device, void *descriptor, void *error);

// MTLDeviceNewComputePipelineState
typedef MetalComputePipelineRef (*MTLDeviceNewComputePipelineState_t)(MetalDeviceRef device, void *descriptor, void *error);

// MTLCommandQueueCommandBuffer
typedef MetalCommandBufferRef (*MTLCommandQueueCommandBuffer_t)(MetalCommandQueueRef queue);

// MTLCommandBufferCommit
typedef void (*MTLCommandBufferCommit_t)(MetalCommandBufferRef commandBuffer);

// MTLCommandBufferPresentDrawables
typedef void (*MTLCommandBufferPresentDrawables_t)(MetalCommandBufferRef commandBuffer);

// Real function pointers
static MTLCreateSystemDefaultDevice_t real_MTLCreateSystemDefaultDevice = NULL;
static MTLDeviceNewBuffer_t real_MTLDeviceNewBuffer = NULL;
static MTLDeviceNewTexture_t real_MTLDeviceNewTexture = NULL;
static MTLDeviceMakeCommandQueue_t real_MTLDeviceMakeCommandQueue = NULL;
static MTLDeviceNewRenderPipelineState_t real_MTLDeviceNewRenderPipelineState = NULL;
static MTLDeviceNewComputePipelineState_t real_MTLDeviceNewComputePipelineState = NULL;
static MTLCommandQueueCommandBuffer_t real_MTLCommandQueueCommandBuffer = NULL;
static MTLCommandBufferCommit_t real_MTLCommandBufferCommit = NULL;
static MTLCommandBufferPresentDrawables_t real_MTLCommandBufferPresentDrawables = NULL;

/******************************************************************************
 * Helper Functions
 ******************************************************************************/

static int get_next_gpu(void) {
    pthread_mutex_lock(&wrapper_state.lock);
    int gpu = wrapper_state.current_gpu;
    wrapper_state.current_gpu = (wrapper_state.current_gpu + 1) % wrapper_state.gpu_count;
    pthread_mutex_unlock(&wrapper_state.lock);
    return gpu;
}

static void log_debug(const char *format, ...) {
    if (!wrapper_state.debug) return;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[METAL DEBUG] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[METAL INFO] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[METAL WARN] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void* get_real_function(const char *name) {
    void *func = dlsym(RTLD_NEXT, name);
    if (!func) {
        log_warn("Could not find real function: %s", name);
    }
    return func;
}

// Submit workload to MVGAL
static void submit_metal_workload(const char *step_name, mvgal_workload_type_t type) {
    if (!wrapper_state.context) {
        return;
    }

    mvgal_workload_submit_info_t info = {
        .type = type,
        .priority = 50,
        .gpu_mask = (1U << get_next_gpu()),
        .user_data = NULL
    };

    mvgal_workload_t workload;
    mvgal_error_t err = mvgal_workload_submit(wrapper_state.context, &info, &workload);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to submit workload: %s", step_name);
    } else {
        log_debug("Submitted workload: type=%d, step=%s", type, step_name);
    }
}

/******************************************************************************
 * Initialization
 ******************************************************************************/

static void init_wrapper(void) {
    const char *enabled_str = getenv("MVGAL_METAL_ENABLED");
    if (enabled_str && atoi(enabled_str) == 0) {
        wrapper_state.enabled = false;
        log_info("Metal interception disabled via MVGAL_METAL_ENABLED=0");
        return;
    }

    const char *debug_str = getenv("MVGAL_METAL_DEBUG");
    if (debug_str && atoi(debug_str) == 1) {
        wrapper_state.debug = true;
    }

    const char *strategy = getenv("MVGAL_METAL_STRATEGY");
    if (strategy) {
        strncpy(wrapper_state.strategy, strategy, sizeof(wrapper_state.strategy) - 1);
    }

    // Initialize MVGAL
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to initialize MVGAL: %d", err);
        return;
    }

    err = mvgal_context_create(&wrapper_state.context);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to create MVGAL context: %d", err);
        wrapper_state.context = NULL;
        return;
    }

    // Set strategy if specified
    if (strcmp(wrapper_state.strategy, "round_robin") == 0) {
        mvgal_scheduler_set_strategy(wrapper_state.context, MVGAL_STRATEGY_ROUND_ROBIN);
    } else if (strcmp(wrapper_state.strategy, "hybrid") == 0) {
        mvgal_scheduler_set_strategy(wrapper_state.context, MVGAL_STRATEGY_HYBRID);
    }

    // Get GPU count
    wrapper_state.gpu_count = mvgal_gpu_get_count();
    if (wrapper_state.gpu_count <= 0) {
        wrapper_state.gpu_count = 1;
    }

    log_info("Metal wrapper initialized (strategy: %s, GPUs: %d)",
             wrapper_state.strategy, wrapper_state.gpu_count);
}

static void fini_wrapper(void) {
    if (wrapper_state.context) {
        mvgal_context_destroy(wrapper_state.context);
        wrapper_state.context = NULL;
        mvgal_shutdown();
        log_info("Metal wrapper shutdown");
    }
}

// Constructor/destructor
__attribute__((constructor)) static void metal_constructor(void) {
    init_wrapper();
}

__attribute__((destructor)) static void metal_destructor(void) {
    fini_wrapper();
}

/******************************************************************************
 * Metal Function Intercepts
 ******************************************************************************/

// MTLCreateSystemDefaultDevice
MetalDeviceRef MTLCreateSystemDefaultDevice(void) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCreateSystemDefaultDevice) {
            real_MTLCreateSystemDefaultDevice = get_real_function("MTLCreateSystemDefaultDevice");
        }
        if (real_MTLCreateSystemDefaultDevice) {
            return real_MTLCreateSystemDefaultDevice();
        }
        return NULL;
    }

    log_debug("MTLCreateSystemDefaultDevice intercepted");

    submit_metal_workload("MTLCreateSystemDefaultDevice", MVGAL_WORKLOAD_METAL_QUEUE);

    if (!real_MTLCreateSystemDefaultDevice) {
        real_MTLCreateSystemDefaultDevice = get_real_function("MTLCreateSystemDefaultDevice");
    }
    if (real_MTLCreateSystemDefaultDevice) {
        return real_MTLCreateSystemDefaultDevice();
    }

    return NULL;
}

// MTLDeviceMakeCommandQueue
MetalCommandQueueRef MTLDeviceMakeCommandQueue(
    MetalDeviceRef device,
    unsigned long maxCommandBufferCount
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceMakeCommandQueue) {
            real_MTLDeviceMakeCommandQueue = get_real_function("MTLDeviceMakeCommandQueue");
        }
        if (real_MTLDeviceMakeCommandQueue) {
            return real_MTLDeviceMakeCommandQueue(device, (uint32_t)maxCommandBufferCount);
        }
        return NULL;
    }

    log_debug("MTLDeviceMakeCommandQueue intercepted");

    submit_metal_workload("MTLDeviceMakeCommandQueue", MVGAL_WORKLOAD_METAL_QUEUE);

    if (!real_MTLDeviceMakeCommandQueue) {
        real_MTLDeviceMakeCommandQueue = get_real_function("MTLDeviceMakeCommandQueue");
    }
    if (real_MTLDeviceMakeCommandQueue) {
        return real_MTLDeviceMakeCommandQueue(device, (uint32_t)maxCommandBufferCount);
    }

    return NULL;
}

// MTLDeviceNewBuffer
MetalBufferRef MTLDeviceNewBuffer(
    MetalDeviceRef device,
    size_t length,
    unsigned long options
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewBuffer) {
            real_MTLDeviceNewBuffer = get_real_function("MTLDeviceNewBuffer");
        }
        if (real_MTLDeviceNewBuffer) {
            return real_MTLDeviceNewBuffer(device, length, (uint32_t)options);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewBuffer intercepted (size: %zu, options: %lu)", length, options);

    submit_metal_workload("MTLDeviceNewBuffer", MVGAL_WORKLOAD_METAL_BUFFER);

    if (!real_MTLDeviceNewBuffer) {
        real_MTLDeviceNewBuffer = get_real_function("MTLDeviceNewBuffer");
    }
    if (real_MTLDeviceNewBuffer) {
        return real_MTLDeviceNewBuffer(device, length, (uint32_t)options);
    }

    return NULL;
}

// MTLDeviceNewTexture
MetalTextureRef MTLDeviceNewTexture(
    MetalDeviceRef device,
    uint32_t descriptor,
    unsigned long options
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewTexture) {
            real_MTLDeviceNewTexture = get_real_function("MTLDeviceNewTexture");
        }
        if (real_MTLDeviceNewTexture) {
            return real_MTLDeviceNewTexture(device, descriptor, (uint32_t)options);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewTexture intercepted");

    submit_metal_workload("MTLDeviceNewTexture", MVGAL_WORKLOAD_METAL_TEXTURE);

    if (!real_MTLDeviceNewTexture) {
        real_MTLDeviceNewTexture = get_real_function("MTLDeviceNewTexture");
    }
    if (real_MTLDeviceNewTexture) {
        return real_MTLDeviceNewTexture(device, descriptor, (uint32_t)options);
    }

    return NULL;
}

// MTLDeviceNewRenderPipelineState
MetalRenderPipelineRef MTLDeviceNewRenderPipelineState(
    MetalDeviceRef device,
    void *descriptor,
    void *error
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewRenderPipelineState) {
            real_MTLDeviceNewRenderPipelineState = get_real_function("MTLDeviceNewRenderPipelineState");
        }
        if (real_MTLDeviceNewRenderPipelineState) {
            return real_MTLDeviceNewRenderPipelineState(device, descriptor, error);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewRenderPipelineState intercepted");

    submit_metal_workload("MTLDeviceNewRenderPipelineState", MVGAL_WORKLOAD_METAL_RENDER);

    if (!real_MTLDeviceNewRenderPipelineState) {
        real_MTLDeviceNewRenderPipelineState = get_real_function("MTLDeviceNewRenderPipelineState");
    }
    if (real_MTLDeviceNewRenderPipelineState) {
        MetalRenderPipelineRef result = real_MTLDeviceNewRenderPipelineState(device, descriptor, error);
        return result;
    }

    return NULL;
}

// MTLDeviceNewComputePipelineState
MetalComputePipelineRef MTLDeviceNewComputePipelineState(
    MetalDeviceRef device,
    void *descriptor,
    void *error
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewComputePipelineState) {
            real_MTLDeviceNewComputePipelineState = get_real_function("MTLDeviceNewComputePipelineState");
        }
        if (real_MTLDeviceNewComputePipelineState) {
            return real_MTLDeviceNewComputePipelineState(device, descriptor, error);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewComputePipelineState intercepted");

    submit_metal_workload("MTLDeviceNewComputePipelineState", MVGAL_WORKLOAD_METAL_COMPUTE);

    if (!real_MTLDeviceNewComputePipelineState) {
        real_MTLDeviceNewComputePipelineState = get_real_function("MTLDeviceNewComputePipelineState");
    }
    if (real_MTLDeviceNewComputePipelineState) {
        MetalComputePipelineRef result = real_MTLDeviceNewComputePipelineState(device, descriptor, error);
        return result;
    }

    return NULL;
}

// MTLCommandQueueCommandBuffer
MetalCommandBufferRef MTLCommandQueueCommandBuffer(
    MetalCommandQueueRef queue
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandQueueCommandBuffer) {
            real_MTLCommandQueueCommandBuffer = get_real_function("MTLCommandQueueCommandBuffer");
        }
        if (real_MTLCommandQueueCommandBuffer) {
            return real_MTLCommandQueueCommandBuffer(queue);
        }
        return NULL;
    }

    log_debug("MTLCommandQueueCommandBuffer intercepted");

    submit_metal_workload("MTLCommandQueueCommandBuffer", MVGAL_WORKLOAD_METAL_COMMAND);

    if (!real_MTLCommandQueueCommandBuffer) {
        real_MTLCommandQueueCommandBuffer = get_real_function("MTLCommandQueueCommandBuffer");
    }
    if (real_MTLCommandQueueCommandBuffer) {
        return real_MTLCommandQueueCommandBuffer(queue);
    }

    return NULL;
}

// MTLCommandBufferCommit
void MTLCommandBufferCommit(
    MetalCommandBufferRef commandBuffer
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandBufferCommit) {
            real_MTLCommandBufferCommit = get_real_function("MTLCommandBufferCommit");
        }
        if (real_MTLCommandBufferCommit) {
            real_MTLCommandBufferCommit(commandBuffer);
        }
        return;
    }

    log_debug("MTLCommandBufferCommit intercepted");

    submit_metal_workload("MTLCommandBufferCommit", MVGAL_WORKLOAD_METAL_COMMIT);

    if (!real_MTLCommandBufferCommit) {
        real_MTLCommandBufferCommit = get_real_function("MTLCommandBufferCommit");
    }
    if (real_MTLCommandBufferCommit) {
        real_MTLCommandBufferCommit(commandBuffer);
    }
}

// MTLCommandBufferPresentDrawables
void MTLCommandBufferPresentDrawables(
    MetalCommandBufferRef commandBuffer
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandBufferPresentDrawables) {
            real_MTLCommandBufferPresentDrawables = get_real_function("MTLCommandBufferPresentDrawables");
        }
        if (real_MTLCommandBufferPresentDrawables) {
            real_MTLCommandBufferPresentDrawables(commandBuffer);
        }
        return;
    }

    log_debug("MTLCommandBufferPresentDrawables intercepted");

    submit_metal_workload("MTLCommandBufferPresentDrawables", MVGAL_WORKLOAD_METAL_PRESENT);

    if (!real_MTLCommandBufferPresentDrawables) {
        real_MTLCommandBufferPresentDrawables = get_real_function("MTLCommandBufferPresentDrawables");
    }
    if (real_MTLCommandBufferPresentDrawables) {
        real_MTLCommandBufferPresentDrawables(commandBuffer);
    }
}
