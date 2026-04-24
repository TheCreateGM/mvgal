/*
 * amd_prototype.c
 *
 * Simple MVGAL AMD prototype that enumerates Vulkan physical devices
 * and reports support for external memory/semaphore extensions
 * (best-effort diagnostic utility).
 *
 * This program intentionally performs read-only queries and prints a
 * human-readable summary of each physical device and whether it
 * advertises the following extensions:
 *
 *  - VK_KHR_external_memory
 *  - VK_KHR_external_memory_fd
 *  - VK_EXT_external_memory_dma_buf
 *  - VK_KHR_external_semaphore
 *  - VK_KHR_external_semaphore_fd
 *
 * The presence of these extensions is necessary (but not sufficient)
 * for zero-copy cross-device sharing via dma-buf/syncfd on Linux.
 *
 * Build:
 *   This file expects the Vulkan headers and loader to be available.
 *   If you configure the project with CMake and the Vulkan SDK is found,
 *   it will be linked and built automatically.
 *
 * Usage:
 *   ./mvgal_amd_external_mem
 *
 * Notes:
 * - This tool is a diagnostic/proof-of-concept; it does not perform
 *   any memory allocation, export, or import. It only reports capability.
 * - On systems with multiple ICDs, the loader will enumerate all physical
 *   devices available through the installed ICDs (AMD, Intel, NVIDIA, etc).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef __has_include
#  if __has_include(<vulkan/vulkan.h>)
#    include <vulkan/vulkan.h>
#    define HAVE_VULKAN_H 1
#  else
#    define HAVE_VULKAN_H 0
#  endif
#else
#  include <vulkan/vulkan.h>
#  define HAVE_VULKAN_H 1
#endif

#if !HAVE_VULKAN_H
int main(void)
{
    fprintf(stderr, "Vulkan headers not available - cannot run AMD prototype.\n");
    fprintf(stderr, "Install the Vulkan SDK or system Vulkan development package.\n");
    return 2;
}
#else

static int has_extension(const char *name,
                         const VkExtensionProperties *exts,
                         uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(name, exts[i].extensionName) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    VkResult res;
    VkInstance instance = VK_NULL_HANDLE;

    /* Application/instance info - minimal and safe */
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "MVGAL AMD Prototype",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "MVGAL-Prototype",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL,
    };

    res = vkCreateInstance(&inst_info, NULL, &instance);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", res);
        return 1;
    }

    /* Enumerate physical devices */
    uint32_t phys_count = 0;
    res = vkEnumeratePhysicalDevices(instance, &phys_count, NULL);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkEnumeratePhysicalDevices (count) failed: %d\n", res);
        vkDestroyInstance(instance, NULL);
        return 1;
    }

    if (phys_count == 0) {
        printf("No Vulkan physical devices found.\n");
        vkDestroyInstance(instance, NULL);
        return 0;
    }

    VkPhysicalDevice *phys_devs = (VkPhysicalDevice*)calloc((size_t)phys_count, sizeof(VkPhysicalDevice));
    if (phys_devs == NULL) {
        fprintf(stderr, "Out of memory\n");
        vkDestroyInstance(instance, NULL);
        return 1;
    }

    res = vkEnumeratePhysicalDevices(instance, &phys_count, phys_devs);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkEnumeratePhysicalDevices failed: %d\n", res);
        free(phys_devs);
        vkDestroyInstance(instance, NULL);
        return 1;
    }

    printf("Found %u Vulkan physical device(s)\n\n", phys_count);

    for (uint32_t i = 0; i < phys_count; ++i) {
        VkPhysicalDevice pd = phys_devs[i];

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);

        VkPhysicalDeviceMemoryProperties memprops;
        vkGetPhysicalDeviceMemoryProperties(pd, &memprops);

        /* Queue families: count only */
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, NULL);

        printf("PhysicalDevice[%u]: %s\n", i, props.deviceName);
        printf("  Vulkan API version: %u.%u.%u\n",
               VK_VERSION_MAJOR(props.apiVersion),
               VK_VERSION_MINOR(props.apiVersion),
               VK_VERSION_PATCH(props.apiVersion));
        printf("  Vendor ID: 0x%04x  Device ID: 0x%04x\n",
               props.vendorID, props.deviceID);
        printf("  Type: ");
        switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: printf("Integrated GPU\n"); break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   printf("Discrete GPU\n");   break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    printf("Virtual GPU\n");    break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            printf("CPU\n");           break;
        default:                                     printf("Other\n");         break;
        }
        printf("  Queue families: %u\n", qf_count);
        printf("  Memory heaps: %u  memory types: %u\n", memprops.memoryHeapCount, memprops.memoryTypeCount);

        /* Enumerate device extensions */
        uint32_t ext_count = 0;
        res = vkEnumerateDeviceExtensionProperties(pd, NULL, &ext_count, NULL);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "  vkEnumerateDeviceExtensionProperties failed (count): %d\n", res);
            printf("\n");
            continue;
        }

        VkExtensionProperties *exts = NULL;
        if (ext_count > 0) {
            exts = (VkExtensionProperties*)calloc((size_t)ext_count, sizeof(VkExtensionProperties));
            if (exts == NULL) {
                fprintf(stderr, "  Out of memory while enumerating extensions\n");
                printf("\n");
                continue;
            }
            res = vkEnumerateDeviceExtensionProperties(pd, NULL, &ext_count, exts);
            if (res != VK_SUCCESS) {
                fprintf(stderr, "  vkEnumerateDeviceExtensionProperties failed: %d\n", res);
                free(exts);
                printf("\n");
                continue;
            }
        }

        /* Extensions we care about */
        const char *ext_names[] = {
            "VK_KHR_external_memory",
            "VK_KHR_external_memory_fd",
            "VK_EXT_external_memory_dma_buf",
            "VK_KHR_external_semaphore",
            "VK_KHR_external_semaphore_fd",
        };
        const size_t ext_names_count = sizeof(ext_names) / sizeof(ext_names[0]);

        printf("  Supported device extensions (%u):\n", ext_count);
        for (uint32_t e = 0; e < ext_count; ++e) {
            printf("    %s (spec %u)\n", exts[e].extensionName, exts[e].specVersion);
        }

        /* Report presence of target extensions */
        printf("  External memory/semaphore capability summary:\n");
        for (size_t t = 0; t < ext_names_count; ++t) {
            int ok = has_extension(ext_names[t], exts, ext_count);
            printf("    %-35s : %s\n", ext_names[t], ok ? "YES" : "no");
        }

        if (exts) free(exts);

        printf("\n");
    }

    free(phys_devs);

    vkDestroyInstance(instance, NULL);
    return 0;
}

#endif /* HAVE_VULKAN_H */