/**
 * @file memory.c
 * @brief Memory management API implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements the public memory management API defined in
 * mvgal_memory.h.
 */

#include "memory_internal.h"
#include "mvgal_log.h"
#include "mvgal_gpu.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/**
 * @brief Ensure memory module is initialized
 */
static mvgal_error_t ensure_memory_initialized(void) {
    if (!mvgal_memory_get_state()->initialized) {
        return mvgal_memory_module_init();
    }
    return MVGAL_SUCCESS;
}

// ============================================================================
// Allocation Functions
// ============================================================================

/**
 * @brief Allocate memory buffer
 */
mvgal_error_t mvgal_memory_allocate(
    void *context,
    const mvgal_memory_alloc_info_t *alloc_info,
    mvgal_buffer_t *buffer
) {
    (void)context; // Context unused for now
    
    if (alloc_info == NULL || buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_error_t err = ensure_memory_initialized();
    if (err != MVGAL_SUCCESS) {
        return err;
    }
    
    struct mvgal_buffer *buf = NULL;
    err = mvgal_buffer_allocate_internal(alloc_info, &buf);
    if (err != MVGAL_SUCCESS) {
        return err;
    }
    
    // Add to global buffer list
    pthread_mutex_lock(&mvgal_memory_get_state()->lock);
    buf->next = mvgal_memory_get_state()->buffers;
    if (mvgal_memory_get_state()->buffers != NULL) {
        mvgal_memory_get_state()->buffers->prev = buf;
    }
    mvgal_memory_get_state()->buffers = buf;
    mvgal_memory_get_state()->total_allocated += buf->size;
    pthread_mutex_unlock(&mvgal_memory_get_state()->lock);
    
    *buffer = buf;
    MVGAL_LOG_DEBUG("Memory allocated: buffer=%p, size=%zu", (void *)buf, buf->size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Allocate memory with explicit size
 */
mvgal_error_t mvgal_memory_allocate_simple(
    void *context,
    size_t size,
    mvgal_memory_flags_t flags,
    mvgal_buffer_t *buffer
) {
    (void)context;
    
    if (buffer == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_memory_alloc_info_t alloc_info = {
        .size = size,
        .alignment = 0,
        .flags = flags | MVGAL_MEMORY_FLAG_HOST_VALID,
        .memory_type = MVGAL_MEMORY_TYPE_SRAM,
        .sharing_mode = MVGAL_MEMORY_SHARING_NONE,
        .access = MVGAL_MEMORY_ACCESS_READ_WRITE,
        .gpu_mask = 0,
        .priority = 50
    };
    
    return mvgal_memory_allocate(context, &alloc_info, buffer);
}

/**
 * @brief Free a memory buffer
 */
void mvgal_memory_free(mvgal_buffer_t buffer) {
    if (buffer == NULL) {
        return;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    mvgal_buffer_release(buf);
}

/**
 * @brief Get buffer descriptor
 */
mvgal_error_t mvgal_memory_get_descriptor(
    mvgal_buffer_t buffer,
    mvgal_memory_descriptor_t *desc
) {
    if (buffer == NULL || desc == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    desc->size = buf->size;
    desc->alignment = buf->alignment;
    desc->flags = buf->flags;
    desc->memory_type = buf->memory_type;
    desc->sharing_mode = buf->sharing_mode;
    desc->gpu_mask = buf->gpu_mask;
    desc->dmabuf_fd = buf->dmabuf_fd;
    desc->dmabuf_offset = buf->dmabuf_offset;
    desc->host_ptr = buf->host_ptr;
    desc->fence = buf->fence;
    desc->bytes_transferred = atomic_load(&buf->bytes_transferred);
    desc->access_count = 0;
    for (uint32_t i = 0; i < MVGAL_MAX_GPUS; i++) {
        desc->access_count += atomic_load(&buf->access_count[i]);
    }
    
    // Copy GPU addresses
    for (uint32_t i = 0; i < MVGAL_MAX_GPUS; i++) {
        desc->gpu_address[i] = buf->gpu_bindings[i].gpu_address;
    }
    
    desc->buffer = buf;
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Create a shared buffer across multiple GPUs
 */
mvgal_error_t mvgal_memory_create_shared(
    void *context,
    size_t size,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_buffer_t *buffer
) {
    (void)context;
    
    if (buffer == NULL || size == 0 || gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Build GPU mask from indices
    uint32_t gpu_mask = 0;
    for (uint32_t i = 0; i < gpu_count; i++) {
        if (gpu_indices[i] < 32) { // Only support first 32 GPUs
            gpu_mask |= (1 << gpu_indices[i]);
        }
    }
    
    mvgal_memory_alloc_info_t alloc_info = {
        .size = size,
        .alignment = 0,
        .flags = MVGAL_MEMORY_FLAG_SHARED | MVGAL_MEMORY_FLAG_DMA_BUF,
        .memory_type = MVGAL_MEMORY_TYPE_SRAM,
        .sharing_mode = MVGAL_MEMORY_SHARING_DMA_BUF,
        .access = MVGAL_MEMORY_ACCESS_READ_WRITE,
        .gpu_mask = gpu_mask,
        .priority = 50
    };
    
    return mvgal_memory_allocate(context, &alloc_info, buffer);
}

/**
 * @brief Wrap existing memory in a MVGAL buffer
 */
mvgal_error_t mvgal_memory_wrap(
    void *context,
    void *ptr,
    size_t size,
    mvgal_memory_flags_t flags,
    mvgal_buffer_t *buffer
) {
    (void)context;
    
    if (buffer == NULL || ptr == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_error_t err = ensure_memory_initialized();
    if (err != MVGAL_SUCCESS) {
        return err;
    }
    
    struct mvgal_buffer *buf = calloc(1, sizeof(struct mvgal_buffer));
    if (buf == NULL) {
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    // Set up buffer
    buf->size = size;
    buf->alignment = 0;
    buf->flags = flags | MVGAL_MEMORY_FLAG_HOST_VALID;
    buf->memory_type = MVGAL_MEMORY_TYPE_SRAM;
    buf->sharing_mode = MVGAL_MEMORY_SHARING_NONE;
    buf->access = MVGAL_MEMORY_ACCESS_READ_WRITE;
    buf->gpu_mask = 0xFFFFFFFF; // All GPUs potentially
    buf->priority = 50;
    buf->host_ptr = ptr;
    buf->original_ptr = ptr;
    buf->should_free = false; // Don't free original pointer
    buf->backend = MVGAL_BUFFER_BACKEND_WRAPPED;
    buf->state = MVGAL_BUFFER_STATE_ALLOCATED | MVGAL_BUFFER_STATE_MAPPED;
    buf->dmabuf_fd = -1;
    buf->fence = NULL;
    buf->callback = NULL;
    buf->callback_user_data = NULL;
    buf->gpu_binding_count = 0;
    
    atomic_init(&buf->refcount, 1);
    for (uint32_t i = 0; i < MVGAL_MAX_GPUS; i++) {
        atomic_init(&buf->access_count[i], 0);
    }
    atomic_init(&buf->bytes_read, 0);
    atomic_init(&buf->bytes_written, 0);
    atomic_init(&buf->bytes_transferred, 0);
    
    // Add to global buffer list
    pthread_mutex_lock(&mvgal_memory_get_state()->lock);
    buf->next = mvgal_memory_get_state()->buffers;
    if (mvgal_memory_get_state()->buffers != NULL) {
        mvgal_memory_get_state()->buffers->prev = buf;
    }
    mvgal_memory_get_state()->buffers = buf;
    pthread_mutex_unlock(&mvgal_memory_get_state()->lock);
    
    *buffer = buf;
    MVGAL_LOG_DEBUG("Memory wrapped: buffer=%p, ptr=%p, size=%zu", (void *)buf, ptr, size);
    
    return MVGAL_SUCCESS;
}

// ============================================================================
// Mapping Functions
// ============================================================================

/**
 * @brief Map buffer into CPU address space
 */
mvgal_error_t mvgal_memory_map(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size,
    void **ptr
) {
    if (buffer == NULL || ptr == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    // If already mapped, return the existing pointer
    if (buf->state & MVGAL_BUFFER_STATE_MAPPED) {
        *ptr = (char *)buf->host_ptr + offset;
        return MVGAL_SUCCESS;
    }
    
    // For DMA-BUF, map it
    if (buf->dmabuf_fd >= 0) {
        if (size == 0) {
            size = buf->size;
        }
        
        void *mapped_ptr = NULL;
        mvgal_error_t err = mvgal_dmabuf_map(buf->dmabuf_fd, size, offset, &mapped_ptr);
        if (err != MVGAL_SUCCESS) {
            return err;
        }
        
        buf->host_ptr = mapped_ptr;
        buf->host_offset = offset;
        buf->state |= MVGAL_BUFFER_STATE_MAPPED;
        
        *ptr = mapped_ptr;
        return MVGAL_SUCCESS;
    }
    
    // For system memory that's not host-valid, create a mapping
    if (!(buf->flags & MVGAL_MEMORY_FLAG_HOST_VALID)) {
        // Create a temporary mapping
        // In a real implementation, this would map GPU memory to CPU
        MVGAL_LOG_ERROR("Cannot map non-host-valid buffer without GPU support");
        return MVGAL_ERROR_NOT_SUPPORTED;
    }
    
    *ptr = (char *)buf->host_ptr + offset;
    return MVGAL_SUCCESS;
}

/**
 * @brief Unmap buffer from CPU address space
 */
void mvgal_memory_unmap(mvgal_buffer_t buffer) {
    if (buffer == NULL) {
        return;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    if (buf->state & MVGAL_BUFFER_STATE_MAPPED) {
        if (buf->dmabuf_fd >= 0 && buf->dmabuf_owner) {
            // Only unmap if we own the DMA-BUF
            // For imported buffers, the owner is responsible for unmapping
            mvgal_dmabuf_unmap(buf->dmabuf_fd, buf->host_ptr, buf->size);
        }
        buf->host_ptr = NULL;
        buf->state &= (uint32_t)~MVGAL_BUFFER_STATE_MAPPED;
    }
}

/**
 * @brief Check if buffer is mapped
 */
bool mvgal_memory_is_mapped(mvgal_buffer_t buffer) {
    if (buffer == NULL) {
        return false;
    }
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    return (buf->state & MVGAL_BUFFER_STATE_MAPPED) != 0;
}

/**
 * @brief Get pointer to mapped buffer
 */
void *mvgal_memory_get_pointer(mvgal_buffer_t buffer) {
    if (buffer == NULL) {
        return NULL;
    }
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    return buf->host_ptr;
}

// ============================================================================
// Data Access Functions
// ============================================================================

/**
 * @brief Write data to buffer
 */
mvgal_error_t mvgal_memory_write(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size,
    const void *data
) {
    if (buffer == NULL || data == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    // Check if mapped
    if (!(buf->state & MVGAL_BUFFER_STATE_MAPPED) && buf->host_ptr == NULL) {
        // Try to map it
        void *ptr = NULL;
        mvgal_error_t err = mvgal_memory_map(buffer, offset, size, &ptr);
        if (err != MVGAL_SUCCESS) {
            return err;
        }
    }
    
    // Check bounds
    if (offset + size > buf->size) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Copy data
    memcpy((char *)buf->host_ptr + offset, data, size);
    
    // Update statistics
    atomic_fetch_add(&buf->bytes_written, size);
    
    // Flush if needed
    if (buf->flags & MVGAL_MEMORY_FLAG_GPU_VALID) {
        // In a real implementation, this would flush to GPU
        // For now, just mark as needing flush
        // mvgal_memory_flush(buffer, offset, size);
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Read data from buffer
 */
mvgal_error_t mvgal_memory_read(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size,
    void *data
) {
    if (buffer == NULL || data == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    // Check if mapped
    if (!(buf->state & MVGAL_BUFFER_STATE_MAPPED) && buf->host_ptr == NULL) {
        void *ptr = NULL;
        mvgal_error_t err = mvgal_memory_map(buffer, offset, size, &ptr);
        if (err != MVGAL_SUCCESS) {
            return err;
        }
    }
    
    // Check bounds
    if (offset + size > buf->size) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Copy data
    memcpy(data, (char *)buf->host_ptr + offset, size);
    
    // Update statistics
    atomic_fetch_add(&buf->bytes_read, size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Flush buffer writes to make them visible to GPUs
 */
mvgal_error_t mvgal_memory_flush(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size
) {
    if (buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    // For DMA-BUF, use msync to ensure writes are visible
    if (buf->dmabuf_fd >= 0 && buf->host_ptr != NULL) {
        if (size == 0) {
            size = buf->size;
        }
        if (msync((char *)buf->host_ptr + offset, size, MS_ASYNC) != 0) {
            MVGAL_LOG_WARN("msync failed: %s", strerror(errno));
            return MVGAL_ERROR_DRIVER;
        }
    }
    
    // In a real implementation, this would also flush GPU caches
    // For now, DMA-BUF synchronization is handled by the kernel
    
    (void)offset; // May be unused if size is 0
    return MVGAL_SUCCESS;
}

/**
 * @brief Invalidate buffer cache to read GPU writes
 */
mvgal_error_t mvgal_memory_invalidate(
    mvgal_buffer_t buffer,
    uint64_t offset,
    size_t size
) {
    if (buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    // For DMA-BUF, invalidate CPU cache
    if (buf->dmabuf_fd >= 0 && buf->host_ptr != NULL) {
        if (size == 0) {
            size = buf->size;
        }
        if (msync((char *)buf->host_ptr + offset, size, MS_INVALIDATE) != 0) {
            MVGAL_LOG_WARN("msync invalidate failed: %s", strerror(errno));
            return MVGAL_ERROR_DRIVER;
        }
    }
    
    // In a real implementation, this would invalidate GPU caches
    
    (void)offset; // May be unused if size is 0
    return MVGAL_SUCCESS;
}

// ============================================================================
// Copy Functions
// ============================================================================

/**
 * @brief Copy memory between buffers
 */
mvgal_error_t mvgal_memory_copy(
    void *context,
    const mvgal_memory_copy_region_t *regions,
    uint32_t region_count,
    mvgal_fence_t fence
) {
    (void)context;
    (void)fence; // Fence unused in sync implementation
    
    if (regions == NULL || region_count == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_error_t overall_err = MVGAL_SUCCESS;
    
    for (uint32_t i = 0; i < region_count; i++) {
        const mvgal_memory_copy_region_t *region = &regions[i];
        
        if (region->src_buffer == NULL || region->dst_buffer == NULL) {
            overall_err = MVGAL_ERROR_INVALID_ARGUMENT;
            continue;
        }
        
        if (region->size == 0) {
            continue;
        }
        
        struct mvgal_buffer *src = (struct mvgal_buffer *)region->src_buffer;
        struct mvgal_buffer *dst = (struct mvgal_buffer *)region->dst_buffer;
        
        // Check bounds
        if (region->src_offset + region->size > src->size ||
            region->dst_offset + region->size > dst->size) {
            overall_err = MVGAL_ERROR_INVALID_ARGUMENT;
            continue;
        }
        
        // Ensure both buffers are mapped
        void *src_ptr = NULL;
        void *dst_ptr = NULL;
        
        mvgal_error_t err = mvgal_memory_map(src, region->src_offset, region->size, &src_ptr);
        if (err != MVGAL_SUCCESS) {
            overall_err = err;
            continue;
        }
        
        err = mvgal_memory_map(dst, region->dst_offset, region->size, &dst_ptr);
        if (err != MVGAL_SUCCESS) {
            mvgal_memory_unmap(src);
            overall_err = err;
            continue;
        }
        
        // Perform the copy
        memcpy((char *)dst_ptr + region->dst_offset,
               (char *)src_ptr + region->src_offset,
               region->size);
        
        // Update statistics
        atomic_fetch_add(&src->bytes_read, region->size);
        atomic_fetch_add(&dst->bytes_written, region->size);
        atomic_fetch_add(&src->bytes_transferred, region->size);
        atomic_fetch_add(&dst->bytes_transferred, region->size);
        
        // Clean up mappings if we created them
        if (!(src->state & MVGAL_BUFFER_STATE_MAPPED)) {
            mvgal_memory_unmap(src);
        }
        if (!(dst->state & MVGAL_BUFFER_STATE_MAPPED)) {
            mvgal_memory_unmap(dst);
        }
    }
    
    return overall_err;
}

/**
 * @brief Copy memory with GPU-to-GPU transfer
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
) {
    (void)context;

    if (src_buffer == NULL || dst_buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    if (size == 0) {
        return MVGAL_SUCCESS;
    }

    struct mvgal_buffer *src = (struct mvgal_buffer *)src_buffer;
    struct mvgal_buffer *dst = (struct mvgal_buffer *)dst_buffer;
    mvgal_memory_copy_method_t method = mvgal_dmabuf_get_copy_method(src_gpu_index, dst_gpu_index);

    if (method != MVGAL_MEMORY_COPY_CPU) {
        mvgal_error_t err = mvgal_dmabuf_copy_gpu_to_gpu(
            src,
            src_offset,
            dst,
            dst_offset,
            size,
            src_gpu_index,
            dst_gpu_index
        );
        if (err == MVGAL_SUCCESS) {
            if (fence != NULL) {
                mvgal_fence_signal_with_status(fence, MVGAL_SUCCESS);
            }
            return MVGAL_SUCCESS;
        }
    }

    mvgal_memory_copy_region_t region = {
        .src_buffer = src_buffer,
        .src_offset = src_offset,
        .dst_buffer = dst_buffer,
        .dst_offset = dst_offset,
        .size = size
    };

    mvgal_error_t err = mvgal_memory_copy(context, &region, 1, fence);
    if (err == MVGAL_SUCCESS && fence != NULL) {
        mvgal_fence_signal_with_status(fence, MVGAL_SUCCESS);
    }
    return err;
}

// ============================================================================
// DMA-BUF Functions
// ============================================================================

/**
 * @brief Export buffer as DMA-BUF
 */
mvgal_error_t mvgal_memory_export_dmabuf(mvgal_buffer_t buffer, int *fd) {
    if (buffer == NULL || fd == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    return mvgal_dmabuf_export(buf, fd);
}

/**
 * @brief Import DMA-BUF as buffer
 */
mvgal_error_t mvgal_memory_import_dmabuf(
    void *context,
    int fd,
    mvgal_buffer_t *buffer
) {
    (void)context;
    
    if (buffer == NULL || fd < 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_memory_state_t *state = mvgal_memory_get_state();
    
    // Determine size from FD
    struct stat buf_stat;
    if (fstat(fd, &buf_stat) != 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    size_t size = (size_t)buf_stat.st_size;
    
    // Import as DMA-BUF
    struct mvgal_buffer *buf = NULL;
    mvgal_error_t err = mvgal_dmabuf_import(fd, size, MVGAL_MEMORY_FLAG_DMA_BUF, &buf);
    if (err != MVGAL_SUCCESS) {
        return err;
    }
    
    // Add to global buffer list
    pthread_mutex_lock(&state->lock);
    buf->next = state->buffers;
    if (state->buffers != NULL) {
        state->buffers->prev = buf;
    }
    state->buffers = buf;
    state->total_allocated += buf->size;
    pthread_mutex_unlock(&state->lock);
    
    *buffer = buf;
    return MVGAL_SUCCESS;
}

// ============================================================================
// Synchronization Functions
// ============================================================================

/**
 * @brief Synchronize buffer across GPUs
 */
mvgal_error_t mvgal_memory_sync(mvgal_buffer_t buffer, mvgal_fence_t fence) {
    (void)fence; // In real implementation, this would use the fence
    
    if (buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Flush any pending writes
    mvgal_error_t err = mvgal_memory_flush(buffer, 0, 0);
    if (err != MVGAL_SUCCESS) {
        return err;
    }
    
    // In a real implementation, this would insert synchronization
    // operations for each GPU that has accessed the buffer
    
    // Mark as synchronized
    // In real implementation, fence would be signaled when sync is complete
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Replicate buffer across multiple GPUs
 */
mvgal_error_t mvgal_memory_replicate(
    mvgal_buffer_t buffer,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_fence_t fence
) {
    if (buffer == NULL || gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    return mvgal_buffer_replicate(buf, gpu_count, gpu_indices, fence);
}

// ============================================================================
// Access Control Functions
// ============================================================================

/**
 * @brief Set buffer access rights for a GPU
 */
mvgal_error_t mvgal_memory_set_access(
    mvgal_buffer_t buffer,
    uint32_t gpu_index,
    mvgal_memory_access_flags_t access
) {
    if (buffer == NULL || gpu_index >= MVGAL_MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    // Update GPU mask
    if (access != 0) {
        buf->gpu_mask |= (uint32_t)(1U << gpu_index);
    } else {
        buf->gpu_mask &= (uint32_t)~(1U << gpu_index);
    }
    
    // Update binding
    for (uint32_t i = 0; i < buf->gpu_binding_count; i++) {
        if (buf->gpu_bindings[i].gpu_index == gpu_index) {
            buf->gpu_bindings[i].readable = 
                (access & (MVGAL_MEMORY_ACCESS_READ | MVGAL_MEMORY_ACCESS_READ_WRITE)) != 0;
            buf->gpu_bindings[i].writable = 
                (access & (MVGAL_MEMORY_ACCESS_WRITE | MVGAL_MEMORY_ACCESS_READ_WRITE)) != 0;
            return MVGAL_SUCCESS;
        }
    }
    
    // Add new binding if needed
    if (access != 0) {
        bool writable = (access & (MVGAL_MEMORY_ACCESS_WRITE | MVGAL_MEMORY_ACCESS_READ_WRITE)) != 0;
        mvgal_buffer_bind_to_gpu(buf, gpu_index, writable);
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get buffer access rights for a GPU
 */
mvgal_error_t mvgal_memory_get_access(
    mvgal_buffer_t buffer,
    uint32_t gpu_index,
    mvgal_memory_access_flags_t *access
) {
    if (buffer == NULL || access == NULL || gpu_index >= MVGAL_MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    // Check if GPU is in mask
    if (!(buf->gpu_mask & (1 << gpu_index))) {
        *access = 0; // No access
        return MVGAL_SUCCESS;
    }
    
    // Check binding
    for (uint32_t i = 0; i < buf->gpu_binding_count; i++) {
        if (buf->gpu_bindings[i].gpu_index == gpu_index) {
            *access = 0;
            if (buf->gpu_bindings[i].readable) {
                *access |= MVGAL_MEMORY_ACCESS_READ;
            }
            if (buf->gpu_bindings[i].writable) {
                *access |= MVGAL_MEMORY_ACCESS_WRITE;
            }
            return MVGAL_SUCCESS;
        }
    }
    
    // Default to read-write if GPU is in mask
    *access = MVGAL_MEMORY_ACCESS_READ_WRITE;
    return MVGAL_SUCCESS;
}

/**
 * @brief Check if buffer is accessible by a GPU
 */
bool mvgal_memory_is_accessible(mvgal_buffer_t buffer, uint32_t gpu_index) {
    if (buffer == NULL || gpu_index >= MVGAL_MAX_GPUS) {
        return false;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    return (buf->gpu_mask & (1 << gpu_index)) != 0;
}

/**
 * @brief Get GPU address for buffer
 */
mvgal_error_t mvgal_memory_get_gpu_address(
    mvgal_buffer_t buffer,
    uint32_t gpu_index,
    uint64_t *address
) {
    if (buffer == NULL || address == NULL || gpu_index >= MVGAL_MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    return mvgal_buffer_get_gpu_address(buf, gpu_index, address);
}

// ============================================================================
// Callback Functions
// ============================================================================

/**
 * @brief Register a callback for buffer access
 */
mvgal_error_t mvgal_memory_register_callback(
    mvgal_buffer_t buffer,
    mvgal_memory_callback_t callback,
    void *user_data
) {
    if (buffer == NULL || callback == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    buf->callback = callback;
    buf->callback_user_data = user_data;
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Unregister a buffer callback
 */
mvgal_error_t mvgal_memory_unregister_callback(
    mvgal_buffer_t buffer,
    mvgal_memory_callback_t callback
) {
    if (buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    if (buf->callback == callback) {
        buf->callback = NULL;
        buf->callback_user_data = NULL;
    }
    
    return MVGAL_SUCCESS;
}

// ============================================================================
// Statistics Functions
// ============================================================================

/**
 * @brief Get buffer statistics
 */
mvgal_error_t mvgal_memory_get_stats(
    mvgal_buffer_t buffer,
    uint64_t *bytes_read,
    uint64_t *bytes_written,
    uint64_t *gpu_access_count
) {
    if (buffer == NULL || bytes_read == NULL || bytes_written == NULL || 
        gpu_access_count == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_buffer *buf = (struct mvgal_buffer *)buffer;
    
    *bytes_read = atomic_load(&buf->bytes_read);
    *bytes_written = atomic_load(&buf->bytes_written);
    
    uint64_t total_access = 0;
    for (uint32_t i = 0; i < MVGAL_MAX_GPUS; i++) {
        total_access += atomic_load(&buf->access_count[i]);
    }
    *gpu_access_count = total_access;
    
    return MVGAL_SUCCESS;
}
