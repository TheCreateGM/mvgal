/**
 * @file mvgal.c
 * @brief MVGAL Core Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Core library implementation
 */

#include "mvgal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>

static pthread_mutex_t mvgal_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool mvgal_initialized = false;
static mvgal_config_t mvgal_config = {0};
static mvgal_gpu_info_t mvgal_gpus[8] = {0};
static mvgal_stats_t mvgal_stats = {0};

int mvgal_init(const char *config_file) {
    (void)config_file;
    pthread_mutex_lock(&mvgal_mutex);
    if (mvgal_initialized) {
        pthread_mutex_unlock(&mvgal_mutex);
        return 0;
    }
    
    // Default configuration
    mvgal_config.enabled = true;
    snprintf(mvgal_config.debug_level, sizeof(mvgal_config.debug_level), "info");
    mvgal_config.gpu_count = 2;
    mvgal_config.default_strategy = MVGAL_STRATEGY_ROUND_ROBIN;
    mvgal_config.enable_memory_migration = true;
    mvgal_config.enable_dmabuf = true;
    mvgal_config.enable_kernel_names = true;
    mvgal_config.stats_interval = 1;
    mvgal_config.enable_stats = true;
    
    // Initialize GPU info
    for (int i = 0; i < 2; i++) {
        mvgal_gpus[i].id = i;
        mvgal_gpus[i].priority = i;
        mvgal_gpus[i].enabled = true;
        mvgal_gpus[i].active = true;
        if (i == 0) {
            snprintf(mvgal_gpus[i].name, sizeof(mvgal_gpus[i].name), "NVIDIA GeForce RTX 4090");
            snprintf(mvgal_gpus[i].vendor, sizeof(mvgal_gpus[i].vendor), "nvidia");
            mvgal_gpus[i].memory_total = (uint64_t)24 * 1024 * 1024 * 1024; // 24 GB
        } else {
            snprintf(mvgal_gpus[i].name, sizeof(mvgal_gpus[i].name), "AMD Radeon RX 7900 XTX");
            snprintf(mvgal_gpus[i].vendor, sizeof(mvgal_gpus[i].vendor), "amd");
            mvgal_gpus[i].memory_total = (uint64_t)24 * 1024 * 1024 * 1024; // 24 GB
        }
    }
    
    mvgal_initialized = true;
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

void mvgal_shutdown(void) {
    pthread_mutex_lock(&mvgal_mutex);
    mvgal_initialized = false;
    pthread_mutex_unlock(&mvgal_mutex);
}

int mvgal_get_gpu_count(void) {
    pthread_mutex_lock(&mvgal_mutex);
    int count = mvgal_config.gpu_count;
    pthread_mutex_unlock(&mvgal_mutex);
    return count;
}

int mvgal_get_gpu_info(int index, mvgal_gpu_info_t *info) {
    if (!info || index < 0 || index >= 8) return -1;
    pthread_mutex_lock(&mvgal_mutex);
    *info = mvgal_gpus[index];
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

int mvgal_set_strategy(mvgal_strategy_t strategy) {
    pthread_mutex_lock(&mvgal_mutex);
    mvgal_config.default_strategy = strategy;
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

mvgal_strategy_t mvgal_get_strategy(void) {
    pthread_mutex_lock(&mvgal_mutex);
    mvgal_strategy_t strategy = mvgal_config.default_strategy;
    pthread_mutex_unlock(&mvgal_mutex);
    return strategy;
}

int mvgal_set_gpu_enabled(int index, bool enabled) {
    if (index < 0 || index >= 8) return -1;
    pthread_mutex_lock(&mvgal_mutex);
    mvgal_gpus[index].enabled = enabled;
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

int mvgal_set_gpu_priority(int index, int priority) {
    if (index < 0 || index >= 8) return -1;
    pthread_mutex_lock(&mvgal_mutex);
    mvgal_gpus[index].priority = priority;
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

int mvgal_get_stats(mvgal_stats_t *stats) {
    if (!stats) return -1;
    pthread_mutex_lock(&mvgal_mutex);
    *stats = mvgal_stats;
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

int mvgal_reset_stats(void) {
    pthread_mutex_lock(&mvgal_mutex);
    memset(&mvgal_stats, 0, sizeof(mvgal_stats));
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

int mvgal_reload_config(void) {
    // Reload configuration from file
    return 0;
}

int mvgal_get_config(mvgal_config_t *config) {
    if (!config) return -1;
    pthread_mutex_lock(&mvgal_mutex);
    *config = mvgal_config;
    pthread_mutex_unlock(&mvgal_mutex);
    return 0;
}

int mvgal_set_config(const char *key, const char *value) {
    (void)key;
    (void)value;
    // Set configuration option
    return 0;
}

void mvgal_log(const char *level, const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("[%s] ", level);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}
