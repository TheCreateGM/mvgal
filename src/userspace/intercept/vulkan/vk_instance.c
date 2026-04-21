/**
 * @file vk_instance.c
 * @brief Vulkan Instance Functions Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * This file implements the intercepted Vulkan instance functions.
 * When Vulkan SDK is not available, this provides minimal stub implementations
 * to allow compilation without Vulkan headers.
 */

#include "vk_layer.h"

/**
 * @addtogroup VulkanLayer
 * @{
 */

// =============================================================================
// vkCreateInstance
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateInstance(
    const void *pCreateInfo,
    const void *pAllocator,
    VkInstance *pInstance
) {
    (void)pCreateInfo;
    (void)pAllocator;

    if (pInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_layer_init();
    if (!mvgal_vk_layer_is_enabled()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_instance_handle_t *instance_handle = calloc(1, sizeof(*instance_handle));
    if (instance_handle == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    instance_handle->magic = MVGAL_VK_INSTANCE_MAGIC;
    instance_handle->id = g_layer_state.frames_submitted + 1U;
    g_layer_state.instance = (VkInstance)instance_handle;
    *pInstance = g_layer_state.instance;

    return VK_SUCCESS;
}

// =============================================================================
// vkDestroyInstance
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const void *pAllocator
) {
    (void)instance;
    (void)pAllocator;

    if (g_layer_state.instance != NULL) {
        free((void *)g_layer_state.instance);
        g_layer_state.instance = NULL;
    }

    mvgal_vk_layer_shutdown();
}

// =============================================================================
// vkEnumeratePhysicalDevices
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices
) {
    (void)instance;

    if (pPhysicalDeviceCount == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_layer_state.initialized) {
        mvgal_vk_layer_init();
    }

    if (g_layer_state.mvgal_physical_device == NULL) {
        *pPhysicalDeviceCount = 0;
        return VK_SUCCESS;
    }

    if (pPhysicalDevices == NULL) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }

    if (*pPhysicalDeviceCount == 0) {
        *pPhysicalDeviceCount = 1;
        return VK_INCOMPLETE;
    }

    pPhysicalDevices[0] = g_layer_state.mvgal_physical_device;
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

// =============================================================================
// vkEnumerateInstanceExtensionProperties
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName,
    uint32_t *pPropertyCount,
    char *pPropertyNames
) {
    (void)pLayerName;
    if (pPropertyCount) {
        *pPropertyCount = 0;
    }
    return VK_SUCCESS;
}

// =============================================================================
// vkEnumerateDeviceExtensionProperties
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    char *pPropertyNames
) {
    (void)physicalDevice;
    (void)pLayerName;
    if (pPropertyCount) {
        *pPropertyCount = 0;
    }
    return VK_SUCCESS;
}

// =============================================================================
// vkGetPhysicalDeviceProperties
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    void *pProperties
) {
    mvgal_vk_physical_device_handle_t *physical_handle =
        (mvgal_vk_physical_device_handle_t *)physicalDevice;

    if (pProperties) {
        memset(pProperties, 0, 128); // Approximate size of VkPhysicalDeviceProperties
        if (physical_handle != NULL && physical_handle->magic == MVGAL_VK_PHYSICAL_DEVICE_MAGIC) {
            ((unsigned char *)pProperties)[0] = (unsigned char)physical_handle->gpu_count;
        }
    }
}

// =============================================================================
// vkGetPhysicalDeviceFeatures
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    void *pFeatures
) {
    (void)physicalDevice;
    if (pFeatures) {
        memset(pFeatures, 0, 64); // Approximate size of VkPhysicalDeviceFeatures
    }
}

// =============================================================================
// vkGetPhysicalDeviceFormatProperties
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    int format,
    void *pFormatProperties
) {
    // Stub implementation
    (void)physicalDevice;
    (void)format;
    if (pFormatProperties) {
        memset(pFormatProperties, 0, 16); // Approximate size
    }
}

// =============================================================================
// vkGetPhysicalDeviceImageFormatProperties
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    int format,
    int type,
    int tiling,
    int usage,
    int flags,
    void *pImageFormatProperties
) {
    (void)physicalDevice;
    (void)format;
    (void)type;
    (void)tiling;
    (void)usage;
    (void)flags;
    if (pImageFormatProperties) {
        memset(pImageFormatProperties, 0, 32); // Approximate size
    }
    return VK_SUCCESS;
}

// =============================================================================
// vkGetPhysicalDeviceMemoryProperties
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    void *pMemoryProperties
) {
    // Stub implementation
    (void)physicalDevice;
    if (pMemoryProperties) {
        memset(pMemoryProperties, 0, 128); // Approximate size
    }
}

// =============================================================================
// vkGetInstanceProcAddr
// =============================================================================

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char *pName
) {
    // Delegate to layer's proc addr function
    (void)instance;
    (void)pName;
    if (g_layer_state.vkGetInstanceProcAddr) {
        return g_layer_state.vkGetInstanceProcAddr(instance, pName);
    }
    return vk_layerGetInstanceProcAddr(instance, pName);
}

/** @} */ // end of VulkanLayer
