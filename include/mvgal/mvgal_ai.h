/**
 * @file mvgal_ai.h
 * @brief AI/ML-driven scheduling model inference API
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header provides the inference API for AI-driven workload
 * scheduling. It defines the model handle, feature vector, and
 * prediction result types used to offload scheduling decisions
 * to an external ML model.
 */

#ifndef MVGAL_AI_H
#define MVGAL_AI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup AIScheduling
 * @{
 */

/**
 * @brief Opaque AI model handle
 *
 * Created by mvgal_ai_model_load() and released by
 * mvgal_ai_model_unload().  Thread-safe: a single handle
 * may be used from multiple threads concurrently.
 */
typedef struct mvgal_ai_model* mvgal_ai_model_t;

/**
 * @brief Feature vector for scheduling inference
 *
 * Collects per-GPU telemetry and workload history into a
 * fixed-size vector consumed by the scheduling model.
 */
typedef struct {
    float gpu_utilization[8];     /* Per-GPU utilization 0.0-1.0 */
    float vram_pressure[8];       /* Per-GPU VRAM pressure 0.0-1.0 */
    float queue_depth[8];         /* Per-GPU queue depth normalized */
    float workload_history[16];   /* Recent workload distribution */
    float temperature[8];         /* Per-GPU temperature normalized */
    uint32_t num_gpus;            /* Number of GPUs in feature set */
    uint32_t workload_type;       /* Type hint from mvgal_workload_type_t */
    float reserved[8];            /* Reserved for future features */
} mvgal_ai_features_t;

/**
 * @brief Prediction result from model inference
 *
 * Returned by mvgal_ai_model_predict() with the recommended
 * GPU assignment and confidence metrics.
 */
typedef struct {
    int32_t recommended_gpu;      /* Recommended GPU index (-1 = no recommendation) */
    float confidence;             /* Confidence score 0.0-1.0 */
    float gpu_scores[8];          /* Per-GPU suitability scores */
    uint32_t inference_time_us;   /* Time taken for inference */
    uint32_t reserved[4];
} mvgal_ai_prediction_t;

/**
 * @brief Load a model from file path
 *
 * Loads the ONNX (or other supported format) model from disk
 * and returns an opaque handle.  The handle must eventually
 * be released with mvgal_ai_model_unload().
 *
 * @param model_path  Path to the model file on disk
 * @return Opaque model handle, or NULL on failure
 */
mvgal_ai_model_t mvgal_ai_model_load(const char* model_path);

/**
 * @brief Run prediction against the loaded model
 *
 * Pushes the feature vector through the model and writes the
 * prediction result into \p out_prediction.
 *
 * @param model          Loaded model handle (must not be NULL)
 * @param features       Feature vector for this scheduling round
 * @param out_prediction Output buffer for the prediction result
 * @return 0 on success, -1 on error
 */
int mvgal_ai_model_predict(mvgal_ai_model_t model,
                           const mvgal_ai_features_t* features,
                           mvgal_ai_prediction_t* out_prediction);

/**
 * @brief Unload model and free all associated resources
 *
 * Releases the model handle.  Calling with NULL is safe and
 * is treated as a no-op.
 *
 * @param model  Model handle to release (may be NULL)
 */
void mvgal_ai_model_unload(mvgal_ai_model_t model);

/** @} */ // end of AIScheduling

#ifdef __cplusplus
}
#endif

#endif /* MVGAL_AI_H */
