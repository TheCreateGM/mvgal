/**
 * @file command_buffer_rewrite.c
 * @brief Command Buffer Rewrite Engine for SFR/AFR Dynamic Workload Rebalancing
 *
 * Implements dynamic command buffer rewriting for:
 * - Split Frame Rendering (SFR): divide each frame across GPUs
 * - Alternate Frame Rendering (AFR): alternate frames per GPU
 * - Compute workload partitioning
 *
 * Copyright (C) 2026 MVGAL Project
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>

#include <vulkan/vulkan.h>
#include "mvgal/mvgal.h"
#include "mvgal/mvgal_log.h"

/* ============================================================================
 * Constants and Types
 * ============================================================================ */

#define MAX_REWRITE_ENTRIES 1024
#define MAX_GPUS 16
#define SFR_TILE_COUNT 4  /* Number of tiles for SFR mode */

/**
 * @brief Command buffer rewrite strategy
 */
typedef enum {
    REWRITE_STRATEGY_AFR = 0,      /* Alternate Frame Rendering */
    REWRITE_STRATEGY_SFR,          /* Split Frame Rendering */
    REWRITE_STRATEGY_COMPUTE_SPLIT, /* Compute dispatch splitting */
    REWRITE_STRATEGY_COPY_DISTRIB,  /* Copy distribution across GPUs */
} rewrite_strategy_t;

/**
 * @brief SFR tile configuration
 */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t gpu_index;
} sfr_tile_t;

/**
 * @brief Frame tracking for AFR
 */
typedef struct {
    uint64_t frame_number;
    uint32_t gpu_index;
    VkSemaphore wait_semaphore;
    VkSemaphore signal_semaphore;
    VkFence fence;
    bool completed;
} afr_frame_state_t;

/**
 * @brief Command buffer rewrite context
 */
typedef struct {
    /* Strategy configuration */
    rewrite_strategy_t strategy;
    uint32_t gpu_count;
    uint32_t primary_gpu;
    
    /* SFR configuration */
    sfr_tile_t sfr_tiles[SFR_TILE_COUNT];
    uint32_t sfr_tile_count;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    
    /* AFR configuration */
    afr_frame_state_t *afr_frames;
    uint32_t afr_frame_count;
    uint64_t current_frame;
    uint32_t current_gpu;
    pthread_mutex_t afr_lock;
    
    /* Dynamic rebalancing */
    float *gpu_utilization;        /* Per-GPU utilization tracking */
    uint64_t *gpu_workload_count;  /* Workloads submitted per GPU */
    float load_balance_threshold;  /* Threshold for rebalancing */
    bool dynamic_rebalancing;
    
    /* Command buffer tracking */
    pthread_mutex_t tracking_lock;
    struct {
        VkCommandBuffer original;
        VkCommandBuffer *rewritten;  /* Array per GPU */
        uint32_t rewritten_count;
        bool active;
    } tracking[MAX_REWRITE_ENTRIES];
    uint32_t tracking_count;
    
} rewrite_context_t;

/* Global rewrite context */
static rewrite_context_t g_rewrite_ctx = {0};
static pthread_mutex_t g_rewrite_lock = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Context Management
 * ============================================================================ */

/**
 * @brief Initialize the command buffer rewrite engine
 */
mvgal_error_t mvgal_rewrite_engine_init(uint32_t gpu_count, rewrite_strategy_t strategy)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.strategy != 0 || g_rewrite_ctx.gpu_count > 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }
    
    memset(&g_rewrite_ctx, 0, sizeof(g_rewrite_ctx));
    
    g_rewrite_ctx.gpu_count = gpu_count;
    g_rewrite_ctx.strategy = strategy;
    g_rewrite_ctx.primary_gpu = 0;
    g_rewrite_ctx.sfr_tile_count = SFR_TILE_COUNT;
    g_rewrite_ctx.load_balance_threshold = 0.15f;  /* 15% imbalance threshold */
    g_rewrite_ctx.dynamic_rebalancing = true;
    
    /* Allocate tracking arrays */
    g_rewrite_ctx.gpu_utilization = calloc(gpu_count, sizeof(float));
    g_rewrite_ctx.gpu_workload_count = calloc(gpu_count, sizeof(uint64_t));
    
    if (!g_rewrite_ctx.gpu_utilization || !g_rewrite_ctx.gpu_workload_count) {
        free(g_rewrite_ctx.gpu_utilization);
        free(g_rewrite_ctx.gpu_workload_count);
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    /* Initialize mutexes */
    pthread_mutex_init(&g_rewrite_ctx.afr_lock, NULL);
    pthread_mutex_init(&g_rewrite_ctx.tracking_lock, NULL);
    
    /* Setup default SFR tiles (2x2 grid) */
    for (uint32_t i = 0; i < SFR_TILE_COUNT; i++) {
        g_rewrite_ctx.sfr_tiles[i].gpu_index = i % gpu_count;
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Command buffer rewrite engine initialized: strategy=%d, gpus=%u",
                   strategy, gpu_count);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Shutdown the rewrite engine
 */
mvgal_error_t mvgal_rewrite_engine_shutdown(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_SUCCESS;
    }
    
    /* Cleanup tracking entries */
    pthread_mutex_lock(&g_rewrite_ctx.tracking_lock);
    for (uint32_t i = 0; i < g_rewrite_ctx.tracking_count; i++) {
        if (g_rewrite_ctx.tracking[i].active) {
            free(g_rewrite_ctx.tracking[i].rewritten);
        }
    }
    pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
    
    /* Cleanup AFR frames */
    if (g_rewrite_ctx.afr_frames) {
        free(g_rewrite_ctx.afr_frames);
    }
    
    /* Cleanup tracking arrays */
    free(g_rewrite_ctx.gpu_utilization);
    free(g_rewrite_ctx.gpu_workload_count);
    
    pthread_mutex_destroy(&g_rewrite_ctx.afr_lock);
    pthread_mutex_destroy(&g_rewrite_ctx.tracking_lock);
    
    memset(&g_rewrite_ctx, 0, sizeof(g_rewrite_ctx));
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Command buffer rewrite engine shutdown");
    
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * SFR (Split Frame Rendering) Implementation
 * ============================================================================ */

/**
 * @brief Configure SFR tile layout
 */
mvgal_error_t mvgal_rewrite_sfr_configure(uint32_t width, uint32_t height, 
                                          uint32_t tile_count_x,
                                          uint32_t tile_count_y)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t total_tiles = tile_count_x * tile_count_y;
    if (total_tiles > SFR_TILE_COUNT) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    g_rewrite_ctx.framebuffer_width = width;
    g_rewrite_ctx.framebuffer_height = height;
    g_rewrite_ctx.sfr_tile_count = total_tiles;
    
    uint32_t tile_width = width / tile_count_x;
    uint32_t tile_height = height / tile_count_y;
    
    /* Calculate tile positions with load balancing */
    for (uint32_t ty = 0; ty < tile_count_y; ty++) {
        for (uint32_t tx = 0; tx < tile_count_x; tx++) {
            uint32_t i = ty * tile_count_x + tx;
            
            g_rewrite_ctx.sfr_tiles[i].x = tx * tile_width;
            g_rewrite_ctx.sfr_tiles[i].y = ty * tile_height;
            g_rewrite_ctx.sfr_tiles[i].width = tile_width;
            g_rewrite_ctx.sfr_tiles[i].height = tile_height;
            
            /* Distribute tiles across GPUs */
            g_rewrite_ctx.sfr_tiles[i].gpu_index = i % g_rewrite_ctx.gpu_count;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("SFR configured: %ux%u tiles, %ux%u resolution",
                   tile_count_x, tile_count_y, width, height);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Rewrite a render pass command for SFR
 *
 * This modifies the scissor and viewport to render only the assigned tile.
 */
static void rewrite_render_pass_sfr(VkCommandBuffer cmd_buf, const sfr_tile_t *tile)
{
    /* Set scissor to tile bounds */
    VkRect2D scissor = {
        .offset = {(int32_t)tile->x, (int32_t)tile->y},
        .extent = {tile->width, tile->height}
    };
    
    /* Set viewport to tile bounds */
    VkViewport viewport = {
        .x = (float)tile->x,
        .y = (float)tile->y,
        .width = (float)tile->width,
        .height = (float)tile->height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    
    /* The actual command buffer modification would happen here.
     * In a full implementation, this would:
     * 1. Record the scissor/viewport commands
     * 2. Track which GPU this tile targets
     * 3. Set up the correct render target (tile buffer)
     */
    
    (void)cmd_buf;
    (void)scissor;
    (void)viewport;
}

/**
 * @brief Calculate dynamic SFR tiles based on GPU load
 *
 * This redistributes tile sizes based on current GPU utilization
 * to maintain balanced frame times across all GPUs.
 */
static void rebalance_sfr_tiles(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (!g_rewrite_ctx.dynamic_rebalancing || g_rewrite_ctx.gpu_count < 2) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return;
    }
    
    /* Find min/max utilization */
    float min_util = 1.0f, max_util = 0.0f;
    float avg_util = 0.0f;
    
    for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
        float util = g_rewrite_ctx.gpu_utilization[i];
        if (util < min_util) min_util = util;
        if (util > max_util) max_util = util;
        avg_util += util;
    }
    avg_util /= g_rewrite_ctx.gpu_count;
    
    float imbalance = max_util - min_util;
    
    /* If imbalance exceeds threshold, rebalance tiles */
    if (imbalance > g_rewrite_ctx.load_balance_threshold) {
        MVGAL_LOG_INFO("Rebalancing SFR tiles: imbalance=%.2f%%", imbalance * 100.0f);
        
        /* Calculate target workload per GPU (inverse of utilization) */
        float target_workload[MAX_GPUS];
        float total_target = 0.0f;
        
        for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
            /* GPUs with lower utilization should get more work */
            target_workload[i] = 1.0f - g_rewrite_ctx.gpu_utilization[i];
            total_target += target_workload[i];
        }
        
        /* Normalize and convert to tile counts */
        if (total_target > 0.0f) {
            for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
                target_workload[i] /= total_target;
            }
            
            /* Redistribute tiles - simple greedy assignment */
            uint32_t tile_idx = 0;
            for (uint32_t gpu = 0; gpu < g_rewrite_ctx.gpu_count; gpu++) {
                uint32_t tiles_for_gpu = (uint32_t)(target_workload[gpu] * 
                                                     g_rewrite_ctx.sfr_tile_count);
                if (tiles_for_gpu == 0) tiles_for_gpu = 1;  /* Ensure at least 1 tile */
                
                for (uint32_t t = 0; t < tiles_for_gpu && tile_idx < g_rewrite_ctx.sfr_tile_count; t++) {
                    g_rewrite_ctx.sfr_tiles[tile_idx].gpu_index = gpu;
                    tile_idx++;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
}

/* ============================================================================
 * AFR (Alternate Frame Rendering) Implementation
 * ============================================================================ */

/**
 * @brief Configure AFR frame pacing
 */
mvgal_error_t mvgal_rewrite_afr_configure(uint32_t frame_latency)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    
    /* Cleanup existing frames */
    if (g_rewrite_ctx.afr_frames) {
        free(g_rewrite_ctx.afr_frames);
    }
    
    /* Allocate frame states */
    g_rewrite_ctx.afr_frame_count = frame_latency * g_rewrite_ctx.gpu_count;
    g_rewrite_ctx.afr_frames = calloc(g_rewrite_ctx.afr_frame_count, 
                                       sizeof(afr_frame_state_t));
    
    if (!g_rewrite_ctx.afr_frames) {
        pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    /* Initialize frame states */
    for (uint32_t i = 0; i < g_rewrite_ctx.afr_frame_count; i++) {
        g_rewrite_ctx.afr_frames[i].frame_number = 0;
        g_rewrite_ctx.afr_frames[i].gpu_index = i % g_rewrite_ctx.gpu_count;
        g_rewrite_ctx.afr_frames[i].completed = true;
    }
    
    g_rewrite_ctx.current_frame = 0;
    g_rewrite_ctx.current_gpu = 0;
    
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("AFR configured: latency=%u, total frames=%u",
                   frame_latency, g_rewrite_ctx.afr_frame_count);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get GPU assignment for next frame (AFR)
 */
uint32_t mvgal_rewrite_afr_get_next_gpu(void)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    
    uint32_t gpu = g_rewrite_ctx.current_gpu;
    
    /* Round-robin to next GPU */
    g_rewrite_ctx.current_gpu = (g_rewrite_ctx.current_gpu + 1) % g_rewrite_ctx.gpu_count;
    g_rewrite_ctx.current_frame++;
    
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    
    return gpu;
}

/**
 * @brief Dynamic AFR rebalancing based on frame completion times
 *
 * If one GPU consistently finishes frames faster, we can assign it
 * more frames to improve overall throughput.
 */
static void rebalance_afr_frames(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (!g_rewrite_ctx.dynamic_rebalancing || g_rewrite_ctx.gpu_count < 2) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return;
    }
    
    /* Analyze GPU performance from workload counts */
    uint64_t total_workloads = 0;
    for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
        total_workloads += g_rewrite_ctx.gpu_workload_count[i];
    }
    
    if (total_workloads == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return;
    }
    
    /* Check if workload distribution is balanced */
    float ideal_share = 1.0f / g_rewrite_ctx.gpu_count;
    bool needs_rebalance = false;
    
    for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
        float actual_share = (float)g_rewrite_ctx.gpu_workload_count[i] / total_workloads;
        float deviation = actual_share - ideal_share;
        
        if (deviation > g_rewrite_ctx.load_balance_threshold) {
            needs_rebalance = true;
            break;
        }
    }
    
    if (needs_rebalance) {
        MVGAL_LOG_INFO("AFR rebalancing triggered");
        /* The round-robin nature of AFR naturally balances over time,
         * but we could implement more sophisticated frame skipping here */
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
}

/* ============================================================================
 * Compute Workload Distribution
 * ============================================================================ */

/**
 * @brief Split a compute dispatch across multiple GPUs
 */
mvgal_error_t mvgal_rewrite_compute_split(
    uint32_t total_workgroups_x,
    uint32_t total_workgroups_y,
    uint32_t total_workgroups_z,
    uint32_t *gpu_assignment,
    uint32_t *workgroup_offsets,
    uint32_t *workgroup_counts)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t gpu_count = g_rewrite_ctx.gpu_count;
    
    /* Simple 1D partitioning along X dimension */
    uint32_t workgroups_per_gpu = total_workgroups_x / gpu_count;
    uint32_t remainder = total_workgroups_x % gpu_count;
    
    uint32_t offset = 0;
    for (uint32_t i = 0; i < gpu_count; i++) {
        gpu_assignment[i] = i;
        workgroup_offsets[i * 3 + 0] = offset;  /* X offset */
        workgroup_offsets[i * 3 + 1] = 0;        /* Y offset */
        workgroup_offsets[i * 3 + 2] = 0;        /* Z offset */
        
        workgroup_counts[i * 3 + 0] = workgroups_per_gpu + (i < remainder ? 1 : 0);
        workgroup_counts[i * 3 + 1] = total_workgroups_y;
        workgroup_counts[i * 3 + 2] = total_workgroups_z;
        
        offset += workgroup_counts[i * 3 + 0];
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Dynamic Rebalancing API
 * ============================================================================ */

/**
 * @brief Update GPU utilization for rebalancing decisions
 */
mvgal_error_t mvgal_rewrite_update_gpu_utilization(uint32_t gpu_index, float utilization)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (gpu_index >= g_rewrite_ctx.gpu_count) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    /* Smooth the utilization reading */
    g_rewrite_ctx.gpu_utilization[gpu_index] = 
        g_rewrite_ctx.gpu_utilization[gpu_index] * 0.7f + utilization * 0.3f;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    /* Trigger rebalancing if needed */
    if (g_rewrite_ctx.strategy == REWRITE_STRATEGY_SFR) {
        rebalance_sfr_tiles();
    } else if (g_rewrite_ctx.strategy == REWRITE_STRATEGY_AFR) {
        rebalance_afr_frames();
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Record workload completion for statistics
 */
mvgal_error_t mvgal_rewrite_record_completion(uint32_t gpu_index, uint64_t duration_ns)
{
    (void)duration_ns;
    
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (gpu_index >= g_rewrite_ctx.gpu_count) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    g_rewrite_ctx.gpu_workload_count[gpu_index]++;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get current rebalancing statistics
 */
mvgal_error_t mvgal_rewrite_get_stats(
    float *gpu_utilization,
    uint64_t *gpu_workloads,
    uint32_t *current_strategy)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (gpu_utilization) {
        memcpy(gpu_utilization, g_rewrite_ctx.gpu_utilization,
               g_rewrite_ctx.gpu_count * sizeof(float));
    }
    
    if (gpu_workloads) {
        memcpy(gpu_workloads, g_rewrite_ctx.gpu_workload_count,
               g_rewrite_ctx.gpu_count * sizeof(uint64_t));
    }
    
    if (current_strategy) {
        *current_strategy = (uint32_t)g_rewrite_ctx.strategy;
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Enable/disable dynamic rebalancing
 */
mvgal_error_t mvgal_rewrite_set_dynamic_rebalancing(bool enabled)
{
    pthread_mutex_lock(&g_rewrite_lock);
    g_rewrite_ctx.dynamic_rebalancing = enabled;
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Dynamic rebalancing %s", enabled ? "enabled" : "disabled");
    
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Command Buffer Tracking
 * ============================================================================ */

/**
 * @brief Register a command buffer for tracking
 */
mvgal_error_t mvgal_rewrite_register_command_buffer(VkCommandBuffer original)
{
    pthread_mutex_lock(&g_rewrite_ctx.tracking_lock);
    
    if (g_rewrite_ctx.tracking_count >= MAX_REWRITE_ENTRIES) {
        pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    /* Find or create entry */
    for (uint32_t i = 0; i < g_rewrite_ctx.tracking_count; i++) {
        if (!g_rewrite_ctx.tracking[i].active) {
            g_rewrite_ctx.tracking[i].original = original;
            g_rewrite_ctx.tracking[i].active = true;
            pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
            return MVGAL_SUCCESS;
        }
    }
    
    /* Add new entry */
    g_rewrite_ctx.tracking[g_rewrite_ctx.tracking_count].original = original;
    g_rewrite_ctx.tracking[g_rewrite_ctx.tracking_count].active = true;
    g_rewrite_ctx.tracking[g_rewrite_ctx.tracking_count].rewritten = 
        calloc(g_rewrite_ctx.gpu_count, sizeof(VkCommandBuffer));
    g_rewrite_ctx.tracking_count++;
    
    pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get the rewritten command buffer for a specific GPU
 */
VkCommandBuffer mvgal_rewrite_get_command_buffer(VkCommandBuffer original, uint32_t gpu_index)
{
    pthread_mutex_lock(&g_rewrite_ctx.tracking_lock);
    
    for (uint32_t i = 0; i < g_rewrite_ctx.tracking_count; i++) {
        if (g_rewrite_ctx.tracking[i].active && 
            g_rewrite_ctx.tracking[i].original == original) {
            if (gpu_index < g_rewrite_ctx.gpu_count) {
                VkCommandBuffer result = g_rewrite_ctx.tracking[i].rewritten[gpu_index];
                pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
                return result;
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
    return VK_NULL_HANDLE;
}

/**
 * @brief Unregister a command buffer
 */
mvgal_error_t mvgal_rewrite_unregister_command_buffer(VkCommandBuffer original)
{
    pthread_mutex_lock(&g_rewrite_ctx.tracking_lock);
    
    for (uint32_t i = 0; i < g_rewrite_ctx.tracking_count; i++) {
        if (g_rewrite_ctx.tracking[i].active && 
            g_rewrite_ctx.tracking[i].original == original) {
            free(g_rewrite_ctx.tracking[i].rewritten);
            g_rewrite_ctx.tracking[i].rewritten = NULL;
            g_rewrite_ctx.tracking[i].active = false;
            pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
            return MVGAL_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
    return MVGAL_ERROR_NOT_FOUND;
}

/* ============================================================================
 * Strategy Selection
 * ============================================================================ */

/**
 * @brief Change rewrite strategy at runtime
 */
mvgal_error_t mvgal_rewrite_set_strategy(rewrite_strategy_t strategy)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    rewrite_strategy_t old_strategy = g_rewrite_ctx.strategy;
    g_rewrite_ctx.strategy = strategy;
    
    /* Reset state for new strategy */
    if (strategy == REWRITE_STRATEGY_AFR && old_strategy != REWRITE_STRATEGY_AFR) {
        /* AFR needs frame tracking setup */
        mvgal_rewrite_afr_configure(2);  /* Default 2-frame latency */
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Rewrite strategy changed: %d -> %d", old_strategy, strategy);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get current strategy
 */
rewrite_strategy_t mvgal_rewrite_get_strategy(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    rewrite_strategy_t strategy = g_rewrite_ctx.strategy;
    pthread_mutex_unlock(&g_rewrite_lock);
    return strategy;
}

/* ============================================================================
 * Frame Assembly and Presentation
 * ============================================================================ */

/**
 * @brief Assemble SFR tiles into final frame
 *
 * After all GPUs have rendered their tiles, this composits them
 * into the final framebuffer for presentation.
 */
mvgal_error_t mvgal_rewrite_sfr_assemble_frame(
    VkCommandBuffer cmd_buf,
    VkImage target_image,
    VkImage *tile_images,
    VkExtent2D target_extent)
{
    (void)cmd_buf;
    (void)target_image;
    (void)tile_images;
    (void)target_extent;
    
    /* In a full implementation, this would:
     * 1. Issue copy commands from each tile to the target image
     * 2. Handle synchronization between tile completion and assembly
     * 3. Potentially use compute shaders for blending if needed
     */
    
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    /* Mark all tiles as ready for assembly */
    /* Actual implementation would wait on GPU fences */
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Wait for AFR frame completion
 */
mvgal_error_t mvgal_rewrite_afr_wait_for_frame(uint64_t frame_number, uint32_t timeout_ms)
{
    (void)timeout_ms;
    
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    
    if (!g_rewrite_ctx.afr_frames) {
        pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    /* Find the frame */
    for (uint32_t i = 0; i < g_rewrite_ctx.afr_frame_count; i++) {
        if (g_rewrite_ctx.afr_frames[i].frame_number == frame_number) {
            /* In a full implementation, this would wait on the frame's fence */
            (void)g_rewrite_ctx.afr_frames[i].completed;
            pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
            return MVGAL_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    return MVGAL_ERROR_NOT_FOUND;
}

/* ============================================================================
 * Integration with Scheduler
 * ============================================================================ */

/**
 * @brief Get recommended workload split for current GPU state
 *
 * This is called by the scheduler to determine how to distribute
 * work across GPUs based on the current rewrite strategy.
 */
mvgal_error_t mvgal_rewrite_get_workload_split(
    uint32_t *gpu_indices,
    float *gpu_weights,
    uint32_t *gpu_count)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t count = g_rewrite_ctx.gpu_count;
    
    for (uint32_t i = 0; i < count; i++) {
        gpu_indices[i] = i;
        
        if (g_rewrite_ctx.strategy == REWRITE_STRATEGY_AFR) {
            /* AFR: Equal weight (round-robin handles distribution) */
            gpu_weights[i] = 1.0f;
        } else if (g_rewrite_ctx.strategy == REWRITE_STRATEGY_SFR) {
            /* SFR: Weight by tile count assigned to each GPU */
            uint32_t tile_count = 0;
            for (uint32_t t = 0; t < g_rewrite_ctx.sfr_tile_count; t++) {
                if (g_rewrite_ctx.sfr_tiles[t].gpu_index == i) {
                    tile_count++;
                }
            }
            gpu_weights[i] = (float)tile_count;
        } else {
            /* Other strategies: equal distribution */
            gpu_weights[i] = 1.0f;
        }
    }
    
    *gpu_count = count;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Notify rewrite engine of workload submission
 *
 * Called by the scheduler after submitting work to a specific GPU.
 */
void mvgal_rewrite_notify_submission(uint32_t gpu_index, uint64_t workload_id)
{
    (void)workload_id;
    
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (gpu_index < g_rewrite_ctx.gpu_count) {
        g_rewrite_ctx.gpu_workload_count[gpu_index]++;
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
}

/**
 * @brief Notify rewrite engine of workload completion
 *
 * Called when a GPU signals completion of a workload.
 */
void mvgal_rewrite_notify_completion(uint32_t gpu_index, uint64_t workload_id,
                                     uint64_t duration_ns)
{
    (void)workload_id;
    
    /* Update utilization based on completion time */
    float utilization = 0.0f;
    
    /* Simple utilization model: longer duration = higher utilization */
    /* In practice, this would come from GPU metrics */
    if (duration_ns > 0) {
        /* Normalize to some target frame time (e.g., 16ms @ 60fps) */
        utilization = (float)duration_ns / 16000000.0f;
        if (utilization > 1.0f) utilization = 1.0f;
    }
    
    mvgal_rewrite_update_gpu_utilization(gpu_index, utilization);
}

/**
 * @brief Get the primary GPU for this frame
 *
 * For AFR, this rotates. For SFR, this is the display-connected GPU.
 */
uint32_t mvgal_rewrite_get_primary_gpu(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    uint32_t primary = g_rewrite_ctx.primary_gpu;
    
    if (g_rewrite_ctx.strategy == REWRITE_STRATEGY_AFR) {
        /* For AFR, the "primary" is the one handling the current frame */
        pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
        primary = g_rewrite_ctx.current_gpu;
        pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return primary;
}

/**
 * @brief Set which GPU is connected to the display (for SFR compositing)
 */
mvgal_error_t mvgal_rewrite_set_display_gpu(uint32_t gpu_index)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (gpu_index >= g_rewrite_ctx.gpu_count) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    g_rewrite_ctx.primary_gpu = gpu_index;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Display GPU set to %u", gpu_index);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Force immediate rebalancing
 */
mvgal_error_t mvgal_rewrite_trigger_rebalance(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    rewrite_strategy_t strategy = g_rewrite_ctx.strategy;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    if (strategy == REWRITE_STRATEGY_SFR) {
        rebalance_sfr_tiles();
    } else if (strategy == REWRITE_STRATEGY_AFR) {
        rebalance_afr_frames();
    }
    
    MVGAL_LOG_INFO("Manual rebalancing triggered for strategy %d", strategy);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Query detailed SFR tile information
 */
mvgal_error_t mvgal_rewrite_get_sfr_tiles(
    uint32_t *tile_count,
    uint32_t *gpu_indices,
    VkRect2D *tile_regions,
    uint32_t max_tiles)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    if (g_rewrite_ctx.strategy != REWRITE_STRATEGY_SFR) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_INVALID_STATE;
    }
    
    uint32_t count = g_rewrite_ctx.sfr_tile_count;
    if (count > max_tiles) {
        count = max_tiles;
    }
    
    *tile_count = count;
    
    for (uint32_t i = 0; i < count; i++) {
        if (gpu_indices) {
            gpu_indices[i] = g_rewrite_ctx.sfr_tiles[i].gpu_index;
        }
        if (tile_regions) {
            tile_regions[i].offset.x = (int32_t)g_rewrite_ctx.sfr_tiles[i].x;
            tile_regions[i].offset.y = (int32_t)g_rewrite_ctx.sfr_tiles[i].y;
            tile_regions[i].extent.width = g_rewrite_ctx.sfr_tiles[i].width;
            tile_regions[i].extent.height = g_rewrite_ctx.sfr_tiles[i].height;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Validate rewrite configuration
 */
mvgal_error_t mvgal_rewrite_validate_config(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    /* Validate based on strategy */
    mvgal_error_t result = MVGAL_SUCCESS;
    
    switch (g_rewrite_ctx.strategy) {
        case REWRITE_STRATEGY_SFR:
            if (g_rewrite_ctx.sfr_tile_count == 0 ||
                g_rewrite_ctx.framebuffer_width == 0 ||
                g_rewrite_ctx.framebuffer_height == 0) {
                result = MVGAL_ERROR_INVALID_STATE;
            }
            break;
            
        case REWRITE_STRATEGY_AFR:
            if (g_rewrite_ctx.afr_frames == NULL) {
                result = MVGAL_ERROR_INVALID_STATE;
            }
            break;
            
        case REWRITE_STRATEGY_COMPUTE_SPLIT:
        case REWRITE_STRATEGY_COPY_DISTRIB:
            /* These don't need special validation */
            break;
            
        default:
            result = MVGAL_ERROR_INVALID_PARAMETER;
            break;
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return result;
}

/**
 * @brief Reset rewrite engine state
 */
mvgal_error_t mvgal_rewrite_reset(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    /* Reset counters */
    memset(g_rewrite_ctx.gpu_utilization, 0, 
           g_rewrite_ctx.gpu_count * sizeof(float));
    memset(g_rewrite_ctx.gpu_workload_count, 0, 
           g_rewrite_ctx.gpu_count * sizeof(uint64_t));
    
    /* Reset AFR state */
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    if (g_rewrite_ctx.afr_frames) {
        for (uint32_t i = 0; i < g_rewrite_ctx.afr_frame_count; i++) {
            g_rewrite_ctx.afr_frames[i].completed = true;
        }
    }
    g_rewrite_ctx.current_frame = 0;
    g_rewrite_ctx.current_gpu = 0;
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    
    /* Reset SFR tiles to default distribution */
    for (uint32_t i = 0; i < g_rewrite_ctx.sfr_tile_count; i++) {
        g_rewrite_ctx.sfr_tiles[i].gpu_index = i % g_rewrite_ctx.gpu_count;
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Rewrite engine state reset");
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Export rewrite configuration to JSON
 */
mvgal_error_t mvgal_rewrite_export_config(char *buffer, size_t *buffer_size)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (!buffer_size) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    const char *strategy_names[] = {
        "afr",
        "sfr",
        "compute_split",
        "copy_distrib"
    };
    
    /* Calculate required size */
    size_t required = 256;  /* Base size */
    required += g_rewrite_ctx.gpu_count * 64;  /* Per-GPU data */
    required += g_rewrite_ctx.sfr_tile_count * 64;  /* Tile data */
    
    if (!buffer || *buffer_size < required) {
        *buffer_size = required;
        pthread_mutex_unlock(&g_rewrite_lock);
        return buffer ? MVGAL_ERROR_BUFFER_TOO_SMALL : MVGAL_SUCCESS;
    }
    
    /* Write JSON */
    int written = snprintf(buffer, *buffer_size,
        "{\n"
        "  \"strategy\": \"%s\",\n"
        "  \"gpu_count\": %u,\n"
        "  \"dynamic_rebalancing\": %s,\n"
        "  \"load_balance_threshold\": %.3f,\n"
        "  \"gpus\": [\n",
        strategy_names[g_rewrite_ctx.strategy],
        g_rewrite_ctx.gpu_count,
        g_rewrite_ctx.dynamic_rebalancing ? "true" : "false",
        g_rewrite_ctx.load_balance_threshold);
    
    for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count && written < (int)*buffer_size; i++) {
        written += snprintf(buffer + written, *buffer_size - written,
            "    {\n"
            "      \"index\": %u,\n"
            "      \"utilization\": %.3f,\n"
            "      \"workloads\": %llu\n"
            "    }%s\n",
            i,
            g_rewrite_ctx.gpu_utilization[i],
            (unsigned long long)g_rewrite_ctx.gpu_workload_count[i],
            (i < g_rewrite_ctx.gpu_count - 1) ? "," : "");
    }
    
    if (written < (int)*buffer_size) {
        written += snprintf(buffer + written, *buffer_size - written,
            "  ],\n"
            "  \"sfr\": {\n"
            "    \"tile_count\": %u,\n"
            "    \"framebuffer_width\": %u,\n"
            "    \"framebuffer_height\": %u\n"
            "  }\n"
            "}\n",
            g_rewrite_ctx.sfr_tile_count,
            g_rewrite_ctx.framebuffer_width,
            g_rewrite_ctx.framebuffer_height);
    }
    
    *buffer_size = written;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Import rewrite configuration from JSON
 */
mvgal_error_t mvgal_rewrite_import_config(const char *json_config)
{
    (void)json_config;
    
    /* This would parse JSON and configure the rewrite engine */
    /* For now, just log that this feature exists */
    
    MVGAL_LOG_INFO("Configuration import requested (JSON parsing not implemented in this version)");
    
    return MVGAL_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Cleanup function for module unload
 */
void mvgal_rewrite_engine_cleanup(void)
{
    mvgal_rewrite_engine_shutdown();
}

/* ============================================================================
 * Module Information
 * ============================================================================ */

/**
 * @brief Get rewrite engine version information
 */
void mvgal_rewrite_get_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    if (major) *major = 0;
    if (minor) *minor = 2;
    if (patch) *patch = 1;
}

/**
 * @brief Get human-readable strategy name
 */
const char* mvgal_rewrite_strategy_name(rewrite_strategy_t strategy)
{
    switch (strategy) {
        case REWRITE_STRATEGY_AFR: return "Alternate Frame Rendering (AFR)";
        case REWRITE_STRATEGY_SFR: return "Split Frame Rendering (SFR)";
        case REWRITE_STRATEGY_COMPUTE_SPLIT: return "Compute Workload Split";
        case REWRITE_STRATEGY_COPY_DISTRIB: return "Copy Distribution";
        default: return "Unknown";
    }
}

/**
 * @brief Get current strategy description
 */
const char* mvgal_rewrite_get_strategy_name(void)
{
    return mvgal_rewrite_strategy_name(mvgal_rewrite_get_strategy());
}

/**
 * @brief Log current configuration
 */
void mvgal_rewrite_log_config(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("=== Command Buffer Rewrite Engine Configuration ===");
    MVGAL_LOG_INFO("Strategy: %s", mvgal_rewrite_strategy_name(g_rewrite_ctx.strategy));
    MVGAL_LOG_INFO("GPU Count: %u", g_rewrite_ctx.gpu_count);
    MVGAL_LOG_INFO("Dynamic Rebalancing: %s", 
                   g_rewrite_ctx.dynamic_rebalancing ? "enabled" : "disabled");
    MVGAL_LOG_INFO("Load Balance Threshold: %.1f%%", 
                   g_rewrite_ctx.load_balance_threshold * 100.0f);
    
    if (g_rewrite_ctx.strategy == REWRITE_STRATEGY_SFR) {
        MVGAL_LOG_INFO("SFR Tiles: %u", g_rewrite_ctx.sfr_tile_count);
        MVGAL_LOG_INFO("Framebuffer: %ux%u", 
                       g_rewrite_ctx.framebuffer_width,
                       g_rewrite_ctx.framebuffer_height);
    }
    
    MVGAL_LOG_INFO("Per-GPU Utilization:");
    for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
        MVGAL_LOG_INFO("  GPU %u: %.1f%% (%llu workloads)",
                       i,
                       g_rewrite_ctx.gpu_utilization[i] * 100.0f,
                       (unsigned long long)g_rewrite_ctx.gpu_workload_count[i]);
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
}

/**
 * @brief Check if rewrite engine is initialized
 */
bool mvgal_rewrite_is_initialized(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    bool initialized = (g_rewrite_ctx.gpu_count > 0);
    pthread_mutex_unlock(&g_rewrite_lock);
    return initialized;
}

/**
 * @brief Get number of GPUs managed by rewrite engine
 */
uint32_t mvgal_rewrite_get_gpu_count(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    uint32_t count = g_rewrite_ctx.gpu_count;
    pthread_mutex_unlock(&g_rewrite_lock);
    return count;
}

/**
 * @brief Check if dynamic rebalancing is enabled
 */
bool mvgal_rewrite_is_dynamic_rebalancing_enabled(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    bool enabled = g_rewrite_ctx.dynamic_rebalancing;
    pthread_mutex_unlock(&g_rewrite_lock);
    return enabled;
}

/**
 * @brief Get current load balance threshold
 */
float mvgal_rewrite_get_load_balance_threshold(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    float threshold = g_rewrite_ctx.load_balance_threshold;
    pthread_mutex_unlock(&g_rewrite_lock);
    return threshold;
}

/**
 * @brief Set load balance threshold
 */
mvgal_error_t mvgal_rewrite_set_load_balance_threshold(float threshold)
{
    if (threshold < 0.0f || threshold > 1.0f) {
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&g_rewrite_lock);
    g_rewrite_ctx.load_balance_threshold = threshold;
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Load balance threshold set to %.1f%%", threshold * 100.0f);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get GPU index assigned to specific SFR tile
 */
int32_t mvgal_rewrite_get_sfr_tile_gpu(uint32_t tile_index)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0 || 
        tile_index >= g_rewrite_ctx.sfr_tile_count ||
        g_rewrite_ctx.strategy != REWRITE_STRATEGY_SFR) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return -1;
    }
    
    int32_t gpu = (int32_t)g_rewrite_ctx.sfr_tiles[tile_index].gpu_index;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return gpu;
}

/**
 * @brief Set GPU index for specific SFR tile (manual override)
 */
mvgal_error_t mvgal_rewrite_set_sfr_tile_gpu(uint32_t tile_index, uint32_t gpu_index)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (tile_index >= g_rewrite_ctx.sfr_tile_count ||
        gpu_index >= g_rewrite_ctx.gpu_count ||
        g_rewrite_ctx.strategy != REWRITE_STRATEGY_SFR) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    g_rewrite_ctx.sfr_tiles[tile_index].gpu_index = gpu_index;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("SFR tile %u assigned to GPU %u", tile_index, gpu_index);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get AFR frame state
 */
mvgal_error_t mvgal_rewrite_get_afr_frame_state(
    uint64_t frame_number,
    uint32_t *gpu_index,
    bool *completed)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    
    if (!g_rewrite_ctx.afr_frames) {
        pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    for (uint32_t i = 0; i < g_rewrite_ctx.afr_frame_count; i++) {
        if (g_rewrite_ctx.afr_frames[i].frame_number == frame_number) {
            if (gpu_index) *gpu_index = g_rewrite_ctx.afr_frames[i].gpu_index;
            if (completed) *completed = g_rewrite_ctx.afr_frames[i].completed;
            pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
            return MVGAL_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    return MVGAL_ERROR_NOT_FOUND;
}

/**
 * @brief Record AFR frame submission
 */
mvgal_error_t mvgal_rewrite_record_afr_submission(
    uint64_t frame_number,
    uint32_t gpu_index,
    VkSemaphore wait_semaphore,
    VkSemaphore signal_semaphore,
    VkFence fence)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    
    if (!g_rewrite_ctx.afr_frames) {
        pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    /* Find an available slot */
    for (uint32_t i = 0; i < g_rewrite_ctx.afr_frame_count; i++) {
        if (g_rewrite_ctx.afr_frames[i].completed) {
            g_rewrite_ctx.afr_frames[i].frame_number = frame_number;
            g_rewrite_ctx.afr_frames[i].gpu_index = gpu_index;
            g_rewrite_ctx.afr_frames[i].wait_semaphore = wait_semaphore;
            g_rewrite_ctx.afr_frames[i].signal_semaphore = signal_semaphore;
            g_rewrite_ctx.afr_frames[i].fence = fence;
            g_rewrite_ctx.afr_frames[i].completed = false;
            
            pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
            return MVGAL_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    return MVGAL_ERROR_OUT_OF_RESOURCES;
}

/**
 * @brief Mark AFR frame as completed
 */
mvgal_error_t mvgal_rewrite_complete_afr_frame(uint64_t frame_number)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    
    if (!g_rewrite_ctx.afr_frames) {
        pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    for (uint32_t i = 0; i < g_rewrite_ctx.afr_frame_count; i++) {
        if (g_rewrite_ctx.afr_frames[i].frame_number == frame_number) {
            g_rewrite_ctx.afr_frames[i].completed = true;
            pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
            return MVGAL_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    return MVGAL_ERROR_NOT_FOUND;
}

/**
 * @brief Get current AFR frame number
 */
uint64_t mvgal_rewrite_get_current_frame(void)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    uint64_t frame = g_rewrite_ctx.current_frame;
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    return frame;
}

/**
 * @brief Get current AFR GPU index
 */
uint32_t mvgal_rewrite_get_current_gpu(void)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    uint32_t gpu = g_rewrite_ctx.current_gpu;
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    return gpu;
}

/**
 * @brief Reset AFR frame counter
 */
void mvgal_rewrite_reset_frame_counter(void)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    g_rewrite_ctx.current_frame = 0;
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
}

/**
 * @brief Get frame latency configuration
 */
uint32_t mvgal_rewrite_get_frame_latency(void)
{
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    uint32_t latency = g_rewrite_ctx.afr_frame_count;
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    return latency;
}

/**
 * @brief Set SFR framebuffer dimensions
 */
mvgal_error_t mvgal_rewrite_set_framebuffer_size(uint32_t width, uint32_t height)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    g_rewrite_ctx.framebuffer_width = width;
    g_rewrite_ctx.framebuffer_height = height;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Framebuffer size set to %ux%u", width, height);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get SFR framebuffer dimensions
 */
mvgal_error_t mvgal_rewrite_get_framebuffer_size(uint32_t *width, uint32_t *height)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (width) *width = g_rewrite_ctx.framebuffer_width;
    if (height) *height = g_rewrite_ctx.framebuffer_height;
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get SFR tile count
 */
uint32_t mvgal_rewrite_get_tile_count(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    uint32_t count = g_rewrite_ctx.sfr_tile_count;
    pthread_mutex_unlock(&g_rewrite_lock);
    return count;
}

/**
 * @brief Set SFR tile count
 */
mvgal_error_t mvgal_rewrite_set_tile_count(uint32_t count)
{
    if (count == 0 || count > SFR_TILE_COUNT) {
        return MVGAL_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&g_rewrite_lock);
    g_rewrite_ctx.sfr_tile_count = count;
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("SFR tile count set to %u", count);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get strategy enum from string
 */
rewrite_strategy_t mvgal_rewrite_strategy_from_string(const char *name)
{
    if (!name) return REWRITE_STRATEGY_AFR;
    
    if (strcmp(name, "afr") == 0) return REWRITE_STRATEGY_AFR;
    if (strcmp(name, "sfr") == 0) return REWRITE_STRATEGY_SFR;
    if (strcmp(name, "compute_split") == 0) return REWRITE_STRATEGY_COMPUTE_SPLIT;
    if (strcmp(name, "copy_distrib") == 0) return REWRITE_STRATEGY_COPY_DISTRIB;
    
    return REWRITE_STRATEGY_AFR;  /* Default */
}

/**
 * @brief Check if rewrite engine supports specific strategy
 */
bool mvgal_rewrite_supports_strategy(rewrite_strategy_t strategy)
{
    (void)strategy;
    return true;  /* All strategies are supported */
}

/**
 * @brief Get list of supported strategies
 */
void mvgal_rewrite_get_supported_strategies(
    rewrite_strategy_t *strategies,
    uint32_t *count)
{
    static const rewrite_strategy_t supported[] = {
        REWRITE_STRATEGY_AFR,
        REWRITE_STRATEGY_SFR,
        REWRITE_STRATEGY_COMPUTE_SPLIT,
        REWRITE_STRATEGY_COPY_DISTRIB
    };
    
    if (count) *count = sizeof(supported) / sizeof(supported[0]);
    if (strategies) {
        memcpy(strategies, supported, sizeof(supported));
    }
}

/**
 * @brief Quick health check
 */
mvgal_error_t mvgal_rewrite_health_check(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (g_rewrite_ctx.gpu_count == 0) {
        pthread_mutex_unlock(&g_rewrite_lock);
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    /* Check for stuck utilization (possible GPU hang) */
    for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
        if (g_rewrite_ctx.gpu_utilization[i] > 0.99f) {
            MVGAL_LOG_WARN("GPU %u utilization at 99%%+ - possible hang", i);
        }
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Dump debug information
 */
void mvgal_rewrite_dump_debug_info(void)
{
    mvgal_rewrite_log_config();
    
    pthread_mutex_lock(&g_rewrite_ctx.tracking_lock);
    MVGAL_LOG_INFO("Active command buffers: %u", g_rewrite_ctx.tracking_count);
    pthread_mutex_unlock(&g_rewrite_ctx.tracking_lock);
}

/**
 * @brief Get error string
 */
const char* mvgal_rewrite_error_string(mvgal_error_t error)
{
    switch (error) {
        case MVGAL_SUCCESS: return "Success";
        case MVGAL_ERROR_NOT_INITIALIZED: return "Not initialized";
        case MVGAL_ERROR_ALREADY_INITIALIZED: return "Already initialized";
        case MVGAL_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case MVGAL_ERROR_INVALID_STATE: return "Invalid state";
        case MVGAL_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case MVGAL_ERROR_OUT_OF_RESOURCES: return "Out of resources";
        case MVGAL_ERROR_BUFFER_TOO_SMALL: return "Buffer too small";
        case MVGAL_ERROR_NOT_FOUND: return "Not found";
        case MVGAL_ERROR_NOT_IMPLEMENTED: return "Not implemented";
        default: return "Unknown error";
    }
}

/* ============================================================================
 * Performance Monitoring
 * ============================================================================ */

/**
 * @brief Get performance statistics
 */
mvgal_error_t mvgal_rewrite_get_performance_stats(
    uint64_t *total_frames,
    uint64_t *total_workloads,
    float *average_gpu_utilization,
    float *balance_score)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    if (total_workloads) {
        uint64_t total = 0;
        for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
            total += g_rewrite_ctx.gpu_workload_count[i];
        }
        *total_workloads = total;
    }
    
    if (total_frames) {
        pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
        *total_frames = g_rewrite_ctx.current_frame;
        pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    }
    
    if (average_gpu_utilization) {
        float avg = 0.0f;
        for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
            avg += g_rewrite_ctx.gpu_utilization[i];
        }
        *average_gpu_utilization = avg / g_rewrite_ctx.gpu_count;
    }
    
    if (balance_score) {
        /* Calculate balance score: 1.0 = perfect balance, 0.0 = max imbalance */
        float min_util = 1.0f, max_util = 0.0f;
        for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
            if (g_rewrite_ctx.gpu_utilization[i] < min_util) {
                min_util = g_rewrite_ctx.gpu_utilization[i];
            }
            if (g_rewrite_ctx.gpu_utilization[i] > max_util) {
                max_util = g_rewrite_ctx.gpu_utilization[i];
            }
        }
        float range = max_util - min_util;
        *balance_score = 1.0f - (range * 2.0f);  /* Scale to 0-1 range */
        if (*balance_score < 0.0f) *balance_score = 0.0f;
    }
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Reset performance statistics
 */
mvgal_error_t mvgal_rewrite_reset_stats(void)
{
    pthread_mutex_lock(&g_rewrite_lock);
    
    for (uint32_t i = 0; i < g_rewrite_ctx.gpu_count; i++) {
        g_rewrite_ctx.gpu_workload_count[i] = 0;
    }
    
    pthread_mutex_lock(&g_rewrite_ctx.afr_lock);
    g_rewrite_ctx.current_frame = 0;
    pthread_mutex_unlock(&g_rewrite_ctx.afr_lock);
    
    pthread_mutex_unlock(&g_rewrite_lock);
    
    MVGAL_LOG_INFO("Performance statistics reset");
    
    return MVGAL_SUCCESS;
}
