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

// Submit workload to MVGAL
static void submit_webgpu_workload(const char *step_name, mvgal_workload_type_t type) {
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

    submit_webgpu_workload("wgpuCreateInstance", MVGAL_WORKLOAD_WEBGPU_COMMAND);

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

    submit_webgpu_workload("wgpuInstanceRequestAdapter", MVGAL_WORKLOAD_WEBGPU_COMMAND);

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

    submit_webgpu_workload("wgpuAdapterRequestDevice", MVGAL_WORKLOAD_WEBGPU_COMMAND);

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

    submit_webgpu_workload("wgpuDeviceCreateQueue", MVGAL_WORKLOAD_WEBGPU_QUEUE);

    if (!real_wgpuDeviceCreateQueue) real_wgpuDeviceCreateQueue = get_real_function("wgpuDeviceCreateQueue");
    return real_wgpuDeviceCreateQueue ? real_wgpuDeviceCreateQueue(device, descriptor) : NULL;
}

// wgpuDeviceCreateBuffer
WGPUType wgpuDeviceCreateBuffer(WGPUType device, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuDeviceCreateBuffer) real_wgpuDeviceCreateBuffer = get_real_function("wgpuDeviceCreateBuffer");
        return real_wgpuDeviceCreateBuffer ? real_wgpuDeviceCreateBuffer(device, descriptor) : NULL;
    }

    log_debug("wgpuDeviceCreateBuffer intercepted");

    submit_webgpu_workload("wgpuDeviceCreateBuffer", MVGAL_WORKLOAD_WEBGPU_BUFFER);

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

    submit_webgpu_workload("wgpuDeviceCreateTexture", MVGAL_WORKLOAD_WEBGPU_TEXTURE);

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

    submit_webgpu_workload("wgpuDeviceCreateShaderModule", MVGAL_WORKLOAD_WEBGPU_SHADER);

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

    submit_webgpu_workload("wgpuDeviceCreateBindGroupLayout", MVGAL_WORKLOAD_WEBGPU_BINDGROUP_LAYOUT);

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

    submit_webgpu_workload("wgpuDeviceCreatePipelineLayout", MVGAL_WORKLOAD_WEBGPU_PIPELINE_LAYOUT);

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

    submit_webgpu_workload("wgpuDeviceCreateRenderPipeline", MVGAL_WORKLOAD_WEBGPU_RENDER);

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

    submit_webgpu_workload("wgpuDeviceCreateComputePipeline", MVGAL_WORKLOAD_WEBGPU_COMPUTE);

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

    submit_webgpu_workload("wgpuDeviceCreateCommandEncoder", MVGAL_WORKLOAD_WEBGPU_COMMAND);

    if (!real_wgpuDeviceCreateCommandEncoder) real_wgpuDeviceCreateCommandEncoder = get_real_function("wgpuDeviceCreateCommandEncoder");
    return real_wgpuDeviceCreateCommandEncoder ? real_wgpuDeviceCreateCommandEncoder(device, descriptor) : NULL;
}

// wgpuCommandEncoderBeginRenderPass
void wgpuCommandEncoderBeginRenderPass(WGPUType encoder, const void* descriptor) {
    if (!wrapper_state.enabled) {
        if (!real_wgpuCommandEncoderBeginRenderPass) real_wgpuCommandEncoderBeginRenderPass = get_real_function("wgpuCommandEncoderBeginRenderPass");
        if (real_wgpuCommandEncoderBeginRenderPass) real_wgpuCommandEncoderBeginRenderPass(encoder, descriptor);
        return;
    }

    log_debug("wgpuCommandEncoderBeginRenderPass intercepted");

    submit_webgpu_workload("wgpuCommandEncoderBeginRenderPass", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS);

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

    submit_webgpu_workload("wgpuCommandEncoderBeginComputePass", MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS);

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

    submit_webgpu_workload("wgpuRenderPassEncoderEnd", MVGAL_WORKLOAD_WEBGPU_RENDER_PASS);

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

    submit_webgpu_workload("wgpuComputePassEncoderEnd", MVGAL_WORKLOAD_WEBGPU_COMPUTE_PASS);

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

    submit_webgpu_workload("wgpuCommandEncoderFinish", MVGAL_WORKLOAD_WEBGPU_COMMAND);

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

    submit_webgpu_workload("wgpuQueueSubmit", MVGAL_WORKLOAD_WEBGPU_SUBMIT);

    if (!real_wgpuQueueSubmit) real_wgpuQueueSubmit = get_real_function("wgpuQueueSubmit");
    if (real_wgpuQueueSubmit) real_wgpuQueueSubmit(queue, count, commands, semaphore);
}
