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

/* Vulkan 1.1 / KHR_get_physical_device_properties2 forward declarations */
void VKAPI_CALL mvgal_vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2 *pProperties);
void VKAPI_CALL mvgal_vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2 *pFeatures);
void VKAPI_CALL mvgal_vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2 *pMemoryProperties);
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
    }    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) {
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

    /* Vulkan 1.1 / KHR_get_physical_device_properties2 */
    if (strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceFeatures2KHR") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceFeatures2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2KHR") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceMemoryProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2KHR") == 0) {
        return (PFN_vkVoidFunction)mvgal_vkGetPhysicalDeviceQueueFamilyProperties2;
    }

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
    uint32_t mvgal_queue_id;  /* UINT32_MAX = not yet bound to scheduler queue */
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
    g_virtual_physical_device.driverVersion = VK_MAKE_VERSION(0, 2, 2);
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
        uint32_t dv = VK_MAKE_VERSION(0, 2, 2);
        memcpy(&pProperties->pipelineCacheUUID[13], &dv, 3);
    }
    
    /* Aggregate limits from all real GPUs — conservative intersection */
    {
        int32_t gpu_count = mvgal_gpu_get_count();
        bool limits_set = false;
        VkPhysicalDeviceLimits agg = {0};

        for (int32_t i = 0; i < gpu_count; i++) {
            mvgal_gpu_descriptor_t desc;
            if (mvgal_gpu_get_descriptor((uint32_t)i, &desc) != MVGAL_SUCCESS) continue;

            /* Derive per-GPU limits from descriptor fields */
            VkPhysicalDeviceLimits lim = {0};

            /* Image dimensions — scale with VRAM: 16 GiB → 16384, 8 GiB → 8192, else 4096 */
            uint32_t img_dim = (desc.vram_total >= 16ULL * 1024 * 1024 * 1024) ? 16384U :
                               (desc.vram_total >=  8ULL * 1024 * 1024 * 1024) ?  8192U : 4096U;
            lim.maxImageDimension1D                    = img_dim;
            lim.maxImageDimension2D                    = img_dim;
            lim.maxImageDimension3D                    = img_dim / 4U;
            lim.maxImageDimensionCube                  = img_dim;
            lim.maxImageArrayLayers                    = 2048U;
            lim.maxTexelBufferElements                 = 134217728U;
            lim.maxUniformBufferRange                  = 65536U;
            lim.maxStorageBufferRange                  = 0xFFFFFFFFU;
            lim.maxPushConstantsSize                   = 256U;
            lim.maxMemoryAllocationCount               = 4096U;
            lim.maxSamplerAllocationCount              = 4000U;
            lim.bufferImageGranularity                 = 1U;
            lim.sparseAddressSpaceSize                 = 0xFFFFFFFFFFFFFFFFULL;
            lim.maxBoundDescriptorSets                 = 8U;
            lim.maxPerStageDescriptorSamplers          = 16U;
            lim.maxPerStageDescriptorUniformBuffers    = 15U;
            lim.maxPerStageDescriptorStorageBuffers    = 16U;
            lim.maxPerStageDescriptorSampledImages     = 128U;
            lim.maxPerStageDescriptorStorageImages     = 8U;
            lim.maxPerStageDescriptorInputAttachments  = 8U;
            lim.maxPerStageResources                   = 128U;
            lim.maxDescriptorSetSamplers               = 80U;
            lim.maxDescriptorSetUniformBuffers         = 90U;
            lim.maxDescriptorSetUniformBuffersDynamic  = 8U;
            lim.maxDescriptorSetStorageBuffers         = 155U;
            lim.maxDescriptorSetStorageBuffersDynamic  = 8U;
            lim.maxDescriptorSetSampledImages          = 256U;
            lim.maxDescriptorSetStorageImages          = 40U;
            lim.maxDescriptorSetInputAttachments       = 8U;
            lim.maxVertexInputAttributes               = 32U;
            lim.maxVertexInputBindings                 = 32U;
            lim.maxVertexInputAttributeOffset          = 2047U;
            lim.maxVertexInputBindingStride            = 2048U;
            lim.maxVertexOutputComponents              = 128U;
            lim.maxTessellationGenerationLevel         = 64U;
            lim.maxTessellationPatchSize               = 32U;
            lim.maxTessellationControlPerVertexInputComponents  = 128U;
            lim.maxTessellationControlPerVertexOutputComponents = 128U;
            lim.maxTessellationControlPerPatchOutputComponents  = 120U;
            lim.maxTessellationControlTotalOutputComponents     = 4096U;
            lim.maxTessellationEvaluationInputComponents        = 128U;
            lim.maxTessellationEvaluationOutputComponents       = 128U;
            lim.maxGeometryShaderInvocations           = 32U;
            lim.maxGeometryInputComponents             = 64U;
            lim.maxGeometryOutputComponents            = 128U;
            lim.maxGeometryOutputVertices              = 256U;
            lim.maxGeometryTotalOutputComponents       = 1024U;
            lim.maxFragmentInputComponents             = 128U;
            lim.maxFragmentOutputAttachments           = 8U;
            lim.maxFragmentDualSrcAttachments          = 1U;
            lim.maxFragmentCombinedOutputResources     = 8U;
            lim.maxComputeSharedMemorySize             = 49152U;
            lim.maxComputeWorkGroupCount[0]            = 65535U;
            lim.maxComputeWorkGroupCount[1]            = 65535U;
            lim.maxComputeWorkGroupCount[2]            = 65535U;
            lim.maxComputeWorkGroupInvocations         = 1024U;
            lim.maxComputeWorkGroupSize[0]             = 1024U;
            lim.maxComputeWorkGroupSize[1]             = 1024U;
            lim.maxComputeWorkGroupSize[2]             = 64U;
            lim.subPixelPrecisionBits                  = 8U;
            lim.subTexelPrecisionBits                  = 8U;
            lim.mipmapPrecisionBits                    = 8U;
            lim.maxDrawIndexedIndexValue               = 0xFFFFFFFFU;
            lim.maxDrawIndirectCount                   = 0xFFFFFFFFU;
            lim.maxSamplerLodBias                      = 16.0f;
            lim.maxSamplerAnisotropy                   = 16.0f;
            lim.maxViewports                           = 16U;
            lim.maxViewportDimensions[0]               = img_dim;
            lim.maxViewportDimensions[1]               = img_dim;
            lim.viewportBoundsRange[0]                 = -32768.0f;
            lim.viewportBoundsRange[1]                 =  32767.0f;
            lim.viewportSubPixelBits                   = 8U;
            lim.minMemoryMapAlignment                  = 64U;
            lim.minTexelBufferOffsetAlignment          = 64U;
            lim.minUniformBufferOffsetAlignment        = 256U;
            lim.minStorageBufferOffsetAlignment        = 64U;
            lim.minTexelOffset                         = (int32_t)-8;
            lim.maxTexelOffset                         = 7U;
            lim.minTexelGatherOffset                   = (int32_t)-32;
            lim.maxTexelGatherOffset                   = 31U;
            lim.minInterpolationOffset                 = -0.5f;
            lim.maxInterpolationOffset                 = 0.4375f;
            lim.subPixelInterpolationOffsetBits        = 4U;
            lim.maxFramebufferWidth                    = img_dim;
            lim.maxFramebufferHeight                   = img_dim;
            lim.maxFramebufferLayers                   = 2048U;
            lim.framebufferColorSampleCounts           = 0x7FU; /* 1,2,4,8,16,32,64 */
            lim.framebufferDepthSampleCounts           = 0x7FU;
            lim.framebufferStencilSampleCounts         = 0x7FU;
            lim.framebufferNoAttachmentsSampleCounts   = 0x7FU;
            lim.maxColorAttachments                    = 8U;
            lim.sampledImageColorSampleCounts          = 0x7FU;
            lim.sampledImageIntegerSampleCounts        = 0x01U;
            lim.sampledImageDepthSampleCounts          = 0x7FU;
            lim.sampledImageStencilSampleCounts        = 0x7FU;
            lim.storageImageSampleCounts               = 0x01U;
            lim.maxSampleMaskWords                     = 1U;
            lim.timestampComputeAndGraphics            = VK_TRUE;
            lim.timestampPeriod                        = 1.0f;
            lim.maxClipDistances                       = 8U;
            lim.maxCullDistances                       = 8U;
            lim.maxCombinedClipAndCullDistances        = 8U;
            lim.discreteQueuePriorities                = 2U;
            lim.pointSizeRange[0]                      = 1.0f;
            lim.pointSizeRange[1]                      = 64.0f;
            lim.lineWidthRange[0]                      = 1.0f;
            lim.lineWidthRange[1]                      = 1.0f;
            lim.pointSizeGranularity                   = 1.0f;
            lim.lineWidthGranularity                   = 1.0f;
            lim.strictLines                            = VK_TRUE;
            lim.standardSampleLocations                = VK_TRUE;
            lim.optimalBufferCopyOffsetAlignment       = 64U;
            lim.optimalBufferCopyRowPitchAlignment     = 64U;
            lim.nonCoherentAtomSize                    = 256U;

            if (!limits_set) {
                agg = lim;
                limits_set = true;
            } else {
                /* Conservative intersection: take minimums for max-limits,
                 * maximums for min-limits (alignment, granularity). */
#define MVGAL_MIN_LIM(f) if (lim.f < agg.f) agg.f = lim.f
#define MVGAL_MAX_LIM(f) if (lim.f > agg.f) agg.f = lim.f
                MVGAL_MIN_LIM(maxImageDimension1D);
                MVGAL_MIN_LIM(maxImageDimension2D);
                MVGAL_MIN_LIM(maxImageDimension3D);
                MVGAL_MIN_LIM(maxImageDimensionCube);
                MVGAL_MIN_LIM(maxImageArrayLayers);
                MVGAL_MIN_LIM(maxTexelBufferElements);
                MVGAL_MIN_LIM(maxUniformBufferRange);
                MVGAL_MIN_LIM(maxStorageBufferRange);
                MVGAL_MIN_LIM(maxPushConstantsSize);
                MVGAL_MIN_LIM(maxMemoryAllocationCount);
                MVGAL_MIN_LIM(maxSamplerAllocationCount);
                MVGAL_MAX_LIM(bufferImageGranularity);
                MVGAL_MIN_LIM(maxBoundDescriptorSets);
                MVGAL_MIN_LIM(maxPerStageDescriptorSamplers);
                MVGAL_MIN_LIM(maxPerStageDescriptorUniformBuffers);
                MVGAL_MIN_LIM(maxPerStageDescriptorStorageBuffers);
                MVGAL_MIN_LIM(maxPerStageDescriptorSampledImages);
                MVGAL_MIN_LIM(maxPerStageDescriptorStorageImages);
                MVGAL_MIN_LIM(maxPerStageDescriptorInputAttachments);
                MVGAL_MIN_LIM(maxPerStageResources);
                MVGAL_MIN_LIM(maxDescriptorSetSamplers);
                MVGAL_MIN_LIM(maxDescriptorSetUniformBuffers);
                MVGAL_MIN_LIM(maxDescriptorSetUniformBuffersDynamic);
                MVGAL_MIN_LIM(maxDescriptorSetStorageBuffers);
                MVGAL_MIN_LIM(maxDescriptorSetStorageBuffersDynamic);
                MVGAL_MIN_LIM(maxDescriptorSetSampledImages);
                MVGAL_MIN_LIM(maxDescriptorSetStorageImages);
                MVGAL_MIN_LIM(maxDescriptorSetInputAttachments);
                MVGAL_MIN_LIM(maxVertexInputAttributes);
                MVGAL_MIN_LIM(maxVertexInputBindings);
                MVGAL_MIN_LIM(maxVertexInputAttributeOffset);
                MVGAL_MIN_LIM(maxVertexInputBindingStride);
                MVGAL_MIN_LIM(maxVertexOutputComponents);
                MVGAL_MIN_LIM(maxComputeSharedMemorySize);
                MVGAL_MIN_LIM(maxComputeWorkGroupCount[0]);
                MVGAL_MIN_LIM(maxComputeWorkGroupCount[1]);
                MVGAL_MIN_LIM(maxComputeWorkGroupCount[2]);
                MVGAL_MIN_LIM(maxComputeWorkGroupInvocations);
                MVGAL_MIN_LIM(maxComputeWorkGroupSize[0]);
                MVGAL_MIN_LIM(maxComputeWorkGroupSize[1]);
                MVGAL_MIN_LIM(maxComputeWorkGroupSize[2]);
                MVGAL_MIN_LIM(maxViewports);
                MVGAL_MIN_LIM(maxViewportDimensions[0]);
                MVGAL_MIN_LIM(maxViewportDimensions[1]);
                MVGAL_MIN_LIM(maxFramebufferWidth);
                MVGAL_MIN_LIM(maxFramebufferHeight);
                MVGAL_MIN_LIM(maxFramebufferLayers);
                MVGAL_MIN_LIM(maxColorAttachments);
                MVGAL_MIN_LIM(maxSamplerLodBias);
                MVGAL_MIN_LIM(maxSamplerAnisotropy);
                MVGAL_MAX_LIM(minMemoryMapAlignment);
                MVGAL_MAX_LIM(minTexelBufferOffsetAlignment);
                MVGAL_MAX_LIM(minUniformBufferOffsetAlignment);
                MVGAL_MAX_LIM(minStorageBufferOffsetAlignment);
                MVGAL_MAX_LIM(nonCoherentAtomSize);
                MVGAL_MAX_LIM(optimalBufferCopyOffsetAlignment);
                MVGAL_MAX_LIM(optimalBufferCopyRowPitchAlignment);
#undef MVGAL_MIN_LIM
#undef MVGAL_MAX_LIM
            }
        }

        /* Fallback when no GPUs are enumerated yet */
        if (!limits_set) {
            agg.maxImageDimension1D                   = 4096U;
            agg.maxImageDimension2D                   = 4096U;
            agg.maxImageDimension3D                   = 1024U;
            agg.maxImageDimensionCube                 = 4096U;
            agg.maxImageArrayLayers                   = 256U;
            agg.maxTexelBufferElements                = 67108864U;
            agg.maxUniformBufferRange                 = 65536U;
            agg.maxStorageBufferRange                 = 0xFFFFFFFFU;
            agg.maxPushConstantsSize                  = 128U;
            agg.maxMemoryAllocationCount              = 4096U;
            agg.maxSamplerAllocationCount             = 4000U;
            agg.bufferImageGranularity                = 1U;
            agg.maxBoundDescriptorSets                = 4U;
            agg.maxPerStageDescriptorSamplers         = 16U;
            agg.maxPerStageDescriptorUniformBuffers   = 12U;
            agg.maxPerStageDescriptorStorageBuffers   = 8U;
            agg.maxPerStageDescriptorSampledImages    = 16U;
            agg.maxPerStageDescriptorStorageImages    = 4U;
            agg.maxPerStageDescriptorInputAttachments = 4U;
            agg.maxPerStageResources                  = 44U;
            agg.maxDescriptorSetSamplers              = 48U;
            agg.maxDescriptorSetUniformBuffers        = 72U;
            agg.maxDescriptorSetUniformBuffersDynamic = 8U;
            agg.maxDescriptorSetStorageBuffers        = 24U;
            agg.maxDescriptorSetStorageBuffersDynamic = 4U;
            agg.maxDescriptorSetSampledImages         = 96U;
            agg.maxDescriptorSetStorageImages         = 24U;
            agg.maxDescriptorSetInputAttachments      = 4U;
            agg.maxVertexInputAttributes              = 16U;
            agg.maxVertexInputBindings                = 16U;
            agg.maxVertexInputAttributeOffset         = 2047U;
            agg.maxVertexInputBindingStride           = 2048U;
            agg.maxVertexOutputComponents             = 64U;
            agg.maxComputeSharedMemorySize            = 32768U;
            agg.maxComputeWorkGroupCount[0]           = 65535U;
            agg.maxComputeWorkGroupCount[1]           = 65535U;
            agg.maxComputeWorkGroupCount[2]           = 65535U;
            agg.maxComputeWorkGroupInvocations        = 1024U;
            agg.maxComputeWorkGroupSize[0]            = 1024U;
            agg.maxComputeWorkGroupSize[1]            = 1024U;
            agg.maxComputeWorkGroupSize[2]            = 64U;
            agg.subPixelPrecisionBits                 = 4U;
            agg.subTexelPrecisionBits                 = 4U;
            agg.mipmapPrecisionBits                   = 4U;
            agg.maxDrawIndexedIndexValue              = 0xFFFFFFFFU;
            agg.maxDrawIndirectCount                  = 1U;
            agg.maxSamplerLodBias                     = 2.0f;
            agg.maxSamplerAnisotropy                  = 1.0f;
            agg.maxViewports                          = 1U;
            agg.maxViewportDimensions[0]              = 4096U;
            agg.maxViewportDimensions[1]              = 4096U;
            agg.viewportBoundsRange[0]                = -8192.0f;
            agg.viewportBoundsRange[1]                =  8191.0f;
            agg.viewportSubPixelBits                  = 0U;
            agg.minMemoryMapAlignment                 = 64U;
            agg.minTexelBufferOffsetAlignment         = 256U;
            agg.minUniformBufferOffsetAlignment       = 256U;
            agg.minStorageBufferOffsetAlignment       = 256U;
            agg.minTexelOffset                        = (int32_t)-8;
            agg.maxTexelOffset                        = 7U;
            agg.minTexelGatherOffset                  = (int32_t)-8;
            agg.maxTexelGatherOffset                  = 7U;
            agg.minInterpolationOffset                = -0.5f;
            agg.maxInterpolationOffset                = 0.4375f;
            agg.subPixelInterpolationOffsetBits       = 4U;
            agg.maxFramebufferWidth                   = 4096U;
            agg.maxFramebufferHeight                  = 4096U;
            agg.maxFramebufferLayers                  = 256U;
            agg.framebufferColorSampleCounts          = 0x0FU;
            agg.framebufferDepthSampleCounts          = 0x0FU;
            agg.framebufferStencilSampleCounts        = 0x0FU;
            agg.framebufferNoAttachmentsSampleCounts  = 0x0FU;
            agg.maxColorAttachments                   = 4U;
            agg.sampledImageColorSampleCounts         = 0x0FU;
            agg.sampledImageIntegerSampleCounts       = 0x01U;
            agg.sampledImageDepthSampleCounts         = 0x0FU;
            agg.sampledImageStencilSampleCounts       = 0x0FU;
            agg.storageImageSampleCounts              = 0x01U;
            agg.maxSampleMaskWords                    = 1U;
            agg.timestampComputeAndGraphics           = VK_TRUE;
            agg.timestampPeriod                       = 1.0f;
            agg.maxClipDistances                      = 8U;
            agg.maxCullDistances                      = 8U;
            agg.maxCombinedClipAndCullDistances       = 8U;
            agg.discreteQueuePriorities               = 2U;
            agg.pointSizeRange[0]                     = 1.0f;
            agg.pointSizeRange[1]                     = 1.0f;
            agg.lineWidthRange[0]                     = 1.0f;
            agg.lineWidthRange[1]                     = 1.0f;
            agg.pointSizeGranularity                  = 0.0f;
            agg.lineWidthGranularity                  = 0.0f;
            agg.strictLines                           = VK_TRUE;
            agg.standardSampleLocations               = VK_TRUE;
            agg.optimalBufferCopyOffsetAlignment      = 256U;
            agg.optimalBufferCopyRowPitchAlignment    = 256U;
            agg.nonCoherentAtomSize                   = 256U;
        }

        pProperties->limits = agg;
    }

    pProperties->sparseProperties = (VkPhysicalDeviceSparseProperties){0};
}

/* ============================================================================
 * mvgal_vkGetPhysicalDeviceMemoryProperties - Aggregate memory
 * ============================================================================ */

void mvgal_vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    
    if (!pMemoryProperties) return;
    
    mvgal_physical_device_t* dev = (mvgal_physical_device_t*)physicalDevice;
    
    /* Dynamically aggregate from all GPUs */
    mvgal_aggregate_memory_properties(pMemoryProperties);
    
    /* Keep cached device record up to date */
    dev->memoryProperties = *pMemoryProperties;
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

/* ============================================================================
 * Extension enumeration — advertise the Vulkan 1.1/1.2/1.3 promoted extensions
 * plus the cross-vendor device-group and external-memory extensions that
 * MVGAL relies on.
 * ============================================================================ */

static const VkExtensionProperties k_device_extensions[] = {
    /* Vulkan 1.1 promoted */
    { VK_KHR_MAINTENANCE1_EXTENSION_NAME,                  VK_KHR_MAINTENANCE1_SPEC_VERSION },
    { VK_KHR_MAINTENANCE2_EXTENSION_NAME,                  VK_KHR_MAINTENANCE2_SPEC_VERSION },
    { VK_KHR_MAINTENANCE3_EXTENSION_NAME,                  VK_KHR_MAINTENANCE3_SPEC_VERSION },
    { VK_KHR_DEVICE_GROUP_EXTENSION_NAME,                  VK_KHR_DEVICE_GROUP_SPEC_VERSION },
    { VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,         VK_KHR_DEVICE_GROUP_CREATION_SPEC_VERSION },
    { VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,                 VK_KHR_BIND_MEMORY_2_SPEC_VERSION },
    { VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,     VK_KHR_GET_MEMORY_REQUIREMENTS_2_SPEC_VERSION },
    { VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,          VK_KHR_DEDICATED_ALLOCATION_SPEC_VERSION },
    { VK_KHR_MULTIVIEW_EXTENSION_NAME,                     VK_KHR_MULTIVIEW_SPEC_VERSION },
    { VK_KHR_VARIABLE_POINTERS_EXTENSION_NAME,             VK_KHR_VARIABLE_POINTERS_SPEC_VERSION },
    { VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,      VK_KHR_SAMPLER_YCBCR_CONVERSION_SPEC_VERSION },
    { VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,        VK_KHR_SHADER_DRAW_PARAMETERS_SPEC_VERSION },
    /* External memory / semaphore (cross-vendor sync) */
    { VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,               VK_KHR_EXTERNAL_MEMORY_SPEC_VERSION },
    { VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,            VK_KHR_EXTERNAL_MEMORY_FD_SPEC_VERSION },
    { VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,            VK_KHR_EXTERNAL_SEMAPHORE_SPEC_VERSION },
    { VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,         VK_KHR_EXTERNAL_SEMAPHORE_FD_SPEC_VERSION },
    { VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,                VK_KHR_EXTERNAL_FENCE_SPEC_VERSION },
    { VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,             VK_KHR_EXTERNAL_FENCE_FD_SPEC_VERSION },
    /* Vulkan 1.2 promoted */
    { VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,            VK_KHR_TIMELINE_SEMAPHORE_SPEC_VERSION },
    { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,         VK_KHR_BUFFER_DEVICE_ADDRESS_SPEC_VERSION },
    { VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,           VK_KHR_SHADER_FLOAT16_INT8_SPEC_VERSION },
    { VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME,         VK_KHR_IMAGELESS_FRAMEBUFFER_SPEC_VERSION },
    { VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME, VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_SPEC_VERSION },
    /* Swapchain */
    { VK_KHR_SWAPCHAIN_EXTENSION_NAME,                     VK_KHR_SWAPCHAIN_SPEC_VERSION },
};

static const uint32_t k_device_extension_count =
    (uint32_t)(sizeof(k_device_extensions) / sizeof(k_device_extensions[0]));

VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    (void)physicalDevice;

    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    /* Layer-specific query: no layer extensions */
    if (pLayerName != NULL) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }

    if (pProperties == NULL) {
        *pPropertyCount = k_device_extension_count;
        return VK_SUCCESS;
    }

    uint32_t copy_count = (*pPropertyCount < k_device_extension_count)
                          ? *pPropertyCount : k_device_extension_count;
    for (uint32_t i = 0; i < copy_count; i++) {
        pProperties[i] = k_device_extensions[i];
    }

    VkResult result = (*pPropertyCount >= k_device_extension_count)
                      ? VK_SUCCESS : VK_INCOMPLETE;
    *pPropertyCount = copy_count;
    return result;
}

VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    
    (void)physicalDevice;
    (void)pProperties;
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
            dev->queues[total_queues].mvgal_queue_id = UINT32_MAX; /* Not yet bound to scheduler */
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
    mvgal_context_t ctx = mvgal_context_get_current();
    if (ctx) {
        mvgal_wait_idle(ctx, 0); /* 0 = wait indefinitely until idle */
    }
    return VK_SUCCESS;
}

/* ============================================================================
 * mvgal_vkQueueWaitIdle - Wait for queue work to complete
 * ============================================================================ */

VkResult VKAPI_CALL mvgal_vkQueueWaitIdle(VkQueue queue) {
    (void)queue;
    mvgal_context_t ctx = mvgal_context_get_current();
    if (ctx) {
        mvgal_wait_idle(ctx, 0); /* 0 = wait indefinitely until idle */
    }
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
 * Vulkan 1.1 / KHR_get_physical_device_properties2 entry points
 *
 * These are required by virtually every modern Vulkan application and by
 * the Vulkan loader itself when negotiating ICD capabilities.
 * ============================================================================ */

void VKAPI_CALL mvgal_vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2 *pProperties)
{
    if (!pProperties) return;

    /* Fill the core Vulkan 1.0 properties */
    mvgal_vkGetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

    /* Walk the pNext chain and fill known structures */
    void *pNext = pProperties->pNext;
    while (pNext) {
        VkBaseOutStructure *base = (VkBaseOutStructure *)pNext;
        switch ((uint32_t)base->sType) {

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
            VkPhysicalDeviceVulkan11Properties *p11 =
                (VkPhysicalDeviceVulkan11Properties *)base;
            /* deviceUUID / driverUUID — use our virtual UUIDs */
            memcpy(p11->deviceUUID,  pProperties->properties.pipelineCacheUUID, VK_UUID_SIZE);
            memcpy(p11->driverUUID,  pProperties->properties.pipelineCacheUUID, VK_UUID_SIZE);
            p11->driverUUID[4] = 0xD0; /* distinguish driver from device UUID */
            memset(p11->deviceLUID, 0, VK_LUID_SIZE);
            p11->deviceLUID[0] = 0x4D; /* 'M' */
            p11->deviceLUID[1] = 0x56; /* 'V' */
            p11->deviceNodeMask = 1U;
            p11->deviceLUIDValid = VK_TRUE;
            p11->subgroupSize = 32U;
            p11->subgroupSupportedStages =
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT;
            p11->subgroupSupportedOperations =
                VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
                VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
                VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
            p11->subgroupQuadOperationsInAllStages = VK_FALSE;
            p11->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
            p11->maxMultiviewViewCount = 6U;
            p11->maxMultiviewInstanceIndex = 0x7FFFFFFFU;
            p11->protectedNoFault = VK_FALSE;
            p11->maxPerSetDescriptors = 1024U;
            p11->maxMemoryAllocationSize = 0xFFFFFFFFFFFFFFFFULL;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: {
            VkPhysicalDeviceVulkan12Properties *p12 =
                (VkPhysicalDeviceVulkan12Properties *)base;
            p12->driverID = VK_DRIVER_ID_MESA_RADV; /* closest open-source analogue */
            strncpy(p12->driverName, "MVGAL", VK_MAX_DRIVER_NAME_SIZE - 1);
            strncpy(p12->driverInfo, "MVGAL Multi-Vendor GPU Aggregation Layer",
                    VK_MAX_DRIVER_INFO_SIZE - 1);
            p12->conformanceVersion = (VkConformanceVersion){1, 3, 0, 0};
            p12->denormBehaviorIndependence =
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
            p12->roundingModeIndependence =
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
            p12->shaderSignedZeroInfNanPreserveFloat16 = VK_FALSE;
            p12->shaderSignedZeroInfNanPreserveFloat32 = VK_TRUE;
            p12->shaderSignedZeroInfNanPreserveFloat64 = VK_TRUE;
            p12->shaderDenormPreserveFloat16           = VK_FALSE;
            p12->shaderDenormPreserveFloat32           = VK_FALSE;
            p12->shaderDenormPreserveFloat64           = VK_FALSE;
            p12->shaderDenormFlushToZeroFloat16        = VK_FALSE;
            p12->shaderDenormFlushToZeroFloat32        = VK_TRUE;
            p12->shaderDenormFlushToZeroFloat64        = VK_FALSE;
            p12->shaderRoundingModeRTEFloat16          = VK_FALSE;
            p12->shaderRoundingModeRTEFloat32          = VK_TRUE;
            p12->shaderRoundingModeRTEFloat64          = VK_TRUE;
            p12->shaderRoundingModeRTZFloat16          = VK_FALSE;
            p12->shaderRoundingModeRTZFloat32          = VK_FALSE;
            p12->shaderRoundingModeRTZFloat64          = VK_FALSE;
            p12->maxUpdateAfterBindDescriptorsInAllPools = 1000000U;
            p12->shaderUniformBufferArrayNonUniformIndexingNative = VK_FALSE;
            p12->shaderSampledImageArrayNonUniformIndexingNative  = VK_FALSE;
            p12->shaderStorageBufferArrayNonUniformIndexingNative = VK_FALSE;
            p12->shaderStorageImageArrayNonUniformIndexingNative  = VK_FALSE;
            p12->shaderInputAttachmentArrayNonUniformIndexingNative = VK_FALSE;
            p12->robustBufferAccessUpdateAfterBind = VK_FALSE;
            p12->quadDivergentImplicitLod = VK_FALSE;
            p12->maxPerStageDescriptorUpdateAfterBindSamplers        = 500000U;
            p12->maxPerStageDescriptorUpdateAfterBindUniformBuffers  = 12U;
            p12->maxPerStageDescriptorUpdateAfterBindStorageBuffers  = 500000U;
            p12->maxPerStageDescriptorUpdateAfterBindSampledImages   = 500000U;
            p12->maxPerStageDescriptorUpdateAfterBindStorageImages   = 500000U;
            p12->maxPerStageDescriptorUpdateAfterBindInputAttachments = 4U;
            p12->maxPerStageUpdateAfterBindResources                 = 500000U;
            p12->maxDescriptorSetUpdateAfterBindSamplers             = 500000U;
            p12->maxDescriptorSetUpdateAfterBindUniformBuffers       = 72U;
            p12->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 8U;
            p12->maxDescriptorSetUpdateAfterBindStorageBuffers       = 500000U;
            p12->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 4U;
            p12->maxDescriptorSetUpdateAfterBindSampledImages        = 500000U;
            p12->maxDescriptorSetUpdateAfterBindStorageImages        = 500000U;
            p12->maxDescriptorSetUpdateAfterBindInputAttachments     = 4U;
            p12->supportedDepthResolveModes =
                VK_RESOLVE_MODE_SAMPLE_ZERO_BIT | VK_RESOLVE_MODE_AVERAGE_BIT |
                VK_RESOLVE_MODE_MIN_BIT | VK_RESOLVE_MODE_MAX_BIT;
            p12->supportedStencilResolveModes =
                VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
            p12->independentResolveNone = VK_TRUE;
            p12->independentResolve     = VK_FALSE;
            p12->filterMinmaxSingleComponentFormats = VK_TRUE;
            p12->filterMinmaxImageComponentMapping  = VK_FALSE;
            p12->maxTimelineSemaphoreValueDifference = 0x7FFFFFFFFFFFFFFFULL;
            p12->framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
            VkPhysicalDeviceIDProperties *pid =
                (VkPhysicalDeviceIDProperties *)base;
            memcpy(pid->deviceUUID, pProperties->properties.pipelineCacheUUID, VK_UUID_SIZE);
            memcpy(pid->driverUUID, pProperties->properties.pipelineCacheUUID, VK_UUID_SIZE);
            pid->driverUUID[4] = 0xD0;
            memset(pid->deviceLUID, 0, VK_LUID_SIZE);
            pid->deviceLUID[0] = 0x4D;
            pid->deviceLUID[1] = 0x56;
            pid->deviceNodeMask = 1U;
            pid->deviceLUIDValid = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
            VkPhysicalDeviceSubgroupProperties *sg =
                (VkPhysicalDeviceSubgroupProperties *)base;
            sg->subgroupSize = 32U;
            sg->supportedStages =
                VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT |
                VK_SHADER_STAGE_FRAGMENT_BIT;
            sg->supportedOperations =
                VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
                VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT;
            sg->quadOperationsInAllStages = VK_FALSE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
            VkPhysicalDeviceMaintenance3Properties *m3 =
                (VkPhysicalDeviceMaintenance3Properties *)base;
            m3->maxPerSetDescriptors    = 1024U;
            m3->maxMemoryAllocationSize = 0xFFFFFFFFFFFFFFFFULL;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
            VkPhysicalDeviceDriverProperties *drv =
                (VkPhysicalDeviceDriverProperties *)base;
            drv->driverID = VK_DRIVER_ID_MESA_RADV;
            strncpy(drv->driverName, "MVGAL", VK_MAX_DRIVER_NAME_SIZE - 1);
            strncpy(drv->driverInfo, "MVGAL Multi-Vendor GPU Aggregation Layer",
                    VK_MAX_DRIVER_INFO_SIZE - 1);
            drv->conformanceVersion = (VkConformanceVersion){1, 3, 0, 0};
            break;
        }

        default:
            /* Unknown structure — skip */
            break;
        }
        pNext = base->pNext;
    }
}

void VKAPI_CALL mvgal_vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2 *pFeatures)
{
    if (!pFeatures) return;

    /* Fill core Vulkan 1.0 features */
    mvgal_physical_device_t *dev = (mvgal_physical_device_t *)physicalDevice;
    pFeatures->features = dev->features;

    /* Walk pNext chain */
    void *pNext = pFeatures->pNext;
    while (pNext) {
        VkBaseOutStructure *base = (VkBaseOutStructure *)pNext;
        switch ((uint32_t)base->sType) {

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            VkPhysicalDeviceVulkan11Features *f11 =
                (VkPhysicalDeviceVulkan11Features *)base;
            f11->storageBuffer16BitAccess           = VK_TRUE;
            f11->uniformAndStorageBuffer16BitAccess = VK_TRUE;
            f11->storagePushConstant16              = VK_FALSE;
            f11->storageInputOutput16               = VK_FALSE;
            f11->multiview                          = VK_TRUE;
            f11->multiviewGeometryShader            = VK_FALSE;
            f11->multiviewTessellationShader        = VK_FALSE;
            f11->variablePointersStorageBuffer      = VK_TRUE;
            f11->variablePointers                   = VK_TRUE;
            f11->protectedMemory                    = VK_FALSE;
            f11->samplerYcbcrConversion             = VK_TRUE;
            f11->shaderDrawParameters               = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            VkPhysicalDeviceVulkan12Features *f12 =
                (VkPhysicalDeviceVulkan12Features *)base;
            f12->samplerMirrorClampToEdge                       = VK_TRUE;
            f12->drawIndirectCount                              = VK_TRUE;
            f12->storageBuffer8BitAccess                        = VK_TRUE;
            f12->uniformAndStorageBuffer8BitAccess              = VK_TRUE;
            f12->storagePushConstant8                           = VK_FALSE;
            f12->shaderBufferInt64Atomics                       = VK_TRUE;
            f12->shaderSharedInt64Atomics                       = VK_FALSE;
            f12->shaderFloat16                                  = VK_TRUE;
            f12->shaderInt8                                     = VK_TRUE;
            f12->descriptorIndexing                             = VK_TRUE;
            f12->shaderInputAttachmentArrayDynamicIndexing      = VK_TRUE;
            f12->shaderUniformTexelBufferArrayDynamicIndexing   = VK_TRUE;
            f12->shaderStorageTexelBufferArrayDynamicIndexing   = VK_TRUE;
            f12->shaderUniformBufferArrayNonUniformIndexing     = VK_TRUE;
            f12->shaderSampledImageArrayNonUniformIndexing      = VK_TRUE;
            f12->shaderStorageBufferArrayNonUniformIndexing     = VK_TRUE;
            f12->shaderStorageImageArrayNonUniformIndexing      = VK_TRUE;
            f12->shaderInputAttachmentArrayNonUniformIndexing   = VK_TRUE;
            f12->shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE;
            f12->shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE;
            f12->descriptorBindingUniformBufferUpdateAfterBind  = VK_FALSE;
            f12->descriptorBindingSampledImageUpdateAfterBind   = VK_TRUE;
            f12->descriptorBindingStorageImageUpdateAfterBind   = VK_TRUE;
            f12->descriptorBindingStorageBufferUpdateAfterBind  = VK_TRUE;
            f12->descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE;
            f12->descriptorBindingUpdateUnusedWhilePending      = VK_TRUE;
            f12->descriptorBindingPartiallyBound                = VK_TRUE;
            f12->descriptorBindingVariableDescriptorCount       = VK_TRUE;
            f12->runtimeDescriptorArray                         = VK_TRUE;
            f12->samplerFilterMinmax                            = VK_TRUE;
            f12->scalarBlockLayout                              = VK_TRUE;
            f12->imagelessFramebuffer                           = VK_TRUE;
            f12->uniformBufferStandardLayout                    = VK_TRUE;
            f12->shaderSubgroupExtendedTypes                    = VK_TRUE;
            f12->separateDepthStencilLayouts                    = VK_TRUE;
            f12->hostQueryReset                                 = VK_TRUE;
            f12->timelineSemaphore                              = VK_TRUE;
            f12->bufferDeviceAddress                            = VK_TRUE;
            f12->bufferDeviceAddressCaptureReplay               = VK_FALSE;
            f12->bufferDeviceAddressMultiDevice                 = VK_TRUE;
            f12->vulkanMemoryModel                              = VK_TRUE;
            f12->vulkanMemoryModelDeviceScope                   = VK_TRUE;
            f12->vulkanMemoryModelAvailabilityVisibilityChains  = VK_FALSE;
            f12->shaderOutputViewportIndex                      = VK_TRUE;
            f12->shaderOutputLayer                              = VK_TRUE;
            f12->subgroupBroadcastDynamicId                     = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: {
            VkPhysicalDeviceTimelineSemaphoreFeatures *ts =
                (VkPhysicalDeviceTimelineSemaphoreFeatures *)base;
            ts->timelineSemaphore = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: {
            VkPhysicalDeviceBufferDeviceAddressFeatures *bda =
                (VkPhysicalDeviceBufferDeviceAddressFeatures *)base;
            bda->bufferDeviceAddress              = VK_TRUE;
            bda->bufferDeviceAddressCaptureReplay = VK_FALSE;
            bda->bufferDeviceAddressMultiDevice   = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: {
            VkPhysicalDeviceDescriptorIndexingFeatures *di =
                (VkPhysicalDeviceDescriptorIndexingFeatures *)base;
            di->shaderInputAttachmentArrayDynamicIndexing      = VK_TRUE;
            di->shaderUniformTexelBufferArrayDynamicIndexing   = VK_TRUE;
            di->shaderStorageTexelBufferArrayDynamicIndexing   = VK_TRUE;
            di->shaderUniformBufferArrayNonUniformIndexing     = VK_TRUE;
            di->shaderSampledImageArrayNonUniformIndexing      = VK_TRUE;
            di->shaderStorageBufferArrayNonUniformIndexing     = VK_TRUE;
            di->shaderStorageImageArrayNonUniformIndexing      = VK_TRUE;
            di->shaderInputAttachmentArrayNonUniformIndexing   = VK_TRUE;
            di->shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE;
            di->shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE;
            di->descriptorBindingUniformBufferUpdateAfterBind  = VK_FALSE;
            di->descriptorBindingSampledImageUpdateAfterBind   = VK_TRUE;
            di->descriptorBindingStorageImageUpdateAfterBind   = VK_TRUE;
            di->descriptorBindingStorageBufferUpdateAfterBind  = VK_TRUE;
            di->descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE;
            di->descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE;
            di->descriptorBindingUpdateUnusedWhilePending      = VK_TRUE;
            di->descriptorBindingPartiallyBound                = VK_TRUE;
            di->descriptorBindingVariableDescriptorCount       = VK_TRUE;
            di->runtimeDescriptorArray                         = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
            VkPhysicalDeviceMultiviewFeatures *mv =
                (VkPhysicalDeviceMultiviewFeatures *)base;
            mv->multiview                   = VK_TRUE;
            mv->multiviewGeometryShader     = VK_FALSE;
            mv->multiviewTessellationShader = VK_FALSE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
            VkPhysicalDeviceVariablePointersFeatures *vp =
                (VkPhysicalDeviceVariablePointersFeatures *)base;
            vp->variablePointersStorageBuffer = VK_TRUE;
            vp->variablePointers              = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
            VkPhysicalDeviceSamplerYcbcrConversionFeatures *yc =
                (VkPhysicalDeviceSamplerYcbcrConversionFeatures *)base;
            yc->samplerYcbcrConversion = VK_TRUE;
            break;
        }

        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: {
            VkPhysicalDeviceShaderDrawParametersFeatures *sdp =
                (VkPhysicalDeviceShaderDrawParametersFeatures *)base;
            sdp->shaderDrawParameters = VK_TRUE;
            break;
        }

        default:
            break;
        }
        pNext = base->pNext;
    }
}

void VKAPI_CALL mvgal_vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
    if (!pMemoryProperties) return;

    mvgal_vkGetPhysicalDeviceMemoryProperties(physicalDevice,
                                              &pMemoryProperties->memoryProperties);

    /* Walk pNext — no known extension structures need filling for our ICD */
    void *pNext = pMemoryProperties->pNext;
    while (pNext) {
        /* Skip unknown structures */
        pNext = ((VkBaseOutStructure *)pNext)->pNext;
    }
}

/* ============================================================================
 * End of icd_entry.c
 * ============================================================================ */
