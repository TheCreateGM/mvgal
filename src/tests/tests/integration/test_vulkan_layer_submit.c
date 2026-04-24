#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vulkan/vulkan.h>

#ifndef MVGAL_TEST_LAYER_DIR
#define MVGAL_TEST_LAYER_DIR "."
#endif

#ifndef MVGAL_TEST_VK_ICD_PATH
#define MVGAL_TEST_VK_ICD_PATH "/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
#endif

static int file_contains_submit_log(const char *path)
{
    char line[512];
    FILE *stream = fopen(path, "r");

    if (stream == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), stream) != NULL) {
        if (strstr(line, "vkQueueSubmit submit_count=1") != NULL) {
            fclose(stream);
            return 1;
        }
    }

    fclose(stream);
    return 0;
}

static uint32_t pick_queue_family(VkPhysicalDevice physical_device)
{
    uint32_t count = 0;
    uint32_t selected = UINT32_MAX;
    VkQueueFamilyProperties *properties = NULL;

    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, NULL);
    if (count == 0) {
        return UINT32_MAX;
    }

    properties = calloc(count, sizeof(*properties));
    if (properties == NULL) {
        return UINT32_MAX;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties);
    for (uint32_t i = 0; i < count; ++i) {
        if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
            selected = i;
            break;
        }
        if ((properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U &&
            selected == UINT32_MAX) {
            selected = i;
        }
    }

    free(properties);
    return selected;
}

int main(void)
{
    char log_path[] = "/tmp/mvgal-vk-layer-log-XXXXXX";
    char implicit_path[512];
    VkApplicationInfo app_info;
    VkInstanceCreateInfo instance_info;
    VkDeviceQueueCreateInfo queue_info;
    VkDeviceCreateInfo device_info;
    VkCommandPoolCreateInfo pool_info;
    VkCommandBufferAllocateInfo alloc_info;
    VkCommandBufferBeginInfo begin_info;
    VkFenceCreateInfo fence_info;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkResult result;
    uint32_t physical_count = 1;
    uint32_t queue_family_index;
    float queue_priority = 1.0f;
    int fd;

    fd = mkstemp(log_path);
    if (fd < 0) {
        perror("mkstemp");
        return EXIT_FAILURE;
    }
    close(fd);
    unlink(log_path);

    if (setenv("MVGAL_VULKAN_ENABLE", "1", 1) != 0 ||
        setenv("MVGAL_VULKAN_LOG_PATH", log_path, 1) != 0 ||
        setenv("VK_IMPLICIT_LAYER_PATH", MVGAL_TEST_LAYER_DIR, 1) != 0 ||
        setenv("VK_DRIVER_FILES", MVGAL_TEST_VK_ICD_PATH, 1) != 0 ||
        setenv("VK_ICD_FILENAMES", MVGAL_TEST_VK_ICD_PATH, 1) != 0) {
        perror("setenv");
        return EXIT_FAILURE;
    }

    snprintf(implicit_path, sizeof(implicit_path), "%s/VK_LAYER_MVGAL.json",
             MVGAL_TEST_LAYER_DIR);
    if (access(implicit_path, R_OK) != 0) {
        fprintf(stderr, "missing manifest: %s\n", implicit_path);
        return EXIT_FAILURE;
    }

    memset(&app_info, 0, sizeof(app_info));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "mvgal-layer-smoke";
    app_info.apiVersion = VK_API_VERSION_1_3;

    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;

    result = vkCreateInstance(&instance_info, NULL, &instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", result);
        return EXIT_FAILURE;
    }

    result = vkEnumeratePhysicalDevices(instance, &physical_count, &physical_device);
    if (result != VK_SUCCESS || physical_count == 0) {
        fprintf(stderr, "vkEnumeratePhysicalDevices failed: %d count=%u\n",
                result, physical_count);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    queue_family_index = pick_queue_family(physical_device);
    if (queue_family_index == UINT32_MAX) {
        fprintf(stderr, "failed to find usable queue family\n");
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    memset(&queue_info, 0, sizeof(queue_info));
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_index;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    memset(&device_info, 0, sizeof(device_info));
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    result = vkCreateDevice(physical_device, &device_info, NULL, &device);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDevice failed: %d\n", result);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    vkGetDeviceQueue(device, queue_family_index, 0, &queue);
    if (queue == VK_NULL_HANDLE) {
        fprintf(stderr, "vkGetDeviceQueue returned NULL\n");
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    memset(&pool_info, 0, sizeof(pool_info));
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = queue_family_index;

    result = vkCreateCommandPool(device, &pool_info, NULL, &command_pool);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkCreateCommandPool failed: %d\n", result);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkAllocateCommandBuffers failed: %d\n", result);
        vkDestroyCommandPool(device, command_pool, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (result == VK_SUCCESS) {
        result = vkEndCommandBuffer(command_buffer);
    }
    if (result != VK_SUCCESS) {
        fprintf(stderr, "command buffer record failed: %d\n", result);
        vkDestroyCommandPool(device, command_pool, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    memset(&fence_info, 0, sizeof(fence_info));
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    result = vkCreateFence(device, &fence_info, NULL, &fence);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkCreateFence failed: %d\n", result);
        vkDestroyCommandPool(device, command_pool, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    {
        VkSubmitInfo submit_info;

        memset(&submit_info, 0, sizeof(submit_info));
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;

        result = vkQueueSubmit(queue, 1, &submit_info, fence);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "vkQueueSubmit failed: %d\n", result);
            vkDestroyFence(device, fence, NULL);
            vkDestroyCommandPool(device, command_pool, NULL);
            vkDestroyDevice(device, NULL);
            vkDestroyInstance(instance, NULL);
            return EXIT_FAILURE;
        }
    }

    result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "vkWaitForFences failed: %d\n", result);
        vkDestroyFence(device, fence, NULL);
        vkDestroyCommandPool(device, command_pool, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return EXIT_FAILURE;
    }

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    if (!file_contains_submit_log(log_path)) {
        fprintf(stderr, "layer log did not contain vkQueueSubmit entry\n");
        return EXIT_FAILURE;
    }

    unlink(log_path);
    return EXIT_SUCCESS;
}
