#include <stddef.h>
#include <dlfcn.h>

typedef void* WGPUType;

// Simple passthrough functions
static void* get_real(const char* name) { return dlsym(RTLD_NEXT, name); }

WGPUType wgpuCreateInstance(const void* desc) {
    static WGPUType (*real_func)(const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuCreateInstance");
    return real_func ? real_func(desc) : NULL;
}

void wgpuInstanceRequestAdapter(WGPUType instance, const void* options, void* callback, void* userdata) {
    static void (*real_func)(WGPUType, const void*, void*, void*) = NULL;
    if (!real_func) real_func = get_real("wgpuInstanceRequestAdapter");
    if (real_func) real_func(instance, options, callback, userdata);
}

void wgpuAdapterRequestDevice(WGPUType adapter, const void* descriptor, void* callback, void* userdata) {
    static void (*real_func)(WGPUType, const void*, void*, void*) = NULL;
    if (!real_func) real_func = get_real("wgpuAdapterRequestDevice");
    if (real_func) real_func(adapter, descriptor, callback, userdata);
}

WGPUType wgpuDeviceCreateQueue(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateQueue");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreateBuffer(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateBuffer");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreateTexture(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateTexture");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreateShaderModule(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateShaderModule");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreateBindGroupLayout(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateBindGroupLayout");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreatePipelineLayout(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreatePipelineLayout");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreateRenderPipeline(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateRenderPipeline");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreateComputePipeline(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateComputePipeline");
    return real_func ? real_func(device, descriptor) : NULL;
}

WGPUType wgpuDeviceCreateCommandEncoder(WGPUType device, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuDeviceCreateCommandEncoder");
    return real_func ? real_func(device, descriptor) : NULL;
}

void wgpuCommandEncoderBeginRenderPass(WGPUType encoder, const void* descriptor) {
    static void (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuCommandEncoderBeginRenderPass");
    if (real_func) real_func(encoder, descriptor);
}

void wgpuCommandEncoderBeginComputePass(WGPUType encoder, const void* descriptor) {
    static void (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuCommandEncoderBeginComputePass");
    if (real_func) real_func(encoder, descriptor);
}

void wgpuRenderPassEncoderEnd(WGPUType encoder) {
    static void (*real_func)(WGPUType) = NULL;
    if (!real_func) real_func = get_real("wgpuRenderPassEncoderEnd");
    if (real_func) real_func(encoder);
}

void wgpuComputePassEncoderEnd(WGPUType encoder) {
    static void (*real_func)(WGPUType) = NULL;
    if (!real_func) real_func = get_real("wgpuComputePassEncoderEnd");
    if (real_func) real_func(encoder);
}

WGPUType wgpuCommandEncoderFinish(WGPUType encoder, const void* descriptor) {
    static WGPUType (*real_func)(WGPUType, const void*) = NULL;
    if (!real_func) real_func = get_real("wgpuCommandEncoderFinish");
    return real_func ? real_func(encoder, descriptor) : NULL;
}

void wgpuQueueSubmit(WGPUType queue, unsigned int count, const void* commands, void* semaphore) {
    static void (*real_func)(WGPUType, unsigned int, const void*, void*) = NULL;
    if (!real_func) real_func = get_real("wgpuQueueSubmit");
    if (real_func) real_func(queue, count, commands, semaphore);
}
