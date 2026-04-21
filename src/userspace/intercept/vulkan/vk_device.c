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
    mvgal_vk_physical_device_handle_t *physical_handle =
        (mvgal_vk_physical_device_handle_t *)physicalDevice;
    mvgal_vk_device_handle_t *device_handle;

    (void)pCreateInfo;
    (void)pAllocator;

    if (pDevice) {
        *pDevice = NULL;
    } else {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_layer_state.initialized) {
        mvgal_vk_layer_init();
    }

    if (physical_handle == NULL || physical_handle->magic != MVGAL_VK_PHYSICAL_DEVICE_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    device_handle = calloc(1, sizeof(*device_handle));
    if (device_handle == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    device_handle->magic = MVGAL_VK_DEVICE_MAGIC;
    device_handle->mvgal_context = g_layer_state.mvgal_context;
    device_handle->strategy = g_layer_state.strategy;

    if (physical_handle->gpu_count > 0) {
        if (mvgal_device_create(
                physical_handle->gpu_count,
                physical_handle->gpu_indices,
                &device_handle->logical_device
            ) == MVGAL_SUCCESS) {
            (void)mvgal_device_get_descriptor(device_handle->logical_device, &device_handle->descriptor);
        }
    }

    *pDevice = (VkDevice)device_handle;
    g_layer_state.device = *pDevice;

    if (device_handle->mvgal_context != NULL) {
        mvgal_set_strategy(device_handle->mvgal_context, device_handle->strategy);
    }

    return VK_SUCCESS;
}

// =============================================================================
// vkDestroyDevice
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const void *pAllocator
) {
    mvgal_vk_device_handle_t *device_handle = (mvgal_vk_device_handle_t *)device;
    (void)pAllocator;

    if (device_handle == NULL || device_handle->magic != MVGAL_VK_DEVICE_MAGIC) {
        return;
    }

    for (uint32_t i = 0; i < device_handle->queue_count; i++) {
        free(device_handle->queues[i]);
        device_handle->queues[i] = NULL;
    }

    if (device_handle->logical_device != NULL) {
        mvgal_device_destroy(device_handle->logical_device);
    }

    if (g_layer_state.device == device) {
        g_layer_state.device = NULL;
    }

    free(device_handle);
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
    mvgal_vk_device_handle_t *device_handle = (mvgal_vk_device_handle_t *)device;
    mvgal_vk_queue_handle_t *queue_handle;

    if (pQueue == NULL) {
        return;
    }

    *pQueue = NULL;

    if (device_handle == NULL || device_handle->magic != MVGAL_VK_DEVICE_MAGIC) {
        return;
    }

    if (queueIndex < MVGAL_MAX_QUEUES_PER_DEVICE &&
        device_handle->queues[queueIndex] != NULL) {
        *pQueue = (VkQueue)device_handle->queues[queueIndex];
        return;
    }

    queue_handle = calloc(1, sizeof(*queue_handle));
    if (queue_handle == NULL) {
        return;
    }

    queue_handle->magic = MVGAL_VK_QUEUE_MAGIC;
    queue_handle->device = device_handle;
    queue_handle->queue_family_index = queueFamilyIndex;
    queue_handle->queue_index = queueIndex;
    queue_handle->flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

    if (queueIndex < MVGAL_MAX_QUEUES_PER_DEVICE) {
        device_handle->queues[queueIndex] = queue_handle;
        if (queueIndex >= device_handle->queue_count) {
            device_handle->queue_count = queueIndex + 1U;
        }
    }

    *pQueue = (VkQueue)queue_handle;
}

// =============================================================================
// vkGetPhysicalDeviceQueueFamilyProperties
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties
) {
    (void)physicalDevice;
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
    (void)physicalDevice;
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
