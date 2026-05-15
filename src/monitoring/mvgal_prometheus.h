/**
 * @file mvgal_prometheus.h
 * @brief Prometheus Metrics Endpoint for MVGAL
 *
 * Exposes MVGAL runtime metrics in Prometheus text format
 * via an HTTP /metrics endpoint.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_PROMETHEUS_H
#define MVGAL_PROMETHEUS_H

#include <stddef.h>
#include "mvgal/mvgal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup Monitoring
 * @{
 */

/* ============================================================================
 * Prometheus Metrics Server
 * ============================================================================ */

/**
 * @brief Start the Prometheus metrics HTTP server
 *
 * Spawns a background thread that serves Prometheus-formatted metrics
 * on the given listen address and port.
 *
 * @param listen_addr Host to bind (e.g. "127.0.0.1" or "0.0.0.0")
 * @param port        TCP port (e.g. 9100)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_prometheus_start(const char *listen_addr, uint16_t port);

/**
 * @brief Stop the Prometheus metrics server
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_prometheus_stop(void);

/**
 * @brief Check if the Prometheus server is running
 * @return true if running
 */
bool mvgal_prometheus_is_running(void);

/* ============================================================================
 * Metric Registration
 * ============================================================================ */

/**
 * @brief Metric type for Prometheus exposition format
 */
typedef enum {
    MVGAL_METRIC_GAUGE = 0,
    MVGAL_METRIC_COUNTER = 1,
    MVGAL_METRIC_HISTOGRAM = 2,
    MVGAL_METRIC_SUMMARY = 3,
} mvgal_metric_type_t;

/**
 * @brief Metric descriptor
 */
typedef struct {
    const char *name;
    const char *help;
    mvgal_metric_type_t type;
    const char *const *label_names;
    uint32_t num_labels;
} mvgal_metric_desc_t;

/**
 * @brief Register a Prometheus metric
 *
 * @param desc Metric descriptor
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_prometheus_register_metric(const mvgal_metric_desc_t *desc);

/**
 * @brief Set a gauge metric value
 *
 * @param name      Metric name
 * @param value     Current value
 * @param label_values Label values (may be NULL if no labels)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_prometheus_gauge_set(
    const char *name,
    double value,
    const char *const *label_values);

/**
 * @brief Increment a counter metric
 *
 * @param name      Metric name
 * @param value     Increment amount
 * @param label_values Label values (may be NULL)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_prometheus_counter_add(
    const char *name,
    double value,
    const char *const *label_values);

/**
 * @brief Observe a value for a histogram metric
 *
 * @param name      Metric name
 * @param value     Observed value
 * @param label_values Label values (may be NULL)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_prometheus_histogram_observe(
    const char *name,
    double value,
    const char *const *label_values);

/**
 * @brief Generate the full /metrics output into a buffer
 *
 * @param buffer   Output buffer
 * @param capacity Buffer capacity
 * @param written  Bytes written (out)
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t mvgal_prometheus_generate(char *buffer, size_t capacity, size_t *written);

/** @} */ // end of Monitoring

#ifdef __cplusplus
}
#endif

#endif // MVGAL_PROMETHEUS_H
