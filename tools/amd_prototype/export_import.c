/*
 * export_import.c
 *
 * Proof-of-concept: Vulkan external memory export (FD) on source device and
 * import (FD) on destination device, with a host-staging fallback when direct
 * import fails.
 *
 * Steps:
 *  1. Create VkInstance and enumerate physical devices.
 *  2. Choose source and destination physical devices (defaults: 0 and 1).
 *  3. Verify support for VK_KHR_external_memory and VK_KHR_external_memory_fd.
 *  4. Create logical devices on source and destination enabling the external
 *     memory FD extension.
 *  5. Allocate a buffer on source with exportable memory (OPAQUE_FD).
 *  6. Export memory FD via vkGetMemoryFdKHR.
 *  7. Attempt to import FD on destination via VkImportMemoryFdInfoKHR + vkAllocateMemory.
 *  8. If import fails, mmap the FD and copy contents into a host-visible buffer
 *     on destination (host-staging fallback).
 *
 * Build:
 *   cc -O2 -std=c11 export_import.c -o export_import -lvulkan
 *
 * Run:
 *   ./export_import [src_index dst_index]
 *
 * Notes:
 *  - This program is diagnostic and best-effort. It performs no GPU work and
 *    does not execute command buffers; it only exercises memory export/import
 *    paths and the fallback path.
 *  - Behavior depends on vendor ICDs and kernel driver support for dma-buf.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include <vulkan/vulkan.h>

#define WANT_EXT(name) (strcmp((name), VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) == 0)
#ifndef VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
#define VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME "VK_KHR_external_memory_fd"
#endif
#ifndef VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME
#define VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME "VK_KHR_external_memory"
#endif

/* Simple helper to print a VkResult */
static const char* vk_result_str(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    default: return "VK_ERROR_?";
    }
}

/* Find queue family index supporting desired flags (simple linear scan) */
static int find_queue_family(VkPhysicalDevice pd, VkQueueFlags want) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, NULL);
    if (count == 0) return -1;
    VkQueueFamilyProperties *props = calloc(count, sizeof(*props));
    if (!props) return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, props);
    int idx = -1;
    for (uint32_t i = 0; i < count; ++i) {
        if ((props[i].queueFlags & want) == want) { idx = (int)i; break; }
    }
    free(props);
    return idx;
}

/* Find a memory type index that satisfies typeBits and requested properties */
static int find_memory_type(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && ((mp.memoryTypes[i].propertyFlags & props) == props))
            return (int)i;
    }
    return -1;
}

/* Check whether a physical device exposes a given device extension */
static int physical_device_has_extension(VkPhysicalDevice pd, const char *extname) {
    uint32_t count = 0;
    VkResult r = vkEnumerateDeviceExtensionProperties(pd, NULL, &count, NULL);
    if (r != VK_SUCCESS || count == 0) return 0;
    VkExtensionProperties *exts = calloc(count, sizeof(VkExtensionProperties));
    if (!exts) return 0;
    r = vkEnumerateDeviceExtensionProperties(pd, NULL, &count, exts);
    int found = 0;
    if (r == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(exts[i].extensionName, extname) == 0) { found = 1; break; }
        }
    }
    free(exts);
    return found;
}

int main(int argc, char **argv) {
    VkResult res;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice *pds = NULL;
    uint32_t pdcount = 0;
    int src_idx = 0, dst_idx = 1;

    if (argc >= 2) src_idx = atoi(argv[1]);
    if (argc >= 3) dst_idx = atoi(argv[2]);

    /* Create Vulkan instance */
    {
        VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
        ai.pApplicationName = "mvgal_export_import_poc";
        ai.applicationVersion = VK_MAKE_VERSION(0,1,0);
        ai.pEngineName = "mvgal_poc";
        ai.engineVersion = VK_MAKE_VERSION(0,1,0);
        ai.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ici.pApplicationInfo = &ai;

        res = vkCreateInstance(&ici, NULL, &instance);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkCreateInstance failed: %s (%d)\n", vk_result_str(res), (int)res);
            return 1;
        }
    }

    /* Enumerate physical devices */
    res = vkEnumeratePhysicalDevices(instance, &pdcount, NULL);
    if (res != VK_SUCCESS || pdcount == 0) {
        fprintf(stderr, "vkEnumeratePhysicalDevices failed or returned 0: %d\n", (int)res);
        vkDestroyInstance(instance, NULL);
        return 1;
    }
    pds = calloc(pdcount, sizeof(*pds));
    if (!pds) { vkDestroyInstance(instance, NULL); return 1; }
    res = vkEnumeratePhysicalDevices(instance, &pdcount, pds);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkEnumeratePhysicalDevices failed: %d\n", (int)res);
        free(pds); vkDestroyInstance(instance, NULL); return 1;
    }

    if (src_idx < 0 || (uint32_t)src_idx >= pdcount || dst_idx < 0 || (uint32_t)dst_idx >= pdcount) {
        fprintf(stderr, "Invalid src/dst indices; available physical devices: %u\n", pdcount);
        free(pds); vkDestroyInstance(instance, NULL); return 1;
    }

    printf("Using physical devices src=%d dst=%d (of %u found)\n", src_idx, dst_idx, pdcount);

    VkPhysicalDevice src_pd = pds[src_idx];
    VkPhysicalDevice dst_pd = pds[dst_idx];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(src_pd, &props);
    printf("Source: %s (vendor=0x%04x device=0x%04x)\n", props.deviceName, props.vendorID, props.deviceID);
    vkGetPhysicalDeviceProperties(dst_pd, &props);
    printf("Dest  : %s (vendor=0x%04x device=0x%04x)\n", props.deviceName, props.vendorID, props.deviceID);

    /* Ensure both devices support VK_KHR_external_memory and VK_KHR_external_memory_fd */
    if (!physical_device_has_extension(src_pd, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) ||
        !physical_device_has_extension(src_pd, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)) {
        fprintf(stderr, "Source device missing external memory FD extensions; aborting\n");
        free(pds); vkDestroyInstance(instance, NULL); return 1;
    }
    if (!physical_device_has_extension(dst_pd, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) ||
        !physical_device_has_extension(dst_pd, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)) {
        fprintf(stderr, "Destination device missing external memory FD extensions; aborting\n");
        free(pds); vkDestroyInstance(instance, NULL); return 1;
    }

    /* Pick queue families */
    int src_qfi = find_queue_family(src_pd, VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT);
    if (src_qfi < 0) src_qfi = find_queue_family(src_pd, VK_QUEUE_TRANSFER_BIT);
    int dst_qfi = find_queue_family(dst_pd, VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT);
    if (dst_qfi < 0) dst_qfi = find_queue_family(dst_pd, VK_QUEUE_TRANSFER_BIT);
    if (src_qfi < 0 || dst_qfi < 0) {
        fprintf(stderr, "Failed to find suitable queue families on one or both devices\n");
        free(pds); vkDestroyInstance(instance, NULL); return 1;
    }

    /* Prepare device creation with external memory FD extensions enabled */
    const char *dev_exts[] = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
    };
    VkDevice src_dev = VK_NULL_HANDLE, dst_dev = VK_NULL_HANDLE;
    VkQueue src_queue = VK_NULL_HANDLE, dst_queue = VK_NULL_HANDLE;

    /* Create source logical device */
    {
        float qprio = 1.0f;
        VkDeviceQueueCreateInfo dq = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        dq.queueFamilyIndex = (uint32_t)src_qfi;
        dq.queueCount = 1;
        dq.pQueuePriorities = &qprio;

        VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &dq;
        dci.enabledExtensionCount = (uint32_t)(sizeof(dev_exts)/sizeof(dev_exts[0]));
        dci.ppEnabledExtensionNames = dev_exts;

        res = vkCreateDevice(src_pd, &dci, NULL, &src_dev);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkCreateDevice(src) failed: %s (%d)\n", vk_result_str(res), (int)res);
            goto cleanup;
        }
        vkGetDeviceQueue(src_dev, (uint32_t)src_qfi, 0, &src_queue);
    }

    /* Create destination logical device */
    {
        float qprio = 1.0f;
        VkDeviceQueueCreateInfo dq = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        dq.queueFamilyIndex = (uint32_t)dst_qfi;
        dq.queueCount = 1;
        dq.pQueuePriorities = &qprio;

        VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &dq;
        dci.enabledExtensionCount = (uint32_t)(sizeof(dev_exts)/sizeof(dev_exts[0]));
        dci.ppEnabledExtensionNames = dev_exts;

        res = vkCreateDevice(dst_pd, &dci, NULL, &dst_dev);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkCreateDevice(dst) failed: %s (%d)\n", vk_result_str(res), (int)res);
            goto cleanup;
        }
        vkGetDeviceQueue(dst_dev, (uint32_t)dst_qfi, 0, &dst_queue);
    }

    printf("Logical devices created.\n");

    /* Create a buffer on source and allocate exportable memory */
    VkBuffer src_buf = VK_NULL_HANDLE;
    VkDeviceMemory src_mem = VK_NULL_HANDLE;
    VkDeviceSize alloc_size = 0;
    const VkDeviceSize BUF_SIZE = 64 * 1024;

    {
        VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size = BUF_SIZE;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        res = vkCreateBuffer(src_dev, &bci, NULL, &src_buf);
        if (res != VK_SUCCESS) { fprintf(stderr, "vkCreateBuffer(src) failed: %d\n", (int)res); goto cleanup; }

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(src_dev, src_buf, &mr);
        alloc_size = mr.size;

        int mti = find_memory_type(src_pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mti < 0) {
            /* fallback to any compatible type */
            for (uint32_t i = 0; i < mr.memoryTypeBits; ++i) {
                if (mr.memoryTypeBits & (1u << i)) { mti = (int)i; break; }
            }
        }
        if (mti < 0) { fprintf(stderr, "No suitable memory type on source\n"); goto cleanup; }

        VkExportMemoryAllocateInfo export_info = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
        export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = alloc_size;
        mai.memoryTypeIndex = (uint32_t)mti;
        mai.pNext = &export_info;

        res = vkAllocateMemory(src_dev, &mai, NULL, &src_mem);
        if (res != VK_SUCCESS) { fprintf(stderr, "vkAllocateMemory(src) failed: %d\n", (int)res); goto cleanup; }

        res = vkBindBufferMemory(src_dev, src_buf, src_mem, 0);
        if (res != VK_SUCCESS) { fprintf(stderr, "vkBindBufferMemory(src) failed: %d\n", (int)res); goto cleanup; }
    }

    printf("Source buffer allocated and bound (size=%" PRIu64 ")\n", (uint64_t)alloc_size);

    /* Export FD from source memory using vkGetMemoryFdKHR */
    int memfd = -1;
    {
        PFN_vkGetMemoryFdKHR fpGetMemoryFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(src_dev, "vkGetMemoryFdKHR");
        if (!fpGetMemoryFd) { fprintf(stderr, "vkGetMemoryFdKHR not available on source device\n"); goto cleanup; }

        VkMemoryGetFdInfoKHR gfi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
        gfi.memory = src_mem;
        gfi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        res = fpGetMemoryFd(src_dev, &gfi, &memfd);
        if (res != VK_SUCCESS || memfd < 0) {
            fprintf(stderr, "fpGetMemoryFd failed: %s (%d) fd=%d\n", vk_result_str(res), (int)res, memfd);
            goto cleanup;
        }
        /* set close-on-exec */
        int flags = fcntl(memfd, F_GETFD);
        if (flags >= 0) fcntl(memfd, F_SETFD, flags | FD_CLOEXEC);

        printf("Exported memfd=%d from source\n", memfd);
    }

    /* Try importing on destination via VkImportMemoryFdInfoKHR (pNext of alloc) */
    {
        VkBufferCreateInfo dst_bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        dst_bci.size = BUF_SIZE;
        dst_bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        dst_bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer dst_buf = VK_NULL_HANDLE;
        VkDeviceMemory dst_mem = VK_NULL_HANDLE;

        res = vkCreateBuffer(dst_dev, &dst_bci, NULL, &dst_buf);
        if (res != VK_SUCCESS) { fprintf(stderr, "vkCreateBuffer(dst) failed: %d\n", (int)res); goto cleanup; }

        VkMemoryRequirements dst_mr;
        vkGetBufferMemoryRequirements(dst_dev, dst_buf, &dst_mr);

        /* pick compatible memory type on dest */
        int mti = find_memory_type(dst_pd, dst_mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mti < 0) {
            /* fallback to any allowed type bit */
            for (uint32_t i = 0; i < dst_mr.memoryTypeBits; ++i) {
                if (dst_mr.memoryTypeBits & (1u << i)) { mti = (int)i; break; }
            }
        }
        if (mti < 0) { fprintf(stderr, "No memory type on dest compatible with buffer\n"); vkDestroyBuffer(dst_dev, dst_buf, NULL); goto cleanup; }

        VkImportMemoryFdInfoKHR import_info = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
        import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        import_info.fd = memfd; /* may be consumed by the call */

        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = dst_mr.size;
        mai.memoryTypeIndex = (uint32_t)mti;
        mai.pNext = &import_info;

        res = vkAllocateMemory(dst_dev, &mai, NULL, &dst_mem);
        if (res == VK_SUCCESS) {
            res = vkBindBufferMemory(dst_dev, dst_buf, dst_mem, 0);
            if (res != VK_SUCCESS) {
                fprintf(stderr, "vkBindBufferMemory(dst) failed after import: %d\n", (int)res);
                /* cleanup imported mem */
                vkFreeMemory(dst_dev, dst_mem, NULL);
                vkDestroyBuffer(dst_dev, dst_buf, NULL);
                goto cleanup;
            }
            printf("Successfully imported memory on destination and bound to buffer.\n");
            /* close memfd if ownership transferred or duplicated as appropriate */
            close(memfd);
            memfd = -1;
            /* cleanup and success path for demo */
            vkFreeMemory(dst_dev, dst_mem, NULL);
            vkDestroyBuffer(dst_dev, dst_buf, NULL);
        } else {
            /* Import failed. Resort to host-staging fallback. */
            fprintf(stderr, "vkAllocateMemory(destination import) failed: %s (%d)\n", vk_result_str(res), (int)res);
            fprintf(stderr, "Attempting host-staging fallback...\n");

            /* mmap the exported memfd on host */
            void *src_map = mmap(NULL, (size_t)alloc_size, PROT_READ, MAP_SHARED, memfd, 0);
            if (src_map == MAP_FAILED) {
                fprintf(stderr, "mmap(memfd) failed: %s\n", strerror(errno));
                vkDestroyBuffer(dst_dev, dst_buf, NULL);
                goto cleanup;
            }

            /* Create host-visible buffer on destination */
            VkBufferCreateInfo host_bci = dst_bci;
            VkBuffer host_buf = VK_NULL_HANDLE;
            res = vkCreateBuffer(dst_dev, &host_bci, NULL, &host_buf);
            if (res != VK_SUCCESS) {
                fprintf(stderr, "vkCreateBuffer(host-visible) failed: %d\n", (int)res);
                munmap(src_map, (size_t)alloc_size);
                vkDestroyBuffer(dst_dev, dst_buf, NULL);
                goto cleanup;
            }
            VkMemoryRequirements host_mr;
            vkGetBufferMemoryRequirements(dst_dev, host_buf, &host_mr);

            int host_mti = find_memory_type(dst_pd, host_mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (host_mti < 0) {
                fprintf(stderr, "No host-visible memory type on destination for staging\n");
                munmap(src_map, (size_t)alloc_size);
                vkDestroyBuffer(dst_dev, host_buf, NULL);
                vkDestroyBuffer(dst_dev, dst_buf, NULL);
                goto cleanup;
            }

            VkMemoryAllocateInfo host_mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            host_mai.allocationSize = host_mr.size;
            host_mai.memoryTypeIndex = (uint32_t)host_mti;

            VkDeviceMemory host_mem = VK_NULL_HANDLE;
            res = vkAllocateMemory(dst_dev, &host_mai, NULL, &host_mem);
            if (res != VK_SUCCESS) {
                fprintf(stderr, "vkAllocateMemory(host-visible) failed: %d\n", (int)res);
                munmap(src_map, (size_t)alloc_size);
                vkDestroyBuffer(dst_dev, host_buf, NULL);
                vkDestroyBuffer(dst_dev, dst_buf, NULL);
                goto cleanup;
            }
            res = vkBindBufferMemory(dst_dev, host_buf, host_mem, 0);
            if (res != VK_SUCCESS) {
                fprintf(stderr, "vkBindBufferMemory(host-visible) failed: %d\n", (int)res);
                vkFreeMemory(dst_dev, host_mem, NULL);
                munmap(src_map, (size_t)alloc_size);
                vkDestroyBuffer(dst_dev, host_buf, NULL);
                vkDestroyBuffer(dst_dev, dst_buf, NULL);
                goto cleanup;
            }

            /* Map host memory and copy data */
            void *dst_ptr = NULL;
            res = vkMapMemory(dst_dev, host_mem, 0, host_mr.size, 0, &dst_ptr);
            if (res != VK_SUCCESS || dst_ptr == NULL) {
                fprintf(stderr, "vkMapMemory(host) failed: %d\n", (int)res);
                vkFreeMemory(dst_dev, host_mem, NULL);
                munmap(src_map, (size_t)alloc_size);
                vkDestroyBuffer(dst_dev, host_buf, NULL);
                vkDestroyBuffer(dst_dev, dst_buf, NULL);
                goto cleanup;
            }

            memcpy(dst_ptr, src_map, (size_t)alloc_size);
            vkUnmapMemory(dst_dev, host_mem);
            munmap(src_map, (size_t)alloc_size);

            printf("Host-staging fallback copied %" PRIu64 " bytes into host-visible buffer on destination\n", (uint64_t)alloc_size);

            /* Clean up host staging resources */
            vkFreeMemory(dst_dev, host_mem, NULL);
            vkDestroyBuffer(dst_dev, host_buf, NULL);
            vkDestroyBuffer(dst_dev, dst_buf, NULL);

            /* Close fd */
            close(memfd);
            memfd = -1;
        }
    }

    /* All done */
    printf("Export/import POC finished.\n");

cleanup:
    if (memfd >= 0) close(memfd);
    if (src_buf != VK_NULL_HANDLE) vkDestroyBuffer(src_dev, src_buf, NULL);
    if (src_mem != VK_NULL_HANDLE) vkFreeMemory(src_dev, src_mem, NULL);
    if (src_dev != VK_NULL_HANDLE) vkDestroyDevice(src_dev, NULL);
    if (dst_dev != VK_NULL_HANDLE) vkDestroyDevice(dst_dev, NULL);
    if (pds) free(pds);
    if (instance) vkDestroyInstance(instance, NULL);
    return 0;
}