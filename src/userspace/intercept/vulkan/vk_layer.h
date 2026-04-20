/**
 * @file vk_layer.h
 * @brief Vulkan Layer Internal Header
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * This header provides internal definitions for the Vulkan layer.
 * It should not be included by external code.
 *
 * When Vulkan SDK is not available, this provides forward declarations
 * and stub implementations to allow compilation without Vulkan headers.
 */

#ifndef VK_LAYER_MVGAL_H
#define VK_LAYER_MVGAL_H

#include <stdint.h>
#include <stdbool.h>

// MVGAL headers
#include "mvgal.h"
#include "mvgal_gpu.h"
#include "mvgal_log.h"
#include "mvgal_scheduler.h"
#include "mvgal_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

// Vulkan layer export macro
#ifndef VK_LAYER_EXPORT
#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_LAYER_EXPORT __attribute__((visibility("default")))
#else
#define VK_LAYER_EXPORT
#endif
#endif

// VKAPI_CALL definition for Linux (empty attribute on Linux, __stdcall on Windows)
// On Linux, Vulkan uses the system calling convention (no special attribute)
#ifndef VKAPI_CALL
#define VKAPI_CALL
#endif

// VKAPI_PTR definition
#ifndef VKAPI_PTR
#define VKAPI_PTR VKAPI_CALL *
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup VulkanLayer
 * @{
 */

// Layer name and version
#define MVGAL_VK_LAYER_NAME "VK_LAYER_MVGAL"
#define MVGAL_VK_LAYER_DESCRIPTION "Multi-Vendor GPU Aggregation Layer"
#define MVGAL_VK_LAYER_VERSION 1
#define MVGAL_VK_API_VERSION 0x00010003  // VK_API_VERSION_1_3

// Maximum number of physical devices
#define MVGAL_MAX_PHYSICAL_DEVICES 16

// Maximum number of queues per device
#define MVGAL_MAX_QUEUES_PER_DEVICE 32

/**
 * @brief Environment variable names
 */
#define ENV_MVGAL_ENABLED "MVGAL_ENABLED"
#define ENV_MVGAL_VULKAN_ENABLED "MVGAL_VULKAN_ENABLED"
#define ENV_MVGAL_VULKAN_DEBUG "MVGAL_VULKAN_DEBUG"
#define ENV_MVGAL_STRATEGY "MVGAL_STRATEGY"
#define ENV_MVGAL_LOG_LEVEL "MVGAL_LOG_LEVEL"

// Forward declarations for Vulkan types
// These allow compilation without Vulkan SDK headers
typedef struct VkInstance_T *VkInstance;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkSwapchainKHR_T *VkSwapchainKHR;
typedef struct VkFence_T *VkFence;
typedef struct VkSemaphore_T *VkSemaphore;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkBuffer_T *VkBuffer;
typedef struct VkImage_T *VkImage;
typedef struct VkDeviceMemory_T *VkDeviceMemory;
typedef struct VkCommandPool_T *VkCommandPool;
typedef uint32_t VkFlags;
typedef int32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkFilter;
typedef uint32_t VkImageLayout;
typedef struct VkAllocationCallbacks_T VkAllocationCallbacks;
typedef struct VkInstanceCreateInfo_T VkInstanceCreateInfo;

// Vulkan result type
typedef int VkResult;
#define VK_SUCCESS 0
#define VK_ERROR_INITIALIZATION_FAILED -3
#define VK_INCOMPLETE 0x40000001
#define VK_EVENT_SET 0x00000001

// Queue flags
typedef uint32_t VkQueueFlags;
#define VK_QUEUE_GRAPHICS_BIT 0x00000001
#define VK_QUEUE_COMPUTE_BIT 0x00000002
#define VK_QUEUE_TRANSFER_BIT 0x00000004

// VkQueueFamilyProperties forward declaration
typedef struct {
    VkQueueFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    void *minImageTransferGranularity;
} VkQueueFamilyProperties;

// VkQueueFamilyProperties2 forward declaration  
typedef struct {
    struct VkBaseOutStructure {
        void *sType;
        void *pNext;
    } sType;
    VkQueueFamilyProperties queueFamilyProperties;
} VkQueueFamilyProperties2;

// VkLayerProperties forward declaration
typedef struct {
    char layerName[256];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char description[256];
} VkLayerProperties;

// Function pointer types (PFN_)
typedef VkResult (*PFN_vkVoidFunction)(void);
typedef VkResult (VKAPI_CALL *PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef void (VKAPI_CALL *PFN_vkDestroyInstance)(VkInstance, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_CALL *PFN_vkEnumerateInstanceLayerProperties)(uint32_t*, VkLayerProperties*);
typedef VkResult (VKAPI_CALL *PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, char*);
typedef VkResult (VKAPI_CALL *PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, void*);
typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceFeatures)(VkPhysicalDevice, void*);
typedef VkResult (VKAPI_CALL *PFN_vkCreateDevice)(VkPhysicalDevice, const void*, const VkAllocationCallbacks*, VkDevice);
typedef void (VKAPI_CALL *PFN_vkDestroyDevice)(VkDevice, const VkAllocationCallbacks*);
typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
typedef void (VKAPI_CALL *PFN_vkGetPhysicalDeviceQueueFamilyProperties2)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties2*);
typedef VkResult (VKAPI_CALL *PFN_vkCreateSwapchainKHR)(VkDevice, const void*, const VkAllocationCallbacks*, VkSwapchainKHR*);
typedef void (VKAPI_CALL *PFN_vkDestroySwapchainKHR)(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_CALL *PFN_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
typedef VkResult (VKAPI_CALL *PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
typedef VkResult (VKAPI_CALL *PFN_vkQueueSubmit)(VkQueue, uint32_t, const void*, VkFence);
typedef VkResult (VKAPI_CALL *PFN_vkQueuePresentKHR)(VkQueue, const void*);
typedef VkResult (VKAPI_CALL *PFN_vkAllocateCommandBuffers)(VkDevice, const void*, VkCommandBuffer*);
typedef void (VKAPI_CALL *PFN_vkFreeCommandBuffers)(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
typedef VkResult (VKAPI_CALL *PFN_vkBeginCommandBuffer)(VkCommandBuffer, const void*);
typedef VkResult (VKAPI_CALL *PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef void (VKAPI_CALL *PFN_vkCmdPipelineBarrier)(VkCommandBuffer, uint32_t, const void*, uint32_t, const void*, uint32_t, const void*, const void*);
typedef VkResult (VKAPI_CALL *PFN_vkCreateFence)(VkDevice, const void*, const VkAllocationCallbacks*, VkFence*);
typedef void (VKAPI_CALL *PFN_vkDestroyFence)(VkDevice, VkFence, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_CALL *PFN_vkWaitForFences)(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
typedef VkResult (VKAPI_CALL *PFN_vkCreateSemaphore)(VkDevice, const void*, const VkAllocationCallbacks*, VkSemaphore*);
typedef void (VKAPI_CALL *PFN_vkDestroySemaphore)(VkDevice, VkSemaphore, const VkAllocationCallbacks());
typedef VkResult (VKAPI_CALL *PFN_vkCreateBuffer)(VkDevice, const void*, const VkAllocationCallbacks*, VkBuffer*);
typedef void (VKAPI_CALL *PFN_vkDestroyBuffer)(VkDevice, VkBuffer, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_CALL *PFN_vkCreateImage)(VkDevice, const void*, const VkAllocationCallbacks*, VkImage*);
typedef void (VKAPI_CALL *PFN_vkDestroyImage)(VkDevice, VkImage, const VkAllocationCallbacks*);
typedef VkResult (VKAPI_CALL *PFN_vkBindBufferMemory)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
typedef VkResult (VKAPI_CALL *PFN_vkBindImageMemory)(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
typedef VkResult (VKAPI_CALL *PFN_vkMapMemory)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, void**);
typedef void (VKAPI_CALL *PFN_vkUnmapMemory)(VkDevice, VkDeviceMemory);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef PFN_vkVoidFunction (VKAPI_CALL *PFN_vkGetDeviceProcAddr)(VkDevice, const char*);

/**
 * @brief MVGAL Vulkan layer state
 */
typedef struct {
    // Layer information
    bool initialized;
    bool enabled;
    bool debug;
    
    // MVGAL context
    mvgal_context_t mvgal_context;
    
    // Vulkan loader functions
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
    
    // Device information
    VkInstance instance;
    VkPhysicalDevice physical_devices[MVGAL_MAX_PHYSICAL_DEVICES];
    uint32_t physical_device_count;
    
    // MVGAL virtual device
    VkPhysicalDevice mvgal_physical_device;
    
    // Device and queues
    VkDevice device;
    VkQueue queues[MVGAL_MAX_QUEUES_PER_DEVICE];
    uint32_t queue_count;
    
    // Swapchain
    VkSwapchainKHR swapchain;
    uint32_t swapchain_image_count;
    VkImage swapchain_images[16];
    
    // Distribution strategy
    mvgal_distribution_strategy_t strategy;
    
    // Synchronization
    pthread_mutex_t mutex;
    
    // Statistics
    uint64_t frames_submitted;
    uint64_t frames_completed;
    uint64_t workloads_submitted;
    
    // GPU information
    mvgal_gpu_descriptor_t gpus[MVGAL_MAX_PHYSICAL_DEVICES];
    uint32_t gpu_count;
    
    // Original function pointers (for pass-through)
    struct {
        PFN_vkCreateInstance vkCreateInstance;
        PFN_vkDestroyInstance vkDestroyInstance;
        PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
        PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
        PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
        PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
        PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
        PFN_vkCreateDevice vkCreateDevice;
        PFN_vkDestroyDevice vkDestroyDevice;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2;
    } original;
    
    // Reserved for future use
    void *user_data;
} mvgal_vk_layer_state_t;

// Global layer state
static mvgal_vk_layer_state_t g_layer_state = {0};

// Logging helpers
static inline void mvgal_vk_log_call(const char *func_name) {
    if (g_layer_state.debug) {
        MVGAL_LOG_DEBUG("VK: %s called", func_name);
    }
}

// Helper macro to load original functions
#define MVGAL_VK_GET_ORIGINAL(func) \
    if (!g_layer_state.original.func) { \
        if (g_layer_state.vkGetInstanceProcAddr && g_layer_state.instance) { \
            g_layer_state.original.func = (PFN_##func)g_layer_state.vkGetInstanceProcAddr(g_layer_state.instance, #func); \
        } \
        if (!g_layer_state.original.func && g_layer_state.vkGetInstanceProcAddr) { \
            g_layer_state.original.func = (PFN_##func)g_layer_state.vkGetInstanceProcAddr(NULL, #func); \
        } \
    }

// Helper macro to call original function
#define MVGAL_VK_CALL_ORIGINAL(func, return_type, ...) \
    MVGAL_VK_GET_ORIGINAL(func); \
    if (g_layer_state.original.func) { \
        return g_layer_state.original.func(__VA_ARGS__); \
    } \
    MVGAL_LOG_ERROR("Original function %s not loaded!", #func); \
    return return_type;

/**
 * @brief Check if layer is enabled
 */
static inline bool mvgal_vk_layer_is_enabled(void) {
    static int checked = 0;
    static int enabled = 0;
    
    if (!checked) {
        const char *env = getenv(ENV_MVGAL_VULKAN_ENABLED);
        enabled = env ? atoi(env) : 1; // Default enabled
        checked = 1;
        
        if (enabled) {
            const char *debug_env = getenv(ENV_MVGAL_VULKAN_DEBUG);
            g_layer_state.debug = debug_env ? atoi(debug_env) : 0;
            
            if (g_layer_state.debug) {
                MVGAL_LOG_INFO("MVGAL Vulkan Layer: DEBUG mode enabled");
            }
        }
    }
    
    return enabled && g_layer_state.enabled;
}

/**
 * @brief Get the current strategy from environment
 */
static inline mvgal_distribution_strategy_t mvgal_vk_get_strategy(void) {
    const char *strategy_env = getenv(ENV_MVGAL_STRATEGY);
    if (strategy_env) {
        if (strcmp(strategy_env, "afr") == 0) return MVGAL_STRATEGY_AFR;
        if (strcmp(strategy_env, "sfr") == 0) return MVGAL_STRATEGY_SFR;
        if (strcmp(strategy_env, "task") == 0) return MVGAL_STRATEGY_TASK;
        if (strcmp(strategy_env, "compute") == 0 || strcmp(strategy_env, "compute_offload") == 0) return MVGAL_STRATEGY_COMPUTE_OFFLOAD;
        if (strcmp(strategy_env, "hybrid") == 0) return MVGAL_STRATEGY_HYBRID;
        if (strcmp(strategy_env, "single") == 0) return MVGAL_STRATEGY_SINGLE_GPU;
    }
    return MVGAL_STRATEGY_HYBRID; // Default
}

/**
 * @brief Initialize layer state
 */
void mvgal_vk_layer_init(void);

/**
 * @brief Shutdown layer state
 */
void mvgal_vk_layer_shutdown(void);

// ProcAddr functions (defined in vk_layer.c)
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vk_layerGetInstanceProcAddr(VkInstance instance, const char *pName);
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vk_layerGetDeviceProcAddr(VkDevice device, const char *pName);

// Initialization functions (defined in vk_layer.c)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif // VK_LAYER_MVGAL_H
