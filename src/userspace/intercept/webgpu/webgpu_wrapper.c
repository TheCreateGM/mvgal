/**
 * @file webgpu_wrapper.c
 * @brief WebGPU API Interception Layer
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * Intercepts WebGPU API calls via LD_PRELOAD to distribute
 * WebGPU workloads across multiple GPUs in a multi-vendor environment.
 *
 * WebGPU is a web standard API for GPU acceleration in web browsers,
 * but can also be used in native applications via wgpu (Rust implementation)
 * or other WebGPU implementations.
 *
 * Usage:
 *   export MVGAL_WEBGPU_ENABLED=1
 *   export LD_PRELOAD=/path/to/libmvgal_webgpu.so
 *   ./your_webgpu_application
 *
 * Environment Variables:
 *   MVGAL_WEBGPU_ENABLED=1    - Enable WebGPU interception (default: 1)
 *   MVGAL_WEBGPU_DEBUG=1      - Enable debug logging (default: 0)
 *   MVGAL_WEBGPU_STRATEGY=round_robin - Distribution strategy
 *   MVGAL_WEBGPU_GPUS="0,1"   - GPU indices to use
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <inttypes.h>
#include <dlfcn.h>
#include <pthread.h>

// MVGAL headers
#include <mvgal/mvgal.h>
#include <mvgal/mvgal_scheduler.h>
#include <mvgal/mvgal_execution.h>

/******************************************************************************
 * WebGPU Type Definitions
 *
 * Simplified versions of WebGPU API types.
 * The real WebGPU API uses opaque handles.
 ******************************************************************************/

typedef void* WGPUType;

// Forward declarations for tracking
typedef struct webgpu_device_mapping_t webgpu_device_mapping_t;
typedef struct webgpu_queue_mapping_t webgpu_queue_mapping_t;
typedef struct webgpu_encoder_mapping_t webgpu_encoder_mapping_t;

/******************************************************************************
 * Configuration and State
 ******************************************************************************/

#define MVGAL_WEBGPU_VERSION "0.2.0"

typedef struct {
    bool enabled;
    bool debug;
    int gpu_count;
    int current_gpu;
    char strategy[64];
    pthread_mutex_t lock;
    mvgal_context_t context;
} webgpu_wrapper_state_t;

static webgpu_wrapper_state_t wrapper_state = {
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

typedef WGPUType (*wgpuCreateInstance_t)(const void* desc);
typedef void (*wgpuInstanceRequestAdapter_t)(WGPUType instance, const void* options, void* callback, void* userdata);
typedef void (*wgpuAdapterRequestDevice_t)(WGPUType adapter, const void* descriptor, void* callback, void* userdata);
typedef WGPUType (*wgpuDeviceCreateQueue_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreateBuffer_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreateTexture_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreateShaderModule_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreateBindGroupLayout_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreatePipelineLayout_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreateRenderPipeline_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreateComputePipeline_t)(WGPUType device, const void* descriptor);
typedef WGPUType (*wgpuDeviceCreateCommandEncoder_t)(WGPUType device, const void* descriptor);
typedef void (*wgpuCommandEncoderBeginRenderPass_t)(WGPUType encoder, const void* descriptor);
typedef void (*wgpuCommandEncoderBeginComputePass_t)(WGPUType encoder, const void* descriptor);
typedef void (*wgpuRenderPassEncoderEnd_t)(WGPUType encoder);
typedef void (*wgpuComputePassEncoderEnd_t)(WGPUType encoder);
typedef WGPUType (*wgpuCommandEncoderFinish_t)(WGPUType encoder, const void* descriptor);
typedef void (*wgpuQueueSubmit_t)(WGPUType queue, unsigned int count, const void* commands, void* semaphore);

// Real function pointers
static wgpuCreateInstance_t real_wgpuCreateInstance = NULL;
static wgpuInstanceRequestAdapter_t real_wgpuInstanceRequestAdapter = NULL;
static wgpuAdapterRequestDevice_t real_wgpuAdapterRequestDevice = NULL;
static wgpuDeviceCreateQueue_t real_wgpuDeviceCreateQueue = NULL;
static wgpuDeviceCreateBuffer_t real_wgpuDeviceCreateBuffer = NULL;
static wgpuDeviceCreateTexture_t real_wgpuDeviceCreateTexture = NULL;
static wgpuDeviceCreateShaderModule_t real_wgpuDeviceCreateShaderModule = NULL;
static wgpuDeviceCreateBindGroupLayout_t real_wgpuDeviceCreateBindGroupLayout = NULL;
static wgpuDeviceCreatePipelineLayout_t real_wgpuDeviceCreatePipelineLayout = NULL;
static wgpuDeviceCreateRenderPipeline_t real_wgpuDeviceCreateRenderPipeline = NULL;
static wgpuDeviceCreateComputePipeline_t real_wgpuDeviceCreateComputePipeline = NULL;
static wgpuDeviceCreateCommandEncoder_t real_wgpuDeviceCreateCommandEncoder = NULL;
static wgpuCommandEncoderBeginRenderPass_t real_wgpuCommandEncoderBeginRenderPass = NULL;
static wgpuCommandEncoderBeginComputePass_t real_wgpuCommandEncoderBeginComputePass = NULL;
static wgpuRenderPassEncoderEnd_t real_wgpuRenderPassEncoderEnd = NULL;
static wgpuComputePassEncoderEnd_t real_wgpuComputePassEncoderEnd = NULL;
static wgpuCommandEncoderFinish_t real_wgpuCommandEncoderFinish = NULL;
static wgpuQueueSubmit_t real_wgpuQueueSubmit = NULL;

// Instance/Adapter method pointers
typedef void (*wgpuInstanceEnumerateAdapters_t)(WGPUType instance, const void* options, void* callback);
static wgpuInstanceEnumerateAdapters_t real_wgpuInstanceEnumerateAdapters = NULL;

typedef void (*wgpuAdapterGetLimits_t)(WGPUType adapter, void* limits);
static wgpuAdapterGetLimits_t real_wgpuAdapterGetLimits = NULL;

typedef void (*wgpuAdapterGetProperties_t)(WGPUType adapter, void* properties);
static wgpuAdapterGetProperties_t real_wgpuAdapterGetProperties = NULL;

// Device method pointers
typedef WGPUType (*wgpuDeviceCreateBindGroup_t)(WGPUType device, const void* descriptor);
static wgpuDeviceCreateBindGroup_t real_wgpuDeviceCreateBindGroup = NULL;

typedef WGPUType (*wgpuDeviceCreateSampler_t)(WGPUType device, const void* descriptor);
static wgpuDeviceCreateSampler_t real_wgpuDeviceCreateSampler = NULL;

typedef WGPUType (*wgpuDeviceCreateQuerySet_t)(WGPUType device, const void* descriptor);
static wgpuDeviceCreateQuerySet_t real_wgpuDeviceCreateQuerySet = NULL;

typedef void (*wgpuDeviceDestroy_t)(WGPUType device);
static wgpuDeviceDestroy_t real_wgpuDeviceDestroy = NULL;

typedef void (*wgpuDeviceGetLimits_t)(WGPUType device, void* limits);
static wgpuDeviceGetLimits_t real_wgpuDeviceGetLimits = NULL;

typedef bool (*wgpuDeviceHasFeature_t)(WGPUType device, uint32_t feature);
static wgpuDeviceHasFeature_t real_wgpuDeviceHasFeature = NULL;

// Queue method pointers
typedef void (*wgpuQueueWriteBuffer_t)(WGPUType queue, WGPUType buffer, uint64_t buffer_offset, const void* data, size_t size);
static wgpuQueueWriteBuffer_t real_wgpuQueueWriteBuffer = NULL;

typedef void (*wgpuQueueWriteTexture_t)(WGPUType queue, const void* destination, const void* data, size_t data_size, const void* data_layout, const void* write_size);
static wgpuQueueWriteTexture_t real_wgpuQueueWriteTexture = NULL;

typedef void (*wgpuQueueCopyExternalImage_t)(WGPUType queue, const void* source, const void* source_origin, const void* destination, const void* copy_size, uint32_t copy_level);
static wgpuQueueCopyExternalImage_t real_wgpuQueueCopyExternalImage = NULL;

// CommandEncoder method pointers
typedef WGPUType (*wgpuCommandEncoderBeginBlitPass_t)(WGPUType encoder, const void* descriptor);
static wgpuCommandEncoderBeginBlitPass_t real_wgpuCommandEncoderBeginBlitPass = NULL;

typedef void (*wgpuCommandEncoderCopyBufferToBuffer_t)(WGPUType encoder, WGPUType source, uint64_t source_offset, WGPUType destination, uint64_t destination_offset, uint64_t size);
static wgpuCommandEncoderCopyBufferToBuffer_t real_wgpuCommandEncoderCopyBufferToBuffer = NULL;

typedef void (*wgpuCommandEncoderCopyBufferToTexture_t)(WGPUType encoder, const void* source, const void* destination, const void* copy_size);
static wgpuCommandEncoderCopyBufferToTexture_t real_wgpuCommandEncoderCopyBufferToTexture = NULL;

typedef void (*wgpuCommandEncoderCopyTextureToBuffer_t)(WGPUType encoder, const void* source, const void* destination, const void* copy_size);
static wgpuCommandEncoderCopyTextureToBuffer_t real_wgpuCommandEncoderCopyTextureToBuffer = NULL;

typedef void (*wgpuCommandEncoderCopyTextureToTexture_t)(WGPUType encoder, const void* source, const void* destination, const void* copy_size);
static wgpuCommandEncoderCopyTextureToTexture_t real_wgpuCommandEncoderCopyTextureToTexture = NULL;

// RenderPassEncoder method pointers
typedef void (*wgpuRenderPassEncoderSetBindGroup_t)(WGPUType encoder, uint32_t group_index, WGPUType group, uint32_t dynamic_offset_count, const uint32_t* dynamic_offsets);
static wgpuRenderPassEncoderSetBindGroup_t real_wgpuRenderPassEncoderSetBindGroup = NULL;

typedef void (*wgpuRenderPassEncoderSetIndexBuffer_t)(WGPUType encoder, WGPUType buffer, uint32_t format, uint64_t offset, uint64_t size);
static wgpuRenderPassEncoderSetIndexBuffer_t real_wgpuRenderPassEncoderSetIndexBuffer = NULL;

typedef void (*wgpuRenderPassEncoderSetPipeline_t)(WGPUType encoder, WGPUType pipeline);
static wgpuRenderPassEncoderSetPipeline_t real_wgpuRenderPassEncoderSetPipeline = NULL;

typedef void (*wgpuRenderPassEncoderSetVertexBuffer_t)(WGPUType encoder, uint32_t slot, WGPUType buffer, uint64_t offset, uint64_t size);
static wgpuRenderPassEncoderSetVertexBuffer_t real_wgpuRenderPassEncoderSetVertexBuffer = NULL;

typedef void (*wgpuRenderPassEncoderDraw_t)(WGPUType encoder, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
static wgpuRenderPassEncoderDraw_t real_wgpuRenderPassEncoderDraw = NULL;

typedef void (*wgpuRenderPassEncoderDrawIndexed_t)(WGPUType encoder, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t base_vertex, uint32_t first_instance);
static wgpuRenderPassEncoderDrawIndexed_t real_wgpuRenderPassEncoderDrawIndexed = NULL;

// ComputePassEncoder method pointers
typedef void (*wgpuComputePassEncoderSetPipeline_t)(WGPUType encoder, WGPUType pipeline);
static wgpuComputePassEncoderSetPipeline_t real_wgpuComputePassEncoderSetPipeline = NULL;

typedef void (*wgpuComputePassEncoderSetBindGroup_t)(WGPUType encoder, uint32_t group_index, WGPUType group, uint32_t dynamic_offset_count, const uint32_t* dynamic_offsets);
static wgpuComputePassEncoderSetBindGroup_t real_wgpuComputePassEncoderSetBindGroup = NULL;

typedef void (*wgpuComputePassEncoderDispatchWorkgroups_t)(WGPUType encoder, uint32_t workgroup_count_x, uint32_t workgroup_count_y, uint32_t workgroup_count_z);
static wgpuComputePassEncoderDispatchWorkgroups_t real_wgpuComputePassEncoderDispatchWorkgroups = NULL;

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
    fprintf(stderr, "[WEBGPU DEBUG] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[WEBGPU INFO] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[WEBGPU WARN] ");
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

/******************************************************************************
 * Device and Queue Tracking
 ******************************************************************************/

#define MAX_WEBGPU_DEVICES 8
#define MAX_WEBGPU_QUEUES 16
#define MAX_WEBGPU_ENCODERS 32

struct webgpu_device_mapping_t {
    WGPUType real_device;
    int mvgal_gpu_id;
    bool in_use;
};

struct webgpu_queue_mapping_t {
    WGPUType real_queue;
    WGPUType device;  // Back-pointer to device
    bool in_use;
};

struct webgpu_encoder_mapping_t {
    WGPUType real_encoder;
    WGPUType device;  // Back-pointer to device
    bool in_use;
};

static webgpu_device_mapping_t webgpu_device_map[MAX_WEBGPU_DEVICES] = {0};
static webgpu_queue_mapping_t webgpu_queue_map[MAX_WEBGPU_QUEUES] = {0};
static webgpu_encoder_mapping_t webgpu_encoder_map[MAX_WEBGPU_ENCODERS] = {0};
static int webgpu_device_count __attribute__((unused)) = 0;
static int webgpu_queue_count __attribute__((unused)) = 0;
static int webgpu_encoder_count __attribute__((unused)) = 0;

static int __attribute__((unused)) register_webgpu_device(WGPUType real_device, int mvgal_gpu_id) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_DEVICES; i++) {
        if (!webgpu_device_map[i].in_use) {
            webgpu_device_map[i].real_device = real_device;
            webgpu_device_map[i].mvgal_gpu_id = mvgal_gpu_id;
            webgpu_device_map[i].in_use = true;
            webgpu_device_count++;
            pthread_mutex_unlock(&wrapper_state.lock);
            log_debug("Registered WebGPU device %p -> GPU %d", real_device, mvgal_gpu_id);
            return i;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    log_warn("Device map full, cannot register WebGPU device %p", real_device);
    return -1;
}

static void __attribute__((unused)) unregister_webgpu_device(WGPUType real_device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_DEVICES; i++) {
        if (webgpu_device_map[i].in_use && webgpu_device_map[i].real_device == real_device) {
            log_debug("Unregistering WebGPU device %p (GPU %d)", real_device, webgpu_device_map[i].mvgal_gpu_id);
            webgpu_device_map[i].in_use = false;
            webgpu_device_map[i].real_device = NULL;
            webgpu_device_map[i].mvgal_gpu_id = -1;
            webgpu_device_count--;
            break;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
}

static int get_gpu_for_webgpu_device(WGPUType real_device) {
    if (!real_device) return get_next_gpu();
    
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_DEVICES; i++) {
        if (webgpu_device_map[i].in_use && webgpu_device_map[i].real_device == real_device) {
            int gpu_id = webgpu_device_map[i].mvgal_gpu_id;
            pthread_mutex_unlock(&wrapper_state.lock);
            return gpu_id;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return get_next_gpu(); // Fallback to round-robin
}

static int register_webgpu_queue(WGPUType queue, WGPUType device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_QUEUES; i++) {
        if (!webgpu_queue_map[i].in_use) {
            webgpu_queue_map[i].real_queue = queue;
            webgpu_queue_map[i].device = device;
            webgpu_queue_map[i].in_use = true;
            webgpu_queue_count++;
            pthread_mutex_unlock(&wrapper_state.lock);
            log_debug("Registered WebGPU queue %p -> device %p", queue, device);
            return i;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    log_warn("Queue map full, cannot register WebGPU queue %p", queue);
    return -1;
}

static void __attribute__((unused)) unregister_webgpu_queue(WGPUType queue) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_QUEUES; i++) {
        if (webgpu_queue_map[i].in_use && webgpu_queue_map[i].real_queue == queue) {
            log_debug("Unregistering WebGPU queue %p", queue);
            webgpu_queue_map[i].in_use = false;
            webgpu_queue_map[i].real_queue = NULL;
            webgpu_queue_map[i].device = NULL;
            webgpu_queue_count--;
            break;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
}

static WGPUType get_device_for_webgpu_queue(WGPUType queue) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_QUEUES; i++) {
        if (webgpu_queue_map[i].in_use && webgpu_queue_map[i].real_queue == queue) {
            WGPUType device = webgpu_queue_map[i].device;
            pthread_mutex_unlock(&wrapper_state.lock);
            return device;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return NULL;
}

static int register_webgpu_encoder(WGPUType encoder, WGPUType device) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_ENCODERS; i++) {
        if (!webgpu_encoder_map[i].in_use) {
            webgpu_encoder_map[i].real_encoder = encoder;
            webgpu_encoder_map[i].device = device;
            webgpu_encoder_map[i].in_use = true;
            webgpu_encoder_count++;
            pthread_mutex_unlock(&wrapper_state.lock);
            log_debug("Registered WebGPU encoder %p -> device %p", encoder, device);
            return i;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    log_warn("Encoder map full, cannot register WebGPU encoder %p", encoder);
    return -1;
}

static void __attribute__((unused)) unregister_webgpu_encoder(WGPUType encoder) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_ENCODERS; i++) {
        if (webgpu_encoder_map[i].in_use && webgpu_encoder_map[i].real_encoder == encoder) {
            log_debug("Unregistering WebGPU encoder %p", encoder);
            webgpu_encoder_map[i].in_use = false;
            webgpu_encoder_map[i].real_encoder = NULL;
            webgpu_encoder_map[i].device = NULL;
            webgpu_encoder_count--;
            break;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
}

static WGPUType get_device_for_webgpu_encoder(WGPUType encoder) {
    pthread_mutex_lock(&wrapper_state.lock);
    for (int i = 0; i < MAX_WEBGPU_ENCODERS; i++) {
        if (webgpu_encoder_map[i].in_use && webgpu_encoder_map[i].real_encoder == encoder) {
            WGPUType device = webgpu_encoder_map[i].device;
            pthread_mutex_unlock(&wrapper_state.lock);
            return device;
        }
    }
    pthread_mutex_unlock(&wrapper_state.lock);
    return NULL;
}

// Submit workload to MVGAL
static void submit_webgpu_workload(const char *step_name, mvgal_workload_type_t type, WGPUType device) {
    if (!wrapper_state.context) {
        return;
    }

    int gpu_id = get_gpu_for_webgpu_device(device);
    mvgal_workload_submit_info_t info = {
        .type = type,
        .priority = 50,
        .gpu_mask = (1U << gpu_id),
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
    const char *enabled_str = getenv("MVGAL_WEBGPU_ENABLED");
    if (enabled_str && atoi(enabled_str) == 0) {
        wrapper_state.enabled = false;
        log_info("WebGPU interception disabled via MVGAL_WEBGPU_ENABLED=0");
        return;
    }

    const char *debug_str = getenv("MVGAL_WEBGPU_DEBUG");
    if (debug_str && atoi(debug_str) == 1) {
        wrapper_state.debug = true;
    }

    const char *strategy = getenv("MVGAL_WEBGPU_STRATEGY");
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

    log_info("WebGPU wrapper initialized (strategy: %s, GPUs: %d)",
             wrapper_state.strategy, wrapper_state.gpu_count);
}

static void fini_wrapper(void) {
    if (wrapper_state.context != NULL) {
        mvgal_context_destroy(wrapper_state.context);
        wrapper_state.context = NULL;
        mvgal_shutdown();
        log_info("WebGPU wrapper shutdown");
    }
}

// Constructor/destructor
__attribute__((constructor)) static void webgpu_constructor(void) {
    init_wrapper();
}

__attribute__((destructor)) static void webgpu_destructor(void) {
    fini_wrapper();
}

/******************************************************************************
 * WebGPU Function Intercepts
 ******************************************************************************/

// wgpuCreateInstance
WGPUType wgpuCreateInstance(const void* desc) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCreateInstance) real_wgpuCreateInstance = get_real_function("wgpuCreateInstance");
        return real_wgpuCreateInstance ? real_wgpuCreateInstance(desc) : NULL;
    }

    log_debug("wgpuCreateInstance intercepted");

    submit_webgpu_workload("wgpuCreateInstance", MVGAL_WORKLOAD_WEBGPU_COMMAND, NULL);

    if (!real_wgpuCreateInstance) real_wgpuCreateInstance = get_real_function("wgpuCreateInstance");
    return real_wgpuCreateInstance ? real_wgpuCreateInstance(desc) : NULL;
}

// wgpuInstanceRequestAdapter
void wgpuInstanceRequestAdapter(WGPUType instance, const void* options, void* callback, void* userdata) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuInstanceRequestAdapter) real_wgpuInstanceRequestAdapter = get_real_function("wgpuInstanceRequestAdapter");
        if (real_wgpuInstanceRequestAdapter) real_wgpuInstanceRequestAdapter(instance, options, callback, userdata);
        return;
    }

    log_debug("wgpuInstanceRequestAdapter intercepted");

    submit_webgpu_workload("wgpuInstanceRequestAdapter", MVGAL_WORKLOAD_WEBGPU_COMMAND, NULL);

    if (!real_wgpuInstanceRequestAdapter) real_wgpuInstanceRequestAdapter = get_real_function("wgpuInstanceRequestAdapter");
    if (real_wgpuInstanceRequestAdapter) real_wgpuInstanceRequestAdapter(instance, options, callback, userdata);
}

// wgpuAdapterRequestDevice
void wgpuAdapterRequestDevice(WGPUType adapter, const void* descriptor, void* callback, void* userdata) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuAdapterRequestDevice) real_wgpuAdapterRequestDevice = get_real_function("wgpuAdapterRequestDevice");
        if (real_wgpuAdapterRequestDevice) real_wgpuAdapterRequestDevice(adapter, descriptor, callback, userdata);
        return;
    }

    log_debug("wgpuAdapterRequestDevice intercepted");

    submit_webgpu_workload("wgpuAdapterRequestDevice", MVGAL_WORKLOAD_WEBGPU_COMMAND, NULL);

    if (!real_wgpuAdapterRequestDevice) real_wgpuAdapterRequestDevice = get_real_function("wgpuAdapterRequestDevice");
    if (real_wgpuAdapterRequestDevice) real_wgpuAdapterRequestDevice(adapter, descriptor, callback, userdata);
}

// wgpuDeviceCreateQueue
WGPUType wgpuDeviceCreateQueue(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateQueue) real_wgpuDeviceCreateQueue = get_real_function("wgpuDeviceCreateQueue");
        return real_wgpuDeviceCreateQueue ? real_wgpuDeviceCreateQueue(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateQueue intercepted");

    submit_webgpu_workload("wgpuDeviceCreateQueue", MVGAL_WORKLOAD_WEBGPU_QUEUE, device);

    if (!real_wgpuDeviceCreateQueue) real_wgpuDeviceCreateQueue = get_real_function("wgpuDeviceCreateQueue");
    if (real_wgpuDeviceCreateQueue) {
        WGPUType queue = real_wgpuDeviceCreateQueue(device, descriptor);
        if (queue) {
            register_webgpu_queue(queue, device);
        }
        return queue;
    }
    return NULL;
}

// wgpuDeviceCreateBuffer
WGPUType wgpuDeviceCreateBuffer(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateBuffer) real_wgpuDeviceCreateBuffer = get_real_function("wgpuDeviceCreateBuffer");
        return real_wgpuDeviceCreateBuffer ? real_wgpuDeviceCreateBuffer(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateBuffer intercepted");

    submit_webgpu_workload("wgpuDeviceCreateBuffer", MVGAL_WORKLOAD_WEBGPU_BUFFER, device);

    if (!real_wgpuDeviceCreateBuffer) real_wgpuDeviceCreateBuffer = get_real_function("wgpuDeviceCreateBuffer");
    return real_wgpuDeviceCreateBuffer ? real_wgpuDeviceCreateBuffer(device, descriptor) : NULL;
}

// wgpuDeviceCreateTexture
WGPUType wgpuDeviceCreateTexture(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateTexture) real_wgpuDeviceCreateTexture = get_real_function("wgpuDeviceCreateTexture");
        return real_wgpuDeviceCreateTexture ? real_wgpuDeviceCreateTexture(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateTexture intercepted");

    submit_webgpu_workload("wgpuDeviceCreateTexture", MVGAL_WORKLOAD_WEBGPU_TEXTURE, device);

    if (!real_wgpuDeviceCreateTexture) real_wgpuDeviceCreateTexture = get_real_function("wgpuDeviceCreateTexture");
    return real_wgpuDeviceCreateTexture ? real_wgpuDeviceCreateTexture(device, descriptor) : NULL;
}

// wgpuDeviceCreateShaderModule
WGPUType wgpuDeviceCreateShaderModule(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateShaderModule) real_wgpuDeviceCreateShaderModule = get_real_function("wgpuDeviceCreateShaderModule");
        return real_wgpuDeviceCreateShaderModule ? real_wgpuDeviceCreateShaderModule(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateShaderModule intercepted");

    submit_webgpu_workload("wgpuDeviceCreateShaderModule", MVGAL_WORKLOAD_WEBGPU_SHADER, device);

    if (!real_wgpuDeviceCreateShaderModule) real_wgpuDeviceCreateShaderModule = get_real_function("wgpuDeviceCreateShaderModule");
    return real_wgpuDeviceCreateShaderModule ? real_wgpuDeviceCreateShaderModule(device, descriptor) : NULL;
}

// wgpuDeviceCreateBindGroupLayout
WGPUType wgpuDeviceCreateBindGroupLayout(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateBindGroupLayout) real_wgpuDeviceCreateBindGroupLayout = get_real_function("wgpuDeviceCreateBindGroupLayout");
        return real_wgpuDeviceCreateBindGroupLayout ? real_wgpuDeviceCreateBindGroupLayout(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateBindGroupLayout intercepted");

    submit_webgpu_workload("wgpuDeviceCreateBindGroupLayout", MVGAL_WORKLOAD_WEBGPU_BINDGROUP_LAYOUT, device);

    if (!real_wgpuDeviceCreateBindGroupLayout) real_wgpuDeviceCreateBindGroupLayout = get_real_function("wgpuDeviceCreateBindGroupLayout");
    return real_wgpuDeviceCreateBindGroupLayout ? real_wgpuDeviceCreateBindGroupLayout(device, descriptor) : NULL;
}

// wgpuDeviceCreatePipelineLayout
WGPUType wgpuDeviceCreatePipelineLayout(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreatePipelineLayout) real_wgpuDeviceCreatePipelineLayout = get_real_function("wgpuDeviceCreatePipelineLayout");
        return real_wgpuDeviceCreatePipelineLayout ? real_wgpuDeviceCreatePipelineLayout(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreatePipelineLayout intercepted");

    submit_webgpu_workload("wgpuDeviceCreatePipelineLayout", MVGAL_WORKLOAD_WEBGPU_PIPELINE_LAYOUT, device);

    if (!real_wgpuDeviceCreatePipelineLayout) real_wgpuDeviceCreatePipelineLayout = get_real_function("wgpuDeviceCreatePipelineLayout");
    return real_wgpuDeviceCreatePipelineLayout ? real_wgpuDeviceCreatePipelineLayout(device, descriptor) : NULL;
}

// wgpuDeviceCreateRenderPipeline
WGPUType wgpuDeviceCreateRenderPipeline(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateRenderPipeline) real_wgpuDeviceCreateRenderPipeline = get_real_function("wgpuDeviceCreateRenderPipeline");
        return real_wgpuDeviceCreateRenderPipeline ? real_wgpuDeviceCreateRenderPipeline(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateRenderPipeline intercepted");

    submit_webgpu_workload("wgpuDeviceCreateRenderPipeline", MVGAL_WORKLOAD_WEBGPU_RENDER, device);

    if (!real_wgpuDeviceCreateRenderPipeline) real_wgpuDeviceCreateRenderPipeline = get_real_function("wgpuDeviceCreateRenderPipeline");
    return real_wgpuDeviceCreateRenderPipeline ? real_wgpuDeviceCreateRenderPipeline(device, descriptor) : NULL;
}

// wgpuDeviceCreateComputePipeline
WGPUType wgpuDeviceCreateComputePipeline(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateComputePipeline) real_wgpuDeviceCreateComputePipeline = get_real_function("wgpuDeviceCreateComputePipeline");
        return real_wgpuDeviceCreateComputePipeline ? real_wgpuDeviceCreateComputePipeline(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateComputePipeline intercepted");

    submit_webgpu_workload("wgpuDeviceCreateComputePipeline", MVGAL_WORKLOAD_WEBGPU_COMPUTE, device);

    if (!real_wgpuDeviceCreateComputePipeline) real_wgpuDeviceCreateComputePipeline = get_real_function("wgpuDeviceCreateComputePipeline");
    return real_wgpuDeviceCreateComputePipeline ? real_wgpuDeviceCreateComputePipeline(device, descriptor) : NULL;
}

// wgpuDeviceCreateCommandEncoder
WGPUType wgpuDeviceCreateCommandEncoder(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateCommandEncoder) real_wgpuDeviceCreateCommandEncoder = get_real_function("wgpuDeviceCreateCommandEncoder");
        return real_wgpuDeviceCreateCommandEncoder ? real_wgpuDeviceCreateCommandEncoder(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateCommandEncoder intercepted");

    submit_webgpu_workload("wgpuDeviceCreateCommandEncoder", MVGAL_WORKLOAD_WEBGPU_COMMAND, device);

    if (!real_wgpuDeviceCreateCommandEncoder) real_wgpuDeviceCreateCommandEncoder = get_real_function("wgpuDeviceCreateCommandEncoder");
    if (real_wgpuDeviceCreateCommandEncoder) {
        WGPUType encoder = real_wgpuDeviceCreateCommandEncoder(device, descriptor);
        if (encoder) {
            register_webgpu_encoder(encoder, device);
        }
        return encoder;
    }
    return NULL;
}

// wgpuCommandEncoderBeginRenderPass
void wgpuCommandEncoderBeginRenderPass(WGPUType encoder, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderBeginRenderPass) real_wgpuCommandEncoderBeginRenderPass = get_real_function("wgpuCommandEncoderBeginRenderPass");
        if (real_wgpuCommandEncoderBeginRenderPass) real_wgpuCommandEncoderBeginRenderPass(encoder, descriptor);
        return;
    }

    log_debug("wgpuCommandEncoderBeginRenderPass intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderBeginRenderPass", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuCommandEncoderBeginRenderPass) real_wgpuCommandEncoderBeginRenderPass = get_real_function("wgpuCommandEncoderBeginRenderPass");
    if (real_wgpuCommandEncoderBeginRenderPass) real_wgpuCommandEncoderBeginRenderPass(encoder, descriptor);
}

// wgpuCommandEncoderBeginComputePass
void wgpuCommandEncoderBeginComputePass(WGPUType encoder, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderBeginComputePass) real_wgpuCommandEncoderBeginComputePass = get_real_function("wgpuCommandEncoderBeginComputePass");
        if (real_wgpuCommandEncoderBeginComputePass) real_wgpuCommandEncoderBeginComputePass(encoder, descriptor);
        return;
    }

    log_debug("wgpuCommandEncoderBeginComputePass intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderBeginComputePass", MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS, device);

    if (!real_wgpuCommandEncoderBeginComputePass) real_wgpuCommandEncoderBeginComputePass = get_real_function("wgpuCommandEncoderBeginComputePass");
    if (real_wgpuCommandEncoderBeginComputePass) real_wgpuCommandEncoderBeginComputePass(encoder, descriptor);
}

// wgpuRenderPassEncoderEnd
void wgpuRenderPassEncoderEnd(WGPUType encoder) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuRenderPassEncoderEnd) real_wgpuRenderPassEncoderEnd = get_real_function("wgpuRenderPassEncoderEnd");
        if (real_wgpuRenderPassEncoderEnd) real_wgpuRenderPassEncoderEnd(encoder);
        return;
    }

    log_debug("wgpuRenderPassEncoderEnd intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuRenderPassEncoderEnd", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuRenderPassEncoderEnd) real_wgpuRenderPassEncoderEnd = get_real_function("wgpuRenderPassEncoderEnd");
    if (real_wgpuRenderPassEncoderEnd) real_wgpuRenderPassEncoderEnd(encoder);
}

// wgpuComputePassEncoderEnd
void wgpuComputePassEncoderEnd(WGPUType encoder) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuComputePassEncoderEnd) real_wgpuComputePassEncoderEnd = get_real_function("wgpuComputePassEncoderEnd");
        if (real_wgpuComputePassEncoderEnd) real_wgpuComputePassEncoderEnd(encoder);
        return;
    }

    log_debug("wgpuComputePassEncoderEnd intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuComputePassEncoderEnd", MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS, device);

    if (!real_wgpuComputePassEncoderEnd) real_wgpuComputePassEncoderEnd = get_real_function("wgpuComputePassEncoderEnd");
    if (real_wgpuComputePassEncoderEnd) real_wgpuComputePassEncoderEnd(encoder);
}

// wgpuCommandEncoderFinish
WGPUType wgpuCommandEncoderFinish(WGPUType encoder, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderFinish) real_wgpuCommandEncoderFinish = get_real_function("wgpuCommandEncoderFinish");
        return real_wgpuCommandEncoderFinish ? real_wgpuCommandEncoderFinish(encoder, descriptor) : NULL;
    }

    log_debug("wgpuCommandEncoderFinish intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderFinish", MVGAL_WORKLOAD_WEBGPU_COMMAND, device);

    if (!real_wgpuCommandEncoderFinish) real_wgpuCommandEncoderFinish = get_real_function("wgpuCommandEncoderFinish");
    return real_wgpuCommandEncoderFinish ? real_wgpuCommandEncoderFinish(encoder, descriptor) : NULL;
}

// wgpuQueueSubmit
void wgpuQueueSubmit(WGPUType queue, unsigned int count, const void* commands, void* semaphore) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuQueueSubmit) real_wgpuQueueSubmit = get_real_function("wgpuQueueSubmit");
        if (real_wgpuQueueSubmit) real_wgpuQueueSubmit(queue, count, commands, semaphore);
        return;
    }

    log_debug("wgpuQueueSubmit intercepted (count: %u)", count);

    WGPUType device = get_device_for_webgpu_queue(queue);
    submit_webgpu_workload("wgpuQueueSubmit", MVGAL_WORKLOAD_WEBGPU_SUBMIT, device);

    if (!real_wgpuQueueSubmit) real_wgpuQueueSubmit = get_real_function("wgpuQueueSubmit");
    if (real_wgpuQueueSubmit) real_wgpuQueueSubmit(queue, count, commands, semaphore);
}

/******************************************************************************
 * Additional WebGPU Function Intercepts
 ******************************************************************************/

// wgpuInstanceEnumerateAdapters
void wgpuInstanceEnumerateAdapters(WGPUType instance, const void* options, void* callback) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuInstanceEnumerateAdapters) real_wgpuInstanceEnumerateAdapters = get_real_function("wgpuInstanceEnumerateAdapters");
        if (real_wgpuInstanceEnumerateAdapters) real_wgpuInstanceEnumerateAdapters(instance, options, callback);
        return;
    }

    log_debug("wgpuInstanceEnumerateAdapters intercepted");

    submit_webgpu_workload("wgpuInstanceEnumerateAdapters", MVGAL_WORKLOAD_WEBGPU_COMMAND, NULL);

    if (!real_wgpuInstanceEnumerateAdapters) real_wgpuInstanceEnumerateAdapters = get_real_function("wgpuInstanceEnumerateAdapters");
    if (real_wgpuInstanceEnumerateAdapters) real_wgpuInstanceEnumerateAdapters(instance, options, callback);
}

// wgpuAdapterGetLimits
void wgpuAdapterGetLimits(WGPUType adapter, void* limits) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuAdapterGetLimits) real_wgpuAdapterGetLimits = get_real_function("wgpuAdapterGetLimits");
        if (real_wgpuAdapterGetLimits) real_wgpuAdapterGetLimits(adapter, limits);
        return;
    }

    log_debug("wgpuAdapterGetLimits intercepted");

    submit_webgpu_workload("wgpuAdapterGetLimits", MVGAL_WORKLOAD_WEBGPU_COMMAND, NULL);

    if (!real_wgpuAdapterGetLimits) real_wgpuAdapterGetLimits = get_real_function("wgpuAdapterGetLimits");
    if (real_wgpuAdapterGetLimits) real_wgpuAdapterGetLimits(adapter, limits);
}

// wgpuAdapterGetProperties
void wgpuAdapterGetProperties(WGPUType adapter, void* properties) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuAdapterGetProperties) real_wgpuAdapterGetProperties = get_real_function("wgpuAdapterGetProperties");
        if (real_wgpuAdapterGetProperties) real_wgpuAdapterGetProperties(adapter, properties);
        return;
    }

    log_debug("wgpuAdapterGetProperties intercepted");

    submit_webgpu_workload("wgpuAdapterGetProperties", MVGAL_WORKLOAD_WEBGPU_COMMAND, NULL);

    if (!real_wgpuAdapterGetProperties) real_wgpuAdapterGetProperties = get_real_function("wgpuAdapterGetProperties");
    if (real_wgpuAdapterGetProperties) real_wgpuAdapterGetProperties(adapter, properties);
}

// wgpuDeviceCreateBindGroup
WGPUType wgpuDeviceCreateBindGroup(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateBindGroup) real_wgpuDeviceCreateBindGroup = get_real_function("wgpuDeviceCreateBindGroup");
        return real_wgpuDeviceCreateBindGroup ? real_wgpuDeviceCreateBindGroup(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateBindGroup intercepted");

    submit_webgpu_workload("wgpuDeviceCreateBindGroup", MVGAL_WORKLOAD_WEBGPU_BINDGROUP_LAYOUT, device);

    if (!real_wgpuDeviceCreateBindGroup) real_wgpuDeviceCreateBindGroup = get_real_function("wgpuDeviceCreateBindGroup");
    return real_wgpuDeviceCreateBindGroup ? real_wgpuDeviceCreateBindGroup(device, descriptor) : NULL;
}

// wgpuDeviceCreateSampler
WGPUType wgpuDeviceCreateSampler(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateSampler) real_wgpuDeviceCreateSampler = get_real_function("wgpuDeviceCreateSampler");
        return real_wgpuDeviceCreateSampler ? real_wgpuDeviceCreateSampler(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateSampler intercepted");

    submit_webgpu_workload("wgpuDeviceCreateSampler", MVGAL_WORKLOAD_WEBGPU_TEXTURE, device);

    if (!real_wgpuDeviceCreateSampler) real_wgpuDeviceCreateSampler = get_real_function("wgpuDeviceCreateSampler");
    return real_wgpuDeviceCreateSampler ? real_wgpuDeviceCreateSampler(device, descriptor) : NULL;
}

// wgpuDeviceCreateQuerySet
WGPUType wgpuDeviceCreateQuerySet(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateQuerySet) real_wgpuDeviceCreateQuerySet = get_real_function("wgpuDeviceCreateQuerySet");
        return real_wgpuDeviceCreateQuerySet ? real_wgpuDeviceCreateQuerySet(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateQuerySet intercepted");

    submit_webgpu_workload("wgpuDeviceCreateQuerySet", MVGAL_WORKLOAD_WEBGPU_COMMAND, device);

    if (!real_wgpuDeviceCreateQuerySet) real_wgpuDeviceCreateQuerySet = get_real_function("wgpuDeviceCreateQuerySet");
    return real_wgpuDeviceCreateQuerySet ? real_wgpuDeviceCreateQuerySet(device, descriptor) : NULL;
}

// wgpuDeviceDestroy
void wgpuDeviceDestroy(WGPUType device) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceDestroy) real_wgpuDeviceDestroy = get_real_function("wgpuDeviceDestroy");
        if (real_wgpuDeviceDestroy) real_wgpuDeviceDestroy(device);
        return;
    }

    log_debug("wgpuDeviceDestroy intercepted");

    submit_webgpu_workload("wgpuDeviceDestroy", MVGAL_WORKLOAD_WEBGPU_COMMAND, device);

    if (!real_wgpuDeviceDestroy) real_wgpuDeviceDestroy = get_real_function("wgpuDeviceDestroy");
    if (real_wgpuDeviceDestroy) real_wgpuDeviceDestroy(device);

    unregister_webgpu_device(device);
}

// wgpuDeviceGetLimits
void wgpuDeviceGetLimits(WGPUType device, void* limits) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceGetLimits) real_wgpuDeviceGetLimits = get_real_function("wgpuDeviceGetLimits");
        if (real_wgpuDeviceGetLimits) real_wgpuDeviceGetLimits(device, limits);
        return;
    }

    log_debug("wgpuDeviceGetLimits intercepted");

    submit_webgpu_workload("wgpuDeviceGetLimits", MVGAL_WORKLOAD_WEBGPU_COMMAND, device);

    if (!real_wgpuDeviceGetLimits) real_wgpuDeviceGetLimits = get_real_function("wgpuDeviceGetLimits");
    if (real_wgpuDeviceGetLimits) real_wgpuDeviceGetLimits(device, limits);
}

// wgpuDeviceHasFeature
bool wgpuDeviceHasFeature(WGPUType device, uint32_t feature) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceHasFeature) real_wgpuDeviceHasFeature = get_real_function("wgpuDeviceHasFeature");
        return real_wgpuDeviceHasFeature ? real_wgpuDeviceHasFeature(device, feature) : false;
    }

    log_debug("wgpuDeviceHasFeature intercepted");

    submit_webgpu_workload("wgpuDeviceHasFeature", MVGAL_WORKLOAD_WEBGPU_COMMAND, device);

    if (!real_wgpuDeviceHasFeature) real_wgpuDeviceHasFeature = get_real_function("wgpuDeviceHasFeature");
    return real_wgpuDeviceHasFeature ? real_wgpuDeviceHasFeature(device, feature) : false;
}

// wgpuQueueWriteBuffer
void wgpuQueueWriteBuffer(WGPUType queue, WGPUType buffer, uint64_t buffer_offset, const void* data, size_t size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuQueueWriteBuffer) real_wgpuQueueWriteBuffer = get_real_function("wgpuQueueWriteBuffer");
        if (real_wgpuQueueWriteBuffer) real_wgpuQueueWriteBuffer(queue, buffer, buffer_offset, data, size);
        return;
    }

    log_debug("wgpuQueueWriteBuffer intercepted (size: %zu)", size);

    WGPUType device = get_device_for_webgpu_queue(queue);
    submit_webgpu_workload("wgpuQueueWriteBuffer", MVGAL_WORKLOAD_WEBGPU_BUFFER, device);

    if (!real_wgpuQueueWriteBuffer) real_wgpuQueueWriteBuffer = get_real_function("wgpuQueueWriteBuffer");
    if (real_wgpuQueueWriteBuffer) real_wgpuQueueWriteBuffer(queue, buffer, buffer_offset, data, size);
}

// wgpuQueueWriteTexture
void wgpuQueueWriteTexture(WGPUType queue, const void* destination, const void* data, size_t data_size, const void* data_layout, const void* write_size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuQueueWriteTexture) real_wgpuQueueWriteTexture = get_real_function("wgpuQueueWriteTexture");
        if (real_wgpuQueueWriteTexture) real_wgpuQueueWriteTexture(queue, destination, data, data_size, data_layout, write_size);
        return;
    }

    log_debug("wgpuQueueWriteTexture intercepted (size: %zu)", data_size);

    WGPUType device = get_device_for_webgpu_queue(queue);
    submit_webgpu_workload("wgpuQueueWriteTexture", MVGAL_WORKLOAD_WEBGPU_TEXTURE, device);

    if (!real_wgpuQueueWriteTexture) real_wgpuQueueWriteTexture = get_real_function("wgpuQueueWriteTexture");
    if (real_wgpuQueueWriteTexture) real_wgpuQueueWriteTexture(queue, destination, data, data_size, data_layout, write_size);
}

// wgpuQueueCopyExternalImage
void wgpuQueueCopyExternalImage(WGPUType queue, const void* source, const void* source_origin, const void* destination, const void* copy_size, uint32_t copy_level) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuQueueCopyExternalImage) real_wgpuQueueCopyExternalImage = get_real_function("wgpuQueueCopyExternalImage");
        if (real_wgpuQueueCopyExternalImage) real_wgpuQueueCopyExternalImage(queue, source, source_origin, destination, copy_size, copy_level);
        return;
    }

    log_debug("wgpuQueueCopyExternalImage intercepted");

    WGPUType device = get_device_for_webgpu_queue(queue);
    submit_webgpu_workload("wgpuQueueCopyExternalImage", MVGAL_WORKLOAD_WEBGPU_TEXTURE, device);

    if (!real_wgpuQueueCopyExternalImage) real_wgpuQueueCopyExternalImage = get_real_function("wgpuQueueCopyExternalImage");
    if (real_wgpuQueueCopyExternalImage) real_wgpuQueueCopyExternalImage(queue, source, source_origin, destination, copy_size, copy_level);
}

// wgpuCommandEncoderBeginBlitPass
WGPUType wgpuCommandEncoderBeginBlitPass(WGPUType encoder, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderBeginBlitPass) real_wgpuCommandEncoderBeginBlitPass = get_real_function("wgpuCommandEncoderBeginBlitPass");
        return real_wgpuCommandEncoderBeginBlitPass ? real_wgpuCommandEncoderBeginBlitPass(encoder, descriptor) : NULL;
    }

    log_debug("wgpuCommandEncoderBeginBlitPass intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderBeginBlitPass", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuCommandEncoderBeginBlitPass) real_wgpuCommandEncoderBeginBlitPass = get_real_function("wgpuCommandEncoderBeginBlitPass");
    return real_wgpuCommandEncoderBeginBlitPass ? real_wgpuCommandEncoderBeginBlitPass(encoder, descriptor) : NULL;
}

// wgpuCommandEncoderCopyBufferToBuffer
void wgpuCommandEncoderCopyBufferToBuffer(WGPUType encoder, WGPUType source, uint64_t source_offset, WGPUType destination, uint64_t destination_offset, uint64_t size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderCopyBufferToBuffer) real_wgpuCommandEncoderCopyBufferToBuffer = get_real_function("wgpuCommandEncoderCopyBufferToBuffer");
        if (real_wgpuCommandEncoderCopyBufferToBuffer) real_wgpuCommandEncoderCopyBufferToBuffer(encoder, source, source_offset, destination, destination_offset, size);
        return;
    }

    log_debug("wgpuCommandEncoderCopyBufferToBuffer intercepted (size: %" PRIu64 ")", size);

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderCopyBufferToBuffer", MVGAL_WORKLOAD_TRANSFER, device);

    if (!real_wgpuCommandEncoderCopyBufferToBuffer) real_wgpuCommandEncoderCopyBufferToBuffer = get_real_function("wgpuCommandEncoderCopyBufferToBuffer");
    if (real_wgpuCommandEncoderCopyBufferToBuffer) real_wgpuCommandEncoderCopyBufferToBuffer(encoder, source, source_offset, destination, destination_offset, size);
}

// wgpuCommandEncoderCopyBufferToTexture
void wgpuCommandEncoderCopyBufferToTexture(WGPUType encoder, const void* source, const void* destination, const void* copy_size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderCopyBufferToTexture) real_wgpuCommandEncoderCopyBufferToTexture = get_real_function("wgpuCommandEncoderCopyBufferToTexture");
        if (real_wgpuCommandEncoderCopyBufferToTexture) real_wgpuCommandEncoderCopyBufferToTexture(encoder, source, destination, copy_size);
        return;
    }

    log_debug("wgpuCommandEncoderCopyBufferToTexture intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderCopyBufferToTexture", MVGAL_WORKLOAD_TRANSFER, device);

    if (!real_wgpuCommandEncoderCopyBufferToTexture) real_wgpuCommandEncoderCopyBufferToTexture = get_real_function("wgpuCommandEncoderCopyBufferToTexture");
    if (real_wgpuCommandEncoderCopyBufferToTexture) real_wgpuCommandEncoderCopyBufferToTexture(encoder, source, destination, copy_size);
}

// wgpuCommandEncoderCopyTextureToBuffer
void wgpuCommandEncoderCopyTextureToBuffer(WGPUType encoder, const void* source, const void* destination, const void* copy_size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderCopyTextureToBuffer) real_wgpuCommandEncoderCopyTextureToBuffer = get_real_function("wgpuCommandEncoderCopyTextureToBuffer");
        if (real_wgpuCommandEncoderCopyTextureToBuffer) real_wgpuCommandEncoderCopyTextureToBuffer(encoder, source, destination, copy_size);
        return;
    }

    log_debug("wgpuCommandEncoderCopyTextureToBuffer intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderCopyTextureToBuffer", MVGAL_WORKLOAD_TRANSFER, device);

    if (!real_wgpuCommandEncoderCopyTextureToBuffer) real_wgpuCommandEncoderCopyTextureToBuffer = get_real_function("wgpuCommandEncoderCopyTextureToBuffer");
    if (real_wgpuCommandEncoderCopyTextureToBuffer) real_wgpuCommandEncoderCopyTextureToBuffer(encoder, source, destination, copy_size);
}

// wgpuCommandEncoderCopyTextureToTexture
void wgpuCommandEncoderCopyTextureToTexture(WGPUType encoder, const void* source, const void* destination, const void* copy_size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderCopyTextureToTexture) real_wgpuCommandEncoderCopyTextureToTexture = get_real_function("wgpuCommandEncoderCopyTextureToTexture");
        if (real_wgpuCommandEncoderCopyTextureToTexture) real_wgpuCommandEncoderCopyTextureToTexture(encoder, source, destination, copy_size);
        return;
    }

    log_debug("wgpuCommandEncoderCopyTextureToTexture intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuCommandEncoderCopyTextureToTexture", MVGAL_WORKLOAD_TRANSFER, device);

    if (!real_wgpuCommandEncoderCopyTextureToTexture) real_wgpuCommandEncoderCopyTextureToTexture = get_real_function("wgpuCommandEncoderCopyTextureToTexture");
    if (real_wgpuCommandEncoderCopyTextureToTexture) real_wgpuCommandEncoderCopyTextureToTexture(encoder, source, destination, copy_size);
}

// wgpuRenderPassEncoderSetBindGroup
void wgpuRenderPassEncoderSetBindGroup(WGPUType encoder, uint32_t group_index, WGPUType group, uint32_t dynamic_offset_count, const uint32_t* dynamic_offsets) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuRenderPassEncoderSetBindGroup) real_wgpuRenderPassEncoderSetBindGroup = get_real_function("wgpuRenderPassEncoderSetBindGroup");
        if (real_wgpuRenderPassEncoderSetBindGroup) real_wgpuRenderPassEncoderSetBindGroup(encoder, group_index, group, dynamic_offset_count, dynamic_offsets);
        return;
    }

    log_debug("wgpuRenderPassEncoderSetBindGroup intercepted (group: %u)", group_index);

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuRenderPassEncoderSetBindGroup", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuRenderPassEncoderSetBindGroup) real_wgpuRenderPassEncoderSetBindGroup = get_real_function("wgpuRenderPassEncoderSetBindGroup");
    if (real_wgpuRenderPassEncoderSetBindGroup) real_wgpuRenderPassEncoderSetBindGroup(encoder, group_index, group, dynamic_offset_count, dynamic_offsets);
}

// wgpuRenderPassEncoderSetIndexBuffer
void wgpuRenderPassEncoderSetIndexBuffer(WGPUType encoder, WGPUType buffer, uint32_t format, uint64_t offset, uint64_t size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuRenderPassEncoderSetIndexBuffer) real_wgpuRenderPassEncoderSetIndexBuffer = get_real_function("wgpuRenderPassEncoderSetIndexBuffer");
        if (real_wgpuRenderPassEncoderSetIndexBuffer) real_wgpuRenderPassEncoderSetIndexBuffer(encoder, buffer, format, offset, size);
        return;
    }

    log_debug("wgpuRenderPassEncoderSetIndexBuffer intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuRenderPassEncoderSetIndexBuffer", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuRenderPassEncoderSetIndexBuffer) real_wgpuRenderPassEncoderSetIndexBuffer = get_real_function("wgpuRenderPassEncoderSetIndexBuffer");
    if (real_wgpuRenderPassEncoderSetIndexBuffer) real_wgpuRenderPassEncoderSetIndexBuffer(encoder, buffer, format, offset, size);
}

// wgpuRenderPassEncoderSetPipeline
void wgpuRenderPassEncoderSetPipeline(WGPUType encoder, WGPUType pipeline) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuRenderPassEncoderSetPipeline) real_wgpuRenderPassEncoderSetPipeline = get_real_function("wgpuRenderPassEncoderSetPipeline");
        if (real_wgpuRenderPassEncoderSetPipeline) real_wgpuRenderPassEncoderSetPipeline(encoder, pipeline);
        return;
    }

    log_debug("wgpuRenderPassEncoderSetPipeline intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuRenderPassEncoderSetPipeline", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuRenderPassEncoderSetPipeline) real_wgpuRenderPassEncoderSetPipeline = get_real_function("wgpuRenderPassEncoderSetPipeline");
    if (real_wgpuRenderPassEncoderSetPipeline) real_wgpuRenderPassEncoderSetPipeline(encoder, pipeline);
}

// wgpuRenderPassEncoderSetVertexBuffer
void wgpuRenderPassEncoderSetVertexBuffer(WGPUType encoder, uint32_t slot, WGPUType buffer, uint64_t offset, uint64_t size) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuRenderPassEncoderSetVertexBuffer) real_wgpuRenderPassEncoderSetVertexBuffer = get_real_function("wgpuRenderPassEncoderSetVertexBuffer");
        if (real_wgpuRenderPassEncoderSetVertexBuffer) real_wgpuRenderPassEncoderSetVertexBuffer(encoder, slot, buffer, offset, size);
        return;
    }

    log_debug("wgpuRenderPassEncoderSetVertexBuffer intercepted (slot: %u)", slot);

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuRenderPassEncoderSetVertexBuffer", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuRenderPassEncoderSetVertexBuffer) real_wgpuRenderPassEncoderSetVertexBuffer = get_real_function("wgpuRenderPassEncoderSetVertexBuffer");
    if (real_wgpuRenderPassEncoderSetVertexBuffer) real_wgpuRenderPassEncoderSetVertexBuffer(encoder, slot, buffer, offset, size);
}

// wgpuRenderPassEncoderDraw
void wgpuRenderPassEncoderDraw(WGPUType encoder, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuRenderPassEncoderDraw) real_wgpuRenderPassEncoderDraw = get_real_function("wgpuRenderPassEncoderDraw");
        if (real_wgpuRenderPassEncoderDraw) real_wgpuRenderPassEncoderDraw(encoder, vertex_count, instance_count, first_vertex, first_instance);
        return;
    }

    log_debug("wgpuRenderPassEncoderDraw intercepted (vertices: %u, instances: %u)", vertex_count, instance_count);

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuRenderPassEncoderDraw", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuRenderPassEncoderDraw) real_wgpuRenderPassEncoderDraw = get_real_function("wgpuRenderPassEncoderDraw");
    if (real_wgpuRenderPassEncoderDraw) real_wgpuRenderPassEncoderDraw(encoder, vertex_count, instance_count, first_vertex, first_instance);
}

// wgpuRenderPassEncoderDrawIndexed
void wgpuRenderPassEncoderDrawIndexed(WGPUType encoder, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t base_vertex, uint32_t first_instance) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuRenderPassEncoderDrawIndexed) real_wgpuRenderPassEncoderDrawIndexed = get_real_function("wgpuRenderPassEncoderDrawIndexed");
        if (real_wgpuRenderPassEncoderDrawIndexed) real_wgpuRenderPassEncoderDrawIndexed(encoder, index_count, instance_count, first_index, base_vertex, first_instance);
        return;
    }

    log_debug("wgpuRenderPassEncoderDrawIndexed intercepted (indices: %u, instances: %u)", index_count, instance_count);

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuRenderPassEncoderDrawIndexed", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS, device);

    if (!real_wgpuRenderPassEncoderDrawIndexed) real_wgpuRenderPassEncoderDrawIndexed = get_real_function("wgpuRenderPassEncoderDrawIndexed");
    if (real_wgpuRenderPassEncoderDrawIndexed) real_wgpuRenderPassEncoderDrawIndexed(encoder, index_count, instance_count, first_index, base_vertex, first_instance);
}

// wgpuComputePassEncoderSetPipeline
void wgpuComputePassEncoderSetPipeline(WGPUType encoder, WGPUType pipeline) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuComputePassEncoderSetPipeline) real_wgpuComputePassEncoderSetPipeline = get_real_function("wgpuComputePassEncoderSetPipeline");
        if (real_wgpuComputePassEncoderSetPipeline) real_wgpuComputePassEncoderSetPipeline(encoder, pipeline);
        return;
    }

    log_debug("wgpuComputePassEncoderSetPipeline intercepted");

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuComputePassEncoderSetPipeline", MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS, device);

    if (!real_wgpuComputePassEncoderSetPipeline) real_wgpuComputePassEncoderSetPipeline = get_real_function("wgpuComputePassEncoderSetPipeline");
    if (real_wgpuComputePassEncoderSetPipeline) real_wgpuComputePassEncoderSetPipeline(encoder, pipeline);
}

// wgpuComputePassEncoderSetBindGroup
void wgpuComputePassEncoderSetBindGroup(WGPUType encoder, uint32_t group_index, WGPUType group, uint32_t dynamic_offset_count, const uint32_t* dynamic_offsets) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuComputePassEncoderSetBindGroup) real_wgpuComputePassEncoderSetBindGroup = get_real_function("wgpuComputePassEncoderSetBindGroup");
        if (real_wgpuComputePassEncoderSetBindGroup) real_wgpuComputePassEncoderSetBindGroup(encoder, group_index, group, dynamic_offset_count, dynamic_offsets);
        return;
    }

    log_debug("wgpuComputePassEncoderSetBindGroup intercepted (group: %u)", group_index);

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuComputePassEncoderSetBindGroup", MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS, device);

    if (!real_wgpuComputePassEncoderSetBindGroup) real_wgpuComputePassEncoderSetBindGroup = get_real_function("wgpuComputePassEncoderSetBindGroup");
    if (real_wgpuComputePassEncoderSetBindGroup) real_wgpuComputePassEncoderSetBindGroup(encoder, group_index, group, dynamic_offset_count, dynamic_offsets);
}

// wgpuComputePassEncoderDispatchWorkgroups
void wgpuComputePassEncoderDispatchWorkgroups(WGPUType encoder, uint32_t workgroup_count_x, uint32_t workgroup_count_y, uint32_t workgroup_count_z) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuComputePassEncoderDispatchWorkgroups) real_wgpuComputePassEncoderDispatchWorkgroups = get_real_function("wgpuComputePassEncoderDispatchWorkgroups");
        if (real_wgpuComputePassEncoderDispatchWorkgroups) real_wgpuComputePassEncoderDispatchWorkgroups(encoder, workgroup_count_x, workgroup_count_y, workgroup_count_z);
        return;
    }

    log_debug("wgpuComputePassEncoderDispatchWorkgroups intercepted (x: %u, y: %u, z: %u)", workgroup_count_x, workgroup_count_y, workgroup_count_z);

    WGPUType device = get_device_for_webgpu_encoder(encoder);
    submit_webgpu_workload("wgpuComputePassEncoderDispatchWorkgroups", MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS, device);

    if (!real_wgpuComputePassEncoderDispatchWorkgroups) real_wgpuComputePassEncoderDispatchWorkgroups = get_real_function("wgpuComputePassEncoderDispatchWorkgroups");
    if (real_wgpuComputePassEncoderDispatchWorkgroups) real_wgpuComputePassEncoderDispatchWorkgroups(encoder, workgroup_count_x, workgroup_count_y, workgroup_count_z);
}
