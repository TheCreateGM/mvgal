/**
 * @file mvgal_sycl.h
 * @brief MVGAL SYCL Backend API
 *
 * SYCL 2020 runtime backend interface for MVGAL.
 * Provides device discovery, queue submission, memory management,
 * and kernel dispatch for SYCL implementations targeting MVGAL
 * aggregated GPUs.
 *
 * Copyright (C) 2026 MVGAL Project
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_SYCL_H
#define MVGAL_SYCL_H

#include "mvgal_types.h"
#include "mvgal_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup SYCLBackend
 * @{
 */

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MVGAL_SYCL_MAX_DEVICES      16
#define MVGAL_SYCL_MAX_PLATFORMS    4
#define MVGAL_SYCL_MAX_KERNEL_NAME  256
#define MVGAL_SYCL_MAX_QUEUES       64

/* ============================================================================
 * SYCL Device Types
 * ============================================================================ */

/**
 * @brief SYCL device type classification
 */
typedef enum {
    MVGAL_SYCL_DEVICE_TYPE_CPU = 0,
    MVGAL_SYCL_DEVICE_TYPE_GPU = 1,
    MVGAL_SYCL_DEVICE_TYPE_ACCELERATOR = 2,
    MVGAL_SYCL_DEVICE_TYPE_CUSTOM = 3,
    MVGAL_SYCL_DEVICE_TYPE_AUTOMATIC = 4,
    MVGAL_SYCL_DEVICE_TYPE_HOST = 5,
} mvgal_sycl_device_type_t;

/**
 * @brief SYCL device information
 */
typedef struct {
    uint32_t            device_index;
    mvgal_sycl_device_type_t device_type;
    mvgal_vendor_t      vendor;
    char                name[128];
    char                vendor_name[64];
    char                driver_version[64];
    uint64_t            global_mem_size;
    uint64_t            local_mem_size;
    uint64_t            max_mem_alloc_size;
    uint32_t            max_compute_units;
    uint64_t            max_work_group_size;
    uint32_t            max_work_item_dimensions;
    size_t              max_work_item_sizes[3];
    uint32_t            subgroup_sizes[8];
    uint32_t            num_subgroup_sizes;
    bool                has_fp64;
    bool                has_fp16;
    bool                has_atomic64;
    bool                has_images;
    uint32_t            clock_frequency_mhz;
    uint32_t            preferred_vector_width_int;
    uint32_t            preferred_vector_width_float;
    uint32_t            preferred_vector_width_double;
    uint32_t            native_vector_width_int;
    uint32_t            native_vector_width_float;
    uint32_t            native_vector_width_double;
    uint32_t            max_num_sub_groups;
} mvgal_sycl_device_info_t;

/**
 * @brief SYCL platform information
 */
typedef struct {
    uint32_t            platform_index;
    char                name[64];
    char                vendor[64];
    char                version[64];
    char                profile[64];
    uint32_t            num_devices;
    uint32_t            device_indices[MVGAL_SYCL_MAX_DEVICES];
} mvgal_sycl_platform_info_t;

/* ============================================================================
 * SYCL Memory Types
 * ============================================================================ */

/**
 * @brief SYCL memory allocation flags
 */
typedef enum {
    MVGAL_SYCL_MEM_FLAG_NONE          = 0,
    MVGAL_SYCL_MEM_FLAG_HOST_VISIBLE  = 1 << 0,
    MVGAL_SYCL_MEM_FLAG_HOST_COHERENT  = 1 << 1,
    MVGAL_SYCL_MEM_FLAG_HOST_CACHED   = 1 << 2,
    MVGAL_SYCL_MEM_FLAG_DEVICE_READ   = 1 << 3,
    MVGAL_SYCL_MEM_FLAG_DEVICE_WRITE  = 1 << 4,
    MVGAL_SYCL_MEM_FLAG_DEVICE_READ_WRITE = 1 << 5,
} mvgal_sycl_mem_flags_t;

/**
 * @brief Opaque SYCL buffer handle
 */
typedef struct mvgal_sycl_buffer *mvgal_sycl_buffer_t;

/* ============================================================================
 * SYCL Queue Types
 * ============================================================================ */

/**
 * @brief SYCL queue properties
 */
typedef enum {
    MVGAL_SYCL_QUEUE_PROP_NONE        = 0,
    MVGAL_SYCL_QUEUE_PROP_OUT_OF_ORDER = 1 << 0,
    MVGAL_SYCL_QUEUE_PROP_PROFILING   = 1 << 1,
    MVGAL_SYCL_QUEUE_PROP_PRIORITY_LOW  = 1 << 2,
    MVGAL_SYCL_QUEUE_PROP_PRIORITY_HIGH = 1 << 3,
} mvgal_sycl_queue_props_t;

/**
 * @brief Opaque SYCL queue handle
 */
typedef struct mvgal_sycl_queue *mvgal_sycl_queue_t;

/* ============================================================================
 * SYCL Kernel Types
 * ============================================================================ */

/**
 * @brief Opaque SYCL kernel handle
 */
typedef struct mvgal_sycl_kernel *mvgal_sycl_kernel_t;

/**
 * @brief Opaque SYCL program handle
 */
typedef struct mvgal_sycl_program *mvgal_sycl_program_t;

/**
 * @brief Kernel argument type
 */
typedef enum {
    MVGAL_SYCL_ARG_TYPE_BUFFER = 0,
    MVGAL_SYCL_ARG_TYPE_SCALAR_INT8 = 1,
    MVGAL_SYCL_ARG_TYPE_SCALAR_INT16 = 2,
    MVGAL_SYCL_ARG_TYPE_SCALAR_INT32 = 3,
    MVGAL_SYCL_ARG_TYPE_SCALAR_INT64 = 4,
    MVGAL_SYCL_ARG_TYPE_SCALAR_UINT8 = 5,
    MVGAL_SYCL_ARG_TYPE_SCALAR_UINT16 = 6,
    MVGAL_SYCL_ARG_TYPE_SCALAR_UINT32 = 7,
    MVGAL_SYCL_ARG_TYPE_SCALAR_UINT64 = 8,
    MVGAL_SYCL_ARG_TYPE_SCALAR_FLOAT = 9,
    MVGAL_SYCL_ARG_TYPE_SCALAR_DOUBLE = 10,
    MVGAL_SYCL_ARG_TYPE_SCALAR_HALF = 11,
    MVGAL_SYCL_ARG_TYPE_LOCAL = 12,
    MVGAL_SYCL_ARG_TYPE_ACCESSOR = 13,
} mvgal_sycl_arg_type_t;

/**
 * @brief Kernel argument descriptor
 */
typedef struct {
    mvgal_sycl_arg_type_t type;
    uint32_t              index;
    union {
        mvgal_sycl_buffer_t buffer;
        int8_t              i8;
        int16_t             i16;
        int32_t             i32;
        int64_t             i64;
        uint8_t             u8;
        uint16_t            u16;
        uint32_t            u32;
        uint64_t            u64;
        float               f32;
        double              f64;
        size_t              local_size;
    } value;
} mvgal_sycl_kernel_arg_t;

/**
 * @brief Kernel execution range (ND-range)
 */
typedef struct {
    uint32_t dimensions;
    size_t   global_offset[3];
    size_t   global_range[3];
    size_t   local_range[3];
} mvgal_sycl_ndrange_t;

/* ============================================================================
 * Device Discovery
 * ============================================================================ */

/**
 * @brief Enumerate SYCL-visible MVGAL devices
 * @param dev_type Device type filter (MVGAL_SYCL_DEVICE_TYPE_AUTOMATIC for all)
 * @param devices Output array of device information
 * @param num_devices IN: capacity, OUT: actual count
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_get_devices(
    mvgal_sycl_device_type_t dev_type,
    mvgal_sycl_device_info_t *devices,
    uint32_t *num_devices);

/**
 * @brief Get device information
 * @param device_index Device index
 * @param info Output device information
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_get_device_info(
    uint32_t device_index,
    mvgal_sycl_device_info_t *info);

/**
 * @brief Enumerate SYCL platforms
 * @param platforms Output array
 * @param num_platforms IN: capacity, OUT: actual count
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_get_platforms(
    mvgal_sycl_platform_info_t *platforms,
    uint32_t *num_platforms);

/* ============================================================================
 * Queue Management
 * ============================================================================ */

/**
 * @brief Create a SYCL command queue
 * @param device_index Target device
 * @param properties Queue properties
 * @param queue Output queue handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_queue_create(
    uint32_t device_index,
    mvgal_sycl_queue_props_t properties,
    mvgal_sycl_queue_t *queue);

/**
 * @brief Destroy a SYCL command queue
 * @param queue Queue handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_queue_destroy(mvgal_sycl_queue_t queue);

/**
 * @brief Submit work to a SYCL queue (non-blocking)
 * @param queue Queue handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_queue_submit(mvgal_sycl_queue_t queue);

/**
 * @brief Wait for all work in queue to complete
 * @param queue Queue handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_queue_wait(mvgal_sycl_queue_t queue);

/**
 * @brief Check if queue has pending work
 * @param queue Queue handle
 * @return true if work is in flight
 */
bool mvgal_sycl_queue_is_running(mvgal_sycl_queue_t queue);

/* ============================================================================
 * Memory Management
 * ============================================================================ */

/**
 * @brief Allocate SYCL memory (device or shared)
 * @param device_index Target device
 * @param size Allocation size
 * @param flags Allocation flags
 * @param buffer Output buffer handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_mem_alloc(
    uint32_t device_index,
    size_t size,
    mvgal_sycl_mem_flags_t flags,
    mvgal_sycl_buffer_t *buffer);

/**
 * @brief Free SYCL memory
 * @param buffer Buffer handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_mem_free(mvgal_sycl_buffer_t buffer);

/**
 * @brief Write data to SYCL buffer (host to device)
 * @param queue Queue for synchronization
 * @param buffer Destination buffer
 * @param offset Byte offset
 * @param size Transfer size
 * @param data Host data pointer
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_mem_write(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer,
    size_t offset,
    size_t size,
    const void *data);

/**
 * @brief Read data from SYCL buffer (device to host)
 * @param queue Queue for synchronization
 * @param buffer Source buffer
 * @param offset Byte offset
 * @param size Transfer size
 * @param data Host data pointer
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_mem_read(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer,
    size_t offset,
    size_t size,
    void *data);

/**
 * @brief Copy between SYCL buffers
 * @param queue Queue for synchronization
 * @param src Source buffer
 * @param dst Destination buffer
 * @param size Transfer size
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_mem_copy(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t src,
    mvgal_sycl_buffer_t dst,
    size_t size);

/**
 * @brief Map SYCL buffer for host access
 * @param queue Queue for synchronization
 * @param buffer Buffer to map
 * @param flags Access flags
 * @param mapped_ptr Output mapped pointer
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_mem_map(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer,
    mvgal_sycl_mem_flags_t flags,
    void **mapped_ptr);

/**
 * @brief Unmap previously mapped SYCL buffer
 * @param queue Queue for synchronization
 * @param buffer Buffer to unmap
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_mem_unmap(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_buffer_t buffer);

/**
 * @brief Get buffer size
 * @param buffer Buffer handle
 * @return Size in bytes, 0 on error
 */
size_t mvgal_sycl_mem_get_size(mvgal_sycl_buffer_t buffer);

/* ============================================================================
 * Program and Kernel Management
 * ============================================================================ */

/**
 * @brief Create a SYCL program from SPIR-V binary
 * @param device_index Target device
 * @param spirv_data SPIR-V binary data
 * @param spirv_size Binary size
 * @param program Output program handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_program_create_from_spirv(
    uint32_t device_index,
    const void *spirv_data,
    size_t spirv_size,
    mvgal_sycl_program_t *program);

/**
 * @brief Create a SYCL program from source string (if JIT supported)
 * @param device_index Target device
 * @param source Source code
 * @param source_len Source length (0 for null-terminated)
 * @param compile_options Compiler options (may be NULL)
 * @param program Output program handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_program_create_from_source(
    uint32_t device_index,
    const char *source,
    size_t source_len,
    const char *compile_options,
    mvgal_sycl_program_t *program);

/**
 * @brief Destroy a SYCL program
 * @param program Program handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_program_destroy(mvgal_sycl_program_t program);

/**
 * @brief Create kernel from program
 * @param program Program handle
 * @param kernel_name Kernel entry point name
 * @param kernel Output kernel handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_kernel_create(
    mvgal_sycl_program_t program,
    const char *kernel_name,
    mvgal_sycl_kernel_t *kernel);

/**
 * @brief Destroy a kernel object
 * @param kernel Kernel handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_kernel_destroy(mvgal_sycl_kernel_t kernel);

/**
 * @brief Set a kernel argument
 * @param kernel Kernel handle
 * @param arg Argument descriptor
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_kernel_set_arg(
    mvgal_sycl_kernel_t kernel,
    const mvgal_sycl_kernel_arg_t *arg);

/**
 * @brief Set multiple kernel arguments at once
 * @param kernel Kernel handle
 * @param num_args Number of arguments
 * @param args Array of argument descriptors
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_kernel_set_args(
    mvgal_sycl_kernel_t kernel,
    uint32_t num_args,
    const mvgal_sycl_kernel_arg_t *args);

/* ============================================================================
 * Command Group Execution
 * ============================================================================ */

/**
 * @brief Execute a parallel kernel (parallel_for)
 * @param queue Target queue
 * @param kernel Kernel to execute
 * @param ndrange ND-range describing execution
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_parallel_for(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_kernel_t kernel,
    const mvgal_sycl_ndrange_t *ndrange);

/**
 * @brief Execute a single-threaded task (single_task)
 * @param queue Target queue
 * @param kernel Kernel to execute
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_single_task(
    mvgal_sycl_queue_t queue,
    mvgal_sycl_kernel_t kernel);

/* ============================================================================
 * Synchronization
 * ============================================================================ */

/**
 * @brief Insert a barrier in the queue
 * @param queue Target queue
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_queue_barrier(mvgal_sycl_queue_t queue);

/**
 * @brief Create a SYCL event
 * @param event Output event handle (opaque)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_event_create(void **event);

/**
 * @brief Wait for an event
 * @param event Event handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_event_wait(void *event);

/**
 * @brief Destroy an event
 * @param event Event handle
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_sycl_event_destroy(void *event);

/** @} */ // end of SYCLBackend

#ifdef __cplusplus
}
#endif

#endif // MVGAL_SYCL_H
