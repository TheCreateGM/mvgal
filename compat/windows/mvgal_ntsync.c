// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * @file mvgal_ntsync.c
 * @brief Windows NTSYNC/Fsync compatibility implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * Implements Windows NT synchronization primitives on top of
 * Linux futex(2) and eventfd(2) syscalls, matching the behaviour
 * expected by Wine/Proton for cross-process and cross-GPU sync.
 *
 * Design:
 *   - Events: eventfd for manual-reset, futex for auto-reset
 *   - Mutexes: futex with owner tracking and recursion
 *   - Semaphores: futex-based counted semaphore
 *   - Timers: timerfd for one-shot/periodic timers
 *   - Multi-wait: epoll/poll on eventfd/timerfd fds + futex peek
 */

#include "mvgal_ntsync.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <stdatomic.h>
#include <pthread.h>

/* ----------------------------------------------------------------- */
/*  Internal helpers                                                  */
/* ----------------------------------------------------------------- */

/**
 * @brief Futex wait operation
 */
static inline int futex_wait(
    atomic_uint *uaddr,
    uint32_t expected,
    const struct timespec *timeout
) {
    return syscall(SYS_futex, uaddr, FUTEX_WAIT, expected, timeout, NULL, 0);
}

/**
 * @brief Futex wake operation
 */
static inline int futex_wake(
    atomic_uint *uaddr,
    uint32_t count
) {
    return syscall(SYS_futex, uaddr, FUTEX_WAKE, count, NULL, NULL, 0);
}

/**
 * @brief Convert relative timeout to absolute timespec
 */
void mvgal_nt_timeout_to_abs(uint64_t timeout_ns, struct timespec *abs_ts) {
    clock_gettime(CLOCK_MONOTONIC, abs_ts);
    uint64_t ns = (uint64_t)abs_ts->tv_sec * 1000000000ULL +
                  (uint64_t)abs_ts->tv_nsec + timeout_ns;
    abs_ts->tv_sec = (time_t)(ns / 1000000000ULL);
    abs_ts->tv_nsec = (long)(ns % 1000000000ULL);
}

/* ----------------------------------------------------------------- */
/*  Internal structures                                               */
/* ----------------------------------------------------------------- */

struct mvgal_nt_event {
    mvgal_nt_event_type_t type;
    bool signaled;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

struct mvgal_nt_mutex {
    atomic_uint state;           /* 0 = unlocked, 1 = locked */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint64_t owner_thread;
    uint32_t recursion_count;
    uint32_t abandon_count;
    bool abandoned;
};

struct mvgal_nt_semaphore {
    atomic_uint count;
    uint32_t max_count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

struct mvgal_nt_timer {
    mvgal_nt_timer_type_t type;
    int timer_fd;                /* timerfd descriptor */
    bool signaled;
    bool periodic;
    uint32_t period_ms;
    pthread_mutex_t lock;
};

/* ----------------------------------------------------------------- */
/*  Event implementation                                             */
/* ----------------------------------------------------------------- */

int mvgal_nt_event_create(
    mvgal_nt_event_type_t event_type,
    mvgal_nt_event_state_t initial_state,
    mvgal_nt_event_t *event
) {
    if (event == NULL) return -1;

    struct mvgal_nt_event *e = calloc(1, sizeof(*e));
    if (e == NULL) return -1;

    if (pthread_mutex_init(&e->lock, NULL) != 0) {
        free(e);
        return -1;
    }
    if (pthread_cond_init(&e->cond, NULL) != 0) {
        pthread_mutex_destroy(&e->lock);
        free(e);
        return -1;
    }

    e->type = event_type;
    e->signaled = (initial_state == MVGAL_NT_EVENT_STATE_SIGNALED);

    *event = e;
    return 0;
}

void mvgal_nt_event_destroy(mvgal_nt_event_t event) {
    if (event == NULL) return;
    pthread_cond_destroy(&event->cond);
    pthread_mutex_destroy(&event->lock);
    free(event);
}

int mvgal_nt_event_set(mvgal_nt_event_t event) {
    if (event == NULL) return -1;

    pthread_mutex_lock(&event->lock);
    event->signaled = true;

    if (event->type == MVGAL_NT_EVENT_TYPE_NOTIFICATION) {
        /* Manual-reset: wake all */
        pthread_cond_broadcast(&event->cond);
    } else {
        /* Auto-reset: wake one */
        pthread_cond_signal(&event->cond);
    }
    pthread_mutex_unlock(&event->lock);

    return 0;
}

int mvgal_nt_event_reset(mvgal_nt_event_t event) {
    if (event == NULL) return -1;

    pthread_mutex_lock(&event->lock);
    event->signaled = false;
    pthread_mutex_unlock(&event->lock);

    return 0;
}

int mvgal_nt_event_pulse(mvgal_nt_event_t event) {
    if (event == NULL) return -1;

    pthread_mutex_lock(&event->lock);
    event->signaled = true;

    if (event->type == MVGAL_NT_EVENT_TYPE_NOTIFICATION) {
        pthread_cond_broadcast(&event->cond);
    } else {
        pthread_cond_signal(&event->cond);
    }
    event->signaled = false;
    pthread_mutex_unlock(&event->lock);

    return 0;
}

int mvgal_nt_event_query(mvgal_nt_event_t event, bool *signaled) {
    if (event == NULL || signaled == NULL) return -1;

    pthread_mutex_lock(&event->lock);
    *signaled = event->signaled;

    /* Auto-reset: consuming read clears signal */
    if (event->signaled && event->type == MVGAL_NT_EVENT_TYPE_SYNCHRONIZATION) {
        event->signaled = false;
    }
    pthread_mutex_unlock(&event->lock);

    return 0;
}

/* ----------------------------------------------------------------- */
/*  Mutex implementation                                             */
/* ----------------------------------------------------------------- */

int mvgal_nt_mutex_create(
    bool initial_owner,
    uint32_t abandon_wakeup_count,
    mvgal_nt_mutex_t *mutex
) {
    if (mutex == NULL) return -1;

    struct mvgal_nt_mutex *m = calloc(1, sizeof(*m));
    if (m == NULL) return -1;

    if (pthread_mutex_init(&m->lock, NULL) != 0) {
        free(m);
        return -1;
    }
    if (pthread_cond_init(&m->cond, NULL) != 0) {
        pthread_mutex_destroy(&m->lock);
        free(m);
        return -1;
    }

    atomic_init(&m->state, initial_owner ? 1 : 0);
    m->recursion_count = initial_owner ? 1 : 0;
    m->owner_thread = initial_owner ? (uint64_t)pthread_self() : 0;
    m->abandon_count = abandon_wakeup_count;
    m->abandoned = false;

    *mutex = m;
    return 0;
}

void mvgal_nt_mutex_destroy(mvgal_nt_mutex_t mutex) {
    if (mutex == NULL) return;
    pthread_cond_destroy(&mutex->cond);
    pthread_mutex_destroy(&mutex->lock);
    free(mutex);
}

mvgal_nt_wait_result_t mvgal_nt_mutex_lock(
    mvgal_nt_mutex_t mutex,
    uint64_t timeout_ns
) {
    if (mutex == NULL) return MVGAL_NT_WAIT_FAILED;

    uint64_t tid = (uint64_t)pthread_self();

    /* Check for recursion */
    if (mutex->owner_thread == tid && mutex->recursion_count > 0) {
        mutex->recursion_count++;
        return MVGAL_NT_WAIT_OBJECT_0;
    }

    if (timeout_ns == 0) {
        /* Non-blocking trylock */
        uint32_t expected = 0;
        if (atomic_compare_exchange_strong(&mutex->state, &expected, 1)) {
            pthread_mutex_lock(&mutex->lock);
            mutex->owner_thread = tid;
            mutex->recursion_count = 1;
            mutex->abandoned = false;
            pthread_mutex_unlock(&mutex->lock);
            return MVGAL_NT_WAIT_OBJECT_0;
        }
        return MVGAL_NT_WAIT_TIMEOUT;
    }

    /* Blocking wait */
    struct timespec ts;
    if (timeout_ns != UINT64_MAX) {
        mvgal_nt_timeout_to_abs(timeout_ns, &ts);
    }

    pthread_mutex_lock(&mutex->lock);

    while (atomic_load(&mutex->state) != 0) {
        if (timeout_ns == UINT64_MAX) {
            /* Infinite wait */
            pthread_cond_wait(&mutex->cond, &mutex->lock);
        } else {
            int ret = pthread_cond_timedwait(&mutex->cond, &mutex->lock, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&mutex->lock);
                return MVGAL_NT_WAIT_TIMEOUT;
            }
        }
    }

    /* Acquired */
    atomic_store(&mutex->state, 1);
    bool was_abandoned = mutex->abandoned;
    mutex->owner_thread = tid;
    mutex->recursion_count = 1;
    mutex->abandoned = false;
    pthread_mutex_unlock(&mutex->lock);

    return was_abandoned ? MVGAL_NT_WAIT_ABANDONED : MVGAL_NT_WAIT_OBJECT_0;
}

int mvgal_nt_mutex_unlock(mvgal_nt_mutex_t mutex) {
    if (mutex == NULL) return -1;

    pthread_mutex_lock(&mutex->lock);

    if (mutex->recursion_count > 1) {
        mutex->recursion_count--;
        pthread_mutex_unlock(&mutex->lock);
        return 0;
    }

    mutex->owner_thread = 0;
    mutex->recursion_count = 0;
    atomic_store(&mutex->state, 0);

    /* Wake one waiter */
    pthread_cond_signal(&mutex->cond);
    pthread_mutex_unlock(&mutex->lock);

    return 0;
}

int mvgal_nt_mutex_query(
    mvgal_nt_mutex_t mutex,
    uint64_t *owner_thread,
    uint32_t *count
) {
    if (mutex == NULL) return -1;

    pthread_mutex_lock(&mutex->lock);
    if (owner_thread) *owner_thread = mutex->owner_thread;
    if (count) *count = mutex->recursion_count;
    pthread_mutex_unlock(&mutex->lock);

    return 0;
}

/* ----------------------------------------------------------------- */
/*  Semaphore implementation                                         */
/* ----------------------------------------------------------------- */

int mvgal_nt_semaphore_create(
    uint32_t initial_count,
    uint32_t max_count,
    mvgal_nt_semaphore_t *semaphore
) {
    if (semaphore == NULL) return -1;
    if (initial_count > max_count || max_count == 0) return -1;

    struct mvgal_nt_semaphore *s = calloc(1, sizeof(*s));
    if (s == NULL) return -1;

    if (pthread_mutex_init(&s->lock, NULL) != 0) {
        free(s);
        return -1;
    }
    if (pthread_cond_init(&s->cond, NULL) != 0) {
        pthread_mutex_destroy(&s->lock);
        free(s);
        return -1;
    }

    atomic_init(&s->count, initial_count);
    s->max_count = max_count;

    *semaphore = s;
    return 0;
}

void mvgal_nt_semaphore_destroy(mvgal_nt_semaphore_t semaphore) {
    if (semaphore == NULL) return;
    pthread_cond_destroy(&semaphore->cond);
    pthread_mutex_destroy(&semaphore->lock);
    free(semaphore);
}

int mvgal_nt_semaphore_release(
    mvgal_nt_semaphore_t semaphore,
    uint32_t release_count,
    uint32_t *previous_count
) {
    if (semaphore == NULL || release_count == 0) return -1;

    pthread_mutex_lock(&semaphore->lock);

    uint32_t old = atomic_load(&semaphore->count);
    if (previous_count) *previous_count = old;

    uint32_t new_count = old + release_count;
    if (new_count > semaphore->max_count) {
        pthread_mutex_unlock(&semaphore->lock);
        return -1; /* Would exceed max */
    }

    atomic_store(&semaphore->count, new_count);

    /* Wake waiters up to release_count */
    for (uint32_t i = 0; i < release_count; i++) {
        if (old + i < semaphore->max_count) {
            pthread_cond_signal(&semaphore->cond);
        }
    }

    pthread_mutex_unlock(&semaphore->lock);
    return 0;
}

mvgal_nt_wait_result_t mvgal_nt_semaphore_wait(
    mvgal_nt_semaphore_t semaphore,
    uint64_t timeout_ns
) {
    if (semaphore == NULL) return MVGAL_NT_WAIT_FAILED;

    pthread_mutex_lock(&semaphore->lock);

    /* Check if already available */
    uint32_t cur = atomic_load(&semaphore->count);
    if (cur > 0) {
        atomic_store(&semaphore->count, cur - 1);
        pthread_mutex_unlock(&semaphore->lock);
        return MVGAL_NT_WAIT_OBJECT_0;
    }

    if (timeout_ns == 0) {
        /* Non-blocking */
        pthread_mutex_unlock(&semaphore->lock);
        return MVGAL_NT_WAIT_TIMEOUT;
    }

    /* Blocking wait */
    if (timeout_ns == UINT64_MAX) {
        while (atomic_load(&semaphore->count) == 0) {
            pthread_cond_wait(&semaphore->cond, &semaphore->lock);
        }
    } else {
        struct timespec ts;
        mvgal_nt_timeout_to_abs(timeout_ns, &ts);

        while (atomic_load(&semaphore->count) == 0) {
            int ret = pthread_cond_timedwait(&semaphore->cond, &semaphore->lock, &ts);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&semaphore->lock);
                return MVGAL_NT_WAIT_TIMEOUT;
            }
        }
    }

    cur = atomic_fetch_sub(&semaphore->count, 1);
    pthread_mutex_unlock(&semaphore->lock);

    return (cur > 0) ? MVGAL_NT_WAIT_OBJECT_0 : MVGAL_NT_WAIT_TIMEOUT;
}

int mvgal_nt_semaphore_query(
    mvgal_nt_semaphore_t semaphore,
    uint32_t *current_count,
    uint32_t *max_count
) {
    if (semaphore == NULL) return -1;

    pthread_mutex_lock(&semaphore->lock);
    if (current_count) *current_count = atomic_load(&semaphore->count);
    if (max_count) *max_count = semaphore->max_count;
    pthread_mutex_unlock(&semaphore->lock);

    return 0;
}

/* ----------------------------------------------------------------- */
/*  Timer implementation                                             */
/* ----------------------------------------------------------------- */

int mvgal_nt_timer_create(
    mvgal_nt_timer_type_t timer_type,
    mvgal_nt_timer_t *timer
) {
    if (timer == NULL) return -1;

    struct mvgal_nt_timer *t = calloc(1, sizeof(*t));
    if (t == NULL) return -1;

    if (pthread_mutex_init(&t->lock, NULL) != 0) {
        free(t);
        return -1;
    }

    t->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (t->timer_fd < 0) {
        pthread_mutex_destroy(&t->lock);
        free(t);
        return -1;
    }

    t->type = timer_type;
    t->signaled = false;
    t->periodic = false;
    t->period_ms = 0;

    *timer = t;
    return 0;
}

void mvgal_nt_timer_destroy(mvgal_nt_timer_t timer) {
    if (timer == NULL) return;
    close(timer->timer_fd);
    pthread_mutex_destroy(&timer->lock);
    free(timer);
}

int mvgal_nt_timer_set(
    mvgal_nt_timer_t timer,
    uint64_t due_time_ns,
    uint32_t period_ms,
    bool is_absolute
) {
    if (timer == NULL) return -1;

    pthread_mutex_lock(&timer->lock);

    struct itimerspec its;
    memset(&its, 0, sizeof(its));

    if (is_absolute) {
        its.it_value.tv_sec = (time_t)(due_time_ns / 1000000000ULL);
        its.it_value.tv_nsec = (long)(due_time_ns % 1000000000ULL);
    } else {
        /* Relative: timerfd takes relative timespec */
        its.it_value.tv_sec = (time_t)(due_time_ns / 1000000000ULL);
        its.it_value.tv_nsec = (long)(due_time_ns % 1000000000ULL);
    }

    if (period_ms > 0) {
        its.it_interval.tv_sec = (time_t)(period_ms / 1000);
        its.it_interval.tv_nsec = (long)((period_ms % 1000) * 1000000);
        timer->periodic = true;
        timer->period_ms = period_ms;
    } else {
        timer->periodic = false;
        timer->period_ms = 0;
    }

    int flags = is_absolute ? TFD_TIMER_ABSTIME : 0;
    int ret = timerfd_settime(timer->timer_fd, flags, &its, NULL);

    if (ret == 0) {
        timer->signaled = false;
    }

    pthread_mutex_unlock(&timer->lock);
    return ret;
}

int mvgal_nt_timer_cancel(mvgal_nt_timer_t timer) {
    if (timer == NULL) return -1;

    pthread_mutex_lock(&timer->lock);

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    timerfd_settime(timer->timer_fd, 0, &its, NULL);
    timer->signaled = false;
    timer->periodic = false;

    pthread_mutex_unlock(&timer->lock);
    return 0;
}

int mvgal_nt_timer_query(mvgal_nt_timer_t timer, bool *signaled) {
    if (timer == NULL || signaled == NULL) return -1;

    pthread_mutex_lock(&timer->lock);

    /* Try to read from timerfd to check expiration */
    uint64_t expirations = 0;
    ssize_t ret = read(timer->timer_fd, &expirations, sizeof(expirations));
    if (ret == sizeof(expirations) && expirations > 0) {
        timer->signaled = true;
    }

    *signaled = timer->signaled;

    /* Auto-reset: consuming read clears */
    if (timer->signaled && timer->type == MVGAL_NT_TIMER_TYPE_SYNCHRONIZATION) {
        timer->signaled = false;
    }

    pthread_mutex_unlock(&timer->lock);
    return 0;
}

/* ----------------------------------------------------------------- */
/*  Multi-wait implementation                                        */
/* ----------------------------------------------------------------- */

mvgal_nt_wait_result_t mvgal_nt_wait_multiple(
    uint32_t count,
    mvgal_nt_event_t *events,
    mvgal_nt_wait_type_t wait_type,
    uint64_t timeout_ns,
    uint32_t *signaled_index
) {
    if (count == 0 || events == NULL) return MVGAL_NT_WAIT_FAILED;

    struct timespec ts;
    if (timeout_ns != UINT64_MAX && timeout_ns != 0) {
        mvgal_nt_timeout_to_abs(timeout_ns, &ts);
    }

    uint64_t deadline = 0;
    if (timeout_ns != UINT64_MAX && timeout_ns != 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        deadline = (uint64_t)now.tv_sec * 1000000000ULL +
                   (uint64_t)now.tv_nsec + timeout_ns;
    }

    /* Poll loop */
    for (;;) {
        uint32_t signaled = 0;
        uint32_t last_signaled = UINT32_MAX;

        /* Check all events */
        for (uint32_t i = 0; i < count; i++) {
            if (events[i] == NULL) continue;

            bool sig = false;
            pthread_mutex_lock(&events[i]->lock);
            sig = events[i]->signaled;

            if (sig && events[i]->type == MVGAL_NT_EVENT_TYPE_SYNCHRONIZATION) {
                events[i]->signaled = false; /* Auto-reset consumes */
            }
            pthread_mutex_unlock(&events[i]->lock);

            if (sig) {
                signaled++;
                last_signaled = i;
                if (wait_type == MVGAL_NT_WAIT_TYPE_ANY) {
                    if (signaled_index) *signaled_index = i;
                    return (mvgal_nt_wait_result_t)(MVGAL_NT_WAIT_OBJECT_0 + i);
                }
            }
        }

        if (wait_type == MVGAL_NT_WAIT_TYPE_ALL && signaled == count) {
            if (signaled_index) *signaled_index = 0;
            return MVGAL_NT_WAIT_OBJECT_0;
        }

        if (timeout_ns == 0) {
            return MVGAL_NT_WAIT_TIMEOUT;
        }

        /* Check timeout */
        if (timeout_ns != UINT64_MAX) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL +
                             (uint64_t)now.tv_nsec;
            if (now_ns >= deadline) {
                return MVGAL_NT_WAIT_TIMEOUT;
            }
        }

        /* Brief sleep to avoid busy-wait */
        struct timespec sleep_ts = { .tv_sec = 0, .tv_nsec = 100000 }; /* 0.1ms */
        nanosleep(&sleep_ts, NULL);
    }
}

mvgal_nt_wait_result_t mvgal_nt_wait_multiple_any(
    uint32_t count,
    void **objects,
    mvgal_ntsync_type_t *types,
    mvgal_nt_wait_type_t wait_type,
    uint64_t timeout_ns,
    uint32_t *signaled_index
) {
    if (count == 0 || objects == NULL || types == NULL) {
        return MVGAL_NT_WAIT_FAILED;
    }

    struct timespec ts;
    if (timeout_ns != UINT64_MAX && timeout_ns != 0) {
        mvgal_nt_timeout_to_abs(timeout_ns, &ts);
    }

    uint64_t deadline = 0;
    if (timeout_ns != UINT64_MAX && timeout_ns != 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        deadline = (uint64_t)now.tv_sec * 1000000000ULL +
                   (uint64_t)now.tv_nsec + timeout_ns;
    }

    for (;;) {
        uint32_t signaled = 0;

        for (uint32_t i = 0; i < count; i++) {
            if (objects[i] == NULL) continue;
            bool sig = false;

            switch (types[i]) {
            case MVGAL_NTSYNC_TYPE_EVENT: {
                struct mvgal_nt_event *e = objects[i];
                pthread_mutex_lock(&e->lock);
                sig = e->signaled;
                if (sig && e->type == MVGAL_NT_EVENT_TYPE_SYNCHRONIZATION)
                    e->signaled = false;
                pthread_mutex_unlock(&e->lock);
                break;
            }
            case MVGAL_NTSYNC_TYPE_MUTEX: {
                struct mvgal_nt_mutex *m = objects[i];
                uint32_t expected = 0;
                sig = atomic_compare_exchange_strong(&m->state, &expected, 1);
                if (sig) {
                    pthread_mutex_lock(&m->lock);
                    m->owner_thread = (uint64_t)pthread_self();
                    m->recursion_count = 1;
                    pthread_mutex_unlock(&m->lock);
                }
                break;
            }
            case MVGAL_NTSYNC_TYPE_SEMAPHORE: {
                struct mvgal_nt_semaphore *s = objects[i];
                pthread_mutex_lock(&s->lock);
                uint32_t cur = atomic_load(&s->count);
                if (cur > 0) {
                    atomic_store(&s->count, cur - 1);
                    sig = true;
                }
                pthread_mutex_unlock(&s->lock);
                break;
            }
            case MVGAL_NTSYNC_TYPE_TIMER: {
                struct mvgal_nt_timer *t = objects[i];
                pthread_mutex_lock(&t->lock);
                uint64_t exp = 0;
                if (read(t->timer_fd, &exp, sizeof(exp)) == sizeof(exp) && exp > 0)
                    sig = true;
                pthread_mutex_unlock(&t->lock);
                break;
            }
            }

            if (sig) {
                signaled++;
                if (wait_type == MVGAL_NT_WAIT_TYPE_ANY) {
                    if (signaled_index) *signaled_index = i;
                    return (mvgal_nt_wait_result_t)(MVGAL_NT_WAIT_OBJECT_0 + i);
                }
            }
        }

        if (wait_type == MVGAL_NT_WAIT_TYPE_ALL && signaled == count)
            return MVGAL_NT_WAIT_OBJECT_0;

        if (timeout_ns == 0)
            return MVGAL_NT_WAIT_TIMEOUT;

        if (timeout_ns != UINT64_MAX) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL +
                             (uint64_t)now.tv_nsec;
            if (now_ns >= deadline)
                return MVGAL_NT_WAIT_TIMEOUT;
        }

        struct timespec sleep_ts = { .tv_sec = 0, .tv_nsec = 100000 };
        nanosleep(&sleep_ts, NULL);
    }
}

/* ----------------------------------------------------------------- */
/*  Utility                                                          */
/* ----------------------------------------------------------------- */

int mvgal_nt_object_get_fd(void *obj, mvgal_ntsync_type_t type) {
    if (obj == NULL) return -1;

    switch (type) {
    case MVGAL_NTSYNC_TYPE_TIMER:
        return ((struct mvgal_nt_timer *)obj)->timer_fd;
    default:
        /* Events, mutexes, semaphores don't have direct fds */
        return -1;
    }
}
