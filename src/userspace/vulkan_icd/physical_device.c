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
    dev->vendorID = 0x1A4B; /* Custom vendor ID for MVGAL */
    dev->deviceID = 0x0001;
    dev->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU; /* Virtual */
    snprintf(dev->deviceName, sizeof(dev->deviceName), 
             "MVGAL Virtual Multi-GPU Device");
    
    /* Initialize properties */
    dev->properties.apiVersion = VK_API_VERSION_1_3;
    dev->properties.driverVersion = VK_MAKE_VERSION(0, 2, 1);
    dev->properties.vendorID = dev->vendorID;
    dev->properties.deviceID = dev->deviceID;
    dev->properties.deviceType = dev->deviceType;
    strncpy(dev->properties.deviceName, dev->deviceName, 
            VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    
    /* TODO: Aggregate from all real GPUs:
     * - Sum VRAM for memory heaps
     * - Merge memory types (HOST_VISIBLE | DEVICE_LOCAL)
     * - Combine queue families
     * - Merge features
     */
    
    return dev;
}

/* ============================================================================
 * Aggregate Memory Properties from All GPUs
 * ============================================================================ */

void mvgal_aggregate_memory_properties(VkPhysicalDeviceMemoryProperties* out) {
    
    /* TODO: For each real GPU:
     * 1. Get VkPhysicalDeviceMemoryProperties from real driver
     * 2. Merge memory types:
     *    - HOST_VISIBLE types from all GPUs
     *    - DEVICE_LOCAL types from all GPUs
     *    - Create unified heaps
     * 3. Sum up heap sizes (total VRAM)
     */
    
    /* Stub: Create basic memory types */
    g_memory_aggregator.memoryTypeCount = 2;
    
    /* Type 0: DEVICE_LOCAL | HOST_VISIBLE (for unified memory) */
    g_memory_aggregator.memoryTypes[0].propertyFlags = 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    g_memory_aggregator.memoryTypes[0].heapIndex = 0;
    
    /* Type 1: DEVICE_LOCAL only */
    g_memory_aggregator.memoryTypes[1].propertyFlags = 
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    g_memory_aggregator.memoryTypes[1].heapIndex = 0;
    
    /* Heap 0: Combined VRAM from all GPUs */
    g_memory_aggregator.memoryHeapCount = 1;
    g_memory_aggregator.memoryHeaps[0].size = 16ULL * 1024 * 1024 * 1024; /* 16GB stub */
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
 * Get Queue Family Properties (Aggregated)
 * ============================================================================ */

void mvgal_get_queue_family_properties(uint32_t* pCount,
                                        VkQueueFamilyProperties* pProperties) {
    
    /* TODO: Aggregate queue families from all GPUs:
     * - Graphics queues: Best GPU for graphics
     * - Compute queues: Best GPU for compute
     * - Transfer queues: GPU with fastest PCIe
     */
    
    if (!pCount) return;
    
    uint32_t count = 3; /* Graphics, Compute, Transfer */
    
    if (!pProperties) {
        *pCount = count;
        return;
    }
    
    if (*pCount < count) {
        *pCount = count;
        return;
    }
    
    /* Graphics queue family */
    pProperties[0] = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
        .queueCount = 4, /* TODO: Sum from all GPUs */
        .timestampValidBits = 64,
        .minImageTransferGranularity = {1, 1, 1}
    };
    
    /* Compute queue family */
    pProperties[1] = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_COMPUTE_BIT,
        .queueCount = 8, /* TODO: Sum from all GPUs */
        .timestampValidBits = 64,
        .minImageTransferGranularity = {1, 1, 1}
    };
    
    /* Transfer queue family */
    pProperties[2] = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_TRANSFER_BIT,
        .queueCount = 4, /* TODO: Sum from all GPUs */
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
    /* TODO: Query mvgal_core for number of detected GPUs */
    return 2; /* Stub: 2 GPUs detected */
}
