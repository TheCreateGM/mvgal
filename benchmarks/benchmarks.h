/**
 * @file benchmarks.h
 * @brief MVGAL Benchmark Framework
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Common header for all benchmark programs.
 */

#ifndef MVGAL_BENCHMARKS_H
#define MVGAL_BENCHMARKS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Benchmark configuration
 */
typedef struct {
    const char *name;           ///< Benchmark name
    uint32_t iterations;       ///< Number of iterations
    uint32_t duration_ms;      ///< Duration in milliseconds (0 = use iterations)
    bool verify;               ///< Verify results
    bool verbose;              ///< Verbose output
    const char *output_file;   ///< Output file for results
} benchmark_config_t;

/**
 * @brief Benchmark result
 */
typedef struct {
    const char *name;           ///< Test name
    double min_time_ms;       ///< Minimum time in milliseconds
    double max_time_ms;       ///< Maximum time in milliseconds
    double avg_time_ms;       ///< Average time in milliseconds
    double stddev_time_ms;    ///< Standard deviation in milliseconds
    uint64_t operations;       ///< Number of operations
    double ops_per_sec;        ///< Operations per second
    bool passed;               ///< Whether benchmark passed verification
    const char *error;         ///< Error message if failed
} benchmark_result_t;

/**
 * @brief Benchmark context
 */
typedef struct {
    benchmark_config_t config;
    FILE *output_file;
    uint64_t test_count;
    uint64_t pass_count;
    uint64_t fail_count;
    struct timeval start_time;
    struct timeval end_time;
} benchmark_context_t;

/**
 * @brief Initialize benchmark context
 */
void benchmark_init(benchmark_context_t *ctx, const benchmark_config_t *config);

/**
 * @brief Cleanup benchmark context
 */
void benchmark_cleanup(benchmark_context_t *ctx);

/**
 * @brief Log benchmark message
 */
void benchmark_log(benchmark_context_t *ctx, const char *format, ...) __attribute__((format(printf, 2, 3)));

/**
 * @brief Start timing
 */
void benchmark_start_timing(benchmark_context_t *ctx);

/**
 * @brief Stop timing and return elapsed milliseconds
 */
double benchmark_stop_timing(benchmark_context_t *ctx);

/**
 * @brief Record a benchmark result
 */
void benchmark_record_result(benchmark_context_t *ctx, const benchmark_result_t *result);

/**
 * @brief Get current timestamp in milliseconds
 */
double benchmark_get_time_ms(void);

/**
 * @brief Sleep for specified milliseconds
 */
void benchmark_sleep_ms(uint32_t ms);

/**
 * @brief Typedef for test function
 */
typedef void (*benchmark_test_func_t)(void *data);

/**
 * @brief Typedef for verification function
 */
typedef bool (*benchmark_verify_func_t)(void *data);

/**
 * @brief Run a single benchmark test
 */
benchmark_result_t benchmark_run(benchmark_context_t *ctx, const char *name,
                                 benchmark_test_func_t test_func, void *data,
                                 benchmark_verify_func_t verify_func);

#ifdef __cplusplus
}
#endif

#endif // MVGAL_BENCHMARKS_H
