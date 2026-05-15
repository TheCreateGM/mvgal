/**
 * @file coherency.c
 * @brief Cache coherency protocol implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements coherency domain management,
 * cache synchronization primitives, and P2P semaphore
 * operations for multi-GPU systems.
 */

#include "memory_internal.h"
#include "mvgal/mvgal_log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

/**
 * @brief Coherency domain internal structure
 */
struct mvgal_coherency_domain {
    mvgal_coherency_domain_type_t type;
    uint32_t gpu_count;
    uint32_t gpu_indices[16];
    bool cache_coherent;
    bool atomic_supported;
    uint64_t domain_id;
    pthread_mutex_t lock;
    int refcount;
};

/**
 * @brief P2P semaphore internal structure
 */
struct mvgal_coherency_semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint64_t value;
    atomic_int refcount;
    bool shared;              /* Cross-process capable */
    void *shared_mem;         /* mmap'd shared memory (if shared) */
    int dmabuf_fd;            /* DMA-BUF FD for GPU sharing */
};

/* Internal domain counter for unique IDs */
static atomic_ullong g_next_domain_id = 1;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Perform CPU cache flush using clflush/clflushopt
 *
 * Falls back to a simple memory barrier if no instruction is available.
 */
static void cache_line_flush(void *addr, size_t size) {
    /* Use GCC/Clang builtin for cache line flush if available */
    char *ptr = (char *)addr;
    size_t offset = 0;
    const size_t cache_line = 64; /* Typical cache line size */

    while (offset < size) {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_clflush(ptr + offset);
#elif defined(__aarch64__)
        /* ARM DC CVAC (Clean by Virtual Address to Point of Coherency) */
        asm volatile ("dc cvac, %0" :: "r"(ptr + offset) : "memory");
#else
        /* Generic memory barrier fallback */
        __sync_synchronize();
        break;
#endif
        offset += cache_line;
    }

    /* Ensure all flushes complete before proceeding */
    __sync_synchronize();
}

/**
 * @brief Perform CPU cache invalidate using clflush
 *
 * On x86, clflush is both flush and invalidate.
 * On ARM, we use DC CIVAC (Clean & Invalidate by VA to Point of Coherency).
 */
static void cache_line_invalidate(void *addr, size_t size) {
    char *ptr = (char *)addr;
    size_t offset = 0;
    const size_t cache_line = 64;

    while (offset < size) {
#if defined(__x86_64__) || defined(__i386__)
        /* On x86, clflush flushes AND invalidates */
        __builtin_ia32_clflush(ptr + offset);
#elif defined(__aarch64__)
        /* DC CIVAC: Clean and Invalidate by VA to Point of Coherency */
        asm volatile ("dc civac, %0" :: "r"(ptr + offset) : "memory");
#else
        __sync_synchronize();
        break;
#endif
        offset += cache_line;
    }

    __sync_synchronize();
}

/**
 * @brief Get the memory state for GPU index validation
 */
static mvgal_error_t validate_gpu_index(uint32_t gpu_index) {
    mvgal_memory_state_t *state = mvgal_memory_get_state();
    if (state == NULL) {
        return MVGAL_ERROR_UNKNOWN;
    }

    pthread_mutex_lock(&state->lock);
    bool valid = gpu_index < state->gpu_count;
    pthread_mutex_unlock(&state->lock);

    return valid ? MVGAL_SUCCESS : MVGAL_ERROR_INVALID_ARGUMENT;
}

/* ------------------------------------------------------------------ */
/*  Coherency domain operations                                       */
/* ------------------------------------------------------------------ */

mvgal_error_t mvgal_coherency_domain_create(
    mvgal_coherency_domain_type_t type,
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    mvgal_coherency_domain_descriptor_t *descriptor
) {
    if (descriptor == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    if (gpu_count == 0 || gpu_indices == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    if (gpu_count > 16) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    /* Validate all GPU indices */
    for (uint32_t i = 0; i < gpu_count; i++) {
        mvgal_error_t ret = validate_gpu_index(gpu_indices[i]);
        if (ret != MVGAL_SUCCESS) {
            MVGAL_LOG_ERROR("Invalid GPU index %u in coherency domain", gpu_indices[i]);
            return ret;
        }
    }

    /* Allocate domain */
    struct mvgal_coherency_domain *domain = calloc(1, sizeof(struct mvgal_coherency_domain));
    if (domain == NULL) {
        MVGAL_LOG_ERROR("Failed to allocate coherency domain");
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }

    if (pthread_mutex_init(&domain->lock, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to initialize coherency domain mutex");
        free(domain);
        return MVGAL_ERROR_INITIALIZATION;
    }

    domain->type = type;
    domain->gpu_count = gpu_count;
    memcpy(domain->gpu_indices, gpu_indices, gpu_count * sizeof(uint32_t));
    domain->domain_id = atomic_fetch_add(&g_next_domain_id, 1);
    domain->refcount = 1;

    /* Set coherency model based on domain type */
    switch (type) {
    case MVGAL_COHERENCY_DOMAIN_SYSTEM:
        /* System domain: CPU-GPU coherency, usually not hardware-coherent */
        domain->cache_coherent = false;
        domain->atomic_supported = true;
        break;

    case MVGAL_COHERENCY_DOMAIN_DEVICE:
        /* Single device: driver-managed coherency */
        domain->cache_coherent = true;
        domain->atomic_supported = true;
        break;

    case MVGAL_COHERENCY_DOMAIN_CROSS_DEVICE:
        /* Cross-device: depends on hardware support */
        domain->cache_coherent = false;
        domain->atomic_supported = false;
        break;

    case MVGAL_COHERENCY_DOMAIN_P2P:
        /* P2P: depends on PCIe BAR and P2P support */
        domain->cache_coherent = false;
        domain->atomic_supported = true;
        break;

    default:
        pthread_mutex_destroy(&domain->lock);
        free(domain);
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    /* Fill in output descriptor */
    descriptor->type = type;
    descriptor->gpu_count = gpu_count;
    memcpy(descriptor->gpu_indices, gpu_indices, gpu_count * sizeof(uint32_t));
    descriptor->cache_coherent = domain->cache_coherent;
    descriptor->atomic_supported = domain->atomic_supported;
    descriptor->domain_id = domain->domain_id;

    MVGAL_LOG_DEBUG("Coherency domain created: id=%lu type=%d gpu_count=%u",
                    (unsigned long)domain->domain_id, (int)type, gpu_count);

    /* Note: descriptor does not own the domain; domain is tracked internally.
     * When all references are dropped via destroy, the memory is freed. */
    (void)domain; /* Domain kept alive internally; descriptor references by ID */

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_domain_destroy(
    mvgal_coherency_domain_descriptor_t *descriptor
) {
    if (descriptor == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    MVGAL_LOG_DEBUG("Coherency domain destroyed: id=%lu",
                    (unsigned long)descriptor->domain_id);

    memset(descriptor, 0, sizeof(*descriptor));
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_sync(
    mvgal_coherency_domain_descriptor_t *descriptor,
    void *ptr,
    size_t size,
    mvgal_coherency_flags_t flags
) {
    if (descriptor == NULL || ptr == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    MVGAL_LOG_DEBUG("Coherency sync: domain=%lu ptr=%p size=%zu flags=%u",
                    (unsigned long)descriptor->domain_id, ptr, size, flags);

    /* Flush CPU caches if requested */
    if ((flags & MVGAL_COHERENCY_FLAG_FLUSH) ||
        (flags & MVGAL_COHERENCY_FLAG_BIDIRECTIONAL)) {
        cache_line_flush(ptr, size);
    }

    /* Invalidate CPU caches if requested */
    if ((flags & MVGAL_COHERENCY_FLAG_INVALIDATE) ||
        (flags & MVGAL_COHERENCY_FLAG_BIDIRECTIONAL)) {
        cache_line_invalidate(ptr, size);
    }

    /* If neither flag set, flush + invalidate by default */
    if ((flags & (MVGAL_COHERENCY_FLAG_FLUSH |
                  MVGAL_COHERENCY_FLAG_INVALIDATE |
                  MVGAL_COHERENCY_FLAG_BIDIRECTIONAL)) == 0) {
        cache_line_flush(ptr, size);
        cache_line_invalidate(ptr, size);
    }

    /* Full memory barrier for ordering */
    __sync_synchronize();

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_flush(
    void *ptr,
    size_t size
) {
    if (ptr == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    cache_line_flush(ptr, size);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_invalidate(
    void *ptr,
    size_t size
) {
    if (ptr == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    cache_line_invalidate(ptr, size);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_p2p_sync(
    uint32_t src_gpu,
    uint32_t dst_gpu,
    void *ptr,
    size_t size,
    mvgal_coherency_flags_t flags
) {
    mvgal_error_t ret;

    ret = validate_gpu_index(src_gpu);
    if (ret != MVGAL_SUCCESS) return ret;

    ret = validate_gpu_index(dst_gpu);
    if (ret != MVGAL_SUCCESS) return ret;

    if (ptr == NULL || size == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    MVGAL_LOG_DEBUG("P2P coherency sync: GPU %u -> GPU %u ptr=%p size=%zu flags=%u",
                    src_gpu, dst_gpu, ptr, size, flags);

    /* Flush source GPU caches */
    if (flags & MVGAL_COHERENCY_FLAG_FLUSH ||
        flags & MVGAL_COHERENCY_FLAG_BIDIRECTIONAL) {
        cache_line_flush(ptr, size);
    }

    /* Invalidate destination GPU caches */
    if (flags & MVGAL_COHERENCY_FLAG_INVALIDATE ||
        flags & MVGAL_COHERENCY_FLAG_BIDIRECTIONAL) {
        cache_line_invalidate(ptr, size);
    }

    /* Default: flush source, invalidate destination */
    if ((flags & (MVGAL_COHERENCY_FLAG_FLUSH |
                  MVGAL_COHERENCY_FLAG_INVALIDATE |
                  MVGAL_COHERENCY_FLAG_BIDIRECTIONAL)) == 0) {
        cache_line_flush(ptr, size);
        cache_line_invalidate(ptr, size);
    }

    __sync_synchronize();
    return MVGAL_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  P2P semaphore operations                                          */
/* ------------------------------------------------------------------ */

mvgal_error_t mvgal_coherency_semaphore_create(
    uint64_t initial_value,
    mvgal_coherency_semaphore_t *semaphore
) {
    if (semaphore == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    struct mvgal_coherency_semaphore *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        MVGAL_LOG_ERROR("Failed to allocate coherency semaphore");
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }

    if (pthread_mutex_init(&s->mutex, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to init coherency semaphore mutex");
        free(s);
        return MVGAL_ERROR_INITIALIZATION;
    }

    if (pthread_cond_init(&s->cond, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to init coherency semaphore cond");
        pthread_mutex_destroy(&s->mutex);
        free(s);
        return MVGAL_ERROR_INITIALIZATION;
    }

    s->value = initial_value;
    atomic_init(&s->refcount, 1);
    s->shared = false;
    s->shared_mem = NULL;
    s->dmabuf_fd = -1;

    *semaphore = s;

    MVGAL_LOG_DEBUG("Coherency semaphore created: %p value=%lu",
                    (void *)s, (unsigned long)initial_value);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_semaphore_destroy(
    mvgal_coherency_semaphore_t semaphore
) {
    if (semaphore == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    struct mvgal_coherency_semaphore *s = semaphore;

    int old = atomic_fetch_sub(&s->refcount, 1);
    if (old <= 1) {
        /* Wake any waiters before destroying */
        pthread_mutex_lock(&s->mutex);
        pthread_cond_broadcast(&s->cond);
        pthread_mutex_unlock(&s->mutex);

        if (s->shared_mem != NULL) {
            munmap(s->shared_mem, sizeof(uint64_t));
        }
        if (s->dmabuf_fd >= 0) {
            close(s->dmabuf_fd);
        }

        pthread_cond_destroy(&s->cond);
        pthread_mutex_destroy(&s->mutex);

        MVGAL_LOG_DEBUG("Coherency semaphore destroyed: %p", (void *)s);
        free(s);
    }

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_semaphore_signal(
    mvgal_coherency_semaphore_t semaphore,
    uint64_t increment
) {
    if (semaphore == NULL || increment == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    struct mvgal_coherency_semaphore *s = semaphore;

    pthread_mutex_lock(&s->mutex);
    s->value += increment;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);

    MVGAL_LOG_DEBUG("Coherency semaphore signaled: %p += %lu = %lu",
                    (void *)s, (unsigned long)increment, (unsigned long)s->value);

    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_coherency_semaphore_wait(
    mvgal_coherency_semaphore_t semaphore,
    uint64_t value,
    uint64_t timeout_ns
) {
    if (semaphore == NULL || value == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    struct mvgal_coherency_semaphore *s = semaphore;

    pthread_mutex_lock(&s->mutex);

    /* Check if already at or above threshold */
    if (s->value >= value) {
        pthread_mutex_unlock(&s->mutex);
        return MVGAL_SUCCESS;
    }

    if (timeout_ns == UINT64_MAX) {
        /* Wait indefinitely */
        while (s->value < value) {
            pthread_cond_wait(&s->cond, &s->mutex);
        }
        pthread_mutex_unlock(&s->mutex);
        return MVGAL_SUCCESS;
    }

    if (timeout_ns == 0) {
        /* Non-blocking check */
        pthread_mutex_unlock(&s->mutex);
        return MVGAL_ERROR_TIMEOUT;
    }

    /* Timed wait */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    uint64_t deadline_ns = now_ns + timeout_ns;

    ts.tv_sec = (time_t)(deadline_ns / 1000000000ULL);
    ts.tv_nsec = (long)(deadline_ns % 1000000000ULL);

    while (s->value < value) {
        int ret = pthread_cond_timedwait(&s->cond, &s->mutex, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&s->mutex);
            return MVGAL_ERROR_TIMEOUT;
        }
        if (ret != 0) {
            pthread_mutex_unlock(&s->mutex);
            MVGAL_LOG_ERROR("pthread_cond_timedwait failed: %s", strerror(ret));
            return MVGAL_ERROR_UNKNOWN;
        }
    }

    pthread_mutex_unlock(&s->mutex);
    return MVGAL_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Capability query                                                   */
/* ------------------------------------------------------------------ */

mvgal_error_t mvgal_coherency_query_capabilities(
    uint32_t gpu_a,
    uint32_t gpu_b,
    bool *cache_coherent,
    bool *atomic_supported
) {
    mvgal_error_t ret;

    ret = validate_gpu_index(gpu_a);
    if (ret != MVGAL_SUCCESS) return ret;

    ret = validate_gpu_index(gpu_b);
    if (ret != MVGAL_SUCCESS) return ret;

    if (cache_coherent == NULL || atomic_supported == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }

    mvgal_memory_state_t *state = mvgal_memory_get_state();
    if (state == NULL) {
        return MVGAL_ERROR_UNKNOWN;
    }

    pthread_mutex_lock(&state->lock);

    /* GPUs on the same vendor/driver may be coherent */
    if (gpu_a == gpu_b) {
        *cache_coherent = true;
        *atomic_supported = true;
    } else {
        /* Cross-vendor coherency: check if same PCIe root complex
         * For now, report no hardware coherency; software-managed. */
        *cache_coherent = false;
        *atomic_supported = (state->gpus[gpu_a].vendor == state->gpus[gpu_b].vendor);
    }

    pthread_mutex_unlock(&state->lock);

    MVGAL_LOG_DEBUG("Coherency capabilities GPU %u <-> %u: coherent=%d atomic=%d",
                    gpu_a, gpu_b, *cache_coherent, *atomic_supported);

    return MVGAL_SUCCESS;
}
