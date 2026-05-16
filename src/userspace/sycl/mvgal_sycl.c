/**
 * @file mvgal_sycl.c
 * @brief MVGAL SYCL Backend Implementation
 *
 * Provides a SYCL 2020 runtime backend that maps SYCL concepts
 * (queues, buffers, kernels, ND-ranges) onto MVGAL's GPU
 * aggregation layer.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include "mvgal/mvgal_sycl.h"
#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_memory.h"
#include "mvgal/mvgal_execution.h"
#include "mvgal/mvgal_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#define MVGAL_ERROR_INVALID_ARG MVGAL_ERROR_INVALID_ARGUMENT

#define MVGAL_LOG_ERROR(...) mvgal_log_error(__VA_ARGS__)
#define MVGAL_LOG_INFO(...) mvgal_log_info(__VA_ARGS__)
#define MVGAL_LOG_DEBUG(...) mvgal_log_debug(__VA_ARGS__)
/* ============================================================================
 * Internal State
 * ============================================================================ */

#define MVGAL_SYCL_MAX_KERNEL_ARGS 32

/**
 * @brief Internal buffer object
 */
struct mvgal_sycl_buffer {
    uint32_t     device_index;
    size_t       size;
    mvgal_sycl_mem_flags_t flags;
    mvgal_buffer_t mvgal_buf;
    void        *host_ptr;
    bool         is_mapped;
    uint32_t     ref_count;
    pthread_mutex_t lock;
};

/**
 * @brief Internal queue object
 */
struct mvgal_sycl_queue {
    uint32_t     device_index;
    mvgal_sycl_queue_props_t properties;
    bool         in_use;
    pthread_mutex_t lock;
    uint64_t     submit_count;
};

/**
 * @brief Internal kernel object
 */
struct mvgal_sycl_kernel {
    char         name[MVGAL_SYCL_MAX_KERNEL_NAME];
    uint32_t     device_index;
    uint32_t     num_args;
    mvgal_sycl_kernel_arg_t args[MVGAL_SYCL_MAX_KERNEL_ARGS];
    void        *binary_data;
    size_t       binary_size;
    bool         is_compiled;
};

/**
 * @brief Internal program object
 */
struct mvgal_sycl_program {
    uint32_t     device_index;
    void        *spirv_data;
    size_t       spirv_size;
    char        *source;
    size_t       source_len;
    char        *compile_options;
    bool         is_spirv;
    bool         is_compiled;
};

/* ---------------------------------------------------------------------------
 * Device cache
 * ------------------------------------------------------------------------- */

static mvgal_sycl_device_info_t g_device_cache[MVGAL_SYCL_MAX_DEVICES];
static uint32_t g_num_cached_devices = 0;
static bool g_cache_initialized = false;
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static mvgal_error_t mvgal_sycl_fill_device_info(uint32_t gpu_index, mvgal_sycl_device_info_t *info) {
    mvgal_gpu_descriptor_t gpu;
    mvgal_error_t err = mvgal_gpu_get_descriptor(gpu_index, &gpu);
    if (err != MVGAL_SUCCESS) return err;

    memset(info, 0, sizeof(*info));
    info->device_index = gpu_index;
    info->device_type = (gpu.type == MVGAL_GPU_TYPE_DISCRETE) ? MVGAL_SYCL_DEVICE_TYPE_GPU : MVGAL_SYCL_DEVICE_TYPE_AUTOMATIC;
    info->vendor = gpu.vendor;
    strncpy(info->name, gpu.name, sizeof(info->name) - 1);
    strncpy(info->vendor_name, gpu.driver_name, sizeof(info->vendor_name) - 1);
    strncpy(info->driver_version, gpu.driver_version, sizeof(info->driver_version) - 1);
    
    info->global_mem_size = gpu.vram_total;
    info->max_mem_alloc_size = gpu.vram_total / 2;
    info->clock_frequency_mhz = (uint32_t)gpu.memory_bandwidth_gbps; /* Placeholder */
    
    return MVGAL_SUCCESS;
}

static void refresh_device_cache(void) {
    pthread_mutex_lock(&g_cache_lock);

    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count < 0) gpu_count = 0;

    g_num_cached_devices = 0;
    for (int32_t i = 0; i < gpu_count && i < MVGAL_SYCL_MAX_DEVICES; i++) {
        if (mvgal_sycl_fill_device_info((uint32_t)i, &g_device_cache[g_num_cached_devices]) == MVGAL_SUCCESS) {
            g_num_cached_devices++;
        }
    }

    g_cache_initialized = true;
    pthread_mutex_unlock(&g_cache_lock);
}

/* ============================================================================
 * Device Discovery
 * ============================================================================ */

mvgal_error_t mvgal_sycl_get_devices(
    mvgal_sycl_device_type_t dev_type,
    mvgal_sycl_device_info_t *devices,
    uint32_t *num_devices)
{
    if (!num_devices) return MVGAL_ERROR_INVALID_ARG;

    refresh_device_cache();

    pthread_mutex_lock(&g_cache_lock);
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_num_cached_devices; i++) {
        if (dev_type == MVGAL_SYCL_DEVICE_TYPE_AUTOMATIC || 
            g_device_cache[i].device_type == dev_type) {
            if (devices && count < *num_devices) {
                memcpy(&devices[count], &g_device_cache[i], sizeof(mvgal_sycl_device_info_t));
            }
            count++;
        }
    }

    *num_devices = count;
    pthread_mutex_unlock(&g_cache_lock);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_get_device_info(
    uint32_t device_index,
    mvgal_sycl_device_info_t *info)
{
    if (!info) return MVGAL_ERROR_INVALID_ARG;
    return mvgal_sycl_fill_device_info(device_index, info);
}

mvgal_error_t mvgal_sycl_get_platforms(
    mvgal_sycl_platform_info_t *platforms,
    uint32_t *num_platforms)
{
    if (!num_platforms || !platforms) return MVGAL_ERROR_INVALID_ARG;
    if (*num_platforms == 0) return MVGAL_ERROR_INVALID_ARG;

    refresh_device_cache();

    /* MVGAL exposes a single SYCL platform */
    mvgal_sycl_platform_info_t *plat = &platforms[0];
    memset(plat, 0, sizeof(mvgal_sycl_platform_info_t));
    plat->platform_index = 0;
    snprintf(plat->name, sizeof(plat->name), "MVGAL SYCL Platform");
    snprintf(plat->vendor, sizeof(plat->vendor), "MVGAL Project");
    snprintf(plat->version, sizeof(plat->version), "2020");
    snprintf(plat->profile, sizeof(plat->profile), "FULL_PROFILE");

    pthread_mutex_lock(&g_cache_lock);
    plat->num_devices = g_num_cached_devices;
    for (uint32_t i = 0; i < g_num_cached_devices && i < MVGAL_SYCL_MAX_DEVICES; i++) {
        plat->device_indices[i] = i;
    }
    pthread_mutex_unlock(&g_cache_lock);

    *num_platforms = 1;
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Queue Management
 * ============================================================================ */

mvgal_error_t mvgal_sycl_queue_create(
    uint32_t device_index,
    mvgal_sycl_queue_props_t properties,
    mvgal_sycl_queue_t *queue)
{
    if (!queue) return MVGAL_ERROR_INVALID_ARG;

    mvgal_sycl_queue_t q = calloc(1, sizeof(struct mvgal_sycl_queue));
    if (!q) return MVGAL_ERROR_OUT_OF_MEMORY;

    q->device_index = device_index;
    q->properties = properties;
    q->in_use = false;
    q->submit_count = 0;
    pthread_mutex_init(&q->lock, NULL);

    *queue = q;
    mvgal_log_debug("sycl: queue created for device %u", device_index);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_queue_destroy(mvgal_sycl_queue_t queue) {
    if (!queue) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_destroy(&queue->lock);
    free(queue);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_queue_submit(mvgal_sycl_queue_t queue) {
    if (!queue) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&queue->lock);
    queue->in_use = true;
    queue->submit_count++;
    pthread_mutex_unlock(&queue->lock);

    mvgal_log_debug("sycl: submit to queue (device %u, count %lu)",
              queue->device_index, (unsigned long)queue->submit_count);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_queue_wait(mvgal_sycl_queue_t queue) {
    if (!queue) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&queue->lock);
    queue->in_use = false;
    pthread_mutex_unlock(&queue->lock);

    return MVGAL_SUCCESS;
}

bool mvgal_sycl_queue_is_running(mvgal_sycl_queue_t queue) {
    if (!queue) return false;
    pthread_mutex_lock(&queue->lock);
    bool running = queue->in_use;
    pthread_mutex_unlock(&queue->lock);
    return running;
}

/* ============================================================================
 * Memory Management
 * ============================================================================ */

mvgal_error_t mvgal_sycl_mem_alloc(
    uint32_t device_index,
    size_t size,
    mvgal_sycl_mem_flags_t flags,
    mvgal_sycl_buffer_t *buffer)
{
    if (!buffer || size == 0) return MVGAL_ERROR_INVALID_ARG;

    mvgal_sycl_buffer_t buf = calloc(1, sizeof(struct mvgal_sycl_buffer));
    if (!buf) return MVGAL_ERROR_OUT_OF_MEMORY;

    buf->device_index = device_index;
    buf->size = size;
    buf->flags = flags;
    buf->is_mapped = false;
    buf->ref_count = 1;
    pthread_mutex_init(&buf->lock, NULL);

    /* Translate SYCL flags to MVGAL allocation flags */
    mvgal_memory_alloc_info_t alloc_info;
    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.size = size;
    alloc_info.flags = MVGAL_MEMORY_FLAG_GPU_VALID;

    if (flags & MVGAL_SYCL_MEM_FLAG_HOST_VISIBLE)
        alloc_info.flags |= MVGAL_MEMORY_FLAG_HOST_VALID;
    if (flags & MVGAL_SYCL_MEM_FLAG_HOST_COHERENT)
        alloc_info.flags |= MVGAL_MEMORY_FLAG_CPU_UNCACHED;
    if (flags & MVGAL_SYCL_MEM_FLAG_DEVICE_READ_WRITE)
        alloc_info.flags |= MVGAL_MEMORY_FLAG_SHARED;

    mvgal_error_t err = mvgal_memory_allocate(NULL, &alloc_info, &buf->mvgal_buf);
    if (err != MVGAL_SUCCESS) {
        mvgal_log_error("sycl: failed to allocate MVGAL buffer (%u bytes)", (unsigned)size);
        pthread_mutex_destroy(&buf->lock);
        free(buf);
        return err;
    }
    *buffer = buf;

    mvgal_log_debug("sycl: allocated %zu bytes on device %u", size, device_index);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_mem_free(mvgal_sycl_buffer_t buffer) {
    if (!buffer) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&buffer->lock);
    if (--buffer->ref_count > 0) {
        pthread_mutex_unlock(&buffer->lock);
        return MVGAL_SUCCESS;
    }

    if (buffer->is_mapped && buffer->host_ptr) {
        if (mvgal_memory_is_mapped(buffer->mvgal_buf)) {
            mvgal_memory_unmap(buffer->mvgal_buf);
        } else {
            free(buffer->host_ptr);
        }
        buffer->host_ptr = NULL;
        buffer->is_mapped = false;
    }
    pthread_mutex_unlock(&buffer->lock);

    if (buffer->mvgal_buf) {
        mvgal_memory_free(buffer->mvgal_buf);
    }
    pthread_mutex_destroy(&buffer->lock);
    free(buffer);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_mem_write(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer,
    size_t offset,
    size_t size,
    const void *data)
{
    if (!buffer || !data) return MVGAL_ERROR_INVALID_ARG;

    mvgal_error_t err = mvgal_memory_write(buffer->mvgal_buf, offset, size, data);
    if (err != MVGAL_SUCCESS) {
        mvgal_log_error("sycl: failed to write to buffer");
        return err;
    }

    if (queue) {
        pthread_mutex_lock(&queue->lock);
        queue->submit_count++;
        pthread_mutex_unlock(&queue->lock);
    }

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_mem_read(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer,
    size_t offset,
    size_t size,
    void *data)
{
    if (!buffer || !data) return MVGAL_ERROR_INVALID_ARG;

    mvgal_error_t err = mvgal_memory_read(buffer->mvgal_buf, offset, size, data);
    if (err != MVGAL_SUCCESS) {
        mvgal_log_error("sycl: failed to read from buffer");
        return err;
    }

    if (queue) {
        pthread_mutex_lock(&queue->lock);
        queue->submit_count++;
        pthread_mutex_unlock(&queue->lock);
    }

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_mem_copy(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t src,
    mvgal_sycl_buffer_t dst,
    size_t size)
{
    if (!src || !dst) return MVGAL_ERROR_INVALID_ARG;

    mvgal_memory_copy_region_t region = {
        .src_buffer = src->mvgal_buf,
        .src_offset = 0,
        .dst_buffer = dst->mvgal_buf,
        .dst_offset = 0,
        .size = size
    };

    mvgal_error_t err = mvgal_memory_copy(NULL, &region, 1, NULL);
    if (err != MVGAL_SUCCESS) {
        mvgal_log_error("sycl: failed to copy buffer");
        return err;
    }

    if (queue) {
        pthread_mutex_lock(&queue->lock);
        queue->submit_count++;
        pthread_mutex_unlock(&queue->lock);
    }

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_mem_map(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer,
    mvgal_sycl_mem_flags_t flags,
    void **mapped_ptr)
{
    (void)queue;
    (void)flags;
    if (!buffer || !mapped_ptr) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&buffer->lock);
    if (buffer->is_mapped) {
        *mapped_ptr = buffer->host_ptr;
        pthread_mutex_unlock(&buffer->lock);
        return MVGAL_SUCCESS;
    }

    /* We use the internal memory map if the backend supports it, otherwise fallback */
    void *ptr = NULL;
    mvgal_error_t err = mvgal_memory_map(buffer->mvgal_buf, 0, buffer->size, &ptr);
    if (err == MVGAL_SUCCESS) {
        buffer->host_ptr = ptr;
        buffer->is_mapped = true;
        *mapped_ptr = ptr;
    } else {
        /* Fallback: allocate host memory and read from buffer */
        buffer->host_ptr = malloc(buffer->size);
        if (!buffer->host_ptr) {
            pthread_mutex_unlock(&buffer->lock);
            return MVGAL_ERROR_OUT_OF_MEMORY;
        }
        mvgal_memory_read(buffer->mvgal_buf, 0, buffer->size, buffer->host_ptr);
        buffer->is_mapped = true;
        *mapped_ptr = buffer->host_ptr;
        err = MVGAL_SUCCESS;
    }

    pthread_mutex_unlock(&buffer->lock);
    return err;
}

mvgal_error_t mvgal_sycl_mem_unmap(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer)
{
    (void)queue;
    if (!buffer) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&buffer->lock);
    if (!buffer->is_mapped || !buffer->host_ptr) {
        pthread_mutex_unlock(&buffer->lock);
        return MVGAL_SUCCESS;
    }

    /* Check if it was a real map or a fallback */
    if (mvgal_memory_is_mapped(buffer->mvgal_buf)) {
        mvgal_memory_unmap(buffer->mvgal_buf);
    } else {
        /* If not a real map, it was a fallback malloc'd buffer */
        mvgal_memory_write(buffer->mvgal_buf, 0, buffer->size, buffer->host_ptr);
        free(buffer->host_ptr);
    }
    
    buffer->host_ptr = NULL;
    buffer->is_mapped = false;

    pthread_mutex_unlock(&buffer->lock);
    return MVGAL_SUCCESS;
}

size_t mvgal_sycl_mem_get_size(mvgal_sycl_buffer_t buffer) {
    if (!buffer) return 0;
    return buffer->size;
}

/* ============================================================================
 * Program and Kernel Management
 * ============================================================================ */

mvgal_error_t mvgal_sycl_program_create_from_spirv(
    uint32_t device_index,
    const void *spirv_data,
    size_t spirv_size,
    mvgal_sycl_program_t *program)
{
    if (!spirv_data || !program || spirv_size == 0)
        return MVGAL_ERROR_INVALID_ARG;

    mvgal_sycl_program_t prog = calloc(1, sizeof(struct mvgal_sycl_program));
    if (!prog) return MVGAL_ERROR_OUT_OF_MEMORY;

    prog->device_index = device_index;
    prog->spirv_data = malloc(spirv_size);
    if (!prog->spirv_data) {
        free(prog);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    memcpy(prog->spirv_data, spirv_data, spirv_size);
    prog->spirv_size = spirv_size;
    prog->is_spirv = true;
    prog->is_compiled = true;

    *program = prog;
    mvgal_log_debug("sycl: program created from SPIR-V (%zu bytes) for device %u", spirv_size, device_index);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_program_create_from_source(
    uint32_t device_index,
    const char *source,
    size_t source_len,
    const char *compile_options,
    mvgal_sycl_program_t *program)
{
    if (!source || !program) return MVGAL_ERROR_INVALID_ARG;

    mvgal_sycl_program_t prog = calloc(1, sizeof(struct mvgal_sycl_program));
    if (!prog) return MVGAL_ERROR_OUT_OF_MEMORY;

    prog->device_index = device_index;
    size_t len = source_len > 0 ? source_len : strlen(source);
    prog->source = malloc(len + 1);
    if (!prog->source) {
        free(prog);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    memcpy(prog->source, source, len);
    prog->source[len] = '\0';
    prog->source_len = len;
    prog->is_spirv = false;
    prog->is_compiled = false;

    if (compile_options) {
        prog->compile_options = strdup(compile_options);
    }

    *program = prog;
    mvgal_log_debug("sycl: program created from source for device %u", device_index);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_program_destroy(mvgal_sycl_program_t program) {
    if (!program) return MVGAL_ERROR_INVALID_ARG;

    free(program->spirv_data);
    free(program->source);
    free(program->compile_options);
    free(program);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_kernel_create(
    mvgal_sycl_program_t program,
    const char *kernel_name,
    mvgal_sycl_kernel_t *kernel)
{
    if (!program || !kernel_name || !kernel)
        return MVGAL_ERROR_INVALID_ARG;

    mvgal_sycl_kernel_t k = calloc(1, sizeof(struct mvgal_sycl_kernel));
    if (!k) return MVGAL_ERROR_OUT_OF_MEMORY;

    k->device_index = program->device_index;
    k->num_args = 0;
    k->is_compiled = program->is_compiled;

    strncpy(k->name, kernel_name, MVGAL_SYCL_MAX_KERNEL_NAME - 1);
    k->name[MVGAL_SYCL_MAX_KERNEL_NAME - 1] = '\0';

    if (program->spirv_data && program->spirv_size > 0) {
        k->binary_data = malloc(program->spirv_size);
        if (k->binary_data) {
            memcpy(k->binary_data, program->spirv_data, program->spirv_size);
            k->binary_size = program->spirv_size;
        }
    }

    *kernel = k;

    mvgal_log_debug("sycl: kernel '%s' created for device %u",
                    kernel_name, program->device_index);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_kernel_destroy(mvgal_sycl_kernel_t kernel) {
    if (!kernel) return MVGAL_ERROR_INVALID_ARG;

    free(kernel->binary_data);
    free(kernel);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_kernel_set_arg(
    mvgal_sycl_kernel_t kernel,
    const mvgal_sycl_kernel_arg_t *arg)
{
    if (!kernel || !arg) return MVGAL_ERROR_INVALID_ARG;
    if (arg->index >= MVGAL_SYCL_MAX_KERNEL_ARGS)
        return MVGAL_ERROR_INVALID_ARG;

    kernel->args[arg->index] = *arg;
    if (arg->index >= kernel->num_args) {
        kernel->num_args = arg->index + 1;
    }

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_kernel_set_args(
    mvgal_sycl_kernel_t kernel,
    uint32_t num_args,
    const mvgal_sycl_kernel_arg_t *args)
{
    if (!kernel || !args) return MVGAL_ERROR_INVALID_ARG;
    if (num_args > MVGAL_SYCL_MAX_KERNEL_ARGS)
        return MVGAL_ERROR_INVALID_ARG;

    for (uint32_t i = 0; i < num_args; i++) {
        kernel->args[i] = args[i];
    }
    kernel->num_args = num_args;

    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Command Group Execution
 * ============================================================================ */

mvgal_error_t mvgal_sycl_parallel_for(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_kernel_t kernel,
    const mvgal_sycl_ndrange_t *ndrange)
{
    if (!queue || !kernel || !ndrange)
        return MVGAL_ERROR_INVALID_ARG;
    if (ndrange->dimensions == 0 || ndrange->dimensions > 3)
        return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&queue->lock);
    queue->in_use = true;
    queue->submit_count++;
    pthread_mutex_unlock(&queue->lock);

    /* Build an execution plan via the MVGAL execution engine */
    mvgal_execution_submit_info_t submit_info;
    mvgal_execution_plan_t plan;
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.api = MVGAL_API_SYCL;
    submit_info.requested_strategy = MVGAL_STRATEGY_ROUND_ROBIN;
    submit_info.gpu_mask = (uint32_t)(1u << kernel->device_index);

    /* Calculate total work items */
    submit_info.telemetry.data_size = 1;
    for (uint32_t d = 0; d < ndrange->dimensions; d++) {
        submit_info.telemetry.data_size *= ndrange->global_range[d];
    }

    mvgal_execution_submit(NULL, &submit_info, &plan);
    mvgal_log_debug("sycl: kernel '%s' launched on device %u (size %lu)",
              kernel->name, kernel->device_index, (unsigned long)submit_info.telemetry.data_size);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_single_task(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_kernel_t kernel)
{
    if (!queue || !kernel) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&queue->lock);
    queue->in_use = true;
    queue->submit_count++;
    pthread_mutex_unlock(&queue->lock);

    mvgal_execution_submit_info_t submit_info;
    mvgal_execution_plan_t plan;
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.api = MVGAL_API_SYCL;
    submit_info.requested_strategy = MVGAL_STRATEGY_SINGLE_GPU;
    submit_info.gpu_mask = (uint32_t)(1u << kernel->device_index);
    submit_info.telemetry.data_size = 1;

    mvgal_execution_submit(NULL, &submit_info, &plan);
    mvgal_log_debug("sycl: kernel '%s' launched on device %u (size %lu)",
              kernel->name, kernel->device_index, (unsigned long)submit_info.telemetry.data_size);
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Synchronization
 * ============================================================================ */

mvgal_error_t mvgal_sycl_queue_barrier(mvgal_sycl_queue_t queue) {
    if (!queue) return MVGAL_ERROR_INVALID_ARG;

    mvgal_log_debug("sycl: barrier on device %u", queue->device_index);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_event_create(void **event) {
    if (!event) return MVGAL_ERROR_INVALID_ARG;

    /* Events are integer handles in this implementation */
    uint64_t *ev = calloc(1, sizeof(uint64_t));
    if (!ev) return MVGAL_ERROR_OUT_OF_MEMORY;

    *event = ev;
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_event_wait(void *event) {
    if (!event) return MVGAL_ERROR_INVALID_ARG;
    /* No-op in this implementation (implicit synchronization) */
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_event_destroy(void *event) {
    if (!event) return MVGAL_ERROR_INVALID_ARG;
    free(event);
    return MVGAL_SUCCESS;
}
