/**
 * @file vk_layer.c
 * @brief Vulkan Layer Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * This file implements the Vulkan layer that intercepts Vulkan API calls
 * and distributes workloads across multiple GPUs.
 *
 * When Vulkan SDK is not available, this provides minimal stub implementations
 * to allow compilation without Vulkan headers.
 */

#include "vk_layer.h"

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

mvgal_vk_layer_state_t g_layer_state = {0};

VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateInstance(
    const void *pCreateInfo,
    const void *pAllocator,
    VkInstance *pInstance
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties
);
VK_LAYER_EXPORT void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const void *pAllocator
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName,
    uint32_t *pPropertyCount,
    char *pPropertyNames
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    char *pPropertyNames
);
VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    void *pProperties
);
VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    void *pFeatures
);
VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties
);
VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2 *pQueueFamilyProperties
);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char *pName
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const void *pCreateInfo,
    const void *pAllocator,
    VkDevice *pDevice
);
VK_LAYER_EXPORT void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const void *pAllocator
);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char *pName
);
VK_LAYER_EXPORT void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue *pQueue
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const void *pSubmits,
    VkFence fence
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue,
    const void *pPresentInfo
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkQueueWaitIdle(
    VkQueue queue
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateFence(
    VkDevice device,
    const void *pCreateInfo,
    const void *pAllocator,
    VkFence *pFence
);
VK_LAYER_EXPORT void VKAPI_CALL vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const void *pAllocator
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence *pFences,
    VkBool32 waitAll,
    uint64_t timeout
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device,
    const void *pCreateInfo,
    const void *pAllocator,
    VkSemaphore *pSemaphore
);
VK_LAYER_EXPORT void VKAPI_CALL vkDestroySemaphore(
    VkDevice device,
    VkSemaphore semaphore,
    const void *pAllocator
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device,
    const void *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers
);
VK_LAYER_EXPORT void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const void *pBeginInfo
);
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer commandBuffer
);
VK_LAYER_EXPORT void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer,
    uint32_t srcStageMask,
    uint32_t dstStageMask,
    uint32_t dependencyFlags,
    uint32_t memoryBarrierCount,
    const void *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const void *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const void *pImageMemoryBarriers
);
VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const void *pRegions
);
VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const void *pRegions
);
VK_LAYER_EXPORT void VKAPI_CALL vkCmdBlitImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const void *pRegions,
    VkFilter filter
);
VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const void *pRegions
);
VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const void *pRegions
);

static void refresh_layer_gpu_inventory(void) {
    g_layer_state.gpu_count = 0;
    memset(g_layer_state.gpus, 0, sizeof(g_layer_state.gpus));

    int32_t gpu_count = mvgal_gpu_enumerate(g_layer_state.gpus, MVGAL_MAX_PHYSICAL_DEVICES);
    if (gpu_count > 0) {
        g_layer_state.gpu_count = (uint32_t)gpu_count;
    }
}

static void rebuild_logical_physical_device(void) {
    mvgal_vk_physical_device_handle_t *physical_handle;
    uint32_t logical_gpu_count = 0;

    if (g_layer_state.mvgal_physical_device != NULL) {
        free((void *)g_layer_state.mvgal_physical_device);
        g_layer_state.mvgal_physical_device = NULL;
    }

    physical_handle = calloc(1, sizeof(*physical_handle));
    if (physical_handle == NULL) {
        return;
    }

    physical_handle->magic = MVGAL_VK_PHYSICAL_DEVICE_MAGIC;
    physical_handle->logical = true;

    for (uint32_t i = 0; i < g_layer_state.gpu_count && logical_gpu_count < MVGAL_EXECUTION_MAX_GPUS; i++) {
        if (!g_layer_state.gpus[i].enabled || !g_layer_state.gpus[i].available) {
            continue;
        }
        physical_handle->gpu_indices[logical_gpu_count++] = i;
    }

    if (logical_gpu_count == 0 && g_layer_state.gpu_count > 0) {
        physical_handle->gpu_indices[0] = 0;
        logical_gpu_count = 1;
    }

    physical_handle->gpu_count = logical_gpu_count;
    g_layer_state.mvgal_physical_device = (VkPhysicalDevice)physical_handle;
}

static PFN_vkVoidFunction mvgal_vk_lookup_instance_proc(const char *pName) {
    if (pName == NULL) {
        return NULL;
    }

    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)vkCreateInstance;
    if (strcmp(pName, "vkDestroyInstance") == 0) return (PFN_vkVoidFunction)vkDestroyInstance;
    if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2") == 0) return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties2;
    if (strcmp(pName, "vkCreateDevice") == 0) return (PFN_vkVoidFunction)vkCreateDevice;
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    return NULL;
}

static PFN_vkVoidFunction mvgal_vk_lookup_device_proc(const char *pName) {
    if (pName == NULL) {
        return NULL;
    }

    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (strcmp(pName, "vkDestroyDevice") == 0) return (PFN_vkVoidFunction)vkDestroyDevice;
    if (strcmp(pName, "vkGetDeviceQueue") == 0) return (PFN_vkVoidFunction)vkGetDeviceQueue;
    if (strcmp(pName, "vkQueueSubmit") == 0) return (PFN_vkVoidFunction)vkQueueSubmit;
    if (strcmp(pName, "vkQueuePresentKHR") == 0) return (PFN_vkVoidFunction)vkQueuePresentKHR;
    if (strcmp(pName, "vkQueueWaitIdle") == 0) return (PFN_vkVoidFunction)vkQueueWaitIdle;
    if (strcmp(pName, "vkCreateFence") == 0) return (PFN_vkVoidFunction)vkCreateFence;
    if (strcmp(pName, "vkDestroyFence") == 0) return (PFN_vkVoidFunction)vkDestroyFence;
    if (strcmp(pName, "vkWaitForFences") == 0) return (PFN_vkVoidFunction)vkWaitForFences;
    if (strcmp(pName, "vkCreateSemaphore") == 0) return (PFN_vkVoidFunction)vkCreateSemaphore;
    if (strcmp(pName, "vkDestroySemaphore") == 0) return (PFN_vkVoidFunction)vkDestroySemaphore;
    if (strcmp(pName, "vkAllocateCommandBuffers") == 0) return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    if (strcmp(pName, "vkFreeCommandBuffers") == 0) return (PFN_vkVoidFunction)vkFreeCommandBuffers;
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if (strcmp(pName, "vkEndCommandBuffer") == 0) return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if (strcmp(pName, "vkCmdPipelineBarrier") == 0) return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    if (strcmp(pName, "vkCmdCopyBuffer") == 0) return (PFN_vkVoidFunction)vkCmdCopyBuffer;
    if (strcmp(pName, "vkCmdCopyImage") == 0) return (PFN_vkVoidFunction)vkCmdCopyImage;
    if (strcmp(pName, "vkCmdBlitImage") == 0) return (PFN_vkVoidFunction)vkCmdBlitImage;
    if (strcmp(pName, "vkCmdCopyBufferToImage") == 0) return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    if (strcmp(pName, "vkCmdCopyImageToBuffer") == 0) return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    return NULL;
}

/**
 * @addtogroup VulkanLayer
 * @{
 */

// =============================================================================
// Layer Discovery Functions
// These functions are called by the Vulkan loader to discover available layers
// =============================================================================

/**
 * @brief Exported function for layer discovery
 *
 * This function is called by the Vulkan loader to get layer properties.
 * It should not be called by applications directly.
 */
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties
) {
    // Static layer properties
    static const VkLayerProperties layerProps = {
        .layerName = MVGAL_VK_LAYER_NAME,
        .specVersion = MVGAL_VK_API_VERSION,
        .implementationVersion = MVGAL_VK_LAYER_VERSION,
        .description = MVGAL_VK_LAYER_DESCRIPTION
    };

    if (pProperties == NULL) {
        *pPropertyCount = 1; // We have one layer
        return VK_SUCCESS;
    }

    if (*pPropertyCount == 0) {
        return VK_INCOMPLETE;
    }

    // Return our layer properties
    *pPropertyCount = 1;
    pProperties[0].layerName[0] = '\0';
    strncpy((char*)pProperties[0].layerName, layerProps.layerName, sizeof(layerProps.layerName) - 1);
    pProperties[0].specVersion = layerProps.specVersion;
    pProperties[0].implementationVersion = layerProps.implementationVersion;
    strncpy((char*)pProperties[0].description, layerProps.description, sizeof(layerProps.description) - 1);

    return VK_SUCCESS;
}

/**
 * @brief Exported function for device layer properties
 *
 * Currently we only provide instance layers, not device layers.
 */
VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties
) {
    // We don't provide device-specific layers
    if (pPropertyCount) {
        *pPropertyCount = 0;
    }
    return VK_SUCCESS;
}

// =============================================================================
// ProcAddr Functions
// These functions are called to get function pointers for Vulkan API calls
// =============================================================================

/**
 * @brief Get instance procedure address
 *
 * This function is called by vkGetInstanceProcAddr to get function pointers.
 */
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vk_layerGetInstanceProcAddr(
    VkInstance instance,
    const char *pName
) {
    (void)instance;
    return mvgal_vk_lookup_instance_proc(pName);
}

/**
 * @brief Get device procedure address
 *
 * This function is called by vkGetDeviceProcAddr to get function pointers.
 */
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vk_layerGetDeviceProcAddr(
    VkDevice device,
    const char *pName
) {
    (void)device;
    return mvgal_vk_lookup_device_proc(pName);
}

/**
 * @brief Initialize layer state
 */
void mvgal_vk_layer_init(void) {
    if (g_layer_state.initialized) return;
    
    pthread_mutex_init(&g_layer_state.mutex, NULL);
    g_layer_state.enabled = true;
    g_layer_state.initialized = true;
    g_layer_state.strategy = mvgal_vk_get_strategy();
    
    // Initialize MVGAL context
    mvgal_init(1);
    mvgal_context_create(&g_layer_state.mvgal_context);
    mvgal_context_set_current(g_layer_state.mvgal_context);
    mvgal_set_strategy(g_layer_state.mvgal_context, g_layer_state.strategy);
    refresh_layer_gpu_inventory();
    rebuild_logical_physical_device();
    
    MVGAL_LOG_INFO("MVGAL Vulkan Layer initialized");
}

/**
 * @brief Shutdown layer state
 */
void mvgal_vk_layer_shutdown(void) {
    if (!g_layer_state.initialized) return;

    if (g_layer_state.mvgal_physical_device != NULL) {
        free((void *)g_layer_state.mvgal_physical_device);
        g_layer_state.mvgal_physical_device = NULL;
    }
    
    // Cleanup MVGAL context
    if (g_layer_state.mvgal_context) {
        mvgal_context_destroy(g_layer_state.mvgal_context);
    }
    mvgal_shutdown();
    
    pthread_mutex_destroy(&g_layer_state.mutex);
    g_layer_state.initialized = false;
    g_layer_state.enabled = false;
    
    MVGAL_LOG_INFO("MVGAL Vulkan Layer shutdown");
}

/** @} */ // end of VulkanLayer
