/**
 * @file memory_internal.h
 * @brief Internal memory management types and declarations
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header defines internal structures for the memory module.
 * Not part of the public API.
 */

#ifndef MVGAL_MEMORY_INTERNAL_H
#define MVGAL_MEMORY_INTERNAL_H

#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include "mvgal_types.h"
#include "mvgal_gpu.h"
#include "mvgal_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of GPUs supported
 */
#define MVGAL_MAX_GPUS 16

/**
 * @brief Buffer state flags
 */
typedef enum {
    MVGAL_BUFFER_STATE_ALLOCATED = 1 << 0,   ///< Buffer memory allocated
    MVGAL_BUFFER_STATE_MAPPED = 1 << 1,       ///< Mapped to CPU address space
    MVGAL_BUFFER_STATE_EXPORTED = 1 << 2,    ///< Exported as DMA-BUF
    MVGAL_BUFFER_STATE_IMPORTED = 1 << 3,    ///< Imported from DMA-BUF
    MVGAL_BUFFER_STATE_REPLICATED = 1 << 4,  ///< Replicated to multiple GPUs
    MVGAL_BUFFER_STATE_LAZY = 1 << 5,        ///< Lazy allocation pending
    MVGAL_BUFFER_STATE_ERROR = 1 << 6,       ///< Error state
} mvgal_buffer_state_flags_t;

/**
 * @brief Buffer backend type
 */
typedef enum {
    MVGAL_BUFFER_BACKEND_SYSTEM,    ///< System memory (malloc/mmap)
    MVGAL_BUFFER_BACKEND_DMA_BUF,   ///< DMA-BUF allocated
    MVGAL_BUFFER_BACKEND_DMA_BUF_IMPORTED, ///< Imported DMA-BUF
    MVGAL_BUFFER_BACKEND_GPU,        ///< GPU-specific allocation
    MVGAL_BUFFER_BACKEND_WRAPPED,    ///< Wrapped existing memory
    MVGAL_BUFFER_BACKEND_P2P,        ///< P2P accessible memory
} mvgal_buffer_backend_type_t;

/**
 * @brief Internal fence structure
 */
struct mvgal_fence {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;
    atomic_int refcount;
    mvgal_error_t status;  ///< Status when signaled (MVGAL_SUCCESS or error)
    void *user_data;       ///< Optional user data
};

/**
 * @brief Internal semaphore structure
 */
struct mvgal_semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint64_t count;
    atomic_int refcount;
};

/**
 * @brief GPU memory binding
 */
typedef struct {
    uint32_t gpu_index;
    uint64_t gpu_address;    ///< GPU virtual address
    void *mapping;          ///< CPU mapping for this GPU (if any)
    bool writable;          ///< Can be written by this GPU
    bool readable;          ///< Can be read by this GPU
    int dmabuf_fd;          ///< DMA-BUF FD for this GPU (if applicable)
} mvgal_gpu_binding_t;

/**
 * @brief Internal buffer structure
 */
struct mvgal_buffer {
    // Reference counting
    atomic_uint refcount;
    
    // State
    mvgal_buffer_state_flags_t state;
    mvgal_buffer_backend_type_t backend;
    
    // Allocation info
    size_t size;
    size_t alignment;
    mvgal_memory_flags_t flags;
    mvgal_memory_type_t memory_type;
    mvgal_memory_sharing_mode_t sharing_mode;
    mvgal_memory_access_flags_t access;
    uint32_t gpu_mask;  ///< Bitmask of GPUs that can access
    uint32_t priority;
    
    // Memory locations
    void *host_ptr;                 ///< CPU pointer (if mapped)
    size_t host_offset;            ///< Offset within host_ptr
    int dmabuf_fd;                  ///< DMA-BUF file descriptor (-1 if none)
    uint64_t dmabuf_offset;         ///< Offset within DMA-BUF
    bool dmabuf_owner;              ///< True if we created the DMA-BUF
    
    // GPU bindings
    mvgal_gpu_binding_t gpu_bindings[MVGAL_MAX_GPUS];
    uint32_t gpu_binding_count;
    
    // Synchronization
    struct mvgal_fence *fence;     ///< Optional fence for this buffer
    
    // Callbacks
    mvgal_memory_callback_t callback;
    void *callback_user_data;
    
    // Statistics
    atomic_ullong bytes_read;
    atomic_ullong bytes_written;
    atomic_ullong bytes_transferred;
    atomic_uint access_count[MVGAL_MAX_GPUS];
    
    // Original allocation (for wrapped memory)
    void *original_ptr;           ///< Original pointer if wrapped
    bool should_free;              ///< Whether to free original_ptr on destroy
    
    // Next/prev for tracking (optional)
    struct mvgal_buffer *next;
    struct mvgal_buffer *prev;
};

/**
 * @brief DMA-BUF heap handle
 */
typedef struct {
    int fd;
    const char *name;
} mvgal_dmabuf_heap_t;

/**
 * @brief Memory module global state
 */
typedef struct {
    pthread_mutex_t lock;
    bool initialized;
    struct mvgal_buffer *buffers;  ///< Head of buffer list
    uint64_t total_allocated;
    uint64_t total_freed;
    
    // DMA-BUF heaps
    mvgal_dmabuf_heap_t system_heap;
    mvgal_dmabuf_heap_t gpu_heaps[MVGAL_MAX_GPUS];
    uint32_t gpu_heap_count;
    
    // GPU information (cached from gpu_manager)
    mvgal_gpu_descriptor_t gpus[MVGAL_MAX_GPUS];
    uint32_t gpu_count;
} mvgal_memory_state_t;

/**
 * @brief Get the global memory state
 */
mvgal_memory_state_t *mvgal_memory_get_state(void);

/**
 * @brief Initialize memory module
 */
mvgal_error_t mvgal_memory_module_init(void);

/**
 * @brief Shutdown memory module
 */
void mvgal_memory_module_shutdown(void);

/**
 * @brief Allocate buffer internal
 */
mvgal_error_t mvgal_buffer_allocate_internal(
    const mvgal_memory_alloc_info_t *alloc_info,
    struct mvgal_buffer **buffer_out
);

/**
 * @brief Free buffer internal
 */
void mvgal_buffer_free_internal(struct mvgal_buffer *buffer);

/**
 * @brief Increment buffer reference count
 */
void mvgal_buffer_retain(struct mvgal_buffer *buffer);

/**
 * @brief Decrement buffer reference count and possibly free
 */
void mvgal_buffer_release(struct mvgal_buffer *buffer);

/**
 * @brief Replicate buffer to multiple GPUs
 */
mvgal_error_t mvgal_buffer_replicate(
    struct mvgal_buffer *buffer,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_fence_t fence
);

/**
 * @brief Get GPU address for a buffer
 */
mvgal_error_t mvgal_buffer_get_gpu_address(
    struct mvgal_buffer *buffer,
    uint32_t gpu_index,
    uint64_t *address
);

/**
 * @brief Check if DMA-BUF is supported for the given GPU mask
 */
bool mvgal_dmabuf_is_supported(uint32_t gpu_mask);

/**
 * @brief Allocate memory using DMA-BUF heap
 */
mvgal_error_t mvgal_dmabuf_allocate(
    size_t size,
    size_t alignment,
    mvgal_memory_flags_t flags,
    uint32_t gpu_mask,
    int *fd_out,
    void **ptr_out
);

/**
 * @brief Free DMA-BUF memory
 */
void mvgal_dmabuf_free(int fd, void *ptr, size_t size);

/**
 * @brief Import existing DMA-BUF
 */
mvgal_error_t mvgal_dmabuf_import(
    int fd,
    size_t size,
    mvgal_memory_flags_t flags,
    struct mvgal_buffer **buffer_out
);

/**
 * @brief Export buffer as DMA-BUF
 */
mvgal_error_t mvgal_dmabuf_export(
    struct mvgal_buffer *buffer,
    int *fd_out
);

/**
 * @brief Map DMA-BUF to CPU address space
 */
mvgal_error_t mvgal_dmabuf_map(int fd, size_t size, size_t offset, void **ptr_out);

/**
 * @brief Unmap DMA-BUF from CPU address space
 */
void mvgal_dmabuf_unmap(int fd, void *ptr, size_t size);

/**
 * @brief Bind buffer to a GPU
 */
mvgal_error_t mvgal_buffer_bind_to_gpu(
    struct mvgal_buffer *buffer,
    uint32_t gpu_index,
    bool writable
);

/**
 * @brief Unbind buffer from all GPUs
 */
void mvgal_buffer_unbind(struct mvgal_buffer *buffer);

#ifdef __cplusplus
}
#endif

#endif // MVGAL_MEMORY_INTERNAL_H
