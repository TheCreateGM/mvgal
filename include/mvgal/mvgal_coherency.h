/**
 * @file mvgal_coherency.h
 * @brief Cache coherency protocol API
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header provides cache coherency domain management
 * and synchronization primitives for multi-GPU systems.
 */

#ifndef MVGAL_COHERENCY_H
#define MVGAL_COHERENCY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "mvgal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup Coherency
 * @{
 */

/**
 * @brief Coherency domain type
 */
typedef enum {
    MVGAL_COHERENCY_DOMAIN_SYSTEM = 0,       /* System (CPU) coherency domain */
    MVGAL_COHERENCY_DOMAIN_DEVICE = 1,       /* Single device coherency domain */
    MVGAL_COHERENCY_DOMAIN_CROSS_DEVICE = 2, /* Cross-device coherency domain */
    MVGAL_COHERENCY_DOMAIN_P2P = 3,          /* Peer-to-peer coherency domain */
} mvgal_coherency_domain_type_t;

/**
 * @brief Coherency operation flags
 */
typedef uint32_t mvgal_coherency_flags_t;
#define MVGAL_COHERENCY_FLAG_NONE           (0)
#define MVGAL_COHERENCY_FLAG_INVALIDATE     (1 << 0)  /* Invalidate caches */
#define MVGAL_COHERENCY_FLAG_FLUSH          (1 << 1)  /* Flush caches */
#define MVGAL_COHERENCY_FLAG_BIDIRECTIONAL  (1 << 2)  /* Bidirectional sync */
#define MVGAL_COHERENCY_FLAG_NON_BLOCKING   (1 << 3)  /* Non-blocking op */

/**
 * @brief Coherency domain descriptor
 */
typedef struct {
    mvgal_coherency_domain_type_t type;
    uint32_t gpu_count;
    uint32_t gpu_indices[16];  /* GPUs in this domain */
    bool cache_coherent;
    bool atomic_supported;
    uint64_t domain_id;
} mvgal_coherency_domain_descriptor_t;

/**
 * @brief Coherency operation descriptor
 */
typedef struct {
    void *ptr;               /* Start address */
    size_t size;             /* Size in bytes */
    uint32_t src_gpu_index;  /* Source GPU (for P2P) */
    uint32_t dst_gpu_index;  /* Destination GPU (for P2P) */
    mvgal_coherency_flags_t flags;
    mvgal_fence_t fence;     /* Optional completion fence */
} mvgal_coherency_operation_t;

/**
 * @brief P2P semaphore for cross-device synchronization
 */
typedef struct mvgal_coherency_semaphore *mvgal_coherency_semaphore_t;

/**
 * @brief Create a coherency domain
 *
 * Creates a coherency domain spanning the specified GPUs.
 * The domain ensures cache coherence between all members.
 *
 * @param type Domain type
 * @param gpu_count Number of GPUs in domain
 * @param gpu_indices Array of GPU indices
 * @param descriptor Domain descriptor (out)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_domain_create(
    mvgal_coherency_domain_type_t type,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_coherency_domain_descriptor_t *descriptor
);

/**
 * @brief Destroy a coherency domain
 *
 * @param descriptor Domain to destroy
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_domain_destroy(
    mvgal_coherency_domain_descriptor_t *descriptor
);

/**
 * @brief Synchronize memory within a coherency domain
 *
 * Ensures all GPUs in the domain see consistent data
 * for the specified memory range.
 *
 * @param descriptor Domain
 * @param ptr Memory pointer
 * @param size Size in bytes
 * @param flags Operation flags
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_sync(
    mvgal_coherency_domain_descriptor_t *descriptor,
    void *ptr,
    size_t size,
    mvgal_coherency_flags_t flags
);

/**
 * @brief Flush CPU caches for a memory range
 *
 * Writes dirty cache lines back to memory.
 *
 * @param ptr Memory pointer
 * @param size Size in bytes
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_flush(
    void *ptr,
    size_t size
);

/**
 * @brief Invalidate CPU caches for a memory range
 *
 * Discards cache lines; next read fetches from memory.
 *
 * @param ptr Memory pointer
 * @param size Size in bytes
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_invalidate(
    void *ptr,
    size_t size
);

/**
 * @brief Perform a peer-to-peer coherency operation
 *
 * Synchronizes data between two GPUs.
 *
 * @param src_gpu Source GPU index
 * @param dst_gpu Destination GPU index
 * @param ptr Memory pointer
 * @param size Size in bytes
 * @param flags Operation flags
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_p2p_sync(
    uint32_t src_gpu,
    uint32_t dst_gpu,
    void *ptr,
    size_t size,
    mvgal_coherency_flags_t flags
);

/**
 * @brief Create a P2P semaphore for cross-device sync
 *
 * @param initial_value Initial semaphore value
 * @param semaphore Semaphore handle (out)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_semaphore_create(
    uint64_t initial_value,
    mvgal_coherency_semaphore_t *semaphore
);

/**
 * @brief Destroy a P2P semaphore
 *
 * @param semaphore Semaphore to destroy
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_semaphore_destroy(
    mvgal_coherency_semaphore_t semaphore
);

/**
 * @brief Signal a P2P semaphore
 *
 * @param semaphore Semaphore to signal
 * @param increment Value to add
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_semaphore_signal(
    mvgal_coherency_semaphore_t semaphore,
    uint64_t increment
);

/**
 * @brief Wait on a P2P semaphore
 *
 * @param semaphore Semaphore to wait on
 * @param value Threshold value
 * @param timeout_ns Timeout in nanoseconds (0 = no wait, UINT64_MAX = infinite)
 * @return MVGAL_SUCCESS on success, MVGAL_ERROR_TIMEOUT on timeout
 */
mvgal_error_t mvgal_coherency_semaphore_wait(
    mvgal_coherency_semaphore_t semaphore,
    uint64_t value,
    uint64_t timeout_ns
);

/**
 * @brief Query coherency capabilities for a GPU pair
 *
 * @param gpu_a First GPU index
 * @param gpu_b Second GPU index
 * @param cache_coherent Set to true if hardware-coherent (out)
 * @param atomic_supported Set to true if atomics are supported (out)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_coherency_query_capabilities(
    uint32_t gpu_a,
    uint32_t gpu_b,
    bool *cache_coherent,
    bool *atomic_supported
);

/** @} */ // end of Coherency

#ifdef __cplusplus
}
#endif

#endif // MVGAL_COHERENCY_H
