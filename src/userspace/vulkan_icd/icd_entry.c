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
void mvgal_aggregate_memory_properties(VkPhysicalDeviceMemoryProperties* out);

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
    
    /* Aggregated device features (computed once at creation) */
    VkPhysicalDeviceFeatures features;
    
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
    
    /* Aggregate memory properties from all real GPUs */
    mvgal_aggregate_memory_properties(&g_virtual_physical_device.memoryProperties);
    
    /* Aggregate device features from all GPUs */
    {
        int32_t gpu_count = mvgal_gpu_get_count();
        bool any_compute = false;
        bool any_graphics = false;
        for (int32_t i = 0; i < gpu_count; i++) {
            mvgal_gpu_descriptor_t desc;
            if (mvgal_gpu_get_descriptor((uint32_t)i, &desc) == MVGAL_SUCCESS) {
                if (desc.features & MVGAL_FEATURE_COMPUTE)  any_compute = true;
                if (desc.features & MVGAL_FEATURE_GRAPHICS) any_graphics = true;
            }
        }
        /* Fallback: assume modern GPUs when mvgal not fully initialized */
        if (gpu_count <= 0) { any_compute = true; any_graphics = true; }

        g_virtual_physical_device.features = (VkPhysicalDeviceFeatures){
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
            .computeShader = any_compute ? VK_TRUE : VK_FALSE,
        };
        (void)any_graphics; /* Reserved for future conditional features */
    }
    
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
    /* Fill remaining UUID bytes from real GPU vendor/device IDs */
    {
        uint32_t hash = dev->vendorID ^ dev->deviceID;
        memcpy(&pProperties->pipelineCacheUUID[5], &hash, sizeof(hash));
        int32_t count = mvgal_gpu_get_count();
        uint32_t gpu_info = (uint32_t)((count > 0) ? count : 0);
        memcpy(&pProperties->pipelineCacheUUID[9], &gpu_info, sizeof(gpu_info));
        uint32_t dv = VK_MAKE_VERSION(0, 2, 1);
        memcpy(&pProperties->pipelineCacheUUID[13], &dv, 3);
    }
    
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
    mvgal_shutdown();
    g_virtual_physical_device.mvgal_initialized = false;
}

void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice,
                                           VkPhysicalDeviceFeatures* pFeatures) {
    if (!pFeatures) return;
    mvgal_physical_device_t* dev = (mvgal_physical_device_t*)physicalDevice;
    *pFeatures = dev->features;
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
