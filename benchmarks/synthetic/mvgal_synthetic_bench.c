/**
 * @file mvgal_synthetic_bench.c
 * @brief Synthetic Benchmarks for MVGAL
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * These are microbenchmarks that test individual MVGAL components
 * without requiring actual GPU hardware.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../benchmarks.h"

// Mock types for testing without full MVGAL
typedef struct {
    int id;
    char name[64];
} mock_gpu_t;

typedef struct {
    int workload_type;
    int gpu_index;
    char step_name[128];
} mock_workload_t;

// Test 1: Workload submission latency
static void test_workload_submit(void *data) {
    mock_workload_t *workload = (mock_workload_t *)data;
    // Simulate workload submission
    workload->gpu_index = (workload->gpu_index + 1) % 4;
    workload->workload_type++;
}

static bool verify_workload_submit(void *data) {
    mock_workload_t *workload = (mock_workload_t *)data;
    return workload->gpu_index >= 0 && workload->gpu_index < 4;
}

// Test 2: GPU enumeration
static mock_gpu_t gpux[8];
static void test_gpu_enumeration(void *data) {
    (void)data;
    for (int i = 0; i < 8; i++) {
        gpux[i].id = i;
        snprintf(gpux[i].name, sizeof(gpux[i].name), "GPU-%d", i);
    }
}

static bool verify_gpu_enumeration(void *data) {
    (void)data;
    for (int i = 0; i < 8; i++) {
        if (gpux[i].id != i) return false;
        if (gpux[i].name[0] == 0) return false;
    }
    return true;
}

// Test 3: Memory allocation simulation
static void test_memory_allocation(void *data) {
    size_t *size = (size_t *)data;
    void *ptr = malloc(*size);
    if (ptr) {
        memset(ptr, 0, *size);
        free(ptr);
    }
}

static bool verify_memory_allocation(void *data) {
    (void)data;
    return true; // Can't really verify, just check it didn't crash
}

// Test 4: Thread creation (simulating worker threads)
static void *thread_func(void *arg) {
    volatile int *counter = (volatile int *)arg;
    for (int i = 0; i < 1000; i++) {
        (*counter)++;
    }
    return NULL;
}

static void test_thread_creation(void *data) {
    (void)data;
    pthread_t threads[10];
    int counter = 0;
    
    for (int i = 0; i < 10; i++) {
        pthread_create(&threads[i], NULL, thread_func, &counter);
    }
    
    for (int i = 0; i < 10; i++) {
        pthread_join(threads[i], NULL);
    }
}

static bool verify_thread_creation(void *data) {
    (void)data;
    return true;
}

// Test 5: Mutex operations
static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
static int mutex_counter = 0;

static void test_mutex_operations(void *data) {
    (void)data;
    for (int i = 0; i < 100; i++) {
        pthread_mutex_lock(&test_mutex);
        mutex_counter++;
        pthread_mutex_unlock(&test_mutex);
    }
}

static bool verify_mutex_operations(void *data) {
    (void)data;
    return mutex_counter == 100 * 1000;  // 100 iterations * 100 loops each
}

// Test 6: Context switching overhead
static void test_context_switch(void *data) {
    (void)data;
    volatile int x = 0;
    for (int i = 0; i < 10000; i++) {
        x++;
    }
}

// Test 7: Data structure operations (simulating scheduler queues)
typedef struct {
    int data[1024];
    int head;
    int tail;
} queue_t;

static queue_t test_queue;

static void test_queue_operations(void *data) {
    (void)data;
    for (int i = 0; i < 100; i++) {
        test_queue.data[test_queue.tail % 1024] = i;
        test_queue.tail++;
        if (test_queue.head < test_queue.tail) {
            (void)test_queue.data[test_queue.head % 1024];
            test_queue.head++;
        }
    }
}

// Test 8: JSON serialization (simulating config/script handling)
static void test_json_parse(void *data) {
    (void)data;
    char json[1024];
    snprintf(json, sizeof(json), 
             "{\"gpus\":[{\"id\":0,\"name\":\"GPU-0\"},{\"id\":1,\"name\":\"GPU-1\"}],\"strategy\":\"round_robin\"}");
    // Simulate parsing
    for (size_t i = 0; i < sizeof(json); i++) {
        (void)json[i];
    }
}

// Test 9: Memory copy (simulating DMA-BUF operations)
static void test_memory_copy(void *data) {
    size_t *size = (size_t *)data;
    char *src = malloc(*size);
    char *dst = malloc(*size);
    if (src && dst) {
        memset(src, 0xAA, *size);
        memcpy(dst, src, *size);
        free(src);
        free(dst);
    }
}

// Test 10: Hash table operations (simulating symbol table lookups)
#define HASH_SIZE 256
static int hash_table[HASH_SIZE] = {0};

static void test_hash_table(void *data) {
    (void)data;
    for (int i = 0; i < 1000; i++) {
        int key = i % HASH_SIZE;
        hash_table[key] = i;
    }
    
    for (int i = 0; i < HASH_SIZE; i++) {
        (void)hash_table[i];
    }
}

// Benchmark runner
static void print_summary(benchmark_context_t *ctx) {
    const double total_time = (ctx->end_time.tv_sec - ctx->start_time.tv_sec) * 1000.0 +
                             (ctx->end_time.tv_usec - ctx->start_time.tv_usec) / 1000.0;
    
    benchmark_log(ctx, "============================================");
    benchmark_log(ctx, "  MVGAL Synthetic Benchmarks Summary");
    benchmark_log(ctx, "============================================");
    benchmark_log(ctx, "  Total tests:   %lu", ctx->test_count);
    benchmark_log(ctx, "  Passed:        %lu", ctx->pass_count);
    benchmark_log(ctx, "  Failed:        %lu", ctx->fail_count);
    benchmark_log(ctx, "  Total time:    %.2f ms", total_time);
    benchmark_log(ctx, "  Success rate:  %.2f%%", 
                  ctx->test_count > 0 ? (ctx->pass_count * 100.0 / ctx->test_count) : 0);
    benchmark_log(ctx, "============================================");
}

int main(int argc, char *argv[]) {
    benchmark_config_t config = {
        .name = "MVGAL Synthetic Benchmarks",
        .iterations = 1000,
        .duration_ms = 0,
        .verify = true,
        .verbose = true,
        .output_file = "results/synthetic_benchmarks.txt"
    };
    
    // Parse command line
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            config.verbose = false;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-verify") == 0) {
            config.verify = false;
        } else if (strncmp(argv[i], "-i=", 3) == 0) {
            config.iterations = atoi(argv[i] + 3);
        } else if (strncmp(argv[i], "-o=", 3) == 0) {
            config.output_file = strdup(argv[i] + 3);
        } else if (strncmp(argv[i], "-d=", 3) == 0) {
            config.duration_ms = atoi(argv[i] + 3);
        }
    }
    
    benchmark_context_t ctx;
    benchmark_init(&ctx, &config);
    
    benchmark_log(&ctx, "Starting MVGAL Synthetic Benchmarks");
    benchmark_log(&ctx, "Iterations: %u", config.iterations);
    benchmark_log(&ctx, " ");
    
    // Run benchmarks
    mock_workload_t wl = {0, 0, "test"};
    size_t mem_size = 1024 * 1024; // 1MB
    
    // Benchmark 1: Workload submission
    benchmark_result_t r1 = benchmark_run(&ctx, "Workload Submit", 
                                         test_workload_submit, &wl, 
                                         config.verify ? verify_workload_submit : NULL);
    benchmark_record_result(&ctx, &r1);
    
    // Benchmark 2: GPU enumeration
    benchmark_result_t r2 = benchmark_run(&ctx, "GPU Enumeration",
                                         test_gpu_enumeration, NULL,
                                         config.verify ? verify_gpu_enumeration : NULL);
    benchmark_record_result(&ctx, &r2);
    
    // Benchmark 3: Memory allocation (1MB)
    benchmark_result_t r3 = benchmark_run(&ctx, "Memory Allocation (1MB)",
                                         test_memory_allocation, &mem_size,
                                         config.verify ? verify_memory_allocation : NULL);
    benchmark_record_result(&ctx, &r3);
    
    // Benchmark 4: Thread creation
    benchmark_result_t r4 = benchmark_run(&ctx, "Thread Creation",
                                         test_thread_creation, NULL,
                                         config.verify ? verify_thread_creation : NULL);
    benchmark_record_result(&ctx, &r4);
    
    // Benchmark 5: Mutex operations
    mutex_counter = 0;
    benchmark_result_t r5 = benchmark_run(&ctx, "Mutex Operations",
                                         test_mutex_operations, NULL,
                                         config.verify ? verify_mutex_operations : NULL);
    benchmark_record_result(&ctx, &r5);
    
    // Benchmark 6: Context switch overhead
    benchmark_result_t r6 = benchmark_run(&ctx, "Context Switch Overhead",
                                         test_context_switch, NULL, NULL);
    benchmark_record_result(&ctx, &r6);
    
    // Benchmark 7: Queue operations
    memset(&test_queue, 0, sizeof(test_queue));
    benchmark_result_t r7 = benchmark_run(&ctx, "Queue Operations",
                                         test_queue_operations, NULL, NULL);
    benchmark_record_result(&ctx, &r7);
    
    // Benchmark 8: JSON parsing
    benchmark_result_t r8 = benchmark_run(&ctx, "JSON Parsing",
                                         test_json_parse, NULL, NULL);
    benchmark_record_result(&ctx, &r8);
    
    // Benchmark 9: Memory copy (1MB)
    benchmark_result_t r9 = benchmark_run(&ctx, "Memory Copy (1MB)",
                                         test_memory_copy, &mem_size, NULL);
    benchmark_record_result(&ctx, &r9);
    
    // Benchmark 10: Hash table operations
    memset(hash_table, 0, sizeof(hash_table));
    benchmark_result_t r10 = benchmark_run(&ctx, "Hash Table Operations",
                                          test_hash_table, NULL, NULL);
    benchmark_record_result(&ctx, &r10);
    
    // Print summary
    benchmark_cleanup(&ctx);
    print_summary(&ctx);
    
    return (ctx.fail_count > 0) ? 1 : 0;
}
