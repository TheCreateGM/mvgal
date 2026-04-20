/**
 * @file sfr.c
 * @brief Split Frame Rendering (SFR) strategy implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This strategy splits each frame into regions and distributes those
 * regions across multiple GPUs for parallel rendering.
 */

#include "../scheduler_internal.h"
#include "mvgal_log.h"
#include <math.h>
#include <stdlib.h>

/**
 * @brief SFR mode
 */
typedef enum {
    SFR_MODE_HORIZONTAL,    ///< Split frame horizontally
    SFR_MODE_VERTICAL,      ///< Split frame vertically
    SFR_MODE_GRID,          ///< Split into 2D grid
    SFR_MODE_CHECKERBOARD,  ///< Checkerboard pattern
} sfr_mode_t;

/**
 * @brief Region for SFR
 */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t gpu_index;      ///< GPU assigned to this region
} sfr_region_t;

/**
 * @brief SFR state for a workload
 */
typedef struct {
    uint32_t region_count;
    sfr_region_t regions[MVGAL_SCHEDULER_MAX_GPUS * 4]; // Max regions
    uint32_t original_width;
    uint32_t original_height;
} sfr_workload_data_t;

/**
 * @brief Distribute workload using SFR strategy
 *
 * Splits the frame into regions assigned to different GPUs.
 */
mvgal_error_t mvgal_scheduler_distribute_sfr(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    mvgal_scheduler_state_t *state = mvgal_scheduler_get_state();
    
    // If workload has specific GPU assignments, use them for splitting
    if (workload->assigned_gpu_count > 0) {
        // Use pre-assigned GPUs
        uint32_t gpu_count = workload->assigned_gpu_count;
        
        // Free existing strategy data
        if (workload->strategy_data != NULL) {
            free(workload->strategy_data);
        }
        
        // Create SFR data
        sfr_workload_data_t *sfr_data = calloc(1, sizeof(sfr_workload_data_t));
        if (sfr_data == NULL) {
            return MVGAL_ERROR_OUT_OF_MEMORY;
        }
        
        // For simplicity, assume the frame can be split equally
        // In a real implementation, this would be based on actual frame dimensions
        sfr_data->original_width = 1920; // Default, would come from workload
        sfr_data->original_height = 1080;
        sfr_data->region_count = gpu_count;
        
        // Split horizontally
        uint32_t region_height = sfr_data->original_height / gpu_count;
        for (uint32_t i = 0; i < gpu_count; i++) {
            sfr_region_t *region = &sfr_data->regions[i];
            region->x = 0;
            region->y = i * region_height;
            region->width = sfr_data->original_width;
            region->height = (i == gpu_count - 1) ? 
                (sfr_data->original_height - i * region_height) : region_height;
            region->gpu_index = workload->assigned_gpus[i];
            
            MVGAL_LOG_DEBUG("SFR: Region %u (GPU %u): x=%u,y=%u,w=%u,h=%u",
                           i, region->gpu_index, region->x, region->y,
                           region->width, region->height);
        }
        
        workload->strategy_data = sfr_data;
        workload->descriptor.assigned_gpu = workload->assigned_gpus[0]; // Primary GPU
        
        MVGAL_LOG_DEBUG("SFR: Workload %u split into %u regions",
                       workload->descriptor.id, gpu_count);
        return MVGAL_SUCCESS;
    }
    
    // If no pre-assigned GPUs, use all available GPUs
    if (state->gpu_count == 0) {
        return mvgal_scheduler_find_best_gpu(workload, &workload->descriptor.assigned_gpu);
    }
    
    // Use all available GPUs
    uint32_t gpu_count = 0;
    uint32_t gpu_indices[MVGAL_SCHEDULER_MAX_GPUS];
    
    for (uint32_t i = 0; i < MVGAL_SCHEDULER_MAX_GPUS; i++) {
        if (i < state->gpu_count && state->gpus[i].available && state->gpus[i].enabled) {
            gpu_indices[gpu_count++] = i;
        }
    }
    
    if (gpu_count == 0) {
        return mvgal_scheduler_find_best_gpu(workload, &workload->descriptor.assigned_gpu);
    }
    
    // Store GPU assignments
    pthread_mutex_lock(&workload->mutex);
    workload->assigned_gpu_count = gpu_count;
    for (uint32_t i = 0; i < gpu_count; i++) {
        workload->assigned_gpus[i] = gpu_indices[i];
    }
    pthread_mutex_unlock(&workload->mutex);
    
    workload->descriptor.assigned_gpu = gpu_indices[0];
    
    MVGAL_LOG_DEBUG("SFR: Workload %u assigned to %u GPUs (primary: %u)",
                   workload->descriptor.id, gpu_count, gpu_indices[0]);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Split frame into regions
 *
 * @param width Frame width
 * @param height Frame height
 * @param gpu_count Number of GPUs
 * @param mode Split mode
 * @param regions Output regions
 * @param region_count Max regions
 * @return Number of regions created
 */
uint32_t sfr_split_frame(
    uint32_t width,
    uint32_t height,
    uint32_t gpu_count,
    sfr_mode_t mode,
    sfr_region_t *regions,
    uint32_t max_regions
) {
    if (gpu_count == 0 || regions == NULL || max_regions == 0) {
        return 0;
    }
    
    uint32_t region_count = 0;
    
    switch (mode) {
        case SFR_MODE_HORIZONTAL: {
            uint32_t region_height = height / gpu_count;
            for (uint32_t i = 0; i < gpu_count && region_count < max_regions; i++) {
                regions[region_count].x = 0;
                regions[region_count].y = i * region_height;
                regions[region_count].width = width;
                regions[region_count].height = (i == gpu_count - 1) ? 
                    (height - i * region_height) : region_height;
                regions[region_count].gpu_index = i;
                region_count++;
            }
            break;
        }
        
        case SFR_MODE_VERTICAL: {
            uint32_t region_width = width / gpu_count;
            for (uint32_t i = 0; i < gpu_count && region_count < max_regions; i++) {
                regions[region_count].x = i * region_width;
                regions[region_count].y = 0;
                regions[region_count].width = (i == gpu_count - 1) ? 
                    (width - i * region_width) : region_width;
                regions[region_count].height = height;
                regions[region_count].gpu_index = i;
                region_count++;
            }
            break;
        }
        
        case SFR_MODE_GRID: {
            uint32_t rows = (uint32_t)ceil(sqrt((float)gpu_count));
            uint32_t cols = (gpu_count + rows - 1) / rows;
            uint32_t region_width = width / cols;
            uint32_t region_height = height / rows;
            
            for (uint32_t i = 0; i < gpu_count && region_count < max_regions; i++) {
                uint32_t row = i / cols;
                uint32_t col = i % cols;
                regions[region_count].x = col * region_width;
                regions[region_count].y = row * region_height;
                regions[region_count].width = (col == cols - 1) ? 
                    (width - col * region_width) : region_width;
                regions[region_count].height = (row == rows - 1) ? 
                    (height - row * region_height) : region_height;
                regions[region_count].gpu_index = i;
                region_count++;
            }
            break;
        }
        
        case SFR_MODE_CHECKERBOARD: {
            // For checkerboard, we'd need more regions than GPUs
            // Each GPU gets a checkerboard pattern
            // This is a simplified version
            uint32_t region_width = width / 2;
            uint32_t region_height = height / 2;
            uint32_t gpu = 0;
            
            for (uint32_t y = 0; y < height && region_count < max_regions; y += region_height) {
                for (uint32_t x = 0; x < width && region_count < max_regions; x += region_width) {
                    regions[region_count].x = x;
                    regions[region_count].y = y;
                    regions[region_count].width = (x + region_width > width) ? 
                        (width - x) : region_width;
                    regions[region_count].height = (y + region_height > height) ? 
                        (height - y) : region_height;
                    regions[region_count].gpu_index = gpu;
                    gpu = (gpu + 1) % gpu_count;
                    region_count++;
                }
                // Offset for checkerboard pattern
                gpu = (gpu + 1) % gpu_count;
            }
            break;
        }
    }
    
    return region_count;
}

/**
 * @brief Create SFR split data for a workload
 *
 * @param workload The workload to split
 * @param width Frame width
 * @param height Frame height
 * @param mode Split mode
 * @return MVGAL_SUCCESS on success
 */
mvgal_error_t sfr_create_split_data(
    struct mvgal_workload *workload,
    uint32_t width,
    uint32_t height,
    sfr_mode_t mode
) {
    if (workload == NULL || workload->assigned_gpu_count == 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Free existing data
    if (workload->strategy_data != NULL) {
        free(workload->strategy_data);
    }
    
    // Create new SFR data
    sfr_workload_data_t *sfr_data = calloc(1, sizeof(sfr_workload_data_t));
    if (sfr_data == NULL) {
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }
    
    sfr_data->original_width = width;
    sfr_data->original_height = height;
    
    // Split the frame
    sfr_data->region_count = sfr_split_frame(
        width, height, workload->assigned_gpu_count, mode,
        sfr_data->regions, MVGAL_SCHEDULER_MAX_GPUS * 4);
    
    // Assign GPUs to regions
    for (uint32_t i = 0; i < sfr_data->region_count; i++) {
        uint32_t gpu_index = workload->assigned_gpus[i % workload->assigned_gpu_count];
        sfr_data->regions[i].gpu_index = gpu_index;
    }
    
    workload->strategy_data = sfr_data;
    
    MVGAL_LOG_DEBUG("SFR: Created %u regions for workload %u",
                   sfr_data->region_count, workload->descriptor.id);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Get region for a specific GPU
 */
const sfr_region_t *sfr_get_region_for_gpu(
    struct mvgal_workload *workload,
    uint32_t gpu_index
) {
    if (workload == NULL || workload->strategy_data == NULL) {
        return NULL;
    }
    
    sfr_workload_data_t *sfr_data = (sfr_workload_data_t *)workload->strategy_data;
    
    for (uint32_t i = 0; i < sfr_data->region_count; i++) {
        if (sfr_data->regions[i].gpu_index == gpu_index) {
            return &sfr_data->regions[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Cleanup SFR data
 */
void sfr_cleanup(struct mvgal_workload *workload) {
    if (workload == NULL) {
        return;
    }
    
    if (workload->strategy_data != NULL) {
        free(workload->strategy_data);
        workload->strategy_data = NULL;
    }
}
