// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * @file mvgal_ntsync.h
 * @brief Windows NTSYNC/Fsync compatibility wrappers for Wine/Proton
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header provides synchronization primitives that map Windows NT
 * synchronization APIs to Linux futex/eventfd primitives, enabling
 * efficient cross-process and cross-GPU synchronization under
 * Wine/Proton.
 *
 * References:
 *   - Wine NTSYNC (ntdll.Nt* synchronization functions)
 *   - Linux FUTEX_WAIT/WAKE (kernel 2.6+)
 *   - eventfd (kernel 2.6.22+)
 */

#ifndef MVGAL_NTSYNC_H
#define MVGAL_NTSYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup NtSync
 * @{
 */

/* ----------------------------------------------------------------- */
/*  Type definitions                                                 */
/* ----------------------------------------------------------------- */

/** Opaque handle types */
typedef struct mvgal_nt_event     *mvgal_nt_event_t;
typedef struct mvgal_nt_mutex     *mvgal_nt_mutex_t;
typedef struct mvgal_nt_semaphore *mvgal_nt_semaphore_t;
typedef struct mvgal_nt_timer     *mvgal_nt_timer_t;

/**
 * @brief NTSYNC object type
 */
typedef enum {
    MVGAL_NTSYNC_TYPE_EVENT,
    MVGAL_NTSYNC_TYPE_MUTEX,
    MVGAL_NTSYNC_TYPE_SEMAPHORE,
    MVGAL_NTSYNC_TYPE_TIMER,
} mvgal_ntsync_type_t;

/**
 * @brief Event types (Windows NtCreateEvent)
 */
typedef enum {
    MVGAL_NT_EVENT_TYPE_NOTIFICATION  = 0,  /* Manual-reset event */
    MVGAL_NT_EVENT_TYPE_SYNCHRONIZATION = 1, /* Auto-reset event */
} mvgal_nt_event_type_t;

/**
 * @brief Event state
 */
typedef enum {
    MVGAL_NT_EVENT_STATE_RESET = 0,
    MVGAL_NT_EVENT_STATE_SIGNALED = 1,
} mvgal_nt_event_state_t;

/**
 * @brief Timer type (Windows NtCreateTimer)
 */
typedef enum {
    MVGAL_NT_TIMER_TYPE_NOTIFICATION    = 0,  /* Manual-reset timer */
    MVGAL_NT_TIMER_TYPE_SYNCHRONIZATION = 1,  /* Auto-reset timer */
} mvgal_nt_timer_type_t;

/**
 * @brief Wait type
 */
typedef enum {
    MVGAL_NT_WAIT_TYPE_ALL = 0,  /* WaitForMultipleObjects: wait for ALL */
    MVGAL_NT_WAIT_TYPE_ANY = 1,  /* WaitForMultipleObjects: wait for ANY */
} mvgal_nt_wait_type_t;

/**
 * @brief Wait result
 */
typedef enum {
    MVGAL_NT_WAIT_OBJECT_0  = 0,       /* Object N signaled */
    MVGAL_NT_WAIT_TIMEOUT   = 0x102,   /* WAIT_TIMEOUT */
    MVGAL_NT_WAIT_ABANDONED = 0x080,   /* WAIT_ABANDONED (mutex) */
    MVGAL_NT_WAIT_FAILED    = 0xFFFFFFFF, /* WAIT_FAILED */
} mvgal_nt_wait_result_t;

/**
 * @brief NTSYNC object state (generic)
 */
typedef struct {
    mvgal_ntsync_type_t type;
    int event_fd;               /* eventfd for wait/wake */
    bool signaled;
} mvgal_ntsync_object_t;

/* ----------------------------------------------------------------- */
/*  Event API                                                        */
/* ----------------------------------------------------------------- */

/**
 * @brief Create an NT event object
 *
 * @param event_type Notification (manual-reset) or Synchronization (auto-reset)
 * @param initial_state Initial signaled state
 * @param event Event handle (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_event_create(
    mvgal_nt_event_type_t event_type,
    mvgal_nt_event_state_t initial_state,
    mvgal_nt_event_t *event
);

/**
 * @brief Destroy an NT event object
 *
 * @param event Event to destroy
 */
void mvgal_nt_event_destroy(mvgal_nt_event_t event);

/**
 * @brief Set event to signaled state
 *
 * For auto-reset events, wakes one waiter.
 * For manual-reset events, wakes all waiters.
 *
 * @param event Event to signal
 * @return 0 on success, -1 on error
 */
int mvgal_nt_event_set(mvgal_nt_event_t event);

/**
 * @brief Reset event to non-signaled state
 *
 * @param event Event to reset
 * @return 0 on success, -1 on error
 */
int mvgal_nt_event_reset(mvgal_nt_event_t event);

/**
 * @brief Pulse event (signal then reset)
 *
 * Wakes all current waiters and immediately resets.
 *
 * @param event Event to pulse
 * @return 0 on success, -1 on error
 */
int mvgal_nt_event_pulse(mvgal_nt_event_t event);

/**
 * @brief Query event state
 *
 * @param event Event to query
 * @param signaled Set to true if signaled (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_event_query(mvgal_nt_event_t event, bool *signaled);

/* ----------------------------------------------------------------- */
/*  Mutex API                                                        */
/* ----------------------------------------------------------------- */

/**
 * @brief Create an NT mutex object
 *
 * @param initial_owner If true, mutex is owned by caller
 * @param abandon_wakeup_count Number of times to wake abandoned
 * @param mutex Mutex handle (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_mutex_create(
    bool initial_owner,
    uint32_t abandon_wakeup_count,
    mvgal_nt_mutex_t *mutex
);

/**
 * @brief Destroy an NT mutex object
 *
 * @param mutex Mutex to destroy
 */
void mvgal_nt_mutex_destroy(mvgal_nt_mutex_t mutex);

/**
 * @brief Acquire mutex (with optional timeout)
 *
 * @param mutex Mutex to acquire
 * @param timeout_ns Timeout in ns (0 = non-blocking, UINT64_MAX = infinite)
 * @return MVGAL_NT_WAIT_OBJECT_0 on success, MVGAL_NT_WAIT_TIMEOUT on timeout
 */
mvgal_nt_wait_result_t mvgal_nt_mutex_lock(
    mvgal_nt_mutex_t mutex,
    uint64_t timeout_ns
);

/**
 * @brief Release mutex
 *
 * @param mutex Mutex to release
 * @return 0 on success, -1 on error
 */
int mvgal_nt_mutex_unlock(mvgal_nt_mutex_t mutex);

/**
 * @brief Query mutex state
 *
 * @param mutex Mutex to query
 * @param owner_thread Set to owner thread id (out)
 * @param count Set to recursion count (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_mutex_query(
    mvgal_nt_mutex_t mutex,
    uint64_t *owner_thread,
    uint32_t *count
);

/* ----------------------------------------------------------------- */
/*  Semaphore API                                                    */
/* ----------------------------------------------------------------- */

/**
 * @brief Create an NT semaphore object
 *
 * @param initial_count Initial count
 * @param max_count Maximum count
 * @param semaphore Semaphore handle (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_semaphore_create(
    uint32_t initial_count,
    uint32_t max_count,
    mvgal_nt_semaphore_t *semaphore
);

/**
 * @brief Destroy an NT semaphore object
 *
 * @param semaphore Semaphore to destroy
 */
void mvgal_nt_semaphore_destroy(mvgal_nt_semaphore_t semaphore);

/**
 * @brief Release semaphore (add count)
 *
 * @param semaphore Semaphore to release
 * @param release_count Amount to add
 * @param previous_count Previous count (out, optional)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_semaphore_release(
    mvgal_nt_semaphore_t semaphore,
    uint32_t release_count,
    uint32_t *previous_count
);

/**
 * @brief Wait on semaphore (with optional timeout)
 *
 * @param semaphore Semaphore to wait on
 * @param timeout_ns Timeout in ns (0 = non-blocking, UINT64_MAX = infinite)
 * @return MVGAL_NT_WAIT_OBJECT_0 on success, MVGAL_NT_WAIT_TIMEOUT on timeout
 */
mvgal_nt_wait_result_t mvgal_nt_semaphore_wait(
    mvgal_nt_semaphore_t semaphore,
    uint64_t timeout_ns
);

/**
 * @brief Query semaphore state
 *
 * @param semaphore Semaphore to query
 * @param current_count Current count (out)
 * @param max_count Maximum count (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_semaphore_query(
    mvgal_nt_semaphore_t semaphore,
    uint32_t *current_count,
    uint32_t *max_count
);

/* ----------------------------------------------------------------- */
/*  Timer API                                                        */
/* ----------------------------------------------------------------- */

/**
 * @brief Create an NT timer object
 *
 * @param timer_type Notification or Synchronization
 * @param timer Timer handle (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_timer_create(
    mvgal_nt_timer_type_t timer_type,
    mvgal_nt_timer_t *timer
);

/**
 * @brief Destroy an NT timer object
 *
 * @param timer Timer to destroy
 */
void mvgal_nt_timer_destroy(mvgal_nt_timer_t timer);

/**
 * @brief Set timer (absolute or relative)
 *
 * @param timer Timer to set
 * @param due_time_ns Due time in ns from now (relative), or absolute
 * @param period_ms Periodic interval in ms (0 = one-shot)
 * @param is_absolute If true, due_time_ns is absolute CLOCK_MONOTONIC
 * @return 0 on success, -1 on error
 */
int mvgal_nt_timer_set(
    mvgal_nt_timer_t timer,
    uint64_t due_time_ns,
    uint32_t period_ms,
    bool is_absolute
);

/**
 * @brief Cancel timer
 *
 * @param timer Timer to cancel
 * @return 0 on success, -1 on error
 */
int mvgal_nt_timer_cancel(mvgal_nt_timer_t timer);

/**
 * @brief Query timer state
 *
 * @param timer Timer to query
 * @param signaled Set to true if signaled (out)
 * @return 0 on success, -1 on error
 */
int mvgal_nt_timer_query(mvgal_nt_timer_t timer, bool *signaled);

/* ----------------------------------------------------------------- */
/*  Multi-wait API (WaitForMultipleObjects equivalent)               */
/* ----------------------------------------------------------------- */

/**
 * @brief Wait on multiple synchronization objects
 *
 * Equivalent to WaitForMultipleObjects on Windows.
 * Supports events, mutexes, and semaphores.
 *
 * @param count Number of objects
 * @param events Array of event handles
 * @param wait_type MVGAL_NT_WAIT_TYPE_ALL or MVGAL_NT_WAIT_TYPE_ANY
 * @param timeout_ns Timeout in ns (0 = non-blocking, UINT64_MAX = infinite)
 * @param signaled_index Index of signaled object (out, for WAIT_ANY)
 * @return MVGAL_NT_WAIT_OBJECT_0+index, or MVGAL_NT_WAIT_TIMEOUT
 */
mvgal_nt_wait_result_t mvgal_nt_wait_multiple(
    uint32_t count,
    mvgal_nt_event_t *events,
    mvgal_nt_wait_type_t wait_type,
    uint64_t timeout_ns,
    uint32_t *signaled_index
);

/**
 * @brief Wait on multiple heterogenous objects (events, mutexes, semaphores)
 *
 * @param count Number of objects
 * @param objects Array of object pointers
 * @param types Array of object types
 * @param wait_type ALL or ANY
 * @param timeout_ns Timeout in ns
 * @param signaled_index Index of signaled object (out)
 * @return MVGAL_NT_WAIT_OBJECT_0+index, or MVGAL_NT_WAIT_TIMEOUT
 */
mvgal_nt_wait_result_t mvgal_nt_wait_multiple_any(
    uint32_t count,
    void **objects,
    mvgal_ntsync_type_t *types,
    mvgal_nt_wait_type_t wait_type,
    uint64_t timeout_ns,
    uint32_t *signaled_index
);

/* ----------------------------------------------------------------- */
/*  Utility                                                          */
/* ----------------------------------------------------------------- */

/**
 * @brief Convert timespec to absolute timeout in ns for CLOCK_MONOTONIC
 *
 * @param timeout_ns Relative timeout in nanoseconds
 * @param abs_ts Absolute timespec (out)
 */
void mvgal_nt_timeout_to_abs(
    uint64_t timeout_ns,
    struct timespec *abs_ts
);

/**
 * @brief Get eventfd descriptor for an NTSYNC object (for poll/epoll)
 *
 * Allows integrating NTSYNC objects with event loops.
 *
 * @param obj NTSYNC object
 * @param type Object type
 * @return file descriptor, or -1 if not supported
 */
int mvgal_nt_object_get_fd(
    void *obj,
    mvgal_ntsync_type_t type
);

/** @} */ // end of NtSync

#ifdef __cplusplus
}
#endif

#endif // MVGAL_NTSYNC_H
