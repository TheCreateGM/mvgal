/**
 * @file sync.c
 * @brief Synchronization primitives implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements fences and semaphores for cross-GPU synchronization.
 */

#include "memory_internal.h"
#include "mvgal/mvgal_log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 * @brief Create a new fence
 *
 * @param context Context to create fence in
 * @param fence Pointer to fence handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_fence_create(mvgal_context_t context, mvgal_fence_t *fence) {
    (void)context; // Context unused for now, for future expansion
    
    if (fence == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_fence *f = calloc(1, sizeof(struct mvgal_fence));
    if (f == NULL) {
        MVGAL_LOG_ERROR("Failed to allocate fence");
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize mutex and condition variable
    if (pthread_mutex_init(&f->mutex, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to initialize fence mutex");
        free(f);
        return MVGAL_ERROR_INITIALIZATION;
    }
    
    if (pthread_cond_init(&f->cond, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to initialize fence condition variable");
        pthread_mutex_destroy(&f->mutex);
        free(f);
        return MVGAL_ERROR_INITIALIZATION;
    }
    
    f->signaled = false;
    f->status = MVGAL_SUCCESS;
    f->user_data = NULL;
    atomic_init(&f->refcount, 1);
    
    *fence = f;
    MVGAL_LOG_DEBUG("Fence created: %p", (void *)f);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Destroy a fence
 *
 * @param fence Fence to destroy
 */
void mvgal_fence_destroy(mvgal_fence_t fence) {
    if (fence == NULL) {
        return;
    }
    
    struct mvgal_fence *f = (struct mvgal_fence *)fence;
    
    // Decrement refcount and check if we should free
    int old_count = atomic_fetch_sub(&f->refcount, 1);
    if (old_count <= 1) {
        // Signal any waiting threads before destroying
        pthread_mutex_lock(&f->mutex);
        f->signaled = true;
        pthread_cond_broadcast(&f->cond);
        pthread_mutex_unlock(&f->mutex);
        
        pthread_mutex_destroy(&f->mutex);
        pthread_cond_destroy(&f->cond);
        MVGAL_LOG_DEBUG("Fence destroyed: %p", (void *)f);
        free(f);
    }
}

/**
 * @brief Wait for a fence to be signaled
 *
 * @param fence Fence to wait for
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return MVGAL_SUCCESS if signaled, MVGAL_ERROR_TIMEOUT if timed out
 */
mvgal_error_t mvgal_fence_wait(mvgal_fence_t fence, uint32_t timeout_ms) {
    if (fence == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_fence *f = (struct mvgal_fence *)fence;
    
    pthread_mutex_lock(&f->mutex);
    
    if (f->signaled) {
        mvgal_error_t status = f->status;
        pthread_mutex_unlock(&f->mutex);
        return status;
    }
    
    if (timeout_ms == 0) {
        // No timeout, wait indefinitely
        while (!f->signaled) {
            pthread_cond_wait(&f->cond, &f->mutex);
        }
        mvgal_error_t status = f->status;
        pthread_mutex_unlock(&f->mutex);
        return status;
    }
    
    // Calculate timeout in nanoseconds
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    ns += (uint64_t)timeout_ms * 1000000ULL;

    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    
    while (!f->signaled) {
        int ret = pthread_cond_timedwait(&f->cond, &f->mutex, &ts);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&f->mutex);
            return MVGAL_ERROR_TIMEOUT;
        }
        if (ret != 0) {
            pthread_mutex_unlock(&f->mutex);
            MVGAL_LOG_ERROR("pthread_cond_timedwait failed: %s", strerror(ret));
            return MVGAL_ERROR_UNKNOWN;
        }
    }
    
    mvgal_error_t status = f->status;
    pthread_mutex_unlock(&f->mutex);
    return status;
}

/**
 * @brief Check if a fence is signaled
 *
 * @param fence Fence to check
 * @return true if signaled, false otherwise
 */
bool mvgal_fence_check(mvgal_fence_t fence) {
    if (fence == NULL) {
        return true; // Treat NULL fence as always signaled
    }
    
    struct mvgal_fence *f = (struct mvgal_fence *)fence;
    pthread_mutex_lock(&f->mutex);
    bool signaled = f->signaled;
    pthread_mutex_unlock(&f->mutex);
    return signaled;
}

/**
 * @brief Reset a fence to unsignaled state
 *
 * @param fence Fence to reset
 */
void mvgal_fence_reset(mvgal_fence_t fence) {
    if (fence == NULL) {
        return;
    }
    
    struct mvgal_fence *f = (struct mvgal_fence *)fence;
    
    pthread_mutex_lock(&f->mutex);
    f->signaled = false;
    f->status = MVGAL_SUCCESS;
    pthread_mutex_unlock(&f->mutex);
    
    MVGAL_LOG_DEBUG("Fence reset: %p", (void *)f);
}

/**
 * @brief Signal a fence
 *
 * This is an internal function. Fences are typically signaled
 * by the system when operations complete.
 *
 * @param fence Fence to signal
 * @param status Status to set when signaling (MVGAL_SUCCESS or error)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_fence_signal_with_status(mvgal_fence_t fence, mvgal_error_t status) {
    if (fence == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_fence *f = (struct mvgal_fence *)fence;
    
    pthread_mutex_lock(&f->mutex);
    f->signaled = true;
    f->status = status;
    pthread_cond_broadcast(&f->cond);
    pthread_mutex_unlock(&f->mutex);
    
    MVGAL_LOG_DEBUG("Fence signaled: %p with status %d", (void *)f, status);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Create a new semaphore
 *
 * @param context Context to create semaphore in
 * @param initial_value Initial semaphore value
 * @param semaphore Pointer to semaphore handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_semaphore_create(
    mvgal_context_t context,
    uint64_t initial_value,
    mvgal_semaphore_t *semaphore
) {
    (void)context; // Context unused for now
    
    if (semaphore == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_semaphore *s = calloc(1, sizeof(struct mvgal_semaphore));
    if (s == NULL) {
        MVGAL_LOG_ERROR("Failed to allocate semaphore");
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    // Initialize mutex and condition variable
    if (pthread_mutex_init(&s->mutex, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to initialize semaphore mutex");
        free(s);
        return MVGAL_ERROR_INITIALIZATION;
    }
    
    if (pthread_cond_init(&s->cond, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to initialize semaphore condition variable");
        pthread_mutex_destroy(&s->mutex);
        free(s);
        return MVGAL_ERROR_INITIALIZATION;
    }
    
    s->count = initial_value;
    atomic_init(&s->refcount, 1);
    
    *semaphore = s;
    MVGAL_LOG_DEBUG("Semaphore created: %p with value %lu", (void *)s, initial_value);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Destroy a semaphore
 *
 * @param semaphore Semaphore to destroy
 */
void mvgal_semaphore_destroy(mvgal_semaphore_t semaphore) {
    if (semaphore == NULL) {
        return;
    }
    
    struct mvgal_semaphore *s = (struct mvgal_semaphore *)semaphore;
    
    int old_count = atomic_fetch_sub(&s->refcount, 1);
    if (old_count <= 1) {
        // Signal any waiting threads before destroying
        pthread_mutex_lock(&s->mutex);
        pthread_cond_broadcast(&s->cond);
        pthread_mutex_unlock(&s->mutex);
        
        pthread_mutex_destroy(&s->mutex);
        pthread_cond_destroy(&s->cond);
        MVGAL_LOG_DEBUG("Semaphore destroyed: %p", (void *)s);
        free(s);
    }
}

/**
 * @brief Signal a semaphore
 *
 * Adds value to the semaphore count and wakes waiting threads.
 *
 * @param semaphore Semaphore to signal
 * @param value Value to add to semaphore
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_semaphore_signal(mvgal_semaphore_t semaphore, uint64_t value) {
    if (semaphore == NULL || value == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_semaphore *s = (struct mvgal_semaphore *)semaphore;
    
    pthread_mutex_lock(&s->mutex);
    s->count += value;
    
    // Wake up waiting threads - one per value added
    for (uint64_t i = 0; i < value; i++) {
        pthread_cond_signal(&s->cond);
    }
    
    pthread_mutex_unlock(&s->mutex);
    MVGAL_LOG_DEBUG("Semaphore signaled: %p, added %lu, new count: %lu", 
                   (void *)s, value, s->count);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Wait on a semaphore
 *
 * Subtracts value from the semaphore count, waiting if necessary.
 *
 * @param semaphore Semaphore to wait on
 * @param value Value to subtract from semaphore
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return MVGAL_SUCCESS if wait succeeded, MVGAL_ERROR_TIMEOUT if timed out
 */
mvgal_error_t mvgal_semaphore_wait(
    mvgal_semaphore_t semaphore,
    uint64_t value,
    uint32_t timeout_ms
) {
    if (semaphore == NULL || value == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_semaphore *s = (struct mvgal_semaphore *)semaphore;
    
    pthread_mutex_lock(&s->mutex);
    
    if (s->count >= value) {
        s->count -= value;
        pthread_mutex_unlock(&s->mutex);
        return MVGAL_SUCCESS;
    }
    
    if (timeout_ms == 0) {
        // Wait indefinitely
        while (s->count < value) {
            pthread_cond_wait(&s->cond, &s->mutex);
        }
        s->count -= value;
        pthread_mutex_unlock(&s->mutex);
        return MVGAL_SUCCESS;
    }
    
    // Calculate timeout
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    ns += (uint64_t)timeout_ms * 1000000ULL;

    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    
    while (s->count < value) {
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
    
    s->count -= value;
    pthread_mutex_unlock(&s->mutex);
    return MVGAL_SUCCESS;
}

/**
 * @brief Get the value of a semaphore
 *
 * @param semaphore Semaphore to query
 * @param value Pointer to value (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_semaphore_get_value(mvgal_semaphore_t semaphore, uint64_t *value) {
    if (semaphore == NULL || value == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    struct mvgal_semaphore *s = (struct mvgal_semaphore *)semaphore;
    pthread_mutex_lock(&s->mutex);
    *value = s->count;
    pthread_mutex_unlock(&s->mutex);
    return MVGAL_SUCCESS;
}
