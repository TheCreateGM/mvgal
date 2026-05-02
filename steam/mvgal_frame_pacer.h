/**
 * MVGAL Frame Pacer — Public API
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_FRAME_PACER_H
#define MVGAL_FRAME_PACER_H

#include <stdint.h>

typedef struct mvgal_frame_pacer mvgal_frame_pacer_t;

mvgal_frame_pacer_t *mvgal_fp_create(uint32_t refresh_hz);
void                 mvgal_fp_destroy(mvgal_frame_pacer_t *fp);
int                  mvgal_fp_submit_frame(mvgal_frame_pacer_t *fp,
                                            uint64_t frame_id,
                                            uint32_t gpu_index);
void                 mvgal_fp_get_stats(const mvgal_frame_pacer_t *fp,
                                         uint64_t *frames_paced,
                                         uint64_t *frames_dropped,
                                         double   *avg_jitter_us);
void                 mvgal_fp_set_refresh_hz(mvgal_frame_pacer_t *fp,
                                              uint32_t hz);

#endif /* MVGAL_FRAME_PACER_H */
