/**
 * @file benchmarks.c
 * @brief MVGAL Benchmark Framework Implementation
 */

#include "benchmarks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>

void benchmark_init(benchmark_context_t *ctx, const benchmark_config_t *config) {
    if (!ctx || !config) return;
    
    memcpy(&ctx->config, config, sizeof(benchmark_config_t));
    ctx->test_count = 0;
    ctx->pass_count = 0;
    ctx->fail_count = 0;
    ctx->output_file = NULL;
    
    if (config->output_file && strlen(config->output_file) > 0) {
        ctx->output_file = fopen(config->output_file, "w");
    }
    
    gettimeofday(&ctx->start_time, NULL);
}

void benchmark_cleanup(benchmark_context_t *ctx) {
    if (!ctx) return;
    
    gettimeofday(&ctx->end_time, NULL);
    
    if (ctx->output_file) {
        fclose(ctx->output_file);
        ctx->output_file = NULL;
    }
}

void benchmark_log(benchmark_context_t *ctx, const char *format, ...) {
    if (!ctx || !format) return;
    
    va_list args;
    va_start(args, format);
    
    if (ctx->output_file) {
        vfprintf(ctx->output_file, format, args);
        fprintf(ctx->output_file, "\n");
    }
    
    if (ctx->config.verbose) {
        vfprintf(stdout, format, args);
        fprintf(stdout, "\n");
    }
    
    va_end(args);
}

static struct timeval timing_start;

void benchmark_start_timing(benchmark_context_t *ctx) {
    (void)ctx;
    gettimeofday(&timing_start, NULL);
}

double benchmark_stop_timing(benchmark_context_t *ctx) {
    (void)ctx;
    struct timeval timing_end;
    gettimeofday(&timing_end, NULL);
    
    double elapsed = (timing_end.tv_sec - timing_start.tv_sec) * 1000.0;
    elapsed += (timing_end.tv_usec - timing_start.tv_usec) / 1000.0;
    
    return elapsed;
}

static void calculate_stats(double *times, uint64_t count, 
                           double *min_time, double *max_time, 
                           double *avg_time, double *stddev_time) {
    if (count == 0) {
        *min_time = 0;
        *max_time = 0;
        *avg_time = 0;
        *stddev_time = 0;
        return;
    }
    
    *min_time = times[0];
    *max_time = times[0];
    double sum = times[0];
    
    for (uint64_t i = 1; i < count; i++) {
        if (times[i] < *min_time) *min_time = times[i];
        if (times[i] > *max_time) *max_time = times[i];
        sum += times[i];
    }
    
    *avg_time = sum / count;
    
    // Calculate standard deviation
    double sum_sq = 0;
    for (uint64_t i = 0; i < count; i++) {
        double diff = times[i] - *avg_time;
        sum_sq += diff * diff;
    }
    *stddev_time = sqrt(sum_sq / count);
}

void benchmark_record_result(benchmark_context_t *ctx, const benchmark_result_t *result) {
    if (!ctx || !result) return;
    
    ctx->test_count++;
    if (result->passed) {
        ctx->pass_count++;
    } else {
        ctx->fail_count++;
    }
    
    benchmark_log(ctx, "=== Benchmark: %s ===", result->name);
    benchmark_log(ctx, "  Min time:    %.4f ms", result->min_time_ms);
    benchmark_log(ctx, "  Max time:    %.4f ms", result->max_time_ms);
    benchmark_log(ctx, "  Avg time:    %.4f ms", result->avg_time_ms);
    benchmark_log(ctx, "  Std dev:     %.4f ms", result->stddev_time_ms);
    benchmark_log(ctx, "  Operations:  %lu", result->operations);
    benchmark_log(ctx, "  Throughput:  %.2f ops/sec", result->ops_per_sec);
    benchmark_log(ctx, "  Status:      %s", result->passed ? "PASS" : "FAIL");
    
    if (result->error) {
        benchmark_log(ctx, "  Error:       %s", result->error);
    }
    benchmark_log(ctx, " ");
}

double benchmark_get_time_ms(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000.0 + now.tv_usec / 1000.0;
}

void benchmark_sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}

/**
 * @brief Run a benchmark with collect statistics
 * 
 * @param ctx Benchmark context
 * @param name Test name
 * @param func Function to benchmark
 * @param data Data to pass to function
 * @param verify_func Optional verification function (returns true if passed)
 * @return benchmark_result_t with results
 */
benchmark_result_t benchmark_run(benchmark_context_t *ctx, const char *name,
                                void (*func)(void*), void *data,
                                bool (*verify_func)(void*)) {
    benchmark_result_t result = {0};
    result.name = name;
    result.passed = true;
    
    const uint32_t iterations = ctx->config.iterations;
    double *times = malloc(iterations * sizeof(double));
    if (!times) {
        result.error = "Failed to allocate memory for timing array";
        result.passed = false;
        return result;
    }
    
    for (uint32_t i = 0; i < iterations; i++) {
        benchmark_start_timing(ctx);
        func(data);
        times[i] = benchmark_stop_timing(ctx);
    }
    
    result.operations = iterations;
    calculate_stats(times, iterations, &result.min_time_ms, &result.max_time_ms, 
                   &result.avg_time_ms, &result.stddev_time_ms);
    
    result.ops_per_sec = iterations / (result.avg_time_ms / 1000.0);
    
    if (verify_func && !verify_func(data)) {
        result.passed = false;
        result.error = "Verification failed";
    }
    
    free(times);
    return result;
}

/**
 * @brief Run a timed benchmark for a fixed duration
 * 
 * @param ctx Benchmark context
 * @param name Test name
 * @param func Function to benchmark
 * @param data Data to pass to function
 * @return benchmark_result_t with results
 */
benchmark_result_t benchmark_run_duration(benchmark_context_t *ctx, const char *name,
                                         void (*func)(void*), void *data) {
    benchmark_result_t result = {0};
    result.name = name;
    result.passed = true;
    
    const uint32_t duration_ms = ctx->config.duration_ms;
    if (duration_ms == 0) {
        result.error = "Duration not specified";
        result.passed = false;
        return result;
    }
    
    const double start = benchmark_get_time_ms();
    uint64_t operations = 0;
    
    while (benchmark_get_time_ms() - start < duration_ms) {
        benchmark_start_timing(ctx);
        func(data);
        benchmark_stop_timing(ctx);
        operations++;
    }
    
    const double elapsed = benchmark_get_time_ms() - start;
    result.operations = operations;
    result.avg_time_ms = elapsed / operations;
    result.min_time_ms = result.avg_time_ms;
    result.max_time_ms = result.avg_time_ms;
    result.stddev_time_ms = 0;
    result.ops_per_sec = operations / (elapsed / 1000.0);
    
    return result;
}
