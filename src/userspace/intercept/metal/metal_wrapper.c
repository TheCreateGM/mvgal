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
 * Metal Type Definitions (for Linux compatibility)
 ******************************************************************************/

// Opaque Metal API types (simulate Objective-C objects on Linux)
typedef void* MetalDeviceRef;
typedef void* MetalCommandQueueRef;
typedef void* MetalBufferRef;
typedef void* MetalTextureRef;
typedef void* MetalRenderPipelineRef;
typedef void* MetalComputePipelineRef;
typedef void* MetalCommandBufferRef;
typedef void* MetalDrawableRef;
typedef void* MetalLibraryRef;
typedef void* MetalFunctionRef;

/******************************************************************************
 * Forward Declarations
 ******************************************************************************/
static int get_next_gpu(void);
static void log_debug(const char *format, ...);
static void log_info(const char *format, ...);
static void log_warn(const char *format, ...);
static void* get_real_function(const char *name);
static void submit_metal_workload(const char *step_name, mvgal_workload_type_t type, MetalDeviceRef device);
static int register_metal_device(MetalDeviceRef real_device, int mvgal_gpu_id);
static void unregister_metal_device(MetalDeviceRef real_device);
static int get_gpu_for_metal_device(MetalDeviceRef real_device);
static int register_metal_queue(MetalCommandQueueRef queue, MetalDeviceRef device);
static void unregister_metal_queue(MetalCommandQueueRef queue);
static MetalDeviceRef get_device_for_queue(MetalCommandQueueRef queue);
static int register_metal_command_buffer(MetalCommandBufferRef command_buffer, MetalCommandQueueRef queue);
static void unregister_metal_command_buffer(MetalCommandBufferRef command_buffer);
static MetalDeviceRef get_device_for_command_buffer(MetalCommandBufferRef command_buffer);

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
 * Device, Queue, and Command Buffer Tracking
 ******************************************************************************/

#define MAX_METAL_DEVICES 8
#define MAX_METAL_QUEUES 16
#define MAX_METAL_COMMAND_BUFFERS 32

typedef struct {
    MetalDeviceRef real_device;
    int mvgal_gpu_id;
    bool in_use;
} metal_device_mapping_t;

typedef struct {
    MetalCommandQueueRef real_queue;
    MetalDeviceRef device;  // Back-pointer to device
    bool in_use;
} metal_queue_mapping_t;

typedef struct {
    MetalCommandBufferRef real_command_buffer;
    MetalCommandQueueRef queue;  // Back-pointer to queue
    bool in_use;
} metal_command_buffer_mapping_t;

static metal_device_mapping_t metal_device_map[MAX_METAL_DEVICES] = {0};
static metal_queue_mapping_t metal_queue_map[MAX_METAL_QUEUES] = {0};
static metal_command_buffer_mapping_t metal_command_buffer_map[MAX_METAL_COMMAND_BUFFERS] = {0};
static int metal_device_count = 0;
static int metal_queue_count = 0;
static int metal_command_buffer_count = 0;

static int register_metal_device(MetalDeviceRef real_device, int mvgal_gpu_id) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_DEVICES; i++) {
        if (!metal_device_map[i].in_use) {
            metal_device_map[i].real_device = real_device;
            metal_device_map[i].mvgal_gpu_id = mvgal_gpu_id;
            metal_device_map[i].in_use = true;
            metal_device_count++;
            pthread_mutex_unlock(&wrapper_state.lock);
            log_debug("Registered Metal device %p -> GPU %d", real_device, mvgal_gpu_id);
            return i;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    log_warn("Device map full, cannot register Metal device %p", real_device);
    return -1;
}

static void unregister_metal_device(MetalDeviceRef real_device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_DEVICES; i++) {
        if (metal_device_map[i].in_use && metal_device_map[i].real_device == real_device) {
            log_debug("Unregistering Metal device %p (GPU %d)", real_device, metal_device_map[i].mvgal_gpu_id);
            metal_device_map[i].in_use = false;
            metal_device_map[i].real_device = NULL;
            metal_device_map[i].mvgal_gpu_id = -1;
            metal_device_count--;
            break;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
}

static int get_gpu_for_metal_device(MetalDeviceRef real_device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_DEVICES; i++) {
        if (metal_device_map[i].in_use && metal_device_map[i].real_device == real_device) {
            int gpu_id = metal_device_map[i].mvgal_gpu_id;
            pthread_mutex_unlock(&wrapper_state.lock);
            return gpu_id;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return get_next_gpu(); // Fallback to round-robin
}

static int register_metal_queue(MetalCommandQueueRef queue, MetalDeviceRef device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_QUEUES; i++) {
        if (!metal_queue_map[i].in_use) {
            metal_queue_map[i].real_queue = queue;
            metal_queue_map[i].device = device;
            metal_queue_map[i].in_use = true;
            metal_queue_count++;
            pthread_mutex_unlock(&wrapper_state.lock);
            log_debug("Registered Metal queue %p -> device %p", queue, device);
            return i;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    log_warn("Queue map full, cannot register Metal queue %p", queue);
    return -1;
}

static void unregister_metal_queue(MetalCommandQueueRef queue) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_QUEUES; i++) {
        if (metal_queue_map[i].in_use && metal_queue_map[i].real_queue == queue) {
            log_debug("Unregistering Metal queue %p", queue);
            metal_queue_map[i].in_use = false;
            metal_queue_map[i].real_queue = NULL;
            metal_queue_map[i].device = NULL;
            metal_queue_count--;
            break;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
}

static MetalDeviceRef get_device_for_queue(MetalCommandQueueRef queue) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_QUEUES; i++) {
        if (metal_queue_map[i].in_use && metal_queue_map[i].real_queue == queue) {
            MetalDeviceRef device = metal_queue_map[i].device;
            pthread_mutex_unlock(&wrapper_state.lock);
            return device;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return NULL;
}

static int register_metal_command_buffer(MetalCommandBufferRef command_buffer, MetalCommandQueueRef queue) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_COMMAND_BUFFERS; i++) {
        if (!metal_command_buffer_map[i].in_use) {
            metal_command_buffer_map[i].real_command_buffer = command_buffer;
            metal_command_buffer_map[i].queue = queue;
            metal_command_buffer_map[i].in_use = true;
            metal_command_buffer_count++;
            pthread_mutex_unlock(&wrapper_state.lock);
            log_debug("Registered Metal command buffer %p -> queue %p", command_buffer, queue);
            return i;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    log_warn("Command buffer map full, cannot register %p", command_buffer);
    return -1;
}

static void unregister_metal_command_buffer(MetalCommandBufferRef command_buffer) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_COMMAND_BUFFERS; i++) {
        if (metal_command_buffer_map[i].in_use && metal_command_buffer_map[i].real_command_buffer == command_buffer) {
            log_debug("Unregistering Metal command buffer %p", command_buffer);
            metal_command_buffer_map[i].in_use = false;
            metal_command_buffer_map[i].real_command_buffer = NULL;
            metal_command_buffer_map[i].queue = NULL;
            metal_command_buffer_count--;
            break;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
}

static MetalDeviceRef get_device_for_command_buffer(MetalCommandBufferRef command_buffer) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_METAL_COMMAND_BUFFERS; i++) {
        if (metal_command_buffer_map[i].in_use && metal_command_buffer_map[i].real_command_buffer == command_buffer) {
            MetalCommandQueueRef queue = metal_command_buffer_map[i].queue;
            pthread_mutex_unlock(&wrapper_state.lock);
            return get_device_for_queue(queue);
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return NULL;
}

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

// MTLDeviceNewBufferWithBytes
typedef MetalBufferRef (*MTLDeviceNewBufferWithBytes_t)(MetalDeviceRef device, const void *bytes, size_t length, uint32_t options);

// MTLDeviceNewTextureWithDescriptor
typedef MetalTextureRef (*MTLDeviceNewTextureWithDescriptor_t)(MetalDeviceRef device, void *descriptor);

// MTLDeviceNewSamplerStateWithDescriptor
typedef void* (*MTLDeviceNewSamplerStateWithDescriptor_t)(MetalDeviceRef device, void *descriptor);

// MTLDeviceNewDepthStencilStateWithDescriptor
typedef void* (*MTLDeviceNewDepthStencilStateWithDescriptor_t)(MetalDeviceRef device, void *descriptor);

// MTLDeviceNewFence
typedef void* (*MTLDeviceNewFence_t)(MetalDeviceRef device, uint32_t descriptor);

// MTLDeviceSupportsFeatureSet
typedef bool (*MTLDeviceSupportsFeatureSet_t)(MetalDeviceRef device, uint32_t featureSet);

// MTLDeviceNewLibraryWithSource
typedef MetalLibraryRef (*MTLDeviceNewLibraryWithSource_t)(MetalDeviceRef device, const char *source, void *options, void *error);

// MTLDeviceNewRenderPipelineStateWithDescriptor
typedef MetalRenderPipelineRef (*MTLDeviceNewRenderPipelineStateWithDescriptor_t)(MetalDeviceRef device, void *descriptor, void *error);

// MTLDeviceMakeCommandBuffer (alternative name)
typedef MetalCommandBufferRef (*MTLDeviceMakeCommandBuffer_t)(MetalDeviceRef device);

// MTLCommandQueueCommandBufferWithUnretainedReferences
typedef MetalCommandBufferRef (*MTLCommandQueueCommandBufferWithUnretainedReferences_t)(MetalCommandQueueRef queue);

// MTLCommandBufferRenderCommandEncoderWithDescriptor
typedef void* (*MTLCommandBufferRenderCommandEncoderWithDescriptor_t)(MetalCommandBufferRef commandBuffer, void *descriptor);

// MTLCommandBufferComputeCommandEncoder
typedef void* (*MTLCommandBufferComputeCommandEncoder_t)(MetalCommandBufferRef commandBuffer);

// MTLCommandBufferBlitCommandEncoder
typedef void* (*MTLCommandBufferBlitCommandEncoder_t)(MetalCommandBufferRef commandBuffer);

// MTLCommandBufferPresentDrawable
typedef void (*MTLCommandBufferPresentDrawable_t)(MetalCommandBufferRef commandBuffer, MetalDrawableRef drawable);

// MTLCommandBufferWaitUntilCompleted
typedef void (*MTLCommandBufferWaitUntilCompleted_t)(MetalCommandBufferRef commandBuffer);

// MTLRenderCommandEncoderSetRenderPipelineState
typedef void (*MTLRenderCommandEncoderSetRenderPipelineState_t)(void *encoder, MetalRenderPipelineRef pipelineState);

// MTLRenderCommandEncoderSetVertexBuffer
typedef void (*MTLRenderCommandEncoderSetVertexBuffer_t)(void *encoder, MetalBufferRef buffer, size_t offset, uint32_t index);

// MTLRenderCommandEncoderSetFragmentBuffer
typedef void (*MTLRenderCommandEncoderSetFragmentBuffer_t)(void *encoder, MetalBufferRef buffer, size_t offset, uint32_t index);

// MTLRenderCommandEncoderDrawPrimitives
typedef void (*MTLRenderCommandEncoderDrawPrimitives_t)(void *encoder, uint32_t primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount);

// MTLRenderCommandEncoderDrawIndexedPrimitives
typedef void (*MTLRenderCommandEncoderDrawIndexedPrimitives_t)(void *encoder, uint32_t primitiveType, size_t indexCount, uint32_t indexType, MetalBufferRef indexBuffer, size_t indexBufferOffset, size_t instanceCount);

// MTLRenderCommandEncoderEndEncoding
typedef void (*MTLRenderCommandEncoderEndEncoding_t)(void *encoder);

// MTLComputeCommandEncoderSetComputePipelineState
typedef void (*MTLComputeCommandEncoderSetComputePipelineState_t)(void *encoder, MetalComputePipelineRef pipelineState);

// MTLComputeCommandEncoderSetBuffer
typedef void (*MTLComputeCommandEncoderSetBuffer_t)(void *encoder, MetalBufferRef buffer, size_t offset, uint32_t index);

// MTLComputeCommandEncoderDispatchThreadgroups
typedef void (*MTLComputeCommandEncoderDispatchThreadgroups_t)(void *encoder, size_t threadgroupsPerGrid_x, size_t threadgroupsPerGrid_y, size_t threadgroupsPerGrid_z, size_t threadsPerThreadgroup_x, size_t threadsPerThreadgroup_y, size_t threadsPerThreadgroup_z);

// MTLComputeCommandEncoderEndEncoding
typedef void (*MTLComputeCommandEncoderEndEncoding_t)(void *encoder);

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

// New real function pointers for additional device methods
static MTLDeviceNewBufferWithBytes_t real_MTLDeviceNewBufferWithBytes = NULL;
static MTLDeviceNewTextureWithDescriptor_t real_MTLDeviceNewTextureWithDescriptor = NULL;
static MTLDeviceNewSamplerStateWithDescriptor_t real_MTLDeviceNewSamplerStateWithDescriptor = NULL;
static MTLDeviceNewDepthStencilStateWithDescriptor_t real_MTLDeviceNewDepthStencilStateWithDescriptor = NULL;
static MTLDeviceNewFence_t real_MTLDeviceNewFence = NULL;
static MTLDeviceSupportsFeatureSet_t real_MTLDeviceSupportsFeatureSet = NULL;
static MTLDeviceNewLibraryWithSource_t real_MTLDeviceNewLibraryWithSource = NULL;
static MTLDeviceNewRenderPipelineStateWithDescriptor_t real_MTLDeviceNewRenderPipelineStateWithDescriptor = NULL;

// New real function pointers for command queue methods
static MTLDeviceMakeCommandBuffer_t real_MTLDeviceMakeCommandBuffer = NULL;
static MTLCommandQueueCommandBufferWithUnretainedReferences_t real_MTLCommandQueueCommandBufferWithUnretainedReferences = NULL;

// New real function pointers for command buffer methods
static MTLCommandBufferRenderCommandEncoderWithDescriptor_t real_MTLCommandBufferRenderCommandEncoderWithDescriptor = NULL;
static MTLCommandBufferComputeCommandEncoder_t real_MTLCommandBufferComputeCommandEncoder = NULL;
static MTLCommandBufferBlitCommandEncoder_t real_MTLCommandBufferBlitCommandEncoder = NULL;
static MTLCommandBufferPresentDrawable_t real_MTLCommandBufferPresentDrawable = NULL;
static MTLCommandBufferWaitUntilCompleted_t real_MTLCommandBufferWaitUntilCompleted = NULL;

// New real function pointers for render command encoder methods
static MTLRenderCommandEncoderSetRenderPipelineState_t real_MTLRenderCommandEncoderSetRenderPipelineState = NULL;
static MTLRenderCommandEncoderSetVertexBuffer_t real_MTLRenderCommandEncoderSetVertexBuffer = NULL;
static MTLRenderCommandEncoderSetFragmentBuffer_t real_MTLRenderCommandEncoderSetFragmentBuffer = NULL;
static MTLRenderCommandEncoderDrawPrimitives_t real_MTLRenderCommandEncoderDrawPrimitives = NULL;
static MTLRenderCommandEncoderDrawIndexedPrimitives_t real_MTLRenderCommandEncoderDrawIndexedPrimitives = NULL;
static MTLRenderCommandEncoderEndEncoding_t real_MTLRenderCommandEncoderEndEncoding = NULL;

// New real function pointers for compute command encoder methods
static MTLComputeCommandEncoderSetComputePipelineState_t real_MTLComputeCommandEncoderSetComputePipelineState = NULL;
static MTLComputeCommandEncoderSetBuffer_t real_MTLComputeCommandEncoderSetBuffer = NULL;
static MTLComputeCommandEncoderDispatchThreadgroups_t real_MTLComputeCommandEncoderDispatchThreadgroups = NULL;
static MTLComputeCommandEncoderEndEncoding_t real_MTLComputeCommandEncoderEndEncoding = NULL;

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
static void submit_metal_workload(const char *step_name, mvgal_workload_type_t type, MetalDeviceRef device) {
    if (!wrapper_state.context) {
        return;
    }

    // Get the MVGAL GPU ID for this device
    int gpu_id = device ? get_gpu_for_metal_device(device) : get_next_gpu();
    uint32_t gpu_mask = (1U << gpu_id);

    mvgal_workload_submit_info_t info = {
        .type = type,
        .priority = 50,
        .gpu_mask = gpu_mask,
        .user_data = device
    };

    mvgal_workload_t workload;
    mvgal_error_t err = mvgal_workload_submit(wrapper_state.context, &info, &workload);
    if (err != MVGAL_SUCCESS) {
        log_warn("Failed to submit workload: %s (GPU %d)", step_name, gpu_id);
    } else {
        log_debug("Submitted workload: type=%d, step=%s, GPU=%d", type, step_name, gpu_id);
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

    // Call real function first
    if (!real_MTLCreateSystemDefaultDevice) {
        real_MTLCreateSystemDefaultDevice = get_real_function("MTLCreateSystemDefaultDevice");
    }
    
    MetalDeviceRef device = NULL;
    if (real_MTLCreateSystemDefaultDevice) {
        device = real_MTLCreateSystemDefaultDevice();
    }
    
    if (device) {
        // Register this device with MVGAL
        int gpu_id = get_next_gpu();
        register_metal_device(device, gpu_id);
        log_info("Registered Metal device %p -> GPU %d", device, gpu_id);
        
        // Submit workload with device info
        submit_metal_workload("MTLCreateSystemDefaultDevice", MVGAL_WORKLOAD_METAL_QUEUE, device);
    }

    return device;
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

    log_debug("MTLDeviceMakeCommandQueue intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceMakeCommandQueue", MVGAL_WORKLOAD_METAL_QUEUE, device);

    if (!real_MTLDeviceMakeCommandQueue) {
        real_MTLDeviceMakeCommandQueue = get_real_function("MTLDeviceMakeCommandQueue");
    }
    if (real_MTLDeviceMakeCommandQueue) {
        MetalCommandQueueRef queue = real_MTLDeviceMakeCommandQueue(device, (uint32_t)maxCommandBufferCount);
        if (queue) {
            // Register queue with device mapping
            register_metal_queue(queue, device);
            log_debug("Created Metal command queue %p for device %p", queue, device);
        }
        return queue;
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

    log_debug("MTLDeviceNewBuffer intercepted (size: %zu, options: %lu, device: %p)", length, options, device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewBuffer", MVGAL_WORKLOAD_METAL_BUFFER, device);

    if (!real_MTLDeviceNewBuffer) {
        real_MTLDeviceNewBuffer = get_real_function("MTLDeviceNewBuffer");
    }
    if (real_MTLDeviceNewBuffer) {
        MetalBufferRef buffer = real_MTLDeviceNewBuffer(device, length, (uint32_t)options);
        if (buffer) {
            log_debug("Created Metal buffer %p (size: %zu) for device %p", buffer, length, device);
        }
        return buffer;
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

    log_debug("MTLDeviceNewTexture intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewTexture", MVGAL_WORKLOAD_METAL_TEXTURE, device);

    if (!real_MTLDeviceNewTexture) {
        real_MTLDeviceNewTexture = get_real_function("MTLDeviceNewTexture");
    }
    if (real_MTLDeviceNewTexture) {
        MetalTextureRef texture = real_MTLDeviceNewTexture(device, descriptor, (uint32_t)options);
        if (texture) {
            log_debug("Created Metal texture %p for device %p", texture, device);
        }
        return texture;
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

    log_debug("MTLDeviceNewRenderPipelineState intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewRenderPipelineState", MVGAL_WORKLOAD_METAL_RENDER, device);

    if (!real_MTLDeviceNewRenderPipelineState) {
        real_MTLDeviceNewRenderPipelineState = get_real_function("MTLDeviceNewRenderPipelineState");
    }
    if (real_MTLDeviceNewRenderPipelineState) {
        MetalRenderPipelineRef result = real_MTLDeviceNewRenderPipelineState(device, descriptor, error);
        if (result) {
            log_debug("Created render pipeline %p for device %p", result, device);
        }
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

    log_debug("MTLDeviceNewComputePipelineState intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewComputePipelineState", MVGAL_WORKLOAD_METAL_COMPUTE, device);

    if (!real_MTLDeviceNewComputePipelineState) {
        real_MTLDeviceNewComputePipelineState = get_real_function("MTLDeviceNewComputePipelineState");
    }
    if (real_MTLDeviceNewComputePipelineState) {
        MetalComputePipelineRef result = real_MTLDeviceNewComputePipelineState(device, descriptor, error);
        if (result) {
            log_debug("Created compute pipeline %p for device %p", result, device);
        }
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

    log_debug("MTLCommandQueueCommandBuffer intercepted (queue: %p)", queue);

    // Get device for this queue
    MetalDeviceRef device = get_device_for_queue(queue);
    submit_metal_workload("MTLCommandQueueCommandBuffer", MVGAL_WORKLOAD_METAL_COMMAND, device);

    if (!real_MTLCommandQueueCommandBuffer) {
        real_MTLCommandQueueCommandBuffer = get_real_function("MTLCommandQueueCommandBuffer");
    }
    if (real_MTLCommandQueueCommandBuffer) {
        MetalCommandBufferRef command_buffer = real_MTLCommandQueueCommandBuffer(queue);
        if (command_buffer) {
            // Register command buffer with queue mapping
            register_metal_command_buffer(command_buffer, queue);
        }
        return command_buffer;
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

    log_debug("MTLCommandBufferCommit intercepted (buffer: %p)", commandBuffer);

    // Get device for this command buffer
    MetalDeviceRef device = get_device_for_command_buffer(commandBuffer);
    submit_metal_workload("MTLCommandBufferCommit", MVGAL_WORKLOAD_METAL_COMMIT, device);

    if (!real_MTLCommandBufferCommit) {
        real_MTLCommandBufferCommit = get_real_function("MTLCommandBufferCommit");
    }
    if (real_MTLCommandBufferCommit) {
        real_MTLCommandBufferCommit(commandBuffer);
        // Unregister after commit
        unregister_metal_command_buffer(commandBuffer);
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

    log_debug("MTLCommandBufferPresentDrawables intercepted (buffer: %p)", commandBuffer);

    // Get device for this command buffer
    MetalDeviceRef device = get_device_for_command_buffer(commandBuffer);
    submit_metal_workload("MTLCommandBufferPresentDrawables", MVGAL_WORKLOAD_METAL_PRESENT, device);

    if (!real_MTLCommandBufferPresentDrawables) {
        real_MTLCommandBufferPresentDrawables = get_real_function("MTLCommandBufferPresentDrawables");
    }
    if (real_MTLCommandBufferPresentDrawables) {
        real_MTLCommandBufferPresentDrawables(commandBuffer);
    }
}

/******************************************************************************
 * Additional Metal Device Method Intercepts
 ******************************************************************************/

// MTLDeviceNewBufferWithBytes
MetalBufferRef MTLDeviceNewBufferWithBytes(
    MetalDeviceRef device,
    const void *bytes,
    size_t length,
    unsigned long options
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewBufferWithBytes) {
            real_MTLDeviceNewBufferWithBytes = get_real_function("MTLDeviceNewBufferWithBytes");
        }
        if (real_MTLDeviceNewBufferWithBytes) {
            return real_MTLDeviceNewBufferWithBytes(device, bytes, length, (uint32_t)options);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewBufferWithBytes intercepted (size: %zu, options: %lu, device: %p)", length, options, device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewBufferWithBytes", MVGAL_WORKLOAD_METAL_BUFFER, device);

    if (!real_MTLDeviceNewBufferWithBytes) {
        real_MTLDeviceNewBufferWithBytes = get_real_function("MTLDeviceNewBufferWithBytes");
    }
    if (real_MTLDeviceNewBufferWithBytes) {
        MetalBufferRef buffer = real_MTLDeviceNewBufferWithBytes(device, bytes, length, (uint32_t)options);
        if (buffer) {
            log_debug("Created Metal buffer %p (size: %zu) for device %p", buffer, length, device);
        }
        return buffer;
    }

    return NULL;
}

// MTLDeviceNewTextureWithDescriptor
MetalTextureRef MTLDeviceNewTextureWithDescriptor(
    MetalDeviceRef device,
    void *descriptor
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewTextureWithDescriptor) {
            real_MTLDeviceNewTextureWithDescriptor = get_real_function("MTLDeviceNewTextureWithDescriptor");
        }
        if (real_MTLDeviceNewTextureWithDescriptor) {
            return real_MTLDeviceNewTextureWithDescriptor(device, descriptor);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewTextureWithDescriptor intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewTextureWithDescriptor", MVGAL_WORKLOAD_METAL_TEXTURE, device);

    if (!real_MTLDeviceNewTextureWithDescriptor) {
        real_MTLDeviceNewTextureWithDescriptor = get_real_function("MTLDeviceNewTextureWithDescriptor");
    }
    if (real_MTLDeviceNewTextureWithDescriptor) {
        MetalTextureRef texture = real_MTLDeviceNewTextureWithDescriptor(device, descriptor);
        if (texture) {
            log_debug("Created Metal texture %p for device %p", texture, device);
        }
        return texture;
    }

    return NULL;
}

// MTLDeviceNewSamplerStateWithDescriptor
void* MTLDeviceNewSamplerStateWithDescriptor(
    MetalDeviceRef device,
    void *descriptor
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewSamplerStateWithDescriptor) {
            real_MTLDeviceNewSamplerStateWithDescriptor = get_real_function("MTLDeviceNewSamplerStateWithDescriptor");
        }
        if (real_MTLDeviceNewSamplerStateWithDescriptor) {
            return real_MTLDeviceNewSamplerStateWithDescriptor(device, descriptor);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewSamplerStateWithDescriptor intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewSamplerStateWithDescriptor", MVGAL_WORKLOAD_METAL_RENDER, device);

    if (!real_MTLDeviceNewSamplerStateWithDescriptor) {
        real_MTLDeviceNewSamplerStateWithDescriptor = get_real_function("MTLDeviceNewSamplerStateWithDescriptor");
    }
    if (real_MTLDeviceNewSamplerStateWithDescriptor) {
        void *sampler = real_MTLDeviceNewSamplerStateWithDescriptor(device, descriptor);
        if (sampler) {
            log_debug("Created sampler state %p for device %p", sampler, device);
        }
        return sampler;
    }

    return NULL;
}

// MTLDeviceNewDepthStencilStateWithDescriptor
void* MTLDeviceNewDepthStencilStateWithDescriptor(
    MetalDeviceRef device,
    void *descriptor
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewDepthStencilStateWithDescriptor) {
            real_MTLDeviceNewDepthStencilStateWithDescriptor = get_real_function("MTLDeviceNewDepthStencilStateWithDescriptor");
        }
        if (real_MTLDeviceNewDepthStencilStateWithDescriptor) {
            return real_MTLDeviceNewDepthStencilStateWithDescriptor(device, descriptor);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewDepthStencilStateWithDescriptor intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewDepthStencilStateWithDescriptor", MVGAL_WORKLOAD_METAL_RENDER, device);

    if (!real_MTLDeviceNewDepthStencilStateWithDescriptor) {
        real_MTLDeviceNewDepthStencilStateWithDescriptor = get_real_function("MTLDeviceNewDepthStencilStateWithDescriptor");
    }
    if (real_MTLDeviceNewDepthStencilStateWithDescriptor) {
        void *depthStencil = real_MTLDeviceNewDepthStencilStateWithDescriptor(device, descriptor);
        if (depthStencil) {
            log_debug("Created depth stencil state %p for device %p", depthStencil, device);
        }
        return depthStencil;
    }

    return NULL;
}

// MTLDeviceNewFence
void* MTLDeviceNewFence(
    MetalDeviceRef device,
    uint32_t descriptor
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewFence) {
            real_MTLDeviceNewFence = get_real_function("MTLDeviceNewFence");
        }
        if (real_MTLDeviceNewFence) {
            return real_MTLDeviceNewFence(device, descriptor);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewFence intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewFence", MVGAL_WORKLOAD_METAL_RENDER, device);

    if (!real_MTLDeviceNewFence) {
        real_MTLDeviceNewFence = get_real_function("MTLDeviceNewFence");
    }
    if (real_MTLDeviceNewFence) {
        void *fence = real_MTLDeviceNewFence(device, descriptor);
        if (fence) {
            log_debug("Created fence %p for device %p", fence, device);
        }
        return fence;
    }

    return NULL;
}

// MTLDeviceSupportsFeatureSet
bool MTLDeviceSupportsFeatureSet(
    MetalDeviceRef device,
    uint32_t featureSet
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceSupportsFeatureSet) {
            real_MTLDeviceSupportsFeatureSet = get_real_function("MTLDeviceSupportsFeatureSet");
        }
        if (real_MTLDeviceSupportsFeatureSet) {
            return real_MTLDeviceSupportsFeatureSet(device, featureSet);
        }
        return false;
    }

    log_debug("MTLDeviceSupportsFeatureSet intercepted (featureSet: %u, device: %p)", featureSet, device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceSupportsFeatureSet", MVGAL_WORKLOAD_METAL_RENDER, device);

    if (!real_MTLDeviceSupportsFeatureSet) {
        real_MTLDeviceSupportsFeatureSet = get_real_function("MTLDeviceSupportsFeatureSet");
    }
    if (real_MTLDeviceSupportsFeatureSet) {
        bool result = real_MTLDeviceSupportsFeatureSet(device, featureSet);
        log_debug("Device %p supports feature set %u: %s", device, featureSet, result ? "yes" : "no");
        return result;
    }

    return false;
}

// MTLDeviceNewLibraryWithSource
MetalLibraryRef MTLDeviceNewLibraryWithSource(
    MetalDeviceRef device,
    const char *source,
    void *options,
    void *error
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewLibraryWithSource) {
            real_MTLDeviceNewLibraryWithSource = get_real_function("MTLDeviceNewLibraryWithSource");
        }
        if (real_MTLDeviceNewLibraryWithSource) {
            return real_MTLDeviceNewLibraryWithSource(device, source, options, error);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewLibraryWithSource intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewLibraryWithSource", MVGAL_WORKLOAD_METAL_COMPUTE, device);

    if (!real_MTLDeviceNewLibraryWithSource) {
        real_MTLDeviceNewLibraryWithSource = get_real_function("MTLDeviceNewLibraryWithSource");
    }
    if (real_MTLDeviceNewLibraryWithSource) {
        MetalLibraryRef library = real_MTLDeviceNewLibraryWithSource(device, source, options, error);
        if (library) {
            log_debug("Created library %p for device %p", library, device);
        }
        return library;
    }

    return NULL;
}

// MTLDeviceNewRenderPipelineStateWithDescriptor
MetalRenderPipelineRef MTLDeviceNewRenderPipelineStateWithDescriptor(
    MetalDeviceRef device,
    void *descriptor,
    void *error
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceNewRenderPipelineStateWithDescriptor) {
            real_MTLDeviceNewRenderPipelineStateWithDescriptor = get_real_function("MTLDeviceNewRenderPipelineStateWithDescriptor");
        }
        if (real_MTLDeviceNewRenderPipelineStateWithDescriptor) {
            return real_MTLDeviceNewRenderPipelineStateWithDescriptor(device, descriptor, error);
        }
        return NULL;
    }

    log_debug("MTLDeviceNewRenderPipelineStateWithDescriptor intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceNewRenderPipelineStateWithDescriptor", MVGAL_WORKLOAD_METAL_RENDER, device);

    if (!real_MTLDeviceNewRenderPipelineStateWithDescriptor) {
        real_MTLDeviceNewRenderPipelineStateWithDescriptor = get_real_function("MTLDeviceNewRenderPipelineStateWithDescriptor");
    }
    if (real_MTLDeviceNewRenderPipelineStateWithDescriptor) {
        MetalRenderPipelineRef result = real_MTLDeviceNewRenderPipelineStateWithDescriptor(device, descriptor, error);
        if (result) {
            log_debug("Created render pipeline %p for device %p", result, device);
        }
        return result;
    }

    return NULL;
}

/******************************************************************************
 * Additional Command Queue Method Intercepts
 ******************************************************************************/

// MTLDeviceMakeCommandBuffer (alternative to MTLCommandQueueCommandBuffer)
MetalCommandBufferRef MTLDeviceMakeCommandBuffer(
    MetalDeviceRef device
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLDeviceMakeCommandBuffer) {
            real_MTLDeviceMakeCommandBuffer = get_real_function("MTLDeviceMakeCommandBuffer");
        }
        if (real_MTLDeviceMakeCommandBuffer) {
            return real_MTLDeviceMakeCommandBuffer(device);
        }
        return NULL;
    }

    log_debug("MTLDeviceMakeCommandBuffer intercepted (device: %p)", device);

    // Submit workload with device info
    submit_metal_workload("MTLDeviceMakeCommandBuffer", MVGAL_WORKLOAD_METAL_COMMAND, device);

    if (!real_MTLDeviceMakeCommandBuffer) {
        real_MTLDeviceMakeCommandBuffer = get_real_function("MTLDeviceMakeCommandBuffer");
    }
    if (real_MTLDeviceMakeCommandBuffer) {
        MetalCommandBufferRef command_buffer = real_MTLDeviceMakeCommandBuffer(device);
        if (command_buffer) {
            // Need to find queue for this device to register
            // For now, register with NULL queue - will be updated when queue is known
            log_debug("Created command buffer %p for device %p", command_buffer, device);
        }
        return command_buffer;
    }

    return NULL;
}

// MTLCommandQueueCommandBufferWithUnretainedReferences
MetalCommandBufferRef MTLCommandQueueCommandBufferWithUnretainedReferences(
    MetalCommandQueueRef queue
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandQueueCommandBufferWithUnretainedReferences) {
            real_MTLCommandQueueCommandBufferWithUnretainedReferences = get_real_function("MTLCommandQueueCommandBufferWithUnretainedReferences");
        }
        if (real_MTLCommandQueueCommandBufferWithUnretainedReferences) {
            return real_MTLCommandQueueCommandBufferWithUnretainedReferences(queue);
        }
        return NULL;
    }

    log_debug("MTLCommandQueueCommandBufferWithUnretainedReferences intercepted (queue: %p)", queue);

    // Get device for this queue
    MetalDeviceRef device = get_device_for_queue(queue);
    submit_metal_workload("MTLCommandQueueCommandBufferWithUnretainedReferences", MVGAL_WORKLOAD_METAL_COMMAND, device);

    if (!real_MTLCommandQueueCommandBufferWithUnretainedReferences) {
        real_MTLCommandQueueCommandBufferWithUnretainedReferences = get_real_function("MTLCommandQueueCommandBufferWithUnretainedReferences");
    }
    if (real_MTLCommandQueueCommandBufferWithUnretainedReferences) {
        MetalCommandBufferRef command_buffer = real_MTLCommandQueueCommandBufferWithUnretainedReferences(queue);
        if (command_buffer) {
            // Register command buffer with queue mapping
            register_metal_command_buffer(command_buffer, queue);
        }
        return command_buffer;
    }

    return NULL;
}

/******************************************************************************
 * Additional Command Buffer Method Intercepts
 ******************************************************************************/

// MTLCommandBufferRenderCommandEncoderWithDescriptor
void* MTLCommandBufferRenderCommandEncoderWithDescriptor(
    MetalCommandBufferRef commandBuffer,
    void *descriptor
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandBufferRenderCommandEncoderWithDescriptor) {
            real_MTLCommandBufferRenderCommandEncoderWithDescriptor = get_real_function("MTLCommandBufferRenderCommandEncoderWithDescriptor");
        }
        if (real_MTLCommandBufferRenderCommandEncoderWithDescriptor) {
            return real_MTLCommandBufferRenderCommandEncoderWithDescriptor(commandBuffer, descriptor);
        }
        return NULL;
    }

    log_debug("MTLCommandBufferRenderCommandEncoderWithDescriptor intercepted (buffer: %p)", commandBuffer);

    // Get device for this command buffer
    MetalDeviceRef device = get_device_for_command_buffer(commandBuffer);
    submit_metal_workload("MTLCommandBufferRenderCommandEncoderWithDescriptor", MVGAL_WORKLOAD_METAL_RENDER, device);

    if (!real_MTLCommandBufferRenderCommandEncoderWithDescriptor) {
        real_MTLCommandBufferRenderCommandEncoderWithDescriptor = get_real_function("MTLCommandBufferRenderCommandEncoderWithDescriptor");
    }
    if (real_MTLCommandBufferRenderCommandEncoderWithDescriptor) {
        void *encoder = real_MTLCommandBufferRenderCommandEncoderWithDescriptor(commandBuffer, descriptor);
        if (encoder) {
            log_debug("Created render command encoder %p for buffer %p", encoder, commandBuffer);
        }
        return encoder;
    }

    return NULL;
}

// MTLCommandBufferComputeCommandEncoder
void* MTLCommandBufferComputeCommandEncoder(
    MetalCommandBufferRef commandBuffer
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandBufferComputeCommandEncoder) {
            real_MTLCommandBufferComputeCommandEncoder = get_real_function("MTLCommandBufferComputeCommandEncoder");
        }
        if (real_MTLCommandBufferComputeCommandEncoder) {
            return real_MTLCommandBufferComputeCommandEncoder(commandBuffer);
        }
        return NULL;
    }

    log_debug("MTLCommandBufferComputeCommandEncoder intercepted (buffer: %p)", commandBuffer);

    // Get device for this command buffer
    MetalDeviceRef device = get_device_for_command_buffer(commandBuffer);
    submit_metal_workload("MTLCommandBufferComputeCommandEncoder", MVGAL_WORKLOAD_METAL_COMPUTE, device);

    if (!real_MTLCommandBufferComputeCommandEncoder) {
        real_MTLCommandBufferComputeCommandEncoder = get_real_function("MTLCommandBufferComputeCommandEncoder");
    }
    if (real_MTLCommandBufferComputeCommandEncoder) {
        void *encoder = real_MTLCommandBufferComputeCommandEncoder(commandBuffer);
        if (encoder) {
            log_debug("Created compute command encoder %p for buffer %p", encoder, commandBuffer);
        }
        return encoder;
    }

    return NULL;
}

// MTLCommandBufferBlitCommandEncoder
void* MTLCommandBufferBlitCommandEncoder(
    MetalCommandBufferRef commandBuffer
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandBufferBlitCommandEncoder) {
            real_MTLCommandBufferBlitCommandEncoder = get_real_function("MTLCommandBufferBlitCommandEncoder");
        }
        if (real_MTLCommandBufferBlitCommandEncoder) {
            return real_MTLCommandBufferBlitCommandEncoder(commandBuffer);
        }
        return NULL;
    }

    log_debug("MTLCommandBufferBlitCommandEncoder intercepted (buffer: %p)", commandBuffer);

    // Get device for this command buffer
    MetalDeviceRef device = get_device_for_command_buffer(commandBuffer);
    submit_metal_workload("MTLCommandBufferBlitCommandEncoder", MVGAL_WORKLOAD_METAL_COMMAND, device);

    if (!real_MTLCommandBufferBlitCommandEncoder) {
        real_MTLCommandBufferBlitCommandEncoder = get_real_function("MTLCommandBufferBlitCommandEncoder");
    }
    if (real_MTLCommandBufferBlitCommandEncoder) {
        void *encoder = real_MTLCommandBufferBlitCommandEncoder(commandBuffer);
        if (encoder) {
            log_debug("Created blit command encoder %p for buffer %p", encoder, commandBuffer);
        }
        return encoder;
    }

    return NULL;
}

// MTLCommandBufferPresentDrawable
void MTLCommandBufferPresentDrawable(
    MetalCommandBufferRef commandBuffer,
    MetalDrawableRef drawable
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandBufferPresentDrawable) {
            real_MTLCommandBufferPresentDrawable = get_real_function("MTLCommandBufferPresentDrawable");
        }
        if (real_MTLCommandBufferPresentDrawable) {
            real_MTLCommandBufferPresentDrawable(commandBuffer, drawable);
        }
        return;
    }

    log_debug("MTLCommandBufferPresentDrawable intercepted (buffer: %p, drawable: %p)", commandBuffer, drawable);

    // Get device for this command buffer
    MetalDeviceRef device = get_device_for_command_buffer(commandBuffer);
    submit_metal_workload("MTLCommandBufferPresentDrawable", MVGAL_WORKLOAD_METAL_PRESENT, device);

    if (!real_MTLCommandBufferPresentDrawable) {
        real_MTLCommandBufferPresentDrawable = get_real_function("MTLCommandBufferPresentDrawable");
    }
    if (real_MTLCommandBufferPresentDrawable) {
        real_MTLCommandBufferPresentDrawable(commandBuffer, drawable);
    }
}

// MTLCommandBufferWaitUntilCompleted
void MTLCommandBufferWaitUntilCompleted(
    MetalCommandBufferRef commandBuffer
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLCommandBufferWaitUntilCompleted) {
            real_MTLCommandBufferWaitUntilCompleted = get_real_function("MTLCommandBufferWaitUntilCompleted");
        }
        if (real_MTLCommandBufferWaitUntilCompleted) {
            real_MTLCommandBufferWaitUntilCompleted(commandBuffer);
        }
        return;
    }

    log_debug("MTLCommandBufferWaitUntilCompleted intercepted (buffer: %p)", commandBuffer);

    // Get device for this command buffer
    MetalDeviceRef device = get_device_for_command_buffer(commandBuffer);
    submit_metal_workload("MTLCommandBufferWaitUntilCompleted", MVGAL_WORKLOAD_METAL_COMMIT, device);

    if (!real_MTLCommandBufferWaitUntilCompleted) {
        real_MTLCommandBufferWaitUntilCompleted = get_real_function("MTLCommandBufferWaitUntilCompleted");
    }
    if (real_MTLCommandBufferWaitUntilCompleted) {
        real_MTLCommandBufferWaitUntilCompleted(commandBuffer);
    }
}

/******************************************************************************
 * Render Command Encoder Method Intercepts
 ******************************************************************************/

// MTLRenderCommandEncoderSetRenderPipelineState
void MTLRenderCommandEncoderSetRenderPipelineState(
    void *encoder,
    MetalRenderPipelineRef pipelineState
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLRenderCommandEncoderSetRenderPipelineState) {
            real_MTLRenderCommandEncoderSetRenderPipelineState = get_real_function("MTLRenderCommandEncoderSetRenderPipelineState");
        }
        if (real_MTLRenderCommandEncoderSetRenderPipelineState) {
            real_MTLRenderCommandEncoderSetRenderPipelineState(encoder, pipelineState);
        }
        return;
    }

    log_debug("MTLRenderCommandEncoderSetRenderPipelineState intercepted (encoder: %p, pipeline: %p)", encoder, pipelineState);

    // Submit workload - device info not directly available from encoder
    submit_metal_workload("MTLRenderCommandEncoderSetRenderPipelineState", MVGAL_WORKLOAD_METAL_RENDER, NULL);

    if (!real_MTLRenderCommandEncoderSetRenderPipelineState) {
        real_MTLRenderCommandEncoderSetRenderPipelineState = get_real_function("MTLRenderCommandEncoderSetRenderPipelineState");
    }
    if (real_MTLRenderCommandEncoderSetRenderPipelineState) {
        real_MTLRenderCommandEncoderSetRenderPipelineState(encoder, pipelineState);
    }
}

// MTLRenderCommandEncoderSetVertexBuffer
void MTLRenderCommandEncoderSetVertexBuffer(
    void *encoder,
    MetalBufferRef buffer,
    size_t offset,
    uint32_t index
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLRenderCommandEncoderSetVertexBuffer) {
            real_MTLRenderCommandEncoderSetVertexBuffer = get_real_function("MTLRenderCommandEncoderSetVertexBuffer");
        }
        if (real_MTLRenderCommandEncoderSetVertexBuffer) {
            real_MTLRenderCommandEncoderSetVertexBuffer(encoder, buffer, offset, index);
        }
        return;
    }

    log_debug("MTLRenderCommandEncoderSetVertexBuffer intercepted (encoder: %p, buffer: %p, index: %u)", encoder, buffer, index);

    // Submit workload
    submit_metal_workload("MTLRenderCommandEncoderSetVertexBuffer", MVGAL_WORKLOAD_METAL_RENDER, NULL);

    if (!real_MTLRenderCommandEncoderSetVertexBuffer) {
        real_MTLRenderCommandEncoderSetVertexBuffer = get_real_function("MTLRenderCommandEncoderSetVertexBuffer");
    }
    if (real_MTLRenderCommandEncoderSetVertexBuffer) {
        real_MTLRenderCommandEncoderSetVertexBuffer(encoder, buffer, offset, index);
    }
}

// MTLRenderCommandEncoderSetFragmentBuffer
void MTLRenderCommandEncoderSetFragmentBuffer(
    void *encoder,
    MetalBufferRef buffer,
    size_t offset,
    uint32_t index
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLRenderCommandEncoderSetFragmentBuffer) {
            real_MTLRenderCommandEncoderSetFragmentBuffer = get_real_function("MTLRenderCommandEncoderSetFragmentBuffer");
        }
        if (real_MTLRenderCommandEncoderSetFragmentBuffer) {
            real_MTLRenderCommandEncoderSetFragmentBuffer(encoder, buffer, offset, index);
        }
        return;
    }

    log_debug("MTLRenderCommandEncoderSetFragmentBuffer intercepted (encoder: %p, buffer: %p, index: %u)", encoder, buffer, index);

    // Submit workload
    submit_metal_workload("MTLRenderCommandEncoderSetFragmentBuffer", MVGAL_WORKLOAD_METAL_RENDER, NULL);

    if (!real_MTLRenderCommandEncoderSetFragmentBuffer) {
        real_MTLRenderCommandEncoderSetFragmentBuffer = get_real_function("MTLRenderCommandEncoderSetFragmentBuffer");
    }
    if (real_MTLRenderCommandEncoderSetFragmentBuffer) {
        real_MTLRenderCommandEncoderSetFragmentBuffer(encoder, buffer, offset, index);
    }
}

// MTLRenderCommandEncoderDrawPrimitives
void MTLRenderCommandEncoderDrawPrimitives(
    void *encoder,
    uint32_t primitiveType,
    size_t vertexStart,
    size_t vertexCount,
    size_t instanceCount
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLRenderCommandEncoderDrawPrimitives) {
            real_MTLRenderCommandEncoderDrawPrimitives = get_real_function("MTLRenderCommandEncoderDrawPrimitives");
        }
        if (real_MTLRenderCommandEncoderDrawPrimitives) {
            real_MTLRenderCommandEncoderDrawPrimitives(encoder, primitiveType, vertexStart, vertexCount, instanceCount);
        }
        return;
    }

    log_debug("MTLRenderCommandEncoderDrawPrimitives intercepted (encoder: %p, type: %u, start: %zu, count: %zu)", encoder, primitiveType, vertexStart, vertexCount);

    // Submit workload
    submit_metal_workload("MTLRenderCommandEncoderDrawPrimitives", MVGAL_WORKLOAD_METAL_RENDER, NULL);

    if (!real_MTLRenderCommandEncoderDrawPrimitives) {
        real_MTLRenderCommandEncoderDrawPrimitives = get_real_function("MTLRenderCommandEncoderDrawPrimitives");
    }
    if (real_MTLRenderCommandEncoderDrawPrimitives) {
        real_MTLRenderCommandEncoderDrawPrimitives(encoder, primitiveType, vertexStart, vertexCount, instanceCount);
    }
}

// MTLRenderCommandEncoderDrawIndexedPrimitives
void MTLRenderCommandEncoderDrawIndexedPrimitives(
    void *encoder,
    uint32_t primitiveType,
    size_t indexCount,
    uint32_t indexType,
    MetalBufferRef indexBuffer,
    size_t indexBufferOffset,
    size_t instanceCount
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLRenderCommandEncoderDrawIndexedPrimitives) {
            real_MTLRenderCommandEncoderDrawIndexedPrimitives = get_real_function("MTLRenderCommandEncoderDrawIndexedPrimitives");
        }
        if (real_MTLRenderCommandEncoderDrawIndexedPrimitives) {
            real_MTLRenderCommandEncoderDrawIndexedPrimitives(encoder, primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, instanceCount);
        }
        return;
    }

    log_debug("MTLRenderCommandEncoderDrawIndexedPrimitives intercepted (encoder: %p, type: %u, count: %zu)", encoder, primitiveType, indexCount);

    // Submit workload
    submit_metal_workload("MTLRenderCommandEncoderDrawIndexedPrimitives", MVGAL_WORKLOAD_METAL_RENDER, NULL);

    if (!real_MTLRenderCommandEncoderDrawIndexedPrimitives) {
        real_MTLRenderCommandEncoderDrawIndexedPrimitives = get_real_function("MTLRenderCommandEncoderDrawIndexedPrimitives");
    }
    if (real_MTLRenderCommandEncoderDrawIndexedPrimitives) {
        real_MTLRenderCommandEncoderDrawIndexedPrimitives(encoder, primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, instanceCount);
    }
}

// MTLRenderCommandEncoderEndEncoding
void MTLRenderCommandEncoderEndEncoding(
    void *encoder
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLRenderCommandEncoderEndEncoding) {
            real_MTLRenderCommandEncoderEndEncoding = get_real_function("MTLRenderCommandEncoderEndEncoding");
        }
        if (real_MTLRenderCommandEncoderEndEncoding) {
            real_MTLRenderCommandEncoderEndEncoding(encoder);
        }
        return;
    }

    log_debug("MTLRenderCommandEncoderEndEncoding intercepted (encoder: %p)", encoder);

    // Submit workload
    submit_metal_workload("MTLRenderCommandEncoderEndEncoding", MVGAL_WORKLOAD_METAL_RENDER, NULL);

    if (!real_MTLRenderCommandEncoderEndEncoding) {
        real_MTLRenderCommandEncoderEndEncoding = get_real_function("MTLRenderCommandEncoderEndEncoding");
    }
    if (real_MTLRenderCommandEncoderEndEncoding) {
        real_MTLRenderCommandEncoderEndEncoding(encoder);
    }
}

/******************************************************************************
 * Compute Command Encoder Method Intercepts
 ******************************************************************************/

// MTLComputeCommandEncoderSetComputePipelineState
void MTLComputeCommandEncoderSetComputePipelineState(
    void *encoder,
    MetalComputePipelineRef pipelineState
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLComputeCommandEncoderSetComputePipelineState) {
            real_MTLComputeCommandEncoderSetComputePipelineState = get_real_function("MTLComputeCommandEncoderSetComputePipelineState");
        }
        if (real_MTLComputeCommandEncoderSetComputePipelineState) {
            real_MTLComputeCommandEncoderSetComputePipelineState(encoder, pipelineState);
        }
        return;
    }

    log_debug("MTLComputeCommandEncoderSetComputePipelineState intercepted (encoder: %p, pipeline: %p)", encoder, pipelineState);

    // Submit workload
    submit_metal_workload("MTLComputeCommandEncoderSetComputePipelineState", MVGAL_WORKLOAD_METAL_COMPUTE, NULL);

    if (!real_MTLComputeCommandEncoderSetComputePipelineState) {
        real_MTLComputeCommandEncoderSetComputePipelineState = get_real_function("MTLComputeCommandEncoderSetComputePipelineState");
    }
    if (real_MTLComputeCommandEncoderSetComputePipelineState) {
        real_MTLComputeCommandEncoderSetComputePipelineState(encoder, pipelineState);
    }
}

// MTLComputeCommandEncoderSetBuffer
void MTLComputeCommandEncoderSetBuffer(
    void *encoder,
    MetalBufferRef buffer,
    size_t offset,
    uint32_t index
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLComputeCommandEncoderSetBuffer) {
            real_MTLComputeCommandEncoderSetBuffer = get_real_function("MTLComputeCommandEncoderSetBuffer");
        }
        if (real_MTLComputeCommandEncoderSetBuffer) {
            real_MTLComputeCommandEncoderSetBuffer(encoder, buffer, offset, index);
        }
        return;
    }

    log_debug("MTLComputeCommandEncoderSetBuffer intercepted (encoder: %p, buffer: %p, index: %u)", encoder, buffer, index);

    // Submit workload
    submit_metal_workload("MTLComputeCommandEncoderSetBuffer", MVGAL_WORKLOAD_METAL_COMPUTE, NULL);

    if (!real_MTLComputeCommandEncoderSetBuffer) {
        real_MTLComputeCommandEncoderSetBuffer = get_real_function("MTLComputeCommandEncoderSetBuffer");
    }
    if (real_MTLComputeCommandEncoderSetBuffer) {
        real_MTLComputeCommandEncoderSetBuffer(encoder, buffer, offset, index);
    }
}

// MTLComputeCommandEncoderDispatchThreadgroups
void MTLComputeCommandEncoderDispatchThreadgroups(
    void *encoder,
    size_t threadgroupsPerGrid_x,
    size_t threadgroupsPerGrid_y,
    size_t threadgroupsPerGrid_z,
    size_t threadsPerThreadgroup_x,
    size_t threadsPerThreadgroup_y,
    size_t threadsPerThreadgroup_z
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLComputeCommandEncoderDispatchThreadgroups) {
            real_MTLComputeCommandEncoderDispatchThreadgroups = get_real_function("MTLComputeCommandEncoderDispatchThreadgroups");
        }
        if (real_MTLComputeCommandEncoderDispatchThreadgroups) {
            real_MTLComputeCommandEncoderDispatchThreadgroups(encoder, threadgroupsPerGrid_x, threadgroupsPerGrid_y, threadgroupsPerGrid_z, threadsPerThreadgroup_x, threadsPerThreadgroup_y, threadsPerThreadgroup_z);
        }
        return;
    }

    log_debug("MTLComputeCommandEncoderDispatchThreadgroups intercepted (encoder: %p, grid: %zu x %zu x %zu)", encoder, threadgroupsPerGrid_x, threadgroupsPerGrid_y, threadgroupsPerGrid_z);

    // Submit workload
    submit_metal_workload("MTLComputeCommandEncoderDispatchThreadgroups", MVGAL_WORKLOAD_METAL_COMPUTE, NULL);

    if (!real_MTLComputeCommandEncoderDispatchThreadgroups) {
        real_MTLComputeCommandEncoderDispatchThreadgroups = get_real_function("MTLComputeCommandEncoderDispatchThreadgroups");
    }
    if (real_MTLComputeCommandEncoderDispatchThreadgroups) {
        real_MTLComputeCommandEncoderDispatchThreadgroups(encoder, threadgroupsPerGrid_x, threadgroupsPerGrid_y, threadgroupsPerGrid_z, threadsPerThreadgroup_x, threadsPerThreadgroup_y, threadsPerThreadgroup_z);
    }
}

// MTLComputeCommandEncoderEndEncoding
void MTLComputeCommandEncoderEndEncoding(
    void *encoder
) {
    if (!wrapper_state.enabled) {
        if (!real_MTLComputeCommandEncoderEndEncoding) {
            real_MTLComputeCommandEncoderEndEncoding = get_real_function("MTLComputeCommandEncoderEndEncoding");
        }
        if (real_MTLComputeCommandEncoderEndEncoding) {
            real_MTLComputeCommandEncoderEndEncoding(encoder);
        }
        return;
    }

    log_debug("MTLComputeCommandEncoderEndEncoding intercepted (encoder: %p)", encoder);

    // Submit workload
    submit_metal_workload("MTLComputeCommandEncoderEndEncoding", MVGAL_WORKLOAD_METAL_COMPUTE, NULL);

    if (!real_MTLComputeCommandEncoderEndEncoding) {
        real_MTLComputeCommandEncoderEndEncoding = get_real_function("MTLComputeCommandEncoderEndEncoding");
    }
    if (real_MTLComputeCommandEncoderEndEncoding) {
        real_MTLComputeCommandEncoderEndEncoding(encoder);
    }
}
