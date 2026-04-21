#include "vk_layer.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

mvgal_layer_state_t g_mvgal_layer_state = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .instances = NULL,
    .devices = NULL,
    .queues = NULL,
    .physical_devices = NULL,
    .submit_count = 0,
};

static bool env_truthy(const char *name)
{
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0') {
        return false;
    }

    if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "off") == 0 || strcasecmp(value, "no") == 0) {
        return false;
    }

    return true;
}

bool mvgal_vk_layer_debug_enabled(void)
{
    return env_truthy("MVGAL_VULKAN_DEBUG") || env_truthy("UMGAL_VULKAN_DEBUG");
}

void mvgal_vk_layer_log(const char *fmt, ...)
{
    FILE *stream = stderr;
    const char *log_path = getenv("MVGAL_VULKAN_LOG_PATH");
    va_list ap;

    if (!mvgal_vk_layer_debug_enabled() && log_path == NULL) {
        return;
    }

    if (log_path != NULL && log_path[0] != '\0') {
        stream = fopen(log_path, "a");
        if (stream == NULL) {
            stream = stderr;
        }
    }

    fprintf(stream, "[mvgal-vk-layer] ");
    va_start(ap, fmt);
    vfprintf(stream, fmt, ap);
    va_end(ap);
    fputc('\n', stream);
    fflush(stream);

    if (stream != stderr) {
        fclose(stream);
    }
}

void mvgal_vk_layer_note_submit(const char *entrypoint, uint32_t submit_count)
{
    uint64_t total = atomic_fetch_add_explicit(
        &g_mvgal_layer_state.submit_count,
        1U,
        memory_order_relaxed
    ) + 1U;

    mvgal_vk_layer_log(
        "%s submit_count=%u total_submissions=%" PRIu64,
        entrypoint,
        submit_count,
        total
    );
}

static void fill_layer_properties(VkLayerProperties *properties)
{
    memset(properties, 0, sizeof(*properties));
    snprintf(properties->layerName, sizeof(properties->layerName), "%s",
             MVGAL_VK_LAYER_NAME);
    properties->specVersion = VK_API_VERSION_1_3;
    properties->implementationVersion = 1;
    snprintf(properties->description, sizeof(properties->description), "%s",
             MVGAL_VK_LAYER_DESCRIPTION);
}

static bool layer_name_matches(const char *layer_name)
{
    return layer_name != NULL &&
           strcmp(layer_name, MVGAL_VK_LAYER_NAME) == 0;
}

static void remove_queues_for_device_locked(mvgal_device_dispatch_t *device_dispatch)
{
    mvgal_queue_dispatch_t **cursor = &g_mvgal_layer_state.queues;

    while (*cursor != NULL) {
        if ((*cursor)->device_dispatch == device_dispatch) {
            mvgal_queue_dispatch_t *victim = *cursor;

            *cursor = victim->next;
            free(victim);
            continue;
        }

        cursor = &(*cursor)->next;
    }
}

static void remove_physical_devices_for_instance_locked(VkInstance instance)
{
    mvgal_physical_device_dispatch_t **cursor = &g_mvgal_layer_state.physical_devices;

    while (*cursor != NULL) {
        if ((*cursor)->instance == instance) {
            mvgal_physical_device_dispatch_t *victim = *cursor;

            *cursor = victim->next;
            free(victim);
            continue;
        }

        cursor = &(*cursor)->next;
    }
}

mvgal_instance_dispatch_t *mvgal_vk_find_instance_dispatch(VkInstance instance)
{
    mvgal_instance_dispatch_t *dispatch;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    dispatch = g_mvgal_layer_state.instances;
    while (dispatch != NULL && dispatch->instance != instance) {
        dispatch = dispatch->next;
    }
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return dispatch;
}

mvgal_device_dispatch_t *mvgal_vk_find_device_dispatch(VkDevice device)
{
    mvgal_device_dispatch_t *dispatch;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    dispatch = g_mvgal_layer_state.devices;
    while (dispatch != NULL && dispatch->device != device) {
        dispatch = dispatch->next;
    }
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return dispatch;
}

mvgal_queue_dispatch_t *mvgal_vk_find_queue_dispatch(VkQueue queue)
{
    mvgal_queue_dispatch_t *dispatch;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    dispatch = g_mvgal_layer_state.queues;
    while (dispatch != NULL && dispatch->queue != queue) {
        dispatch = dispatch->next;
    }
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return dispatch;
}

mvgal_physical_device_dispatch_t *mvgal_vk_find_physical_device_dispatch(
    VkPhysicalDevice physical_device
)
{
    mvgal_physical_device_dispatch_t *dispatch;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    dispatch = g_mvgal_layer_state.physical_devices;
    while (dispatch != NULL && dispatch->physical_device != physical_device) {
        dispatch = dispatch->next;
    }
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return dispatch;
}

VkResult mvgal_vk_register_instance(
    VkInstance instance,
    PFN_vkGetInstanceProcAddr next_gipa,
    PFN_GetPhysicalDeviceProcAddr next_gpipa
)
{
    mvgal_instance_dispatch_t *dispatch;

    dispatch = calloc(1, sizeof(*dispatch));
    if (dispatch == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    dispatch->instance = instance;
    dispatch->next_gipa = next_gipa;
    dispatch->next_gpipa = next_gpipa;
    dispatch->destroy_instance =
        (PFN_vkDestroyInstance)next_gipa(instance, "vkDestroyInstance");
    dispatch->create_device =
        (PFN_vkCreateDevice)next_gipa(instance, "vkCreateDevice");

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    dispatch->next = g_mvgal_layer_state.instances;
    g_mvgal_layer_state.instances = dispatch;
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return VK_SUCCESS;
}

VkResult mvgal_vk_register_device(
    VkDevice device,
    PFN_vkGetDeviceProcAddr next_gdpa
)
{
    mvgal_device_dispatch_t *dispatch;

    dispatch = calloc(1, sizeof(*dispatch));
    if (dispatch == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    dispatch->device = device;
    dispatch->next_gdpa = next_gdpa;
    dispatch->destroy_device =
        (PFN_vkDestroyDevice)next_gdpa(device, "vkDestroyDevice");
    dispatch->get_device_queue =
        (PFN_vkGetDeviceQueue)next_gdpa(device, "vkGetDeviceQueue");
    dispatch->get_device_queue2 =
        (PFN_vkGetDeviceQueue2)next_gdpa(device, "vkGetDeviceQueue2");
    dispatch->queue_submit =
        (PFN_vkQueueSubmit)next_gdpa(device, "vkQueueSubmit");
    dispatch->queue_submit2 =
        (PFN_vkQueueSubmit2)next_gdpa(device, "vkQueueSubmit2");
    dispatch->queue_submit2_khr =
        (PFN_vkQueueSubmit2KHR)next_gdpa(device, "vkQueueSubmit2KHR");

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    dispatch->next = g_mvgal_layer_state.devices;
    g_mvgal_layer_state.devices = dispatch;
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return VK_SUCCESS;
}

VkResult mvgal_vk_register_queue(
    VkQueue queue,
    mvgal_device_dispatch_t *device_dispatch
)
{
    mvgal_queue_dispatch_t *dispatch;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    dispatch = g_mvgal_layer_state.queues;
    while (dispatch != NULL) {
        if (dispatch->queue == queue) {
            dispatch->device_dispatch = device_dispatch;
            pthread_mutex_unlock(&g_mvgal_layer_state.lock);
            return VK_SUCCESS;
        }
        dispatch = dispatch->next;
    }

    dispatch = calloc(1, sizeof(*dispatch));
    if (dispatch == NULL) {
        pthread_mutex_unlock(&g_mvgal_layer_state.lock);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    dispatch->queue = queue;
    dispatch->device_dispatch = device_dispatch;
    dispatch->next = g_mvgal_layer_state.queues;
    g_mvgal_layer_state.queues = dispatch;
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return VK_SUCCESS;
}

VkResult mvgal_vk_register_physical_devices(
    VkInstance instance,
    mvgal_instance_dispatch_t *instance_dispatch,
    uint32_t physical_device_count,
    const VkPhysicalDevice *physical_devices
)
{
    uint32_t i;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    for (i = 0; i < physical_device_count; ++i) {
        mvgal_physical_device_dispatch_t *dispatch =
            g_mvgal_layer_state.physical_devices;

        while (dispatch != NULL) {
            if (dispatch->physical_device == physical_devices[i]) {
                dispatch->instance = instance;
                dispatch->instance_dispatch = instance_dispatch;
                break;
            }
            dispatch = dispatch->next;
        }

        if (dispatch == NULL) {
            dispatch = calloc(1, sizeof(*dispatch));
            if (dispatch == NULL) {
                pthread_mutex_unlock(&g_mvgal_layer_state.lock);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }

            dispatch->physical_device = physical_devices[i];
            dispatch->instance = instance;
            dispatch->instance_dispatch = instance_dispatch;
            dispatch->next = g_mvgal_layer_state.physical_devices;
            g_mvgal_layer_state.physical_devices = dispatch;
        }
    }
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);

    return VK_SUCCESS;
}

void mvgal_vk_unregister_instance(VkInstance instance)
{
    mvgal_instance_dispatch_t **cursor;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    cursor = &g_mvgal_layer_state.instances;
    while (*cursor != NULL) {
        if ((*cursor)->instance == instance) {
            mvgal_instance_dispatch_t *victim = *cursor;

            *cursor = victim->next;
            remove_physical_devices_for_instance_locked(instance);
            free(victim);
            break;
        }
        cursor = &(*cursor)->next;
    }
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);
}

void mvgal_vk_unregister_device(VkDevice device)
{
    mvgal_device_dispatch_t **cursor;

    pthread_mutex_lock(&g_mvgal_layer_state.lock);
    cursor = &g_mvgal_layer_state.devices;
    while (*cursor != NULL) {
        if ((*cursor)->device == device) {
            mvgal_device_dispatch_t *victim = *cursor;

            *cursor = victim->next;
            remove_queues_for_device_locked(victim);
            free(victim);
            break;
        }
        cursor = &(*cursor)->next;
    }
    pthread_mutex_unlock(&g_mvgal_layer_state.lock);
}

VkLayerInstanceCreateInfo *mvgal_vk_find_instance_layer_info(
    const VkInstanceCreateInfo *create_info,
    VkLayerFunction function
)
{
    VkLayerInstanceCreateInfo *info;

    info = (VkLayerInstanceCreateInfo *)create_info->pNext;
    while (info != NULL) {
        if (info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
            info->function == function) {
            return info;
        }

        info = (VkLayerInstanceCreateInfo *)info->pNext;
    }

    return NULL;
}

VkLayerDeviceCreateInfo *mvgal_vk_find_device_layer_info(
    const VkDeviceCreateInfo *create_info,
    VkLayerFunction function
)
{
    VkLayerDeviceCreateInfo *info;

    info = (VkLayerDeviceCreateInfo *)create_info->pNext;
    while (info != NULL) {
        if (info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
            info->function == function) {
            return info;
        }

        info = (VkLayerDeviceCreateInfo *)info->pNext;
    }

    return NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties
)
{
    if (pPropertyCount == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pProperties == NULL) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }

    if (*pPropertyCount == 0) {
        return VK_INCOMPLETE;
    }

    fill_layer_properties(&pProperties[0]);
    *pPropertyCount = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physical_device,
    uint32_t *pPropertyCount,
    VkLayerProperties *pProperties
)
{
    (void)physical_device;
    return vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties
)
{
    if (!layer_name_matches(pLayerName)) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (pPropertyCount != NULL) {
        *pPropertyCount = 0;
    }
    (void)pProperties;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physical_device,
    const char *pLayerName,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties
)
{
    mvgal_physical_device_dispatch_t *dispatch;
    PFN_vkEnumerateDeviceExtensionProperties next_enumerate;

    if (layer_name_matches(pLayerName)) {
        if (pPropertyCount != NULL) {
            *pPropertyCount = 0;
        }
        (void)pProperties;
        return VK_SUCCESS;
    }

    dispatch = mvgal_vk_find_physical_device_dispatch(physical_device);
    if (dispatch == NULL || dispatch->instance_dispatch == NULL ||
        dispatch->instance_dispatch->next_gipa == NULL) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    next_enumerate = (PFN_vkEnumerateDeviceExtensionProperties)
        dispatch->instance_dispatch->next_gipa(
            dispatch->instance,
            "vkEnumerateDeviceExtensionProperties"
        );
    if (next_enumerate == NULL) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    return next_enumerate(
        physical_device,
        pLayerName,
        pPropertyCount,
        pProperties
    );
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    if (pApiVersion == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *pApiVersion = VK_API_VERSION_1_3;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance *pInstance
)
{
    VkLayerInstanceCreateInfo *layer_info;
    PFN_vkGetInstanceProcAddr next_gipa;
    PFN_GetPhysicalDeviceProcAddr next_gpipa;
    PFN_vkCreateInstance next_create_instance;
    VkResult result;

    layer_info = mvgal_vk_find_instance_layer_info(
        pCreateInfo,
        VK_LAYER_LINK_INFO
    );
    if (layer_info == NULL || layer_info->u.pLayerInfo == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    next_gipa = layer_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    next_gpipa = layer_info->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    layer_info->u.pLayerInfo = layer_info->u.pLayerInfo->pNext;

    next_create_instance =
        (PFN_vkCreateInstance)next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (next_create_instance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    result = next_create_instance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS) {
        return result;
    }

    result = mvgal_vk_register_instance(*pInstance, next_gipa, next_gpipa);
    if (result != VK_SUCCESS) {
        mvgal_instance_dispatch_t *dispatch = mvgal_vk_find_instance_dispatch(*pInstance);
        if (dispatch != NULL && dispatch->destroy_instance != NULL) {
            dispatch->destroy_instance(*pInstance, pAllocator);
        }
        return result;
    }

    mvgal_vk_layer_log("created instance=%p", (void *)*pInstance);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks *pAllocator
)
{
    mvgal_instance_dispatch_t *dispatch = mvgal_vk_find_instance_dispatch(instance);

    if (dispatch != NULL && dispatch->destroy_instance != NULL) {
        dispatch->destroy_instance(instance, pAllocator);
    }

    mvgal_vk_unregister_instance(instance);
    mvgal_vk_layer_log("destroyed instance=%p", (void *)instance);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDevice *pDevice
)
{
    VkLayerDeviceCreateInfo *layer_info;
    PFN_vkGetInstanceProcAddr next_gipa;
    PFN_vkGetDeviceProcAddr next_gdpa;
    PFN_vkCreateDevice next_create_device;
    VkResult result;

    layer_info = mvgal_vk_find_device_layer_info(
        pCreateInfo,
        VK_LAYER_LINK_INFO
    );
    if (layer_info == NULL || layer_info->u.pLayerInfo == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    next_gipa = layer_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    next_gdpa = layer_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    layer_info->u.pLayerInfo = layer_info->u.pLayerInfo->pNext;

    next_create_device =
        (PFN_vkCreateDevice)next_gipa(VK_NULL_HANDLE, "vkCreateDevice");
    if (next_create_device == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    result = next_create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        return result;
    }

    result = mvgal_vk_register_device(*pDevice, next_gdpa);
    if (result != VK_SUCCESS) {
        return result;
    }

    mvgal_vk_layer_log("created device=%p", (void *)*pDevice);
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t *pPhysicalDeviceCount,
    VkPhysicalDevice *pPhysicalDevices
)
{
    mvgal_instance_dispatch_t *dispatch = mvgal_vk_find_instance_dispatch(instance);
    PFN_vkEnumeratePhysicalDevices next_enumerate;
    VkResult result;

    if (dispatch == NULL || dispatch->next_gipa == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    next_enumerate = (PFN_vkEnumeratePhysicalDevices)
        dispatch->next_gipa(instance, "vkEnumeratePhysicalDevices");
    if (next_enumerate == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    result = next_enumerate(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) &&
        pPhysicalDeviceCount != NULL &&
        pPhysicalDevices != NULL) {
        VkResult map_result = mvgal_vk_register_physical_devices(
            instance,
            dispatch,
            *pPhysicalDeviceCount,
            pPhysicalDevices
        );
        if (map_result != VK_SUCCESS) {
            return map_result;
        }
    }

    return result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks *pAllocator
)
{
    mvgal_device_dispatch_t *dispatch = mvgal_vk_find_device_dispatch(device);

    if (dispatch != NULL && dispatch->destroy_device != NULL) {
        dispatch->destroy_device(device, pAllocator);
    }

    mvgal_vk_unregister_device(device);
    mvgal_vk_layer_log("destroyed device=%p", (void *)device);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue *pQueue
)
{
    mvgal_device_dispatch_t *dispatch = mvgal_vk_find_device_dispatch(device);

    if (dispatch == NULL || dispatch->get_device_queue == NULL) {
        if (pQueue != NULL) {
            *pQueue = VK_NULL_HANDLE;
        }
        return;
    }

    dispatch->get_device_queue(device, queueFamilyIndex, queueIndex, pQueue);
    if (pQueue != NULL && *pQueue != VK_NULL_HANDLE) {
        (void)mvgal_vk_register_queue(*pQueue, dispatch);
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(
    VkDevice device,
    const VkDeviceQueueInfo2 *pQueueInfo,
    VkQueue *pQueue
)
{
    mvgal_device_dispatch_t *dispatch = mvgal_vk_find_device_dispatch(device);

    if (dispatch == NULL || dispatch->get_device_queue2 == NULL) {
        if (pQueue != NULL) {
            *pQueue = VK_NULL_HANDLE;
        }
        return;
    }

    dispatch->get_device_queue2(device, pQueueInfo, pQueue);
    if (pQueue != NULL && *pQueue != VK_NULL_HANDLE) {
        (void)mvgal_vk_register_queue(*pQueue, dispatch);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo *pSubmits,
    VkFence fence
)
{
    mvgal_queue_dispatch_t *queue_dispatch = mvgal_vk_find_queue_dispatch(queue);

    if (queue_dispatch == NULL ||
        queue_dispatch->device_dispatch == NULL ||
        queue_dispatch->device_dispatch->queue_submit == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_layer_note_submit("vkQueueSubmit", submitCount);
    return queue_dispatch->device_dispatch->queue_submit(
        queue,
        submitCount,
        pSubmits,
        fence
    );
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo2 *pSubmits,
    VkFence fence
)
{
    mvgal_queue_dispatch_t *queue_dispatch = mvgal_vk_find_queue_dispatch(queue);

    if (queue_dispatch == NULL ||
        queue_dispatch->device_dispatch == NULL ||
        queue_dispatch->device_dispatch->queue_submit2 == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_layer_note_submit("vkQueueSubmit2", submitCount);
    return queue_dispatch->device_dispatch->queue_submit2(
        queue,
        submitCount,
        pSubmits,
        fence
    );
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo2 *pSubmits,
    VkFence fence
)
{
    mvgal_queue_dispatch_t *queue_dispatch = mvgal_vk_find_queue_dispatch(queue);

    if (queue_dispatch == NULL ||
        queue_dispatch->device_dispatch == NULL ||
        queue_dispatch->device_dispatch->queue_submit2_khr == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_layer_note_submit("vkQueueSubmit2KHR", submitCount);
    return queue_dispatch->device_dispatch->queue_submit2_khr(
        queue,
        submitCount,
        pSubmits,
        fence
    );
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_layerGetPhysicalDeviceProcAddr(
    VkInstance instance,
    const char *pName
)
{
    mvgal_instance_dispatch_t *dispatch = mvgal_vk_find_instance_dispatch(instance);

    (void)pName;
    if (dispatch == NULL || dispatch->next_gpipa == NULL) {
        return NULL;
    }

    return dispatch->next_gpipa(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char *pName
)
{
    mvgal_device_dispatch_t *dispatch;

    if (pName == NULL) {
        return NULL;
    }

    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        return (PFN_vkVoidFunction)vkDestroyDevice;
    }
    if (strcmp(pName, "vkGetDeviceQueue") == 0) {
        return (PFN_vkVoidFunction)vkGetDeviceQueue;
    }
    if (strcmp(pName, "vkGetDeviceQueue2") == 0) {
        return (PFN_vkVoidFunction)vkGetDeviceQueue2;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueSubmit2") == 0) {
        return (PFN_vkVoidFunction)vkQueueSubmit2;
    }
    if (strcmp(pName, "vkQueueSubmit2KHR") == 0) {
        return (PFN_vkVoidFunction)vkQueueSubmit2KHR;
    }

    dispatch = mvgal_vk_find_device_dispatch(device);
    if (dispatch == NULL || dispatch->next_gdpa == NULL) {
        return NULL;
    }

    return dispatch->next_gdpa(device, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char *pName
)
{
    mvgal_instance_dispatch_t *dispatch;

    if (pName == NULL) {
        return NULL;
    }

    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }
    if (strcmp(pName, "vkNegotiateLoaderLayerInterfaceVersion") == 0) {
        return (PFN_vkVoidFunction)vkNegotiateLoaderLayerInterfaceVersion;
    }
    if (strcmp(pName, "vk_layerGetPhysicalDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)vk_layerGetPhysicalDeviceProcAddr;
    }
    if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
    }
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    }
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateInstanceVersion;
    }
    if (strcmp(pName, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)vkCreateInstance;
    }
    if (strcmp(pName, "vkDestroyInstance") == 0) {
        return (PFN_vkVoidFunction)vkDestroyInstance;
    }
    if (strcmp(pName, "vkCreateDevice") == 0) {
        return (PFN_vkVoidFunction)vkCreateDevice;
    }
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) {
        return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        return (PFN_vkVoidFunction)vkDestroyDevice;
    }
    if (strcmp(pName, "vkGetDeviceQueue") == 0) {
        return (PFN_vkVoidFunction)vkGetDeviceQueue;
    }
    if (strcmp(pName, "vkGetDeviceQueue2") == 0) {
        return (PFN_vkVoidFunction)vkGetDeviceQueue2;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueSubmit2") == 0) {
        return (PFN_vkVoidFunction)vkQueueSubmit2;
    }
    if (strcmp(pName, "vkQueueSubmit2KHR") == 0) {
        return (PFN_vkVoidFunction)vkQueueSubmit2KHR;
    }

    dispatch = mvgal_vk_find_instance_dispatch(instance);
    if (dispatch == NULL || dispatch->next_gipa == NULL) {
        return NULL;
    }

    return dispatch->next_gipa(instance, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface *pVersionStruct
)
{
    if (pVersionStruct == NULL ||
        pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pVersionStruct->loaderLayerInterfaceVersion >
        CURRENT_LOADER_LAYER_INTERFACE_VERSION) {
        pVersionStruct->loaderLayerInterfaceVersion =
            CURRENT_LOADER_LAYER_INTERFACE_VERSION;
    }

    if (pVersionStruct->loaderLayerInterfaceVersion <
        MIN_SUPPORTED_LOADER_LAYER_INTERFACE_VERSION) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr =
        vk_layerGetPhysicalDeviceProcAddr;

    return VK_SUCCESS;
}
