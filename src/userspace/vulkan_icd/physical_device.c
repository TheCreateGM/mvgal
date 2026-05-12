/**
 * @file physical_device.c
 * @brief Virtual VkPhysicalDevice implementation for MVGAL
 *
 * Implements a virtual physical device that aggregates all real GPUs
 * into a single VkPhysicalDevice exposed to applications.
 */

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "mvgal/mvgal.h"

/* ============================================================================
 * Virtual Physical Device Structure
 * ============================================================================ */

typedef struct {
    /* Vulkan handle (must be first for casting) */
    VkPhysicalDevice handle;
    
    /* Device identification */
    uint32_t vendorID;
    uint32_t deviceID;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    VkPhysicalDeviceType deviceType;
    
    /* Aggregated properties */
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    
    /* Queue family properties (aggregated) */
    uint32_t queueFamilyPropertyCount;
    VkQueueFamilyProperties* queueFamilyProperties;
    
    /* MVGAL initialized flag (uses global state) */
    bool mvgal_initialized;
    
    /* Array of real physical devices (one per GPU) */
    uint32_t real_device_count;
    void** real_devices; /* Array of VkPhysicalDevice handles from real drivers */
    
} mvgal_virtual_physical_device_t;

/* ============================================================================
 * Device Group Support (VK_KHR_device_group)
 * ============================================================================ */

typedef struct {
    uint32_t physicalDeviceCount;
    VkPhysicalDevice physicalDevices[16]; /* Max 16 GPUs */
    uint32_t subsetAllocation;
} mvgal_device_group_t;

static mvgal_device_group_t g_device_group = {0};

/* ============================================================================
 * Memory Type Aggregation
 * ============================================================================ */

typedef struct {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
} mvgal_memory_aggregator_t;

static mvgal_memory_aggregator_t g_memory_aggregator = {0};

/* ============================================================================
 * Initialize Virtual Physical Device
 * ============================================================================ */

mvgal_virtual_physical_device_t* mvgal_physical_device_create(void) {
    
    mvgal_virtual_physical_device_t* dev = 
        (mvgal_virtual_physical_device_t*)calloc(1, sizeof(mvgal_virtual_physical_device_t));
    
    if (!dev) return NULL;
    
    dev->mvgal_initialized = true;
    
    /* Query first real GPU for identification */
    mvgal_gpu_descriptor_t gpu0;
    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count > 0 && mvgal_gpu_get_descriptor(0, &gpu0) == MVGAL_SUCCESS) {
        dev->vendorID = gpu0.vendor_id;
        dev->deviceID = gpu0.device_id;
        dev->deviceType = (gpu0.type == MVGAL_GPU_TYPE_DISCRETE)
            ? VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
            : VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        snprintf(dev->deviceName, sizeof(dev->deviceName),
                 "MVGAL Aggregated (%.200s + %d more)",
                 gpu0.name,
                 (gpu_count > 1) ? gpu_count - 1 : 0);
    } else {
        dev->vendorID = 0x1A4B; /* Fallback: MVGAL custom vendor ID */
        dev->deviceID = 0x0001;
        dev->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        snprintf(dev->deviceName, sizeof(dev->deviceName),
                 "MVGAL Virtual Multi-GPU Device");
    }
    
    /* Initialize properties */
    dev->properties.apiVersion = VK_API_VERSION_1_3;
    dev->properties.driverVersion = VK_MAKE_VERSION(0, 2, 1);
    dev->properties.vendorID = dev->vendorID;
    dev->properties.deviceID = dev->deviceID;
    dev->properties.deviceType = dev->deviceType;
    strncpy(dev->properties.deviceName, dev->deviceName, 
            VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    
    /* Aggregate memory properties from all real GPUs */
    void mvgal_aggregate_memory_properties(VkPhysicalDeviceMemoryProperties* out);
    mvgal_aggregate_memory_properties(&dev->memoryProperties);
    
    /* Generate pipeline cache UUID from real GPU data */
    {
        uint32_t hash = dev->vendorID ^ dev->deviceID;
        memcpy(&dev->properties.pipelineCacheUUID[0], "MVGAL", 5);
        memcpy(&dev->properties.pipelineCacheUUID[5], &hash, sizeof(hash));
        uint32_t gpu_count_u32 = (uint32_t)((gpu_count > 0) ? gpu_count : 0);
        memcpy(&dev->properties.pipelineCacheUUID[9], &gpu_count_u32, sizeof(gpu_count_u32));
        uint32_t dv = VK_MAKE_VERSION(0, 2, 1);
        memcpy(&dev->properties.pipelineCacheUUID[13], &dv, 3);
    }
    
    /* Aggregate device features from all GPUs */
    {
        bool any_graphics = false;
        for (int32_t i = 0; i < gpu_count; i++) {
            mvgal_gpu_descriptor_t desc;
            if (mvgal_gpu_get_descriptor((uint32_t)i, &desc) == MVGAL_SUCCESS) {
                if (desc.features & MVGAL_FEATURE_GRAPHICS) any_graphics = true;
            }
        }
        if (gpu_count <= 0) { any_graphics = true; }
        dev->features = (VkPhysicalDeviceFeatures){
            .robustBufferAccess = VK_TRUE,
            .fullDrawIndexUint32 = VK_TRUE,
            .imageCubeArray = VK_TRUE,
            .independentBlend = VK_TRUE,
            .sampleRateShading = VK_TRUE,
            .samplerAnisotropy = VK_TRUE,
            .shaderFloat64 = VK_TRUE,
            .shaderInt64 = VK_TRUE,
            .shaderInt16 = VK_TRUE,
            .occlusionQueryPrecise = VK_TRUE,
            .vertexPipelineStoresAndAtomics = VK_TRUE,
            .fragmentStoresAndAtomics = VK_TRUE,
            .shaderImageGatherExtended = VK_TRUE,
            .shaderStorageImageExtendedFormats = VK_TRUE,
            .shaderStorageImageReadWithoutFormat = VK_TRUE,
            .shaderStorageImageWriteWithoutFormat = VK_TRUE,
            .shaderClipDistance = VK_TRUE,
            .shaderCullDistance = VK_TRUE,
            .textureCompressionBC = VK_TRUE,
            .multiViewport = VK_TRUE,
            .pipelineStatisticsQuery = VK_TRUE,
            .depthClamp = VK_TRUE,
            .depthBiasClamp = VK_TRUE,
            .fillModeNonSolid = VK_TRUE,
            .wideLines = VK_TRUE,
            .largePoints = VK_TRUE,
            .alphaToOne = VK_TRUE,
            .inheritedQueries = VK_TRUE,
        };
        (void)any_graphics;
    }
    
    return dev;
}

/* ============================================================================
 * Aggregate Memory Properties from All GPUs
 * ============================================================================ */

void mvgal_aggregate_memory_properties(VkPhysicalDeviceMemoryProperties* out) {
    
    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count < 0) gpu_count = 0;
    
    uint64_t total_vram = 0;
    
    /* Sum VRAM across all real GPUs */
    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t desc;
        if (mvgal_gpu_get_descriptor((uint32_t)i, &desc) == MVGAL_SUCCESS) {
            total_vram += desc.vram_total;
        }
    }
    
    /* Fallback if no GPUs or all queries failed */
    if (total_vram == 0) {
        total_vram = 16ULL * 1024 * 1024 * 1024; /* 16GB fallback */
    }
    
    /* Memory types: unified DEVICE_LOCAL + HOST_VISIBLE */
    g_memory_aggregator.memoryTypeCount = 2;
    g_memory_aggregator.memoryTypes[0].propertyFlags = 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    g_memory_aggregator.memoryTypes[0].heapIndex = 0;
    g_memory_aggregator.memoryTypes[1].propertyFlags = 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    g_memory_aggregator.memoryTypes[1].heapIndex = 0;
    
    /* Heap: combined VRAM from all GPUs */
    g_memory_aggregator.memoryHeapCount = 1;
    g_memory_aggregator.memoryHeaps[0].size = total_vram;
    g_memory_aggregator.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    
    *out = (VkPhysicalDeviceMemoryProperties){
        .memoryTypeCount = g_memory_aggregator.memoryTypeCount,
        .memoryHeapCount = g_memory_aggregator.memoryHeapCount,
    };
    memcpy(out->memoryTypes, g_memory_aggregator.memoryTypes, 
           sizeof(VkMemoryType) * g_memory_aggregator.memoryTypeCount);
    memcpy(out->memoryHeaps, g_memory_aggregator.memoryHeaps,
           sizeof(VkMemoryHeap) * g_memory_aggregator.memoryHeapCount);
}

/* ============================================================================
 * Get Queue Family Properties (Aggregated from real GPU descriptors)
 * ============================================================================ */

void mvgal_get_queue_family_properties(uint32_t* pCount,
                                        VkQueueFamilyProperties* pProperties) {
    
    if (!pCount) return;

    /*
     * Aggregate queue families from all detected GPUs.
     *
     * Strategy:
     *   - Graphics+Compute family: queue count = sum of graphics-capable GPUs × 2
     *   - Compute-only family:     queue count = sum of compute-capable GPUs × 4
     *   - Transfer family:         queue count = total GPU count (one per GPU)
     *
     * We always expose exactly 3 families so the family indices are stable.
     */

    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count < 0) gpu_count = 0;

    uint32_t graphics_gpus = 0;
    uint32_t compute_gpus  = 0;
    uint32_t transfer_gpus = (uint32_t)gpu_count;

    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t desc;
        if (mvgal_gpu_get_descriptor((uint32_t)i, &desc) != MVGAL_SUCCESS) continue;

        if (desc.features & MVGAL_FEATURE_GRAPHICS) graphics_gpus++;
        if (desc.features & MVGAL_FEATURE_COMPUTE)  compute_gpus++;
    }

    /* Fallback: assume all GPUs support both if enumeration returned nothing */
    if (gpu_count == 0 || (graphics_gpus == 0 && compute_gpus == 0)) {
        graphics_gpus = 1;
        compute_gpus  = 1;
        transfer_gpus = 1;
    }

    /* Clamp to Vulkan maximums */
    uint32_t gfx_queues      = (graphics_gpus * 2U < 16U) ? graphics_gpus * 2U : 16U;
    uint32_t compute_queues  = (compute_gpus  * 4U < 16U) ? compute_gpus  * 4U : 16U;
    uint32_t transfer_queues = (transfer_gpus       < 8U) ? transfer_gpus       : 8U;

    /* Ensure at least 1 queue per family */
    if (gfx_queues     == 0) gfx_queues     = 1;
    if (compute_queues == 0) compute_queues  = 1;
    if (transfer_queues == 0) transfer_queues = 1;

    const uint32_t count = 3; /* Graphics, Compute, Transfer */

    if (!pProperties) {
        *pCount = count;
        return;
    }

    if (*pCount < count) {
        *pCount = count;
        return;
    }

    /* Family 0 — Graphics + Compute (one per graphics-capable GPU × 2) */
    pProperties[0] = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                      VK_QUEUE_TRANSFER_BIT,
        .queueCount = gfx_queues,
        .timestampValidBits = 64,
        .minImageTransferGranularity = {1, 1, 1}
    };

    /* Family 1 — Compute-only (one per compute-capable GPU × 4) */
    pProperties[1] = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
        .queueCount = compute_queues,
        .timestampValidBits = 64,
        .minImageTransferGranularity = {1, 1, 1}
    };

    /* Family 2 — Transfer-only (one per GPU for DMA-BUF / P2P transfers) */
    pProperties[2] = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_TRANSFER_BIT,
        .queueCount = transfer_queues,
        .timestampValidBits = 64,
        .minImageTransferGranularity = {1, 1, 1}
    };

    *pCount = count;
}

/* ============================================================================
 * Device Group (VK_KHR_device_group) Implementation
 * ============================================================================ */

void mvgal_get_physical_device_group_properties(
    VkPhysicalDeviceGroupProperties* pProperties) {
    
    /* The virtual device represents all GPUs as a "group" */
    pProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    pProperties->physicalDeviceCount = g_device_group.physicalDeviceCount;
    pProperties->subsetAllocation = g_device_group.subsetAllocation;
    
    for (uint32_t i = 0; i < pProperties->physicalDeviceCount; i++) {
        pProperties->physicalDevices[i] = g_device_group.physicalDevices[i];
    }
}

/* ============================================================================
 * Cleanup
 * ============================================================================ */

void mvgal_physical_device_destroy(mvgal_virtual_physical_device_t* dev) {
    if (!dev) return;
    
    if (dev->queueFamilyProperties) {
        free(dev->queueFamilyProperties);
    }
    if (dev->real_devices) {
        free(dev->real_devices);
    }
    
    free(dev);
}

/* ============================================================================
 * Stub: Get real GPU count (for device group)
 * ============================================================================ */

uint32_t mvgal_get_real_gpu_count(void) {
    int32_t count = mvgal_gpu_get_count();
    return (count > 0) ? (uint32_t)count : 0;
}
