/**
 * @file mvgal_memory.h
 * @brief Memory management API
 * 
 * Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * This header provides memory allocation, sharing, and synchronization functions.
 */

#ifndef MVGAL_MEMORY_H
#define MVGAL_MEMORY_H

#include <stddef.h>
#include <stdatomic.h>
#include "mvgal_types.h"
#include "mvgal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup MemoryManagement
 * @{
 */

/**
 * @brief Memory buffer handle
 */
typedef struct mvgal_buffer *mvgal_buffer_t;

/**
 * @brief Memory allocation flags
 */
typedef enum {
    MVGAL_MEMORY_FLAG_NONE = 0,              ///< No special flags
    MVGAL_MEMORY_FLAG_HOST_VALID = 1 << 0,   ///< CPU can access (mapped)
    MVGAL_MEMORY_FLAG_GPU_VALID = 1 << 1,   ///< GPUs can access
    MVGAL_MEMORY_FLAG_CPU_CACHED = 1 << 2,   ///< CPU cached memory
    MVGAL_MEMORY_FLAG_CPU_UNCACHED = 1 << 3, ///< CPU uncached memory
    MVGAL_MEMORY_FLAG_SHARED = 1 << 4,      ///< Shared across GPUs
    MVGAL_MEMORY_FLAG_DMA_BUF = 1 << 5,     ///< Use DMA-BUF
    MVGAL_MEMORY_FLAG_P2P = 1 << 6,         ///< Enable P2P transfers
    MVGAL_MEMORY_FLAG_REPLICATED = 1 << 7,  ///< Replicate across GPUs
    MVGAL_MEMORY_FLAG_PERSISTENT = 1 << 8,   ///< Persistent mapping
    MVGAL_MEMORY_FLAG_LAZY_ALLOCATE = 1 << 9,///< Lazy allocation
    MVGAL_MEMORY_FLAG_ZERO_INITIALIZED = 1 << 10, ///< Zero-initialized
} mvgal_memory_flags_t;

/**
 * @brief Memory allocation info
 */
typedef struct {
    size_t size;                        ///< Size in bytes
    size_t alignment;                   ///< Alignment requirement
    mvgal_memory_flags_t flags;         ///< Allocation flags
    mvgal_memory_type_t memory_type;   ///< Preferred memory type
    mvgal_memory_sharing_mode_t sharing_mode; ///< Sharing mode
    mvgal_memory_access_flags_t access;///< Access flags
    uint32_t gpu_mask;                  ///< Bitmask of GPUs that can access
    uint32_t priority;                   ///< Allocation priority (0-100)
} mvgal_memory_alloc_info_t;

/**
 * @brief Memory buffer descriptor
 */
typedef struct {
    size_t size;                        ///< Size in bytes
    size_t alignment;                   ///< Alignment
    mvgal_memory_flags_t flags;         ///< Allocation flags
    mvgal_memory_type_t memory_type;   ///< Actual memory type
    mvgal_memory_sharing_mode_t sharing_mode; ///< Sharing mode
    uint32_t gpu_mask;                  ///< Bitmask of accessible GPUs
    
    // DMA-BUF information
    int dmabuf_fd;                      ///< DMA-BUF file descriptor (-1 if none)
    uint64_t dmabuf_offset;             ///< Offset in DMA-BUF
    
    // Memory mapping
    void *host_ptr;                     ///< CPU pointer (NULL if not mapped)
    uint64_t gpu_address[16];           ///< GPU addresses for each device
    
    // Synchronization
    mvgal_fence_t fence;                ///< Fence for synchronization
    
    // Statistics
    uint64_t bytes_transferred;         ///< Total bytes transferred
    uint64_t access_count;              ///< Number of times accessed
    
    // Handle
    mvgal_buffer_t buffer;             ///< Internal buffer handle
} mvgal_memory_descriptor_t;

/**
 * @brief Memory copy region
 */
typedef struct {
    mvgal_buffer_t src_buffer;         ///< Source buffer
    uint64_t src_offset;               ///< Source offset
    mvgal_buffer_t dst_buffer;         ///< Destination buffer
    uint64_t dst_offset;               ///< Destination offset
    size_t size;                       ///< Copy size in bytes
} mvgal_memory_copy_region_t;

/**
 * @brief Memory transfer callback
 * @param buffer Buffer handle
 * @param offset Offset in buffer
 * @param size Size of data
 * @param user_data User data
 */
typedef void (*mvgal_memory_callback_t)(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size,
    void *user_data
);

/**
 * @brief Allocate memory buffer
 * 
 * Allocates a memory buffer that can be accessed by CPUs and/or GPUs.
 * 
 * @param context Context to allocate in
 * @param alloc_info Allocation information
 * @param buffer Buffer handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_allocate(
    void *context,
    const mvgal_memory_alloc_info_t *alloc_info,
    mvgal_buffer_t *buffer
);

/**
 * @brief Allocate memory with explicit size
 * 
 * Simplified allocation with just size and flags.
 * 
 * @param context Context to allocate in
 * @param size Size in bytes
 * @param flags Allocation flags
 * @param buffer Buffer handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_allocate_simple(
    void *context,
    size_t size,
    mvgal_memory_flags_t flags,
    mvgal_buffer_t *buffer
);

/**
 * @brief Free a memory buffer
 * 
 * @param buffer Buffer to free
 */
void mvgal_memory_free(mvgal_buffer_t buffer);

/**
 * @brief Get buffer descriptor
 * 
 * @param buffer Buffer to query
 * @param desc Descriptor (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_get_descriptor(
    mvgal_buffer_t buffer,
    mvgal_memory_descriptor_t *desc
);

/**
 * @brief Map buffer into CPU address space
 * 
 * @param buffer Buffer to map
 * @param offset Offset in buffer
 * @param size Size to map (0 for entire buffer)
 * @param ptr Pointer to mapped memory (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_map(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size,
    void **ptr
);

/**
 * @brief Unmap buffer from CPU address space
 * 
 * @param buffer Buffer to unmap
 */
void mvgal_memory_unmap(mvgal_buffer_t buffer);

/**
 * @brief Check if buffer is mapped
 * 
 * @param buffer Buffer to check
 * @return true if mapped, false otherwise
 */
bool mvgal_memory_is_mapped(mvgal_buffer_t buffer);

/**
 * @brief Get pointer to mapped buffer
 * 
 * @param buffer Buffer to get pointer for
 * @return Pointer to buffer data, or NULL if not mapped
 */
void *mvgal_memory_get_pointer(mvgal_buffer_t buffer);

/**
 * @brief Write data to buffer
 * 
 * @param buffer Buffer to write to
 * @param offset Offset in buffer
 * @param size Size of data to write
 * @param data Data to write
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_write(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size,
    const void *data
);

/**
 * @brief Read data from buffer
 * 
 * @param buffer Buffer to read from
 * @param offset Offset in buffer
 * @param size Size of data to read
 * @param data Data buffer (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_read(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size,
    void *data
);

/**
 * @brief Copy memory between buffers
 * 
 * @param context Context
 * @param regions Array of copy regions
 * @param region_count Number of regions
 * @param fence Fence to signal on completion (optional)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_copy(
    void *context,
    const mvgal_memory_copy_region_t *regions,
    uint32_t region_count,
    mvgal_fence_t fence
);

/**
 * @brief Copy memory with GPU-to-GPU transfer
 * 
 * @param context Context
 * @param src_buffer Source buffer
 * @param src_offset Source offset
 * @param dst_buffer Destination buffer
 * @param dst_offset Destination offset
 * @param size Size to copy
 * @param src_gpu_index Source GPU index
 * @param dst_gpu_index Destination GPU index
 * @param fence Fence to signal on completion
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_copy_gpu(
    void *context,
    mvgal_buffer_t src_buffer,
    uint64_t src_offset,
    mvgal_buffer_t dst_buffer,
    uint64_t dst_offset,
    size_t size,
    uint32_t src_gpu_index,
    uint32_t dst_gpu_index,
    mvgal_fence_t fence
);

/**
 * @brief Export buffer as DMA-BUF
 * 
 * @param buffer Buffer to export
 * @param fd File descriptor (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_export_dmabuf(mvgal_buffer_t buffer, int *fd);

/**
 * @brief Import DMA-BUF as buffer
 * 
 * @param context Context
 * @param fd DMA-BUF file descriptor
 * @param buffer Buffer handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_import_dmabuf(
    void *context,
    int fd,
    mvgal_buffer_t *buffer
);

/**
 * @brief Flush buffer writes to make them visible to GPUs
 * 
 * @param buffer Buffer to flush
 * @param offset Offset
 * @param size Size to flush (0 for entire buffer)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_flush(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size
);

/**
 * @brief Invalidate buffer cache to read GPU writes
 * 
 * @param buffer Buffer to invalidate
 * @param offset Offset
 * @param size Size to invalidate (0 for entire buffer)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_invalidate(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size
);

/**
 * @brief Synchronize buffer across GPUs
 * 
 * Ensures all GPUs can see the latest data.
 * 
 * @param buffer Buffer to synchronize
 * @param fence Fence to signal on completion (optional)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_sync(mvgal_buffer_t buffer, mvgal_fence_t fence);

/**
 * @brief Replicate buffer across multiple GPUs
 * 
 * @param buffer Buffer to replicate
 * @param gpu_count Number of GPUs to replicate to
 * @param gpu_indices Array of GPU indices
 * @param fence Fence to signal on completion
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_replicate(
    mvgal_buffer_t buffer,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_fence_t fence
);

/**
 * @brief Set buffer access rights for a GPU
 * 
 * @param buffer Buffer
 * @param gpu_index GPU index
 * @param access Access flags
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_set_access(
    mvgal_buffer_t buffer,
    uint32_t gpu_index,
    mvgal_memory_access_flags_t access
);

/**
 * @brief Get buffer access rights for a GPU
 * 
 * @param buffer Buffer
 * @param gpu_index GPU index
 * @param access Access flags (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_get_access(
    mvgal_buffer_t buffer,
    uint32_t gpu_index,
    mvgal_memory_access_flags_t *access
);

/**
 * @brief Register a callback for buffer access
 * 
 * @param buffer Buffer to monitor
 * @param callback Callback function
 * @param user_data User data
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_register_callback(
    mvgal_buffer_t buffer,
    mvgal_memory_callback_t callback,
    void *user_data
);

/**
 * @brief Unregister a buffer callback
 * 
 * @param buffer Buffer
 * @param callback Callback function to remove
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_unregister_callback(
    mvgal_buffer_t buffer,
    mvgal_memory_callback_t callback
);

/**
 * @brief Get buffer statistics
 * 
 * @param buffer Buffer to query
 * @param bytes_read Bytes read (out)
 * @param bytes_written Bytes written (out)
 * @param gpu_access_count Access count per GPU (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_get_stats(
    mvgal_buffer_t buffer,
    uint64_t *bytes_read,
    uint64_t *bytes_written,
    uint64_t *gpu_access_count
);

/**
 * @brief Create a buffer from existing memory
 * 
 * Wraps existing memory (e.g., from malloc, mmap) in a MVGAL buffer.
 * 
 * @param context Context
 * @param ptr Pointer to existing memory
 * @param size Size of memory
 * @param flags Buffer flags
 * @param buffer Buffer handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_wrap(
    void *context,
    void *ptr,
    size_t size,
    mvgal_memory_flags_t flags,
    mvgal_buffer_t *buffer
);

/**
 * @brief Check if buffer is accessible by a GPU
 * 
 * @param buffer Buffer to check
 * @param gpu_index GPU index
 * @return true if accessible, false otherwise
 */
bool mvgal_memory_is_accessible(mvgal_buffer_t buffer, uint32_t gpu_index);

/**
 * @brief Get GPU address for buffer
 * 
 * @param buffer Buffer
 * @param gpu_index GPU index
 * @param address GPU address (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_get_gpu_address(
    mvgal_buffer_t buffer,
    uint32_t gpu_index,
    uint64_t *address
);

/**
 * @brief Create a shared buffer across multiple GPUs
 * 
 * @param context Context
 * @param size Size in bytes
 * @param gpu_count Number of GPUs to share with
 * @param gpu_indices Array of GPU indices
 * @param buffer Buffer handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_memory_create_shared(
    void *context,
    size_t size,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_buffer_t *buffer
);

/** @} */ // end of MemoryManagement

#ifdef __cplusplus
}
#endif

#endif // MVGAL_MEMORY_H
