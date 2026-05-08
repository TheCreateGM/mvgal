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
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
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
void mvgal_get_queue_family_properties(uint32_t* pCount,
                                        VkQueueFamilyProperties* pProperties);

/* Device function forward declarations */
void VKAPI_CALL mvgal_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice, uint32_t* pCount,
    VkQueueFamilyProperties* pProperties);
void VKAPI_CALL mvgal_vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, uint32_t* pCount,
    VkQueueFamilyProperties2* pProperties);
VkResult VKAPI_CALL mvgal_vkCreateDevice(
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
void VKAPI_CALL mvgal_vkDestroyDevice(
    VkDevice device, const VkAllocationCallbacks* pAllocator);
void VKAPI_CALL mvgal_vkGetDeviceQueue(
    VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex,
    VkQueue* pQueue);
VkResult VKAPI_CALL mvgal_vkGetDeviceQueue2(
    VkDevice device, const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue);
VkResult VKAPI_CALL mvgal_vkDeviceWaitIdle(VkDevice device);
VkResult VKAPI_CALL mvgal_vkQueueWaitIdle(VkQueue queue);

/* Memory function forward declarations */
VkResult VKAPI_CALL mvgal_vkAllocateMemory(
    VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);
void VKAPI_CALL mvgal_vkFreeMemory(
    VkDevice device, VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator);
VkResult VKAPI_CALL mvgal_vkMapMemory(
    VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
    VkDeviceSize size, VkMemoryMapFlags flags, void** ppData);
void VKAPI_CALL mvgal_vkUnmapMemory(
    VkDevice device, VkDeviceMemory memory);
VkResult VKAPI_CALL mvgal_vkGetDeviceMemoryCommitment(
    VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommitted);

/* Command pool forward declarations */
VkResult VKAPI_CALL mvgal_vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool);
void VKAPI_CALL mvgal_vkDestroyCommandPool(
    VkDevice device, VkCommandPool commandPool,
    const VkAllocationCallbacks* pAllocator);
VkResult VKAPI_CALL mvgal_vkResetCommandPool(
    VkDevice device, VkCommandPool commandPool,
    VkCommandPoolResetFlags flags);

/* Command buffer forward declarations */
VkResult VKAPI_CALL mvgal_vkAllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers);
void VKAPI_CALL mvgal_vkFreeCommandBuffers(
    VkDevice device, VkCommandPool commandPool,
    uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);
VkResult VKAPI_CALL mvgal_vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo);
VkResult VKAPI_CALL mvgal_vkEndCommandBuffer(
    VkCommandBuffer commandBuffer);
VkResult VKAPI_CALL mvgal_vkResetCommandBuffer(
    VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);

/* Queue submit forward declarations */
VkResult VKAPI_CALL mvgal_vkQueueSubmit(
    VkQueue queue, uint32_t submitCount,
    const VkSubmitInfo* pSubmits, VkFence fence);

/* Fence forward declarations */
VkResult VKAPI_CALL mvgal_vkCreateFence(
    VkDevice device, const VkFenceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkFence* pFence);
void VKAPI_CALL mvgal_vkDestroyFence(
    VkDevice device, VkFence fence,
    const VkAllocationCallbacks* pAllocator);
VkResult VKAPI_CALL mvgal_vkWaitForFences(
    VkDevice device, uint32_t fenceCount,
    const VkFence* pFences, VkBool32 waitAll, uint64_t timeout);
VkResult VKAPI_CALL mvgal_vkResetFences(
    VkDevice device, uint32_t fenceCount,
    const VkFence* pFences);
VkResult VKAPI_CALL mvgal_vkGetFenceStatus(
    VkDevice device, VkFence fence);

/* Semaphore forward declarations */
VkResult VKAPI_CALL mvgal_vkCreateSemaphore(
    VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
void VKAPI_CALL mvgal_vkDestroySemaphore(
    VkDevice device, VkSemaphore semaphore,
    const VkAllocationCallbacks* pAllocator);

/* Event forward declarations */
VkResult VKAPI_CALL mvgal_vkCreateEvent(
    VkDevice device, const VkEventCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkEvent* pEvent);
void VKAPI_CALL mvgal_vkDestroyEvent(
    VkDevice device, VkEvent event,
    const VkAllocationCallbacks* pAllocator);
VkResult VKAPI_CALL mvgal_vkGetEventStatus(
    VkDevice device, VkEvent event);
VkResult VKAPI_CALL mvgal_vkSetEvent(
    VkDevice device, VkEvent event);
VkResult VKAPI_CALL mvgal_vkResetEvent(
    VkDevice device, VkEvent event);

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
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceQueueFamilyProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceQueueFamilyProperties2;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    }
    
    /* Device functions */
    if (strcmp(pName, "vkCreateDevice") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkCreateDevice;
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkDestroyDevice;
    }
    if (strcmp(pName, "vkGetDeviceQueue") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetDeviceQueue;
    }
    if (strcmp(pName, "vkGetDeviceQueue2") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetDeviceQueue2;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkDeviceWaitIdle;
    }
    if (strcmp(pName, "vkQueueWaitIdle") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkQueueWaitIdle;
    }
    
    /* Memory functions */
    if (strcmp(pName, "vkAllocateMemory") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkAllocateMemory;
    }
    if (strcmp(pName, "vkFreeMemory") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkFreeMemory;
    }
    if (strcmp(pName, "vkMapMemory") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkMapMemory;
    }
    if (strcmp(pName, "vkUnmapMemory") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkUnmapMemory;
    }
    if (strcmp(pName, "vkGetDeviceMemoryCommitment") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetDeviceMemoryCommitment;
    }
    
    /* Command pool functions */
    if (strcmp(pName, "vkCreateCommandPool") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkCreateCommandPool;
    }
    if (strcmp(pName, "vkDestroyCommandPool") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkDestroyCommandPool;
    }
    if (strcmp(pName, "vkResetCommandPool") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkResetCommandPool;
    }
    
    /* Command buffer functions */
    if (strcmp(pName, "vkAllocateCommandBuffers") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkAllocateCommandBuffers;
    }
    if (strcmp(pName, "vkFreeCommandBuffers") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkFreeCommandBuffers;
    }
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkBeginCommandBuffer;
    }
    if (strcmp(pName, "vkEndCommandBuffer") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkEndCommandBuffer;
    }
    if (strcmp(pName, "vkResetCommandBuffer") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkResetCommandBuffer;
    }
    
    /* Queue submit */
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkQueueSubmit;
    }
    
    /* Fence functions */
    if (strcmp(pName, "vkCreateFence") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkCreateFence;
    }
    if (strcmp(pName, "vkDestroyFence") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkDestroyFence;
    }
    if (strcmp(pName, "vkWaitForFences") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkWaitForFences;
    }
    if (strcmp(pName, "vkResetFences") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkResetFences;
    }
    if (strcmp(pName, "vkGetFenceStatus") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetFenceStatus;
    }
    
    /* Semaphore functions */
    if (strcmp(pName, "vkCreateSemaphore") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkCreateSemaphore;
    }
    if (strcmp(pName, "vkDestroySemaphore") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkDestroySemaphore;
    }
    
    /* Event functions */
    if (strcmp(pName, "vkCreateEvent") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkCreateEvent;
    }
    if (strcmp(pName, "vkDestroyEvent") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkDestroyEvent;
    }
    if (strcmp(pName, "vkGetEventStatus") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetEventStatus;
    }
    if (strcmp(pName, "vkSetEvent") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkSetEvent;
    }
    if (strcmp(pName, "vkResetEvent") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkResetEvent;
    }
    
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

/* Forward declaration of logical device type (defined below) */
typedef struct mvgal_logical_device_t mvgal_logical_device_t;

/* Track the single logical device (simplification — no multi-device yet) */
static mvgal_logical_device_t* g_active_logical_device = NULL;

/* Maximum queues across all families per logical device */
#define MVGAL_ICD_MAX_QUEUES 64

/* Queue handle stored in a logical device */
typedef struct {
    uint32_t familyIndex;
    uint32_t queueIndex;
    uint32_t mvgal_queue_id;  /* Placeholder for MVGAL scheduler queue id */
} mvgal_icd_queue_t;

/* Logical device created from the virtual physical device */
struct mvgal_logical_device_t {
    VkDevice handle;           /* Unique handle value */
    uint32_t queueCount;
    mvgal_icd_queue_t queues[MVGAL_ICD_MAX_QUEUES];
    bool destroyed;
    uint64_t submitCount;      /* Monotonic submit counter */
};   /* freed by vkDestroyDevice */

/* ============================================================================
 * Allocation / Sync  Tracking Structures
 *
 * Each object below is heap-allocated, and its pointer cast to the
 * corresponding Vulkan handle type.  The leading Vk..._TYPE_TAG member
 * is a sentinel used to validate that the handle was created by this ICD
 * (crude type-check to catch obvious caller mistakes).
 * ============================================================================ */

/* Unique tags for crude handle validation */
#define MVGAL_MEM_TAG    0x4D454DAB /* "MEM" */
#define MVGAL_POOL_TAG   0x504F4CAC /* "POL" */
#define MVGAL_CMDBUF_TAG 0x434D44AD /* "CMD" */
#define MVGAL_FENCE_TAG  0x46454EAE /* "FEN" */
#define MVGAL_SEM_TAG    0x53454DAF /* "SEM" */
#define MVGAL_EVENT_TAG  0x455654B0 /* "EVT" */

/* VkDeviceMemory allocation record */
typedef struct {
    uint32_t    tag;
    VkDeviceSize size;
    uint32_t    memoryTypeIndex;
    void*       hostPointer;   /* malloc'd backing store */
    bool        isMapped;
    VkDeviceSize mapOffset;
    VkDeviceSize mapSize;
} mvgal_icd_allocation_t;

/* VkCommandPool record */
typedef struct mvgal_icd_cmdbuf_node mvgal_icd_cmdbuf_node_t;
struct mvgal_icd_cmdbuf_node {
    mvgal_icd_cmdbuf_node_t* next;
    VkCommandBuffer          handle;   /* pointer to the cmdbuf record */
};
typedef struct {
    uint32_t    tag;
    uint32_t    queueFamilyIndex;
    VkCommandPoolCreateFlags flags;
    mvgal_icd_cmdbuf_node_t* cmdbufs;  /* intrusive linked list */
} mvgal_icd_command_pool_t;

/* VkCommandBuffer state machine */
typedef enum {
    MVGAL_CMDBUF_INITIAL    = 0,
    MVGAL_CMDBUF_RECORDING  = 1,
    MVGAL_CMDBUF_EXECUTABLE = 2,
    MVGAL_CMDBUF_INVALID    = 3,
} mvgal_icd_cmdbuf_state_t;

typedef struct {
    uint32_t               tag;
    VkCommandBufferLevel   level;
    mvgal_icd_cmdbuf_state_t state;
    mvgal_icd_command_pool_t* pool;   /* owning pool */
} mvgal_icd_command_buffer_t;

/* Fence / Semaphore / Event */
typedef struct {
    uint32_t tag;
    bool     signaled;
} mvgal_icd_fence_t;

typedef struct {
    uint32_t tag;
    /* binary semaphore only for now */
} mvgal_icd_semaphore_t;

typedef struct {
    uint32_t tag;
    bool     signaled;
} mvgal_icd_event_t;

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
        };
        (void)any_graphics;
        (void)any_compute; /* Reserved for future conditional features */
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
 * mvgal_vkGetPhysicalDeviceQueueFamilyProperties
 * ============================================================================ */

void VKAPI_CALL mvgal_vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pCount,
    VkQueueFamilyProperties* pProperties) {
    
    (void)physicalDevice;
    mvgal_get_queue_family_properties(pCount, pProperties);
}

/* ============================================================================
 * mvgal_vkGetPhysicalDeviceQueueFamilyProperties2 (VK_KHR_get_physical_device_properties2)
 * ============================================================================ */

void VKAPI_CALL mvgal_vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice,
    uint32_t* pCount,
    VkQueueFamilyProperties2* pProperties) {
    
    if (pProperties) {
        uint32_t count = *pCount;
        for (uint32_t i = 0; i < count; i++) {
            mvgal_vkGetPhysicalDeviceQueueFamilyProperties(
                physicalDevice, &count,
                &pProperties[i].queueFamilyProperties);
        }
        *pCount = count;
    } else {
        mvgal_vkGetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, pCount, NULL);
    }
}

/* ============================================================================
 * mvgal_vkCreateDevice - Create virtual logical device
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    
    if (!pDevice || !pCreateInfo) return VK_ERROR_INITIALIZATION_FAILED;
    (void)pAllocator;
    
    /* Validate physical device is our virtual one */
    if ((mvgal_physical_device_t*)physicalDevice != &g_virtual_physical_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    /* Query available queue families */
    uint32_t familyCount = 0;
    mvgal_get_queue_family_properties(&familyCount, NULL);
    VkQueueFamilyProperties* families = NULL;
    if (familyCount > 0) {
        families = (VkQueueFamilyProperties*)calloc(familyCount, sizeof(VkQueueFamilyProperties));
        if (families) mvgal_get_queue_family_properties(&familyCount, families);
    }
    
    /* Allocate logical device */
    mvgal_logical_device_t* dev = 
        (mvgal_logical_device_t*)calloc(1, sizeof(mvgal_logical_device_t));
    if (!dev) {
        free(families);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    
    /* Track queue usage for each family */
    uint32_t* used_per_family = NULL;
    if (familyCount > 0) {
        used_per_family = (uint32_t*)calloc(familyCount, sizeof(uint32_t));
    }
    
    /* Process queue create infos */
    uint32_t total_queues = 0;
    for (uint32_t qci = 0; qci < pCreateInfo->queueCreateInfoCount; qci++) {
        const VkDeviceQueueCreateInfo* qi = &pCreateInfo->pQueueCreateInfos[qci];
        
        /* Validate family index */
        if (qi->queueFamilyIndex >= familyCount) {
            free(families);
            free(used_per_family);
            free(dev);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        
        /* Validate queue count */
        uint32_t max_queues = families ? families[qi->queueFamilyIndex].queueCount : 4;
        if (qi->queueCount > max_queues) {
            free(families);
            free(used_per_family);
            free(dev);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        
        /* Assign queue indices */
        for (uint32_t q = 0; q < qi->queueCount && total_queues < MVGAL_ICD_MAX_QUEUES; q++) {
            dev->queues[total_queues].familyIndex = qi->queueFamilyIndex;
            dev->queues[total_queues].queueIndex = used_per_family[qi->queueFamilyIndex]++;
            dev->queues[total_queues].mvgal_queue_id = -1; /* Not yet bound to scheduler */
            total_queues++;
        }
    }
    
    dev->queueCount = total_queues;
    
    /* Handle enabled features (copy from physical device if not specified) */
    if (pCreateInfo->pEnabledFeatures) {
        /* App specified exact features — store for validation */
        (void)pCreateInfo->pEnabledFeatures;
    }
    
    /* Use pointer as unique handle (ICD convention) */
    dev->handle = (VkDevice)(uintptr_t)(dev);
    *pDevice = dev->handle;
    
    /* Track as the active logical device for submit counting */
    g_active_logical_device = dev;
    
    free(families);
    free(used_per_family);
    
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkDestroyDevice - Destroy virtual logical device
 * ============================================================================ */

void VKAPI_CALL mvgal_vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {
    
    (void)pAllocator;
    if (!device) return;
    
    mvgal_logical_device_t* dev = (mvgal_logical_device_t*)(uintptr_t)device;
    
    if (dev->destroyed) return;
    dev->destroyed = true;
    
    free(dev);
}

/* ============================================================================
 * mvgal_vkGetDeviceQueue - Return queue handle by family + index
 * ============================================================================ */

void VKAPI_CALL mvgal_vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue) {
    
    if (!pQueue) return;
    *pQueue = VK_NULL_HANDLE;
    if (!device) return;
    
    mvgal_logical_device_t* dev = (mvgal_logical_device_t*)(uintptr_t)device;
    if (dev->destroyed) return;
    
    /* Linear search for matching queue */
    for (uint32_t i = 0; i < dev->queueCount; i++) {
        if (dev->queues[i].familyIndex == queueFamilyIndex &&
            dev->queues[i].queueIndex == queueIndex) {
            /* Use index+1 as handle (VK_NULL_HANDLE = 0) */
            *pQueue = (VkQueue)(uintptr_t)(i + 1);
            return;
        }
    }
}

/* ============================================================================
 * mvgal_vkGetDeviceQueue2 (VK_KHR_device_group)
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkGetDeviceQueue2(
    VkDevice device,
    const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue) {
    
    if (!pQueueInfo || !pQueue) return VK_ERROR_INITIALIZATION_FAILED;
    
    mvgal_vkGetDeviceQueue(device,
                          pQueueInfo->queueFamilyIndex,
                          pQueueInfo->queueIndex,
                          pQueue);
    
    return *pQueue ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

/* ============================================================================
 * mvgal_vkDeviceWaitIdle - Wait for all device work to complete
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkDeviceWaitIdle(VkDevice device) {
    (void)device;
    /* TODO: Forward to MVGAL scheduler flush */
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkQueueWaitIdle - Wait for queue work to complete
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkQueueWaitIdle(VkQueue queue) {
    (void)queue;
    /* TODO: Forward to MVGAL scheduler flush per-queue */
    return VK_SUCCESS;
}

/* ============================================================================
 * Helper: validate a handle belongs to this ICD
 * ============================================================================ */

static bool mvgal_icd_valid_tag(void* ptr, uint32_t expected_tag) {
    if (!ptr) return false;
    return *(uint32_t*)ptr == expected_tag;
}

/* ============================================================================
 * mvgal_vkAllocateMemory — host-memory backing for virtual allocations
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDeviceMemory* pMemory) {

    if (!pAllocateInfo || !pMemory) return VK_ERROR_INITIALIZATION_FAILED;
    (void)device;
    (void)pAllocator;

    mvgal_icd_allocation_t* alloc =
        (mvgal_icd_allocation_t*)calloc(1, sizeof(mvgal_icd_allocation_t));
    if (!alloc) return VK_ERROR_OUT_OF_HOST_MEMORY;

    alloc->tag            = MVGAL_MEM_TAG;
    alloc->size           = pAllocateInfo->allocationSize;
    alloc->memoryTypeIndex = pAllocateInfo->memoryTypeIndex;

    /* Backing store — use calloc so mapped reads return zero */
    alloc->hostPointer = calloc(1, (size_t)pAllocateInfo->allocationSize);
    if (!alloc->hostPointer && pAllocateInfo->allocationSize > 0) {
        free(alloc);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    *pMemory = (VkDeviceMemory)alloc;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkFreeMemory
 * ============================================================================ */

void VKAPI_CALL mvgal_vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator) {

    (void)device;
    (void)pAllocator;
    if (!mvgal_icd_valid_tag(memory, MVGAL_MEM_TAG)) return;

    mvgal_icd_allocation_t* alloc = (mvgal_icd_allocation_t*)memory;
    alloc->tag = 0;  /* invalidate */
    free(alloc->hostPointer);
    free(alloc);
}

/* ============================================================================
 * mvgal_vkMapMemory
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkMapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkMemoryMapFlags flags,
    void** ppData) {

    (void)device;
    (void)flags;
    if (!ppData) return VK_ERROR_INITIALIZATION_FAILED;
    *ppData = NULL;
    if (!mvgal_icd_valid_tag(memory, MVGAL_MEM_TAG)) return VK_ERROR_MEMORY_MAP_FAILED;

    mvgal_icd_allocation_t* alloc = (mvgal_icd_allocation_t*)memory;

    if (offset >= alloc->size) return VK_ERROR_MEMORY_MAP_FAILED;

    alloc->isMapped  = true;
    alloc->mapOffset = offset;
    alloc->mapSize   = (size == VK_WHOLE_SIZE) ? (alloc->size - offset) : size;

    *ppData = (uint8_t*)alloc->hostPointer + offset;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkUnmapMemory
 * ============================================================================ */

void VKAPI_CALL mvgal_vkUnmapMemory(
    VkDevice device,
    VkDeviceMemory memory) {

    (void)device;
    if (!mvgal_icd_valid_tag(memory, MVGAL_MEM_TAG)) return;

    mvgal_icd_allocation_t* alloc = (mvgal_icd_allocation_t*)memory;
    alloc->isMapped  = false;
    alloc->mapOffset = 0;
    alloc->mapSize   = 0;
}

/* ============================================================================
 * mvgal_vkGetDeviceMemoryCommitment
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkGetDeviceMemoryCommitment(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize* pCommitted) {

    (void)device;
    if (!pCommitted) return VK_ERROR_INITIALIZATION_FAILED;
    *pCommitted = 0;
    if (!mvgal_icd_valid_tag(memory, MVGAL_MEM_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    *pCommitted = ((mvgal_icd_allocation_t*)memory)->size;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkCreateCommandPool
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkCreateCommandPool(
    VkDevice device,
    const VkCommandPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkCommandPool* pCommandPool) {

    if (!pCreateInfo || !pCommandPool) return VK_ERROR_INITIALIZATION_FAILED;
    (void)device;
    (void)pAllocator;

    mvgal_icd_command_pool_t* pool =
        (mvgal_icd_command_pool_t*)calloc(1, sizeof(mvgal_icd_command_pool_t));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;

    pool->tag              = MVGAL_POOL_TAG;
    pool->queueFamilyIndex = pCreateInfo->queueFamilyIndex;
    pool->flags            = pCreateInfo->flags;

    *pCommandPool = (VkCommandPool)pool;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkDestroyCommandPool — frees all cmdbufs in the pool then the pool
 * ============================================================================ */

void VKAPI_CALL mvgal_vkDestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const VkAllocationCallbacks* pAllocator) {

    (void)device;
    (void)pAllocator;
    if (!mvgal_icd_valid_tag(commandPool, MVGAL_POOL_TAG)) return;

    mvgal_icd_command_pool_t* pool = (mvgal_icd_command_pool_t*)commandPool;

    /* Free all command buffers in this pool */
    mvgal_icd_cmdbuf_node_t* node = pool->cmdbufs;
    while (node) {
        mvgal_icd_cmdbuf_node_t* next = node->next;
        if (node->handle) {
            mvgal_icd_command_buffer_t* cb =
                (mvgal_icd_command_buffer_t*)node->handle;
            cb->tag = 0;
            free(cb);
        }
        free(node);
        node = next;
    }

    pool->tag = 0;
    free(pool);
}

/* ============================================================================
 * mvgal_vkResetCommandPool — reset all cmdbufs to INITIAL
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkResetCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    VkCommandPoolResetFlags flags) {

    (void)device;
    (void)flags;
    if (!mvgal_icd_valid_tag(commandPool, MVGAL_POOL_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    mvgal_icd_command_pool_t* pool = (mvgal_icd_command_pool_t*)commandPool;
    mvgal_icd_cmdbuf_node_t* node = pool->cmdbufs;
    while (node) {
        if (node->handle) {
            mvgal_icd_command_buffer_t* cb =
                (mvgal_icd_command_buffer_t*)node->handle;
            cb->state = MVGAL_CMDBUF_INITIAL;
        }
        node = node->next;
    }
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkAllocateCommandBuffers
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkAllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers) {

    if (!pAllocateInfo || !pCommandBuffers) return VK_ERROR_INITIALIZATION_FAILED;
    (void)device;

    VkCommandPool poolHandle = pAllocateInfo->commandPool;
    if (!mvgal_icd_valid_tag(poolHandle, MVGAL_POOL_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    mvgal_icd_command_pool_t* pool = (mvgal_icd_command_pool_t*)poolHandle;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        mvgal_icd_command_buffer_t* cb =
            (mvgal_icd_command_buffer_t*)calloc(1, sizeof(mvgal_icd_command_buffer_t));
        if (!cb) {
            /* Free previously allocated buffers on failure */
            for (uint32_t j = 0; j < i; j++) {
                mvgal_icd_command_buffer_t* fail_cb =
                    (mvgal_icd_command_buffer_t*)pCommandBuffers[j];
                if (fail_cb) { fail_cb->tag = 0; free(fail_cb); }
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        cb->tag   = MVGAL_CMDBUF_TAG;
        cb->level = pAllocateInfo->level;
        cb->state = MVGAL_CMDBUF_INITIAL;
        cb->pool  = pool;

        VkCommandBuffer handle = (VkCommandBuffer)cb;
        pCommandBuffers[i] = handle;

        /* Link into the pool's intrusive list */
        mvgal_icd_cmdbuf_node_t* node =
            (mvgal_icd_cmdbuf_node_t*)malloc(sizeof(mvgal_icd_cmdbuf_node_t));
        if (!node) {
            cb->tag = 0;
            free(cb);
            pCommandBuffers[i] = VK_NULL_HANDLE;
            for (uint32_t j = 0; j < i; j++) {
                mvgal_icd_command_buffer_t* fail_cb =
                    (mvgal_icd_command_buffer_t*)pCommandBuffers[j];
                if (fail_cb) { fail_cb->tag = 0; free(fail_cb); }
            }
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        node->handle = handle;
        node->next   = pool->cmdbufs;
        pool->cmdbufs = node;
    }

    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkFreeCommandBuffers — unlink from pool and free
 * ============================================================================ */

void VKAPI_CALL mvgal_vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer* pCommandBuffers) {

    (void)device;
    if (!mvgal_icd_valid_tag(commandPool, MVGAL_POOL_TAG)) return;

    mvgal_icd_command_pool_t* pool = (mvgal_icd_command_pool_t*)commandPool;

    for (uint32_t i = 0; i < commandBufferCount; i++) {
        VkCommandBuffer handle = pCommandBuffers[i];
        if (!mvgal_icd_valid_tag(handle, MVGAL_CMDBUF_TAG)) continue;

        mvgal_icd_command_buffer_t* cb = (mvgal_icd_command_buffer_t*)handle;
        cb->tag = 0;

        /* Unlink from pool */
        mvgal_icd_cmdbuf_node_t** pp = &pool->cmdbufs;
        while (*pp) {
            if ((*pp)->handle == handle) {
                mvgal_icd_cmdbuf_node_t* del = *pp;
                *pp = del->next;
                free(del);
                break;
            }
            pp = &(*pp)->next;
        }

        free(cb);
    }
}

/* ============================================================================
 * mvgal_vkBeginCommandBuffer
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo) {

    (void)pBeginInfo;
    if (!mvgal_icd_valid_tag(commandBuffer, MVGAL_CMDBUF_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    mvgal_icd_command_buffer_t* cb = (mvgal_icd_command_buffer_t*)commandBuffer;
    cb->state = MVGAL_CMDBUF_RECORDING;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkEndCommandBuffer
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkEndCommandBuffer(
    VkCommandBuffer commandBuffer) {

    if (!mvgal_icd_valid_tag(commandBuffer, MVGAL_CMDBUF_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    mvgal_icd_command_buffer_t* cb = (mvgal_icd_command_buffer_t*)commandBuffer;
    if (cb->state != MVGAL_CMDBUF_RECORDING) return VK_NOT_READY;

    cb->state = MVGAL_CMDBUF_EXECUTABLE;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkResetCommandBuffer
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkResetCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkCommandBufferResetFlags flags) {

    (void)flags;
    if (!mvgal_icd_valid_tag(commandBuffer, MVGAL_CMDBUF_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    mvgal_icd_command_buffer_t* cb = (mvgal_icd_command_buffer_t*)commandBuffer;
    cb->state = MVGAL_CMDBUF_INITIAL;
    return VK_SUCCESS;
}

/* Track the single logical device (simplification — no multi-device yet) */
static mvgal_logical_device_t* g_active_logical_device = NULL;

/* ============================================================================
 * mvgal_vkQueueSubmit — accept submission, signal fence
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence fence) {

    (void)queue;
    (void)submitCount;
    (void)pSubmits;

    /* Signal the optional fence immediately */
    if (fence != VK_NULL_HANDLE &&
        mvgal_icd_valid_tag(fence, MVGAL_FENCE_TAG)) {
        ((mvgal_icd_fence_t*)fence)->signaled = true;
    }

    /* Increment device-level submit counter */
    if (g_active_logical_device) {
        g_active_logical_device->submitCount++;
    }

    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkCreateFence
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkCreateFence(
    VkDevice device,
    const VkFenceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFence* pFence) {

    if (!pCreateInfo || !pFence) return VK_ERROR_INITIALIZATION_FAILED;
    (void)device;
    (void)pAllocator;

    mvgal_icd_fence_t* f = (mvgal_icd_fence_t*)calloc(1, sizeof(mvgal_icd_fence_t));
    if (!f) return VK_ERROR_OUT_OF_HOST_MEMORY;

    f->tag      = MVGAL_FENCE_TAG;
    f->signaled = (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;

    *pFence = (VkFence)f;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkDestroyFence
 * ============================================================================ */

void VKAPI_CALL mvgal_vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const VkAllocationCallbacks* pAllocator) {

    (void)device;
    (void)pAllocator;
    if (!mvgal_icd_valid_tag(fence, MVGAL_FENCE_TAG)) return;

    mvgal_icd_fence_t* f = (mvgal_icd_fence_t*)fence;
    f->tag = 0;
    free(f);
}

/* ============================================================================
 * mvgal_vkWaitForFences — immediately succeed (fences signal on submit)
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences,
    VkBool32 waitAll,
    uint64_t timeout) {

    (void)device;
    (void)timeout;
    (void)waitAll;

    for (uint32_t i = 0; i < fenceCount; i++) {
        if (!mvgal_icd_valid_tag(pFences[i], MVGAL_FENCE_TAG)) continue;
        /* In our virtual ICD, fences are signaled immediately on submit,
         * so waiting always succeeds instantly. */
    }
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkResetFences
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkResetFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences) {

    (void)device;
    for (uint32_t i = 0; i < fenceCount; i++) {
        if (!mvgal_icd_valid_tag(pFences[i], MVGAL_FENCE_TAG)) continue;
        ((mvgal_icd_fence_t*)pFences[i])->signaled = false;
    }
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkGetFenceStatus
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkGetFenceStatus(
    VkDevice device,
    VkFence fence) {

    (void)device;
    if (!mvgal_icd_valid_tag(fence, MVGAL_FENCE_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    return ((mvgal_icd_fence_t*)fence)->signaled ? VK_SUCCESS : VK_NOT_READY;
}

/* ============================================================================
 * mvgal_vkCreateSemaphore
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkCreateSemaphore(
    VkDevice device,
    const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSemaphore* pSemaphore) {

    if (!pCreateInfo || !pSemaphore) return VK_ERROR_INITIALIZATION_FAILED;
    (void)device;
    (void)pAllocator;

    mvgal_icd_semaphore_t* s =
        (mvgal_icd_semaphore_t*)calloc(1, sizeof(mvgal_icd_semaphore_t));
    if (!s) return VK_ERROR_OUT_OF_HOST_MEMORY;

    s->tag = MVGAL_SEM_TAG;
    *pSemaphore = (VkSemaphore)s;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkDestroySemaphore
 * ============================================================================ */

void VKAPI_CALL mvgal_vkDestroySemaphore(
    VkDevice device,
    VkSemaphore semaphore,
    const VkAllocationCallbacks* pAllocator) {

    (void)device;
    (void)pAllocator;
    if (!mvgal_icd_valid_tag(semaphore, MVGAL_SEM_TAG)) return;

    ((mvgal_icd_semaphore_t*)semaphore)->tag = 0;
    free(semaphore);
}

/* ============================================================================
 * mvgal_vkCreateEvent
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkCreateEvent(
    VkDevice device,
    const VkEventCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkEvent* pEvent) {

    if (!pCreateInfo || !pEvent) return VK_ERROR_INITIALIZATION_FAILED;
    (void)device;
    (void)pAllocator;

    mvgal_icd_event_t* e = (mvgal_icd_event_t*)calloc(1, sizeof(mvgal_icd_event_t));
    if (!e) return VK_ERROR_OUT_OF_HOST_MEMORY;

    e->tag      = MVGAL_EVENT_TAG;
    e->signaled = false;

    *pEvent = (VkEvent)e;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkDestroyEvent
 * ============================================================================ */

void VKAPI_CALL mvgal_vkDestroyEvent(
    VkDevice device,
    VkEvent event,
    const VkAllocationCallbacks* pAllocator) {

    (void)device;
    (void)pAllocator;
    if (!mvgal_icd_valid_tag(event, MVGAL_EVENT_TAG)) return;

    ((mvgal_icd_event_t*)event)->tag = 0;
    free(event);
}

/* ============================================================================
 * mvgal_vkGetEventStatus
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkGetEventStatus(
    VkDevice device,
    VkEvent event) {

    (void)device;
    if (!mvgal_icd_valid_tag(event, MVGAL_EVENT_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    return ((mvgal_icd_event_t*)event)->signaled ? VK_EVENT_SET : VK_EVENT_RESET;
}

/* ============================================================================
 * mvgal_vkSetEvent
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkSetEvent(
    VkDevice device,
    VkEvent event) {

    (void)device;
    if (!mvgal_icd_valid_tag(event, MVGAL_EVENT_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    ((mvgal_icd_event_t*)event)->signaled = true;
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkResetEvent
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkResetEvent(
    VkDevice device,
    VkEvent event) {

    (void)device;
    if (!mvgal_icd_valid_tag(event, MVGAL_EVENT_TAG)) return VK_ERROR_INITIALIZATION_FAILED;

    ((mvgal_icd_event_t*)event)->signaled = false;
    return VK_SUCCESS;
}

/* ============================================================================
 * End of icd_entry.c
 * ============================================================================ */
