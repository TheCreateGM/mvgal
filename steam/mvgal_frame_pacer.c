/**
 * MVGAL Frame Pacer
 *
 * Holds completed frames in a queue and releases them at the correct display
 * refresh interval to prevent microstutter in multi-GPU AFR mode.
 *
 * The frame pacer runs as a background thread inside mvgald.  The Vulkan
 * layer notifies it via the IPC socket when a frame is ready for
 * presentation.  The pacer then signals the presentation semaphore at the
 * correct vsync-aligned time.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define MVGAL_FP_MAX_FRAMES   8      /* ring buffer depth */
#define MVGAL_FP_DEFAULT_HZ   60     /* default display refresh rate */

/* -------------------------------------------------------------------------
 * Frame entry
 * ---------------------------------------------------------------------- */

typedef struct {
    uint64_t frame_id;          /* monotonically increasing frame number */
    uint64_t ready_ns;          /* time the frame became ready (CLOCK_MONOTONIC) */
    uint64_t target_present_ns; /* target presentation time */
    uint32_t gpu_index;         /* which GPU rendered this frame */
    bool     valid;
} mvgal_frame_entry_t;

/* -------------------------------------------------------------------------
 * Frame pacer state
 * ---------------------------------------------------------------------- */

typedef struct {
    pthread_mutex_t     lock;
    pthread_cond_t      cond;
    pthread_t           thread;

    mvgal_frame_entry_t ring[MVGAL_FP_MAX_FRAMES];
    int                 head;   /* next write position */
    int                 tail;   /* next read position */
    int                 count;

    uint32_t            refresh_hz;
    uint64_t            frame_interval_ns;  /* 1e9 / refresh_hz */
    uint64_t            last_present_ns;

    bool                running;
    bool                enabled;

    /* Statistics */
    uint64_t            frames_paced;
    uint64_t            frames_dropped;
    uint64_t            total_jitter_ns;
} mvgal_frame_pacer_t;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void sleep_until_ns(uint64_t target_ns)
{
    uint64_t now = mono_ns();
    if (now >= target_ns) return;
    uint64_t delta = target_ns - now;
    struct timespec ts = {
        .tv_sec  = (time_t)(delta / 1000000000ULL),
        .tv_nsec = (long)(delta % 1000000000ULL),
    };
    nanosleep(&ts, NULL);
}

/* -------------------------------------------------------------------------
 * Pacer thread
 * ---------------------------------------------------------------------- */

static void *pacer_thread(void *arg)
{
    mvgal_frame_pacer_t *fp = (mvgal_frame_pacer_t *)arg;

    while (fp->running) {
        pthread_mutex_lock(&fp->lock);

        /* Wait for a frame to be available */
        while (fp->count == 0 && fp->running)
            pthread_cond_wait(&fp->cond, &fp->lock);

        if (!fp->running) {
            pthread_mutex_unlock(&fp->lock);
            break;
        }

        /* Dequeue oldest frame */
        mvgal_frame_entry_t frame = fp->ring[fp->tail];
        fp->tail = (fp->tail + 1) % MVGAL_FP_MAX_FRAMES;
        fp->count--;

        pthread_mutex_unlock(&fp->lock);

        /* Sleep until target presentation time */
        sleep_until_ns(frame.target_present_ns);

        uint64_t actual_ns = mono_ns();
        int64_t jitter = (int64_t)actual_ns - (int64_t)frame.target_present_ns;
        if (jitter < 0) jitter = -jitter;

        pthread_mutex_lock(&fp->lock);
        fp->frames_paced++;
        fp->total_jitter_ns += (uint64_t)jitter;
        fp->last_present_ns = actual_ns;
        pthread_mutex_unlock(&fp->lock);

        /* In a real implementation, signal the Vulkan presentation semaphore
         * here.  For now, we log the timing. */
        if (getenv("MVGAL_FRAME_PACING_DEBUG")) {
            fprintf(stderr,
                    "[mvgal-fp] frame=%llu gpu=%u jitter=%+lld ns\n",
                    (unsigned long long)frame.frame_id,
                    frame.gpu_index,
                    (long long)((int64_t)actual_ns - (int64_t)frame.target_present_ns));
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

mvgal_frame_pacer_t *mvgal_fp_create(uint32_t refresh_hz)
{
    mvgal_frame_pacer_t *fp = calloc(1, sizeof(*fp));
    if (!fp) return NULL;

    pthread_mutex_init(&fp->lock, NULL);
    pthread_cond_init(&fp->cond, NULL);

    fp->refresh_hz       = refresh_hz ? refresh_hz : MVGAL_FP_DEFAULT_HZ;
    fp->frame_interval_ns = 1000000000ULL / fp->refresh_hz;
    fp->enabled          = true;
    fp->running          = true;

    if (pthread_create(&fp->thread, NULL, pacer_thread, fp) != 0) {
        pthread_mutex_destroy(&fp->lock);
        pthread_cond_destroy(&fp->cond);
        free(fp);
        return NULL;
    }

    return fp;
}

void mvgal_fp_destroy(mvgal_frame_pacer_t *fp)
{
    if (!fp) return;

    pthread_mutex_lock(&fp->lock);
    fp->running = false;
    pthread_cond_signal(&fp->cond);
    pthread_mutex_unlock(&fp->lock);

    pthread_join(fp->thread, NULL);
    pthread_mutex_destroy(&fp->lock);
    pthread_cond_destroy(&fp->cond);
    free(fp);
}

/**
 * Submit a completed frame to the pacer.
 * @param fp         Frame pacer instance.
 * @param frame_id   Monotonically increasing frame number.
 * @param gpu_index  GPU that rendered this frame.
 * @return 0 on success, -1 if the queue is full (frame dropped).
 */
int mvgal_fp_submit_frame(mvgal_frame_pacer_t *fp,
                           uint64_t frame_id,
                           uint32_t gpu_index)
{
    if (!fp || !fp->enabled) return 0;

    pthread_mutex_lock(&fp->lock);

    if (fp->count >= MVGAL_FP_MAX_FRAMES) {
        fp->frames_dropped++;
        pthread_mutex_unlock(&fp->lock);
        return -1;
    }

    uint64_t now = mono_ns();

    /* Calculate target presentation time: next vsync boundary after now */
    uint64_t last = fp->last_present_ns ? fp->last_present_ns : now;
    uint64_t next_vsync = last + fp->frame_interval_ns;
    if (next_vsync < now)
        next_vsync = now + fp->frame_interval_ns;

    mvgal_frame_entry_t *entry = &fp->ring[fp->head];
    entry->frame_id          = frame_id;
    entry->ready_ns          = now;
    entry->target_present_ns = next_vsync;
    entry->gpu_index         = gpu_index;
    entry->valid             = true;

    fp->head = (fp->head + 1) % MVGAL_FP_MAX_FRAMES;
    fp->count++;

    pthread_cond_signal(&fp->cond);
    pthread_mutex_unlock(&fp->lock);
    return 0;
}

void mvgal_fp_get_stats(const mvgal_frame_pacer_t *fp,
                         uint64_t *frames_paced,
                         uint64_t *frames_dropped,
                         double   *avg_jitter_us)
{
    if (!fp) return;
    pthread_mutex_lock((pthread_mutex_t *)&fp->lock);
    if (frames_paced)   *frames_paced   = fp->frames_paced;
    if (frames_dropped) *frames_dropped = fp->frames_dropped;
    if (avg_jitter_us && fp->frames_paced > 0)
        *avg_jitter_us = (double)fp->total_jitter_ns /
                         (double)fp->frames_paced / 1000.0;
    pthread_mutex_unlock((pthread_mutex_t *)&fp->lock);
}

void mvgal_fp_set_refresh_hz(mvgal_frame_pacer_t *fp, uint32_t hz)
{
    if (!fp || hz == 0) return;
    pthread_mutex_lock(&fp->lock);
    fp->refresh_hz        = hz;
    fp->frame_interval_ns = 1000000000ULL / hz;
    pthread_mutex_unlock(&fp->lock);
}
