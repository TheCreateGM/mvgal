/**
 * @file vk_device.c
 * @brief Vulkan Device Functions Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * This file implements the intercepted Vulkan device creation and management functions.
 * When Vulkan SDK is not available, this provides minimal stub implementations
 * to allow compilation without Vulkan headers.
 */

#include "vk_layer.h"

/**
 * @addtogroup VulkanLayer
 * @{
 */

// =============================================================================
// vkCreateDevice
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const void *pCreateInfo,
    const void *pAllocator,
    VkDevice *pDevice
) {
    // Stub implementation - would create device in full implementation
    if (pDevice) {
        *pDevice = NULL; // Can't create device without Vulkan SDK
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

// =============================================================================
// vkDestroyDevice
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const void *pAllocator
) {
    // Stub implementation
    (void)device;
    (void)pAllocator;
}

// =============================================================================
// vkGetDeviceProcAddr
// =============================================================================

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char *pName
) {
    // Delegate to layer's proc addr function
    if (g_layer_state.vkGetDeviceProcAddr) {
        return g_layer_state.vkGetDeviceProcAddr(device, pName);
    }
    return vk_layerGetDeviceProcAddr(device, pName);
}

// =============================================================================
// vkGetDeviceQueue
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue *pQueue
) {
    // Stub implementation - return NULL queue
    if (pQueue) {
        *pQueue = NULL;
    }
}

// =============================================================================
// vkGetPhysicalDeviceQueueFamilyProperties
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties
) {
    // Stub implementation - return a single queue family
    if (pQueueFamilyPropertyCount) {
        if (pQueueFamilyProperties == NULL) {
            *pQueueFamilyPropertyCount = 1;
        } else {
            *pQueueFamilyPropertyCount = 1;
            if (*pQueueFamilyPropertyCount > 0) {
                pQueueFamilyProperties[0].queueFlags = 0xFFFFFFFF; // All flags
                pQueueFamilyProperties[0].queueCount = 1;
                pQueueFamilyProperties[0].timestampValidBits = 0;
                pQueueFamilyProperties[0].minImageTransferGranularity = NULL;
            }
        }
    }
}

// =============================================================================
// vkGetPhysicalDeviceQueueFamilyProperties2
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2 *pQueueFamilyProperties
) {
    // Stub implementation - similar to non-2 version
    if (pQueueFamilyPropertyCount) {
        if (pQueueFamilyProperties == NULL) {
            *pQueueFamilyPropertyCount = 1;
        } else {
            *pQueueFamilyPropertyCount = 1;
            if (*pQueueFamilyPropertyCount > 0) {
                pQueueFamilyProperties[0].queueFamilyProperties.queueFlags = 0xFFFFFFFF;
                pQueueFamilyProperties[0].queueFamilyProperties.queueCount = 1;
                pQueueFamilyProperties[0].queueFamilyProperties.timestampValidBits = 0;
                pQueueFamilyProperties[0].queueFamilyProperties.minImageTransferGranularity = NULL;
            }
        }
    }
}

// =============================================================================
// vkGetPhysicalDeviceFeatures2
// ============================================================================= 

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    void *pFeatures
) {
    // Stub implementation
    if (pFeatures) {
        memset(pFeatures, 0, 256); // Approximate size
    }
}

// =============================================================================
// vkGetPhysicalDeviceProperties2
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    void *pProperties
) {
    // Stub implementation
    if (pProperties) {
        memset(pProperties, 0, 256); // Approximate size
    }
}

// =============================================================================
// vkGetPhysicalDeviceMemoryProperties2
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    void *pMemoryProperties
) {
    // Stub implementation
    if (pMemoryProperties) {
        memset(pMemoryProperties, 0, 256); // Approximate size
    }
}

// =============================================================================
// vkGetPhysicalDeviceSparseProperties
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceSparseProperties(
    VkPhysicalDevice physicalDevice,
    void *pSparseProperties
) {
    // Stub implementation
    if (pSparseProperties) {
        memset(pSparseProperties, 0, 64);
    }
}

/** @} */ // end of VulkanLayer
