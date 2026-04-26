/**
 * @file dmabuf.c
 * @brief DMA-BUF backend implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements DMA-BUF memory allocation, import, and export
 * for cross-GPU memory sharing.
 */

#include "memory_internal.h"
#include "mvgal/mvgal_log.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

// For mkostemp (GNU extension)
#ifndef HAVE_MKOSTEMP
#define mkostemp(template, flags) mkstemp(template)
#endif

// DMA-BUF heap ioctl definitions (Linux kernel)
#ifndef DMA_HEAP_IOCTL_CASE
#define DMA_HEAP_IOCTL_CASE _IOW('D', 0x01, struct dma_heap_allocation_data)
#endif

/**
 * @brief DMA-BUF allocation data structure
 */
struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};

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
) {
    (void)alignment; // Alignment handled by mmap
    (void)gpu_mask;  // GPU mask used for strategy selection
    
    if (fd_out == NULL || ptr_out == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_memory_state_t *state = mvgal_memory_get_state();
    void *ptr = NULL;
    int fd = -1;
    
    // If we have a system DMA-BUF heap available, try to use it
    if (state->system_heap.fd >= 0) {
        struct dma_heap_allocation_data alloc = {0};
        alloc.len = size;
        
        // Set flags based on requested flags
        if (flags & MVGAL_MEMORY_FLAG_CPU_CACHED) {
            alloc.fd_flags |= O_SYNC;
        }
        // If uncached
        if (flags & MVGAL_MEMORY_FLAG_CPU_UNCACHED) {
            // No special flags for uncached
        }
        
        if (ioctl(state->system_heap.fd, DMA_HEAP_IOCTL_CASE, &alloc) == 0) {
            fd = alloc.fd;
            MVGAL_LOG_DEBUG("DMA-BUF heap allocation: size=%zu, fd=%d", size, fd);
        }
    }
    
    // If kernel heap allocation failed, try to use user-space fallback
    if (fd < 0) {
        // Try to create a shared memory mapping using memfd
        // This is a fallback for systems without kernel DMA-BUF heap support
        
        // Create an anonymous file (memfd if available, otherwise use tmpfile)
        int memfd = -1;
        
        // Try memfd_create first (preferred)
#ifdef HAVE_MEMFD_CREATE
        memfd = memfd_create("mvgal-dmabuf", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#endif
        
        if (memfd < 0) {
            // Fall back to tmpfile
            char template[] = "/tmp/mvgal-dmabuf-XXXXXX";
            memfd = mkostemp(template, O_CLOEXEC);
            if (memfd >= 0) {
                unlink(template); // Remove file, but keep FD
            }
        }
        
        if (memfd >= 0) {
            // Set size
            if (ftruncate(memfd, (off_t)size) == 0) {
                fd = memfd;
                MVGAL_LOG_DEBUG("DMA-BUF fallback: using anonymous file, fd=%d", fd);
            } else {
                close(memfd);
                fd = -1;
            }
        }
    }
    
    if (fd < 0) {
        MVGAL_LOG_ERROR("Failed to allocate DMA-BUF: %s", strerror(errno));
        return MVGAL_ERROR_MEMORY;
    }
    
    // Map the FD to CPU address space
    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        MVGAL_LOG_ERROR("Failed to mmap DMA-BUF: %s", strerror(errno));
        close(fd);
        return MVGAL_ERROR_MEMORY;
    }
    
    // Advise the kernel about expected usage
    if (flags & MVGAL_MEMORY_FLAG_CPU_CACHED) {
        madvise(ptr, size, MADV_NORMAL);
    } else if (flags & MVGAL_MEMORY_FLAG_CPU_UNCACHED) {
        madvise(ptr, size, MADV_SEQUENTIAL);
    }
    
    // If the buffer should be zero-initialized
    if (flags & MVGAL_MEMORY_FLAG_ZERO_INITIALIZED) {
        memset(ptr, 0, size);
    }
    
    // Ensure writes are visible
    if (msync(ptr, size, MS_ASYNC) != 0) {
        MVGAL_LOG_WARN("msync failed: %s", strerror(errno));
    }
    
    *fd_out = fd;
    *ptr_out = ptr;
    
    MVGAL_LOG_DEBUG("DMA-BUF allocated: fd=%d, ptr=%p, size=%zu", fd, ptr, size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Free DMA-BUF memory
 */
void mvgal_dmabuf_free(int fd, void *ptr, size_t size) {
    if (ptr != NULL && ptr != MAP_FAILED) {
        munmap(ptr, size);
    }
    if (fd >= 0) {
        close(fd);
    }
    MVGAL_LOG_DEBUG("DMA-BUF freed: fd=%d, ptr=%p", fd, ptr);
}

/**
 * @brief Import existing DMA-BUF as a buffer
 */
mvgal_error_t mvgal_dmabuf_import(
    int fd,
    size_t size,
    mvgal_memory_flags_t flags,
    struct mvgal_buffer **buffer_out
) {
    if (buffer_out == NULL || fd < 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_error_t err;
    struct mvgal_buffer *buffer = NULL;
    
    // Map the FD to get size if size is 0
    if (size == 0) {
        struct stat st;
        if (fstat(fd, &st) == 0) {
            size = (size_t)st.st_size;
        } else {
            MVGAL_LOG_ERROR("Cannot determine DMA-BUF size");
            return MVGAL_ERROR_INVALID_ARGUMENT;
        }
    }
    
    // Create allocation info
    mvgal_memory_alloc_info_t alloc_info = {
        .size = size,
        .alignment = 0,
        .flags = flags | MVGAL_MEMORY_FLAG_DMA_BUF,
        .memory_type = MVGAL_MEMORY_TYPE_SRAM,
        .sharing_mode = MVGAL_MEMORY_SHARING_DMA_BUF,
        .access = MVGAL_MEMORY_ACCESS_READ_WRITE,
        .gpu_mask = 0xFFFFFFFF, // All GPUs by default
        .priority = 50
    };
    
    // Allocate buffer structure
    err = mvgal_buffer_allocate_internal(&alloc_info, &buffer);
    if (err != MVGAL_SUCCESS) {
        return err;
    }
    
    // Store the imported FD
    buffer->dmabuf_fd = fd;
    buffer->dmabuf_owner = false; // We don't own the FD
    buffer->dmabuf_offset = 0;
    buffer->backend = MVGAL_BUFFER_BACKEND_DMA_BUF_IMPORTED;
    
    // Map the DMA-BUF
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        MVGAL_LOG_ERROR("Failed to mmap imported DMA-BUF: %s", strerror(errno));
        mvgal_buffer_free_internal(buffer);
        return MVGAL_ERROR_MEMORY;
    }
    
    buffer->host_ptr = ptr;
    buffer->original_ptr = ptr;
    buffer->state |= MVGAL_BUFFER_STATE_ALLOCATED | MVGAL_BUFFER_STATE_MAPPED;
    
    // Add to global buffer list
    pthread_mutex_lock(&mvgal_memory_get_state()->lock);
    buffer->next = mvgal_memory_get_state()->buffers;
    if (mvgal_memory_get_state()->buffers != NULL) {
        mvgal_memory_get_state()->buffers->prev = buffer;
    }
    mvgal_memory_get_state()->buffers = buffer;
    pthread_mutex_unlock(&mvgal_memory_get_state()->lock);
    
    mvgal_memory_get_state()->total_allocated += size;
    
    *buffer_out = buffer;
    MVGAL_LOG_DEBUG("DMA-BUF imported: fd=%d, ptr=%p, size=%zu", fd, ptr, size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Export buffer as DMA-BUF
 */
mvgal_error_t mvgal_dmabuf_export(
    struct mvgal_buffer *buffer,
    int *fd_out
) {
    if (buffer == NULL || fd_out == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // If already exported, return the existing FD
    if (buffer->state & MVGAL_BUFFER_STATE_EXPORTED) {
        *fd_out = buffer->dmabuf_fd;
        return MVGAL_SUCCESS;
    }
    
    // If this is already a DMA-BUF, just return the FD
    if (buffer->backend == MVGAL_BUFFER_BACKEND_DMA_BUF ||
        buffer->backend == MVGAL_BUFFER_BACKEND_DMA_BUF_IMPORTED) {
        *fd_out = buffer->dmabuf_fd;
        buffer->state |= MVGAL_BUFFER_STATE_EXPORTED;
        return MVGAL_SUCCESS;
    }
    
    // For system memory, we need to create a DMA-BUF
    // This is not directly possible - we would need to use a driver
    // For now, return an error for non-DMA-BUF buffers
    MVGAL_LOG_ERROR("Cannot export non-DMA-BUF buffer as DMA-BUF");
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Map DMA-BUF to CPU address space
 */
mvgal_error_t mvgal_dmabuf_map(int fd, size_t size, size_t offset, void **ptr_out) {
    if (fd < 0 || ptr_out == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)offset);
    if (ptr == MAP_FAILED) {
        MVGAL_LOG_ERROR("Failed to mmap DMA-BUF: %s", strerror(errno));
        return MVGAL_ERROR_MEMORY;
    }
    
    *ptr_out = ptr;
    MVGAL_LOG_DEBUG("DMA-BUF mapped: fd=%d, offset=%zu, ptr=%p, size=%zu", 
                   fd, offset, ptr, size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Unmap DMA-BUF from CPU address space
 */
void mvgal_dmabuf_unmap(int fd, void *ptr, size_t size) {
    if (ptr != NULL && ptr != MAP_FAILED) {
        munmap(ptr, size);
    }
    MVGAL_LOG_DEBUG("DMA-BUF unmapped: fd=%d, ptr=%p", fd, ptr);
}

/**
 * @brief Get GPU address for a buffer on a specific GPU
 *
 * This function would be implemented with GPU-specific APIs in a real implementation.
 * For now, it returns 0 as a placeholder.
 */
mvgal_error_t mvgal_buffer_get_gpu_address(
    struct mvgal_buffer *buffer,
    uint32_t gpu_index,
    uint64_t *address
) {
    if (buffer == NULL || address == NULL || gpu_index >= MVGAL_MAX_GPUS) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // In a real implementation, this would query the GPU driver
    // For DMA-BUF, the GPU address might be the same as the CPU address
    // or require a translation through the driver
    
    // Check if we have a GPU-specific binding
    for (uint32_t i = 0; i < buffer->gpu_binding_count; i++) {
        if (buffer->gpu_bindings[i].gpu_index == gpu_index) {
            if (buffer->gpu_bindings[i].gpu_address != 0) {
                *address = buffer->gpu_bindings[i].gpu_address;
                return MVGAL_SUCCESS;
            }
            break;
        }
    }
    
    // Default: for DMA-BUF, GPU address equals CPU address (cast to uint64_t)
    if (buffer->dmabuf_fd >= 0) {
        *address = (uint64_t)(uintptr_t)buffer->host_ptr;
        return MVGAL_SUCCESS;
    }
    
    // For system memory, return 0 (not accessible by GPU)
    *address = 0;
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Replicate buffer to multiple GPUs
 *
 * This creates copies of the buffer on each specified GPU.
 * In a real implementation, this would use PCIe P2P transfers
 * or GPU-specific copy commands.
 */
mvgal_error_t mvgal_buffer_replicate(
    struct mvgal_buffer *buffer,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_fence_t fence
) {
    (void)fence; // In async implementation, fence would be signaled on completion
    if (buffer == NULL || gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_error_t err = MVGAL_SUCCESS;
    
    for (uint32_t i = 0; i < gpu_count; i++) {
        uint32_t gpu_index = gpu_indices[i];
        
        if (gpu_index >= MVGAL_MAX_GPUS) {
            continue;
        }
        
        // In a real implementation, this would:
        // 1. Allocate memory on the target GPU
        // 2. Initiate a copy from the source (buffer) to the target GPU
        // 3. Optionally add the copy operation to the fence
        
        // For now, just mark the buffer as accessible by this GPU
        err = mvgal_buffer_bind_to_gpu(buffer, gpu_index, false);
        if (err != MVGAL_SUCCESS) {
            MVGAL_LOG_WARN("Failed to bind buffer to GPU %u: %d", gpu_index, err);
            // Continue with other GPUs
        }
    }
    
    // Mark buffer as replicated
    buffer->state |= MVGAL_BUFFER_STATE_REPLICATED;
    
    // If a fence was provided, we could signal it when all copies are complete
    // In a real async implementation
    
    MVGAL_LOG_DEBUG("Buffer %p replicated to %u GPUs", (void *)buffer, gpu_count);
    
    return MVGAL_SUCCESS;
}

// ============================================================================
// P2P (Peer-to-Peer) Transfer Functions
// ============================================================================

/**
 * @brief Check if P2P transfer is possible between two GPUs
 *
 * P2P transfers require:
 * 1. Both GPUs are on the same PCIe root complex
 * 2. The PCIe switch supports P2P
 * 3. Drivers support P2P
 * 4. For cross-vendor: DMA-BUF support
 *
 * Note: In a full implementation, this would query the GPU manager
 * for PCI topology and capability information.
 */
bool mvgal_dmabuf_p2p_is_supported(uint32_t src_gpu_index, uint32_t dst_gpu_index) {
    if (src_gpu_index >= MVGAL_MAX_GPUS || dst_gpu_index >= MVGAL_MAX_GPUS) {
        return false;
    }
    
    // Check if both GPUs are valid
    int32_t gpu_count = mvgal_gpu_get_count();
    if (src_gpu_index >= (uint32_t)gpu_count || dst_gpu_index >= (uint32_t)gpu_count) {
        return false;
    }
    
    // Check if both GPUs are enabled
    if (!mvgal_gpu_is_enabled(src_gpu_index) || !mvgal_gpu_is_enabled(dst_gpu_index)) {
        return false;
    }
    
    // In a real implementation, we would check:
    // - PCI root complex (must be same)
    // - PCIe switch P2P support
    // - Driver support for P2P
    // - Vendor-specific capabilities (NVLink, Infinity Fabric, etc.)
    
    // For now, we assume P2P is possible if both GPUs are valid and enabled
    // The actual copy operation will use DMA-BUF for cross-vendor transfers
    return true;
}

/**
 * @brief Check if two GPUs are connected via NVLink (NVIDIA-specific)
 */
bool mvgal_dmabuf_nvlink_is_available(uint32_t gpu1, uint32_t gpu2) {
    (void)gpu1;
    (void)gpu2;
    
    // In a real implementation, this would check NVIDIA's NVML API
    // or sysfs for NVLink presence
    // For now, return false as we don't have NVML integration
    return false;
}

/**
 * @brief Perform GPU-to-GPU copy using DMA-BUF
 *
 * This function performs a P2P copy by:
 * 1. Ensuring both buffers are accessible via DMA-BUF
 * 2. Using the DMA-BUF FD for zero-copy access on the destination GPU
 * 3. Or initiating a GPU copy command
 */
mvgal_error_t mvgal_dmabuf_copy_gpu_to_gpu(
    struct mvgal_buffer *src_buffer,
    uint64_t src_offset,
    struct mvgal_buffer *dst_buffer,
    uint64_t dst_offset,
    size_t size,
    uint32_t src_gpu_index,
    uint32_t dst_gpu_index
) {
    if (src_buffer == NULL || dst_buffer == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (size == 0) {
        return MVGAL_SUCCESS;
    }
    
    // Check bounds
    if (src_offset + size > src_buffer->size || dst_offset + size > dst_buffer->size) {
        MVGAL_LOG_ERROR("P2P copy: out of bounds");
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Check if P2P is supported between these GPUs
    if (!mvgal_dmabuf_p2p_is_supported(src_gpu_index, dst_gpu_index)) {
        MVGAL_LOG_DEBUG("P2P not supported between GPU %u and %u, falling back to CPU copy",
                       src_gpu_index, dst_gpu_index);
        // Fall back to CPU copy
        unsigned char *src_ptr = (unsigned char *)src_buffer->host_ptr + src_offset;
        unsigned char *dst_ptr = (unsigned char *)dst_buffer->host_ptr + dst_offset;
        memcpy(dst_ptr, src_ptr, size);
        return MVGAL_SUCCESS;
    }
    
    // Check if we can use DMA-BUF for zero-copy access
    if (src_buffer->dmabuf_fd >= 0 && dst_buffer->dmabuf_fd >= 0) {
        // Both buffers are DMA-BUF backed
        // In a real implementation, we would:
        // 1. Check if the destination GPU can access the source DMA-BUF
        // 2. If yes, map the source DMA-BUF on the destination GPU
        // 3. Use GPU copy commands (e.g., CUDA memcpy, OpenCL enqueue_copy_buffer)
        
        MVGAL_LOG_DEBUG("P2P copy using DMA-BUF: GPU %u -> GPU %u, size=%zu",
                       src_gpu_index, dst_gpu_index, size);
        
        // For now, fall back to CPU copy
        // In production, this would use GPU-specific APIs
        unsigned char *src_ptr = (unsigned char *)src_buffer->host_ptr + src_offset;
        unsigned char *dst_ptr = (unsigned char *)dst_buffer->host_ptr + dst_offset;
        memcpy(dst_ptr, src_ptr, size);
        
        return MVGAL_SUCCESS;
    }
    
    // If source is a DMA-BUF but destination is not, we need to:
    // 1. Export destination to DMA-BUF (if possible)
    // 2. Then do the P2P copy
    
    if (src_buffer->dmabuf_fd >= 0 && dst_buffer->dmabuf_fd < 0) {
        // Try to export destination as DMA-BUF
        int dst_fd;
        if (mvgal_memory_export_dmabuf(dst_buffer, &dst_fd) == MVGAL_SUCCESS) {
            dst_buffer->dmabuf_fd = dst_fd;
            dst_buffer->backend = MVGAL_BUFFER_BACKEND_DMA_BUF;
            
            // Now both have DMA-BUF, retry
            return mvgal_dmabuf_copy_gpu_to_gpu(
                src_buffer, src_offset, dst_buffer, dst_offset,
                size, src_gpu_index, dst_gpu_index);
        }
    }
    
    // Fall back to CPU copy
    MVGAL_LOG_DEBUG("P2P copy: falling back to CPU copy");
    unsigned char *src_ptr = (unsigned char *)src_buffer->host_ptr + src_offset;
    unsigned char *dst_ptr = (unsigned char *)dst_buffer->host_ptr + dst_offset;
    memcpy(dst_ptr, src_ptr, size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Bind source buffer's DMA-BUF to destination GPU
 *
 * This maps the source DMA-BUF FD on the destination GPU's address space.
 * Returns the GPU address for the source buffer.
 */
mvgal_error_t mvgal_dmabuf_bind_to_gpu(
    struct mvgal_buffer *buffer,
    uint32_t gpu_index,
    uint64_t *gpu_address
) {
    if (buffer == NULL || gpu_address == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (buffer->dmabuf_fd < 0) {
        // Not a DMA-BUF, cannot bind
        return MVGAL_ERROR_NOT_SUPPORTED;
    }
    
    // Check if already bound to this GPU
    for (uint32_t i = 0; i < buffer->gpu_binding_count; i++) {
        if (buffer->gpu_bindings[i].gpu_index == gpu_index) {
            *gpu_address = buffer->gpu_bindings[i].gpu_address;
            return MVGAL_SUCCESS;
        }
    }
    
    // In a real implementation, this would use GPU-specific APIs:
    // - NVIDIA: cuMemImportFromShareableHandle or nvmlDeviceGetP2PStatus
    // - AMD: amdgpu_vm_map_buffer or HSA hUVA
    // - Intel: drm_prime_handle_to_fd + i915_gem_mmap_gtt
    
    // For now, we'll use a placeholder approach:
    // If the GPU is the one that originally imported this buffer, use the existing address
    // Otherwise, we need to do the binding
    
    // Check if GPU is valid and enabled
    if (gpu_index >= (uint32_t)mvgal_gpu_get_count() || !mvgal_gpu_is_enabled(gpu_index)) {
        return MVGAL_ERROR_GPU_NOT_FOUND;
    }
    
    // Simulate binding: on Linux with DMA-BUF, the GPU can access the memory
    // via the DMA-BUF FD. The GPU address would be obtained through the driver.
    // For now, we use the CPU address as a placeholder.
    
    uint64_t addr = (uint64_t)(uintptr_t)buffer->host_ptr;
    
    // Add binding
    if (buffer->gpu_binding_count < MVGAL_MAX_GPUS) {
        buffer->gpu_bindings[buffer->gpu_binding_count].gpu_index = gpu_index;
        buffer->gpu_bindings[buffer->gpu_binding_count].gpu_address = addr;
        buffer->gpu_bindings[buffer->gpu_binding_count].writable = true;
        buffer->gpu_bindings[buffer->gpu_binding_count].readable = true;
        buffer->gpu_binding_count++;
    }
    
    *gpu_address = addr;
    return MVGAL_SUCCESS;
}

/**
 * @brief Check P2P capability and get optimal copy method
 */
mvgal_memory_copy_method_t mvgal_dmabuf_get_copy_method(
    uint32_t src_gpu_index,
    uint32_t dst_gpu_index
) {
    if (src_gpu_index >= MVGAL_MAX_GPUS || dst_gpu_index >= MVGAL_MAX_GPUS) {
        return MVGAL_MEMORY_COPY_CPU;
    }
    
    int32_t gpu_count = mvgal_gpu_get_count();
    if (src_gpu_index >= (uint32_t)gpu_count || dst_gpu_index >= (uint32_t)gpu_count) {
        return MVGAL_MEMORY_COPY_CPU;
    }
    
    // Check for NVLink (fastest) - NVIDIA-specific
    if (mvgal_dmabuf_nvlink_is_available(src_gpu_index, dst_gpu_index)) {
        return MVGAL_MEMORY_COPY_NVLINK;
    }
    
    // Check if P2P is supported
    if (mvgal_dmabuf_p2p_is_supported(src_gpu_index, dst_gpu_index)) {
        // In a real implementation, we would check same vendor here
        // and return MVGAL_MEMORY_COPY_P2P for same vendor
        // For cross-vendor, return MVGAL_MEMORY_COPY_DMA_BUF
        return MVGAL_MEMORY_COPY_DMA_BUF;
    }
    
    // Fall back to CPU copy
    return MVGAL_MEMORY_COPY_CPU;
}

// ============================================================================
// UVM (Unified Virtual Memory) Support
// ============================================================================

/**
 * @brief Check if UVM is supported on a specific GPU
 */
bool mvgal_dmabuf_uvm_is_supported(uint32_t gpu_index) {
    if (gpu_index >= MVGAL_MAX_GPUS) {
        return false;
    }
    
    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_index >= (uint32_t)gpu_count || !mvgal_gpu_is_enabled(gpu_index)) {
        return false;
    }
    
    // In a real implementation, this would check:
    // - For NVIDIA: CUDA UVM support (CUDA >= 6.0)
    // - For AMD: HUMA (Heterogeneous Uniform Memory Access) support
    // - For Intel: Shared Virtual Memory (SVM) support
    
    // For now, we assume NVIDIA GPUs support UVM
    // We would check the vendor in a real implementation
    return true;
}

/**
 * @brief Allocate UVM memory
 *
 * UVM memory is accessible from both CPU and GPU with the same virtual address.
 * This is NVIDIA CUDA UVM, AMD HUMA, or Intel SVM.
 */
mvgal_error_t mvgal_dmabuf_allocate_uvm(
    size_t size,
    size_t alignment,
    mvgal_memory_flags_t flags,
    uint32_t gpu_mask,
    int *fd_out,
    void **ptr_out
) {
    (void)alignment;
    (void)gpu_mask;
    (void)fd_out; // UVM doesn't use file descriptors
    
    if (ptr_out == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // In a real implementation, this would use:
    // - NVIDIA CUDA: cuMemAllocManaged or cudaMallocManaged
    // - AMD ROCm: hipMallocManaged or hsa_memory_allocate
    // - Intel oneAPI: sycl::malloc_shared
    
    // For now, we fall back to regular system allocation
    // In production, this would be replaced with actual UVM allocation
    
    void *ptr = NULL;
    
    // Try aligned allocation first
    if (alignment > 0) {
        if (posix_memalign(&ptr, alignment, size) != 0) {
            ptr = NULL;
        }
    } else {
        ptr = malloc(size);
    }
    
    if (ptr == NULL) {
        MVGAL_LOG_ERROR("UVM allocation failed: out of memory");
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    // Zero-initialize if requested
    if (flags & MVGAL_MEMORY_FLAG_ZERO_INITIALIZED) {
        memset(ptr, 0, size);
    }
    
    *ptr_out = ptr;
    
    MVGAL_LOG_DEBUG("UVM allocated: ptr=%p, size=%zu", ptr, size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Free UVM memory
 */
void mvgal_dmabuf_free_uvm(void *ptr, size_t size) {
    if (ptr != NULL) {
        // In a real implementation, this would use:
        // - NVIDIA CUDA: cuMemFree or cudaFree
        // - AMD ROCm: hipFree or hsa_memory_free
        // - Intel oneAPI: sycl::free
        MVGAL_LOG_DEBUG("UVM freeing: ptr=%p, size=%zu", ptr, size);
        free(ptr);
    }
}

/**
 * @brief Set UVM access preferences for a GPU
 *
 * This hints to the UVM system which GPU is likely to access the memory next.
 */
mvgal_error_t mvgal_dmabuf_uvm_set_access_preference(
    void *ptr,
    size_t size,
    uint32_t gpu_index,
    mvgal_memory_access_flags_t access
) {
    (void)ptr;
    (void)size;
    (void)gpu_index;
    (void)access;
    
    // In a real implementation, this would use:
    // - NVIDIA CUDA: cudaMemAdvise
    // - AMD ROCm: hipMemAdvise
    
    MVGAL_LOG_DEBUG("UVM access preference: ptr=%p, GPU=%u, access=%d", ptr, gpu_index, access);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get UVM pointer attributes
 *
 * Returns information about UVM memory allocation.
 */
mvgal_error_t mvgal_dmabuf_uvm_get_attributes(
    void *ptr,
    uint32_t *gpu_count,
    uint32_t *gpu_indices
) {
    (void)ptr;
    (void)gpu_count;
    (void)gpu_indices;
    
    // In a real implementation, this would query the UVM system
    // For now, return basic information
    
    return MVGAL_ERROR_NOT_SUPPORTED;
}

/**
 * @brief Map UVM memory to a specific GPU
 *
 * Ensures the UVM memory is accessible by the specified GPU.
 */
mvgal_error_t mvgal_dmabuf_uvm_map_to_gpu(
    void *ptr,
    size_t size,
    uint32_t gpu_index
) {
    (void)ptr;
    (void)size;
    (void)gpu_index;
    
    // In a real implementation, this would ensure the memory is mapped
    // on the specified GPU's address space
    
    MVGAL_LOG_DEBUG("UVM mapped to GPU %u: ptr=%p, size=%zu", gpu_index, ptr, size);
    
    return MVGAL_SUCCESS;
}
