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

static mvgal_vendor_t mvgal_gpu_index_to_vendor(uint32_t gpu_index) {
    mvgal_gpu_info_t info;
    if (mvgal_gpu_get_info(gpu_index, &info) == MVGAL_SUCCESS) {
        return (mvgal_vendor_t)info.vendor_id;
    }
    return MVGAL_VENDOR_UNKNOWN;
}

static void refresh_device_cache(void) {
    pthread_mutex_lock(&g_cache_lock);

    if (g_cache_initialized) {
        pthread_mutex_unlock(&g_cache_lock);
        return;
    }

    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count <= 0) {
        g_num_cached_devices = 0;
        g_cache_initialized = true;
        pthread_mutex_unlock(&g_cache_lock);
        return;
    }

    uint32_t count = (uint32_t)gpu_count;
    if (count > MVGAL_SYCL_MAX_DEVICES)
        count = MVGAL_SYCL_MAX_DEVICES;

    for (uint32_t i = 0; i < count; i++) {
        mvgal_gpu_info_t gpu_info;
        memset(&g_device_cache[i], 0, sizeof(mvgal_sycl_device_info_t));
        g_device_cache[i].device_index = i;
        g_device_cache[i].device_type = MVGAL_SYCL_DEVICE_TYPE_GPU;

        if (mvgal_gpu_get_info(i, &gpu_info) == MVGAL_SUCCESS) {
            snprintf(g_device_cache[i].name, sizeof(g_device_cache[i].name),
                     "%s", gpu_info.name);
            g_device_cache[i].vendor = (mvgal_vendor_t)gpu_info.vendor_id;
            g_device_cache[i].global_mem_size = gpu_info.vram_total_bytes;
            g_device_cache[i].max_compute_units = (uint32_t)gpu_info.num_compute_units;
            g_device_cache[i].clock_frequency_mhz = gpu_info.core_clock_mhz;
            g_device_cache[i].local_mem_size = 65536;
            g_device_cache[i].max_mem_alloc_size = gpu_info.vram_total_bytes / 2;
            g_device_cache[i].max_work_group_size = 1024;
            g_device_cache[i].max_work_item_dimensions = 3;
            g_device_cache[i].max_work_item_sizes[0] = 1024;
            g_device_cache[i].max_work_item_sizes[1] = 1024;
            g_device_cache[i].max_work_item_sizes[2] = 1024;
            g_device_cache[i].has_fp64 = true;
            g_device_cache[i].has_fp16 = true;
            g_device_cache[i].has_atomic64 = true;
            g_device_cache[i].preferred_vector_width_int = 4;
            g_device_cache[i].preferred_vector_width_float = 4;
            g_device_cache[i].preferred_vector_width_double = 2;
            g_device_cache[i].native_vector_width_int = 4;
            g_device_cache[i].native_vector_width_float = 4;
            g_device_cache[i].native_vector_width_double = 2;
            g_device_cache[i].subgroup_sizes[0] = 32;
            g_device_cache[i].num_subgroup_sizes = 1;
            g_device_cache[i].max_num_sub_groups = 32;

            switch (gpu_info.vendor_id) {
                case 0x1002:
                    snprintf(g_device_cache[i].vendor_name,
                             sizeof(g_device_cache[i].vendor_name), "AMD");
                    break;
                case 0x10DE:
                    snprintf(g_device_cache[i].vendor_name,
                             sizeof(g_device_cache[i].vendor_name), "NVIDIA");
                    break;
                case 0x8086:
                    snprintf(g_device_cache[i].vendor_name,
                             sizeof(g_device_cache[i].vendor_name), "Intel");
                    break;
                case 0x1ED5:
                    snprintf(g_device_cache[i].vendor_name,
                             sizeof(g_device_cache[i].vendor_name), "Moore Threads");
                    break;
                default:
                    snprintf(g_device_cache[i].vendor_name,
                             sizeof(g_device_cache[i].vendor_name), "Unknown");
                    break;
            }
            snprintf(g_device_cache[i].driver_version,
                     sizeof(g_device_cache[i].driver_version), "MVGAL %s",
                     MVGAL_VERSION_STRING);
        }
    }

    g_num_cached_devices = count;
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
    if (!num_devices || !devices) return MVGAL_ERROR_INVALID_ARG;
    if (*num_devices == 0) return MVGAL_ERROR_INVALID_ARG;

    refresh_device_cache();

    uint32_t capacity = *num_devices;
    uint32_t written = 0;

    pthread_mutex_lock(&g_cache_lock);

    for (uint32_t i = 0; i < g_num_cached_devices && written < capacity; i++) {
        if (dev_type != MVGAL_SYCL_DEVICE_TYPE_AUTOMATIC &&
            g_device_cache[i].device_type != dev_type) {
            continue;
        }
        memcpy(&devices[written], &g_device_cache[i],
               sizeof(mvgal_sycl_device_info_t));
        written++;
    }

    pthread_mutex_unlock(&g_cache_lock);
    *num_devices = written;

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: enumerated %u device(s)", written);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_get_device_info(
    uint32_t device_index,
    mvgal_sycl_device_info_t *info)
{
    if (!info) return MVGAL_ERROR_INVALID_ARG;

    refresh_device_cache();

    pthread_mutex_lock(&g_cache_lock);
    if (device_index >= g_num_cached_devices) {
        pthread_mutex_unlock(&g_cache_lock);
        return MVGAL_ERROR_INVALID_ARG;
    }
    memcpy(info, &g_device_cache[device_index], sizeof(mvgal_sycl_device_info_t));
    pthread_mutex_unlock(&g_cache_lock);

    return MVGAL_SUCCESS;
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
    mvgal_log(MVGAL_LOG_DEBUG, "sycl: queue created for device %u", device_index);
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

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: submit to queue (device %u, count %lu)",
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
    mvgal_memory_flags_t mflags = MVGAL_MEMORY_FLAG_GPU_VALID;
    if (flags & MVGAL_SYCL_MEM_FLAG_HOST_VISIBLE) {
        mflags |= MVGAL_MEMORY_FLAG_HOST_VALID;
    }
    if (flags & MVGAL_SYCL_MEM_FLAG_HOST_COHERENT) {
        mflags |= MVGAL_MEMORY_FLAG_CPU_UNCACHED;
    }
    if (flags & MVGAL_SYCL_MEM_FLAG_DEVICE_READ_WRITE) {
        mflags |= MVGAL_MEMORY_FLAG_SHARED;
    }

    mvgal_buffer_t mvgal_buf = NULL;
    mvgal_error_t err = mvgal_buffer_create(&mvgal_buf, size, mflags);
    if (err != MVGAL_SUCCESS) {
        pthread_mutex_destroy(&buf->lock);
        free(buf);
        return err;
    }

    buf->mvgal_buf = mvgal_buf;
    *buffer = buf;

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: allocated %zu bytes on device %u", size, device_index);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_sycl_mem_free(mvgal_sycl_buffer_t buffer) {
    if (!buffer) return MVGAL_ERROR_INVALID_ARG;

    pthread_mutex_lock(&buffer->lock);
    if (buffer->ref_count > 1) {
        buffer->ref_count--;
        pthread_mutex_unlock(&buffer->lock);
        return MVGAL_SUCCESS;
    }

    if (buffer->mvgal_buf) {
        mvgal_buffer_destroy(buffer->mvgal_buf);
    }
    if (buffer->host_ptr) {
        free(buffer->host_ptr);
    }
    pthread_mutex_unlock(&buffer->lock);
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
    (void)queue;
    if (!buffer || !data) return MVGAL_ERROR_INVALID_ARG;
    if (offset + size > buffer->size) return MVGAL_ERROR_INVALID_ARG;

    mvgal_error_t err = mvgal_buffer_write(buffer->mvgal_buf, data, size, offset);
    if (err != MVGAL_SUCCESS) {
        mvgal_log(MVGAL_LOG_ERROR, "sycl: mem_write failed: %d", err);
    }
    return err;
}

mvgal_error_t mvgal_sycl_mem_read(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer,
    size_t offset,
    size_t size,
    void *data)
{
    (void)queue;
    if (!buffer || !data) return MVGAL_ERROR_INVALID_ARG;
    if (offset + size > buffer->size) return MVGAL_ERROR_INVALID_ARG;

    mvgal_error_t err = mvgal_buffer_read(buffer->mvgal_buf, data, size, offset);
    if (err != MVGAL_SUCCESS) {
        mvgal_log(MVGAL_LOG_ERROR, "sycl: mem_read failed: %d", err);
    }
    return err;
}

mvgal_error_t mvgal_sycl_mem_copy(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t src,
    mvgal_sycl_buffer_t dst,
    size_t size)
{
    (void)queue;
    if (!src || !dst) return MVGAL_ERROR_INVALID_ARG;
    if (size > src->size || size > dst->size) return MVGAL_ERROR_INVALID_ARG;

    /* Stage through host memory */
    void *tmp = malloc(size);
    if (!tmp) return MVGAL_ERROR_OUT_OF_MEMORY;

    mvgal_error_t err;
    err = mvgal_buffer_read(src->mvgal_buf, tmp, size, 0);
    if (err == MVGAL_SUCCESS) {
        err = mvgal_buffer_write(dst->mvgal_buf, tmp, size, 0);
    }

    free(tmp);
    return err;
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

    buffer->host_ptr = malloc(buffer->size);
    if (!buffer->host_ptr) {
        pthread_mutex_unlock(&buffer->lock);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }

    mvgal_buffer_read(buffer->mvgal_buf, buffer->host_ptr, buffer->size, 0);
    buffer->is_mapped = true;
    *mapped_ptr = buffer->host_ptr;

    pthread_mutex_unlock(&buffer->lock);
    return MVGAL_SUCCESS;
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

    /* Write back and free host copy */
    mvgal_buffer_write(buffer->mvgal_buf, buffer->host_ptr, buffer->size, 0);
    free(buffer->host_ptr);
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

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: program created from SPIR-V (%zu bytes)", spirv_size);
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

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: program created from source (%zu bytes)", len);
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

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: kernel '%s' created for device %u",
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
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.api = MVGAL_API_SYCL;
    submit_info.strategy = MVGAL_STRATEGY_ROUND_ROBIN;
    submit_info.gpu_mask = (uint32_t)(1u << kernel->device_index);

    /* Calculate total work items */
    submit_info.workload_size = 1;
    for (uint32_t d = 0; d < ndrange->dimensions; d++) {
        submit_info.workload_size *= ndrange->global_range[d];
    }

    mvgal_execution_submit(&submit_info);

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: parallel_for '%s' (%ux%ux%u) on device %u",
              kernel->name,
              (unsigned)ndrange->global_range[0],
              (unsigned)ndrange->global_range[1],
              (unsigned)ndrange->global_range[2],
              kernel->device_index);

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
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.api = MVGAL_API_SYCL;
    submit_info.strategy = MVGAL_STRATEGY_SINGLE_GPU;
    submit_info.gpu_mask = (uint32_t)(1u << kernel->device_index);
    submit_info.workload_size = 1;

    mvgal_execution_submit(&submit_info);

    mvgal_log(MVGAL_LOG_DEBUG, "sycl: single_task '%s' on device %u",
              kernel->name, kernel->device_index);

    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Synchronization
 * ============================================================================ */

mvgal_error_t mvgal_sycl_queue_barrier(mvgal_sycl_queue_t queue) {
    if (!queue) return MVGAL_ERROR_INVALID_ARG;

    mvgal_execution_barrier();
    mvgal_log(MVGAL_LOG_DEBUG, "sycl: barrier on device %u", queue->device_index);
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
