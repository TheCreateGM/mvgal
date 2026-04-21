/**
 * @file allocator.c
 * @brief Memory allocator implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements the core memory allocation and management.
 * It handles system memory allocations and interfaces with backend-specific
 * allocators (DMA-BUF, GPU-specific, etc.)
 */

#include "memory_internal.h"
#include "mvgal_gpu.h"
#include "mvgal_log.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// Global memory state
static mvgal_memory_state_t g_memory_state = {0};

/**
 * @brief Get the global memory state
 */
mvgal_memory_state_t *mvgal_memory_get_state(void) {
    return &g_memory_state;
}

/**
 * @brief Initialize memory module
 */
mvgal_error_t mvgal_memory_module_init(void) {
    if (g_memory_state.initialized) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }
    
    pthread_mutex_init(&g_memory_state.lock, NULL);
    g_memory_state.buffers = NULL;
    g_memory_state.total_allocated = 0;
    g_memory_state.total_freed = 0;
    
    // Initialize DMA-BUF heaps
    g_memory_state.system_heap.fd = -1;
    g_memory_state.system_heap.name = "system";
    g_memory_state.gpu_heap_count = 0;
    for (uint32_t i = 0; i < MVGAL_MAX_GPUS; i++) {
        g_memory_state.gpu_heaps[i].fd = -1;
        g_memory_state.gpu_heaps[i].name = NULL;
    }
    
    // Initialize GPU cache
    g_memory_state.gpu_count = 0;
    
    // Try to open system DMA-BUF heap
    int fd = open("/dev/dma_heap/system", O_RDWR);
    if (fd >= 0) {
        g_memory_state.system_heap.fd = fd;
        MVGAL_LOG_INFO("Opened system DMA-BUF heap at /dev/dma_heap/system");
    } else {
        MVGAL_LOG_DEBUG("System DMA-BUF heap not available (this is normal on older kernels)");
    }
    
    // Scan for GPUs
    int32_t count = mvgal_gpu_get_count();
    if (count > 0) {
        g_memory_state.gpu_count = (uint32_t)count;
        mvgal_gpu_enumerate(g_memory_state.gpus, MVGAL_MAX_GPUS);
        MVGAL_LOG_INFO("Memory module detected %d GPUs", g_memory_state.gpu_count);
    } else {
        MVGAL_LOG_WARN("No GPUs detected by memory module");
    }
    
    g_memory_state.initialized = true;
    MVGAL_LOG_INFO("Memory module initialized");
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Shutdown memory module
 */
void mvgal_memory_module_shutdown(void) {
    if (!g_memory_state.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_memory_state.lock);
    
    // Close DMA-BUF heap
    if (g_memory_state.system_heap.fd >= 0) {
        close(g_memory_state.system_heap.fd);
        g_memory_state.system_heap.fd = -1;
    }
    
    // Close GPU heaps
    for (uint32_t i = 0; i < g_memory_state.gpu_heap_count; i++) {
        if (g_memory_state.gpu_heaps[i].fd >= 0) {
            close(g_memory_state.gpu_heaps[i].fd);
            g_memory_state.gpu_heaps[i].fd = -1;
        }
    }
    
    // Free remaining buffers
    struct mvgal_buffer *buffer = g_memory_state.buffers;
    while (buffer != NULL) {
        struct mvgal_buffer *next = buffer->next;
        mvgal_buffer_free_internal(buffer);
        buffer = next;
    }
    
    g_memory_state.buffers = NULL;
    g_memory_state.initialized = false;
    
    pthread_mutex_unlock(&g_memory_state.lock);
    pthread_mutex_destroy(&g_memory_state.lock);
    
    MVGAL_LOG_INFO("Memory module shut down");
}

/**
 * @brief Increment buffer reference count
 */
void mvgal_buffer_retain(struct mvgal_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    atomic_fetch_add(&buffer->refcount, 1);
    MVGAL_LOG_DEBUG("Buffer %p retained, refcount: %u", (void *)buffer, 
                   atomic_load(&buffer->refcount));
}

/**
 * @brief Decrement buffer reference count and possibly free
 */
void mvgal_buffer_release(struct mvgal_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    
    uint32_t old_count = atomic_fetch_sub(&buffer->refcount, 1);
    if (old_count == 1) {
        mvgal_buffer_free_internal(buffer);
    }
}

/**
 * @brief Allocate buffer internal
 */
mvgal_error_t mvgal_buffer_allocate_internal(
    const mvgal_memory_alloc_info_t *alloc_info,
    struct mvgal_buffer **buffer_out
) {
    if (alloc_info == NULL || buffer_out == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (alloc_info->size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Align size to alignment requirement
    size_t aligned_size = alloc_info->size;
    if (alloc_info->alignment > 0) {
        aligned_size = (aligned_size + alloc_info->alignment - 1) & 
                      ~(alloc_info->alignment - 1);
    }
    
    struct mvgal_buffer *buffer = calloc(1, sizeof(struct mvgal_buffer));
    if (buffer == NULL) {
        MVGAL_LOG_ERROR("Failed to allocate buffer structure");
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    // Set up buffer state
    buffer->size = alloc_info->size;
    buffer->alignment = alloc_info->alignment;
    buffer->flags = alloc_info->flags;
    buffer->memory_type = alloc_info->memory_type;
    buffer->sharing_mode = alloc_info->sharing_mode;
    buffer->access = alloc_info->access;
    buffer->gpu_mask = alloc_info->gpu_mask;
    buffer->priority = alloc_info->priority;
    buffer->host_ptr = NULL;
    buffer->host_offset = 0;
    buffer->dmabuf_fd = -1;
    buffer->dmabuf_offset = 0;
    buffer->dmabuf_owner = false;
    buffer->fence = NULL;
    buffer->callback = NULL;
    buffer->callback_user_data = NULL;
    buffer->original_ptr = NULL;
    buffer->should_free = false;
    buffer->gpu_binding_count = 0;
    
    atomic_init(&buffer->refcount, 1);
    for (uint32_t i = 0; i < MVGAL_MAX_GPUS; i++) {
        atomic_init(&buffer->access_count[i], 0);
    }
    atomic_init(&buffer->bytes_read, 0);
    atomic_init(&buffer->bytes_written, 0);
    atomic_init(&buffer->bytes_transferred, 0);
    
    // Try DMA-BUF allocation first if requested or if GPU mask has multiple GPUs
    if ((alloc_info->flags & MVGAL_MEMORY_FLAG_DMA_BUF) ||
        (alloc_info->sharing_mode == MVGAL_MEMORY_SHARING_DMA_BUF) ||
        (alloc_info->gpu_mask != 0 && __builtin_popcount(alloc_info->gpu_mask) > 1)) {
        
        if (mvgal_dmabuf_is_supported(alloc_info->gpu_mask)) {
            int fd = -1;
            void *ptr = NULL;
            
            mvgal_error_t err = mvgal_dmabuf_allocate(
                aligned_size,
                alloc_info->alignment,
                alloc_info->flags,
                alloc_info->gpu_mask,
                &fd,
                &ptr
            );
            
            if (err == MVGAL_SUCCESS && fd >= 0 && ptr != NULL) {
                buffer->host_ptr = ptr;
                buffer->dmabuf_fd = fd;
                buffer->dmabuf_owner = true;
                buffer->backend = MVGAL_BUFFER_BACKEND_DMA_BUF;
                buffer->state = MVGAL_BUFFER_STATE_ALLOCATED | 
                               MVGAL_BUFFER_STATE_MAPPED;
                
                // Map to all GPUs in the mask
                for (uint32_t i = 0; i < MVGAL_MAX_GPUS; i++) {
                    if (alloc_info->gpu_mask & (1 << i)) {
                        // For now, just store the CPU pointer
                        // GPU-specific addressing would be handled by the driver
                        buffer->gpu_bindings[i].gpu_index = i;
                        buffer->gpu_bindings[i].mapping = ptr;
                        buffer->gpu_bindings[i].readable = true;
                        buffer->gpu_bindings[i].writable = 
                            (alloc_info->access & MVGAL_MEMORY_ACCESS_WRITE) != 0;
                        buffer->gpu_binding_count++;
                    }
                }
                
                MVGAL_LOG_DEBUG("Allocated DMA-BUF buffer: size=%zu, fd=%d, ptr=%p",
                               aligned_size, fd, ptr);
                *buffer_out = buffer;
                return MVGAL_SUCCESS;
            }
            
            // DMA-BUF allocation failed, fall through to system allocation
            MVGAL_LOG_DEBUG("DMA-BUF allocation failed, falling back to system memory");
        }
    }
    
    // Fall back to system memory allocation
    void *ptr = NULL;
    
    if (alloc_info->flags & MVGAL_MEMORY_FLAG_HOST_VALID) {
        // Host-visible allocations should be directly CPU-accessible and
        // freeable through the normal system allocator path.
        if (alloc_info->alignment > sizeof(void *) && alloc_info->alignment != 0) {
            int ret = posix_memalign(&ptr, alloc_info->alignment, aligned_size);
            if (ret != 0) {
                ptr = NULL;
            }
        } else {
            ptr = malloc(aligned_size);
        }
    } else {
        // Use regular malloc
        ptr = malloc(aligned_size);
    }
    
    if (ptr == NULL) {
        MVGAL_LOG_ERROR("Failed to allocate %zu bytes", aligned_size);
        free(buffer);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    // Zero-initialize if requested
    if (alloc_info->flags & MVGAL_MEMORY_FLAG_ZERO_INITIALIZED) {
        memset(ptr, 0, aligned_size);
    }
    
    buffer->host_ptr = ptr;
    buffer->original_ptr = ptr;
    buffer->should_free = true;
    buffer->backend = MVGAL_BUFFER_BACKEND_SYSTEM;
    buffer->state = MVGAL_BUFFER_STATE_ALLOCATED;
    
    if (alloc_info->flags & MVGAL_MEMORY_FLAG_HOST_VALID) {
        buffer->state |= MVGAL_BUFFER_STATE_MAPPED;
    }
    
    MVGAL_LOG_DEBUG("Allocated system buffer: size=%zu, ptr=%p", 
                   aligned_size, ptr);
    
    *buffer_out = buffer;
    return MVGAL_SUCCESS;
}

/**
 * @brief Free buffer internal
 */
void mvgal_buffer_free_internal(struct mvgal_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    
    MVGAL_LOG_DEBUG("Freeing buffer: %p, ptr=%p, backend=%d", 
                   (void *)buffer, buffer->host_ptr, buffer->backend);
    
    // Unbind from all GPUs
    mvgal_buffer_unbind(buffer);
    
    // Free DMA-BUF if we own it
    if (buffer->dmabuf_owner && buffer->dmabuf_fd >= 0) {
        mvgal_dmabuf_free(buffer->dmabuf_fd, buffer->host_ptr, buffer->size);
        buffer->dmabuf_fd = -1;
        buffer->host_ptr = NULL;
    }
    
    // Free system memory if we own it
    if (buffer->should_free && buffer->original_ptr != NULL) {
        free(buffer->original_ptr);
        buffer->original_ptr = NULL;
    }
    
    // Release fence if any
    if (buffer->fence != NULL) {
        mvgal_fence_destroy(buffer->fence);
        buffer->fence = NULL;
    }
    
    // Remove from global list
    pthread_mutex_lock(&g_memory_state.lock);
    if (buffer->prev != NULL) {
        buffer->prev->next = buffer->next;
    } else {
        g_memory_state.buffers = buffer->next;
    }
    if (buffer->next != NULL) {
        buffer->next->prev = buffer->prev;
    }
    pthread_mutex_unlock(&g_memory_state.lock);
    
    g_memory_state.total_freed += buffer->size;
    
    free(buffer);
}

/**
 * @brief Bind buffer to a GPU
 */
mvgal_error_t mvgal_buffer_bind_to_gpu(
    struct mvgal_buffer *buffer,
    uint32_t gpu_index,
    bool writable
) {
    if (buffer == NULL || gpu_index >= MVGAL_MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Check if GPU exists
    if (gpu_index >= g_memory_state.gpu_count) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }
    
    // Check if already bound
    for (uint32_t i = 0; i < buffer->gpu_binding_count; i++) {
        if (buffer->gpu_bindings[i].gpu_index == gpu_index) {
            // Already bound, update flags
            buffer->gpu_bindings[i].writable = writable;
            buffer->gpu_bindings[i].readable = true; // Must be readable if writable
            return MVGAL_SUCCESS;
        }
    }
    
    // Check if GPU is in the mask
    if (!(buffer->gpu_mask & (1 << gpu_index))) {
        buffer->gpu_mask |= (1 << gpu_index);
    }
    
    // Add binding
    if (buffer->gpu_binding_count < MVGAL_MAX_GPUS) {
        mvgal_gpu_binding_t *binding = &buffer->gpu_bindings[buffer->gpu_binding_count];
        binding->gpu_index = gpu_index;
        binding->mapping = buffer->host_ptr; // For now, use CPU pointer
        binding->writable = writable;
        binding->readable = true;
        binding->dmabuf_fd = buffer->dmabuf_fd;
        buffer->gpu_binding_count++;
        
        MVGAL_LOG_DEBUG("Buffer %p bound to GPU %u (writable=%d)", 
                       (void *)buffer, gpu_index, writable);
        return MVGAL_SUCCESS;
    }
    
    MVGAL_LOG_ERROR("Cannot bind buffer to GPU %u: too many bindings", gpu_index);
    return MVGAL_ERROR_MEMORY;
}

/**
 * @brief Unbind buffer from all GPUs
 */
void mvgal_buffer_unbind(struct mvgal_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    
    for (uint32_t i = 0; i < buffer->gpu_binding_count; i++) {
        if (buffer->gpu_bindings[i].mapping != NULL &&
            buffer->gpu_bindings[i].mapping != buffer->host_ptr) {
            // Unmap GPU-specific mapping if different from CPU pointer
            // In a real implementation, this would call GPU-specific unmap
            buffer->gpu_bindings[i].mapping = NULL;
        }
    }
    
    buffer->gpu_binding_count = 0;
}

/**
 * @brief Check if DMA-BUF is supported for the given GPU mask
 */
bool mvgal_dmabuf_is_supported(uint32_t gpu_mask) {
    if (gpu_mask == 0) {
        return false;
    }
    
    // Check each GPU in the mask
    for (uint32_t i = 0; i < g_memory_state.gpu_count; i++) {
        if (gpu_mask & (1 << i)) {
            const mvgal_gpu_descriptor_t *gpu = &g_memory_state.gpus[i];
            // Check if GPU supports DMA-BUF
            if (!(gpu->features & MVGAL_FEATURE_DMA_BUF)) {
                return false;
            }
        }
    }
    
    return true;
}
