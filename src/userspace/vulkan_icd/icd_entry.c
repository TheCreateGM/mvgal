/**
 * @file icd_entry.c
 * @brief Vulkan ICD entry points for MVGAL
 *
 * This is the Installable Client Driver (ICD) that provides a virtual
 * VkPhysicalDevice aggregating all GPUs (AMD, NVIDIA, Intel, MTT).
 *
 * The Vulkan loader calls vk_icdGetInstanceProcAddr to get function pointers.
 */

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "mvgal/mvgal.h"

/* ============================================================================
 * ICD Entry Points (required by Vulkan loader)
 * ============================================================================ */

/* Forward declarations */
VkResult mvgal_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                VkInstance* pInstance);
VkResult mvgal_vkEnumeratePhysicalDevices(VkInstance instance,
                                            uint32_t* pPhysicalDeviceCount,
                                            VkPhysicalDevice* pPhysicalDevices);
void mvgal_vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                          VkPhysicalDeviceProperties* pProperties);
void mvgal_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                                VkPhysicalDeviceMemoryProperties* pMemoryProperties);

/* ============================================================================
 * vk_icdGetInstanceProcAddr - Main ICD entry point
 * ============================================================================ */

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance instance,
    const char* pName) {
    
    if (!pName) return NULL;
    
    /* Instance functions */
    if (strcmp(pName, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkCreateInstance;
    }
    if (strcmp(pName, "vkDestroyInstance") == 0) {
        return (PFN_vkVoidFunction)vkDestroyInstance;
    }
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkEnumeratePhysicalDevices;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceMemoryProperties;
    }
    
    /* Physical device functions */
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) {
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    }
    
    /* TODO: Add more function pointers as implemented */
    
    return NULL;
}

/* ============================================================================
 * vk_icdEnumerateAdapterPhysicalDevices (Windows ICD interface)
 * ============================================================================ */

VkResult VKAPI_CALL vk_icdEnumerateAdapterPhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {
    
    return mvgal_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

/* ============================================================================
 * MVGAL Virtual Physical Device Implementation
 * ============================================================================ */

typedef struct {
    VkPhysicalDeviceType deviceType;
    uint32_t vendorID;
    uint32_t deviceID;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint32_t apiVersion;
    uint32_t driverVersion;
    
    /* Aggregated memory properties */
    VkPhysicalDeviceMemoryProperties memoryProperties;
    
    /* MVGAL initialized flag (uses global state, not instance handle) */
    bool mvgal_initialized;
} mvgal_physical_device_t;

/* Global virtual physical device (singleton) */
static mvgal_physical_device_t g_virtual_physical_device = {0};

/* ============================================================================
 * mvgal_vkCreateInstance - Create virtual Vulkan instance
 * ============================================================================ */

VkResult mvgal_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkInstance* pInstance) {
    
    if (!pInstance) return VK_ERROR_INITIALIZATION_FAILED;
    
    /* Initialize MVGAL core (uses global state, returns error code) */
    mvgal_error_t err = mvgal_init(0); /* flags = 0 */
    if (err != MVGAL_SUCCESS) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    /* Setup virtual physical device */
    g_virtual_physical_device.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU; /* Virtual type */
    g_virtual_physical_device.vendorID = 0x1A4B; /* "MV" in ASCII-ish, custom vendor ID */
    g_virtual_physical_device.deviceID = 0x0001;
    snprintf(g_virtual_physical_device.deviceName, 
             VK_MAX_PHYSICAL_DEVICE_NAME_SIZE,
             "MVGAL Virtual Multi-GPU Device");
    g_virtual_physical_device.apiVersion = VK_API_VERSION_1_3;
    g_virtual_physical_device.driverVersion = VK_MAKE_VERSION(0, 2, 1);
    g_virtual_physical_device.mvgal_initialized = true;
    
    /* TODO: Aggregate memory properties from all GPUs */
    /* TODO: Merge device features from all GPUs */
    
    *pInstance = (VkInstance)&g_virtual_physical_device;
    
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkEnumeratePhysicalDevices - Return virtual device
 * ============================================================================ */

VkResult mvgal_vkEnumeratePhysicalDevices(VkInstance instance,
                                            uint32_t* pPhysicalDeviceCount,
                                            VkPhysicalDevice* pPhysicalDevices) {
    
    if (!pPhysicalDeviceCount) return VK_ERROR_INITIALIZATION_FAILED;
    
    /* We expose exactly 1 virtual physical device */
    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }
    
    if (*pPhysicalDeviceCount < 1) {
        return VK_INCOMPLETE;
    }
    
    pPhysicalDevices[0] = (VkPhysicalDevice)&g_virtual_physical_device;
    *pPhysicalDeviceCount = 1;
    
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkGetPhysicalDeviceProperties - Merged properties
 * ============================================================================ */

void mvgal_vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                          VkPhysicalDeviceProperties* pProperties) {
    
    if (!pProperties) return;
    
    mvgal_physical_device_t* dev = (mvgal_physical_device_t*)physicalDevice;
    
    pProperties->deviceType = dev->deviceType;
    pProperties->vendorID = dev->vendorID;
    pProperties->deviceID = dev->deviceID;
    strncpy(pProperties->deviceName, dev->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    pProperties->pipelineCacheUUID[0] = 0x4D; /* 'M' */
    pProperties->pipelineCacheUUID[1] = 0x56; /* 'V' */
    pProperties->pipelineCacheUUID[2] = 0x47; /* 'G' */
    pProperties->pipelineCacheUUID[3] = 0x41; /* 'A' */
    pProperties->pipelineCacheUUID[4] = 0x4C; /* 'L' */
    /* TODO: Fill rest of UUID from mvgal instance */
    
    pProperties->limits = (VkPhysicalDeviceLimits){0}; /* TODO: Merge limits */
    pProperties->sparseProperties = (VkPhysicalDeviceSparseProperties){0};
    
    /* TODO: Aggregate features from all GPUs */
}

/* ============================================================================
 * mvgal_vkGetPhysicalDeviceMemoryProperties - Aggregate memory
 * ============================================================================ */

void mvgal_vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    
    if (!pMemoryProperties) return;
    
    mvgal_physical_device_t* dev = (mvgal_physical_device_t*)physicalDevice;
    
    /* Return aggregated memory properties */
    *pMemoryProperties = dev->memoryProperties;
    
    /* TODO: Actually aggregate from all GPUs:
     * - Sum up all VRAM heaps
     * - Merge memory types (HOST_VISIBLE, DEVICE_LOCAL, etc.)
     * - Create unified memory heap
     */
}

/* ============================================================================
 * Stub implementations for required Vulkan functions
 * ============================================================================ */

void VKAPI_CALL vkDestroyInstance(VkInstance instance,
                                    const VkAllocationCallbacks* pAllocator) {
    (void)instance;
    (void)pAllocator;
    /* TODO: Cleanup MVGAL - call mvgal_shutdown() */
    g_virtual_physical_device.mvgal_initialized = false;
}

void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                           VkPhysicalDeviceFeatures* pFeatures) {
    if (!pFeatures) return;
    *pFeatures = (VkPhysicalDeviceFeatures){0};
    /* TODO: Merge features from all GPUs */
}

VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    
    /* TODO: Return merged extension properties */
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

/* ============================================================================
 * ICD JSON manifest (to be installed to /etc/vulkan/icd.d/mvgal_icd.json)
 * ============================================================================ */
/*
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "libmvgal_vulkan_icd.so",
        "api_version": "1.3.0",
        "function_name": "vk_icdGetInstanceProcAddr"
    }
}
*/
