#ifndef MVGAL_VK_LAYER_H
#define MVGAL_VK_LAYER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#define MVGAL_VK_LAYER_NAME "VK_LAYER_MVGAL"
#define MVGAL_VK_LAYER_DESCRIPTION \
    "MVGAL Vulkan interception layer for multi-vendor GPU aggregation"

/* Maximum physical devices we track per instance */
#define MVGAL_VK_MAX_PHYSICAL_DEVICES 32U

typedef struct mvgal_instance_dispatch mvgal_instance_dispatch_t;
typedef struct mvgal_device_dispatch mvgal_device_dispatch_t;
typedef struct mvgal_queue_dispatch mvgal_queue_dispatch_t;
typedef struct mvgal_physical_device_dispatch mvgal_physical_device_dispatch_t;

struct mvgal_instance_dispatch {
    VkInstance instance;
    PFN_vkGetInstanceProcAddr next_gipa;
    PFN_GetPhysicalDeviceProcAddr next_gpipa;
    PFN_vkDestroyInstance destroy_instance;
    PFN_vkCreateDevice create_device;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices;
    PFN_vkGetPhysicalDeviceProperties get_physical_device_properties;
    PFN_vkGetPhysicalDeviceFeatures get_physical_device_features;
    PFN_vkGetPhysicalDeviceMemoryProperties get_physical_device_memory_properties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_physical_device_queue_family_properties;
    PFN_vkGetPhysicalDeviceProperties2 get_physical_device_properties2;
    PFN_vkGetPhysicalDeviceFeatures2 get_physical_device_features2;
    PFN_vkGetPhysicalDeviceMemoryProperties2 get_physical_device_memory_properties2;
    mvgal_instance_dispatch_t *next;
};

struct mvgal_device_dispatch {
    VkDevice device;
    PFN_vkGetDeviceProcAddr next_gdpa;
    PFN_vkDestroyDevice destroy_device;
    PFN_vkGetDeviceQueue get_device_queue;
    PFN_vkGetDeviceQueue2 get_device_queue2;
    PFN_vkQueueSubmit queue_submit;
    PFN_vkQueueSubmit2 queue_submit2;
    PFN_vkQueueSubmit2KHR queue_submit2_khr;
    mvgal_device_dispatch_t *next;
};

struct mvgal_queue_dispatch {
    VkQueue queue;
    mvgal_device_dispatch_t *device_dispatch;
    mvgal_queue_dispatch_t *next;
};

struct mvgal_physical_device_dispatch {
    VkPhysicalDevice physical_device;
    VkInstance instance;
    mvgal_instance_dispatch_t *instance_dispatch;
    /* Cached properties for telemetry / future aggregation */
    VkPhysicalDeviceProperties properties;
    bool properties_cached;
    mvgal_physical_device_dispatch_t *next;
};

typedef struct mvgal_layer_state {
    pthread_mutex_t lock;
    mvgal_instance_dispatch_t *instances;
    mvgal_device_dispatch_t *devices;
    mvgal_queue_dispatch_t *queues;
    mvgal_physical_device_dispatch_t *physical_devices;
    atomic_uint_fast64_t submit_count;
    atomic_uint_fast64_t physical_device_count;
} mvgal_layer_state_t;

extern mvgal_layer_state_t g_mvgal_layer_state;

bool mvgal_vk_layer_debug_enabled(void);
void mvgal_vk_layer_log(const char *fmt, ...);
void mvgal_vk_layer_note_submit(const char *entrypoint, uint32_t submit_count);

mvgal_instance_dispatch_t *mvgal_vk_find_instance_dispatch(VkInstance instance);
mvgal_device_dispatch_t *mvgal_vk_find_device_dispatch(VkDevice device);
mvgal_queue_dispatch_t *mvgal_vk_find_queue_dispatch(VkQueue queue);
mvgal_physical_device_dispatch_t *mvgal_vk_find_physical_device_dispatch(
    VkPhysicalDevice physical_device
);

VkResult mvgal_vk_register_instance(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr next_gipa,
    PFN_GetPhysicalDeviceProcAddr next_gpipa
);
VkResult mvgal_vk_register_device(
    VkDevice device,
    PFN_vkGetDeviceProcAddr next_gdpa
);
VkResult mvgal_vk_register_queue(
    VkQueue queue,
    mvgal_device_dispatch_t *device_dispatch
);
VkResult mvgal_vk_register_physical_devices(
    VkInstance instance,
    mvgal_instance_dispatch_t *instance_dispatch,
    uint32_t physical_device_count,
    const VkPhysicalDevice *physical_devices
);
void mvgal_vk_unregister_instance(VkInstance instance);
void mvgal_vk_unregister_device(VkDevice device);

VkLayerInstanceCreateInfo *mvgal_vk_find_instance_layer_info(
    const VkInstanceCreateInfo *create_info,
    VkLayerFunction function
);
VkLayerDeviceCreateInfo *mvgal_vk_find_device_layer_info(
    const VkDeviceCreateInfo *create_info,
    VkLayerFunction function
);

/* Physical device property caching */
void mvgal_vk_cache_physical_device_properties(
    mvgal_physical_device_dispatch_t *dispatch
);

#endif
