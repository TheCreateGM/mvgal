/*
 * mvgal_vulkan_layer.c
 *
 * Minimal MVGAL Vulkan layer stub for pass-through build.
 *
 * This file provides a very small, safe Vulkan loader layer implementation
 * that is sufficient for development and testing of the MVGAL pass-through
 * layer packaging and loader integration. It intentionally implements only
 * the minimal exported symbols the loader expects:
 *
 *  - vkNegotiateLoaderLayerInterfaceVersion
 *  - vkGetInstanceProcAddr
 *  - vkGetDeviceProcAddr
 *
 * and a sentinel symbol (`MVGAL_vulkan_layer_sentinel`) so packaging/tests
 * can verify the shared object exports a known symbol quickly.
 *
 * The implementation is intentionally conservative:
 *  - It does not attempt to implement full dispatch/chaining.
 *  - It prints lightweight diagnostics when core entrypoints are invoked.
 *  - If the real Vulkan headers are available, the real types are used;
 *    otherwise minimal compatible typedefs are provided so this file
 *    compiles in environments without the Vulkan SDK.
 *
 * This stub is suitable as an initial pass-through layer placeholder.
 */

#if defined(_MSC_VER)
#  define MVGAL_EXPORT __declspec(dllexport)
#else
#  define MVGAL_EXPORT __attribute__((visibility("default")))
#endif

/* Detect if Vulkan headers are available. If so, use them. Otherwise
 * provide minimal definitions so this file can be built without the SDK.
 */
#if defined(__has_include)
#  if __has_include(<vulkan/vulkan.h>)
#    include <vulkan/vulkan.h>
#    define MVGAL_HAVE_VULKAN 1
#  else
#    define MVGAL_HAVE_VULKAN 0
#  endif
#else
/* Fallback: assume Vulkan header is not present */
#  define MVGAL_HAVE_VULKAN 0
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Minimal Vulkan-compatible types and constants when SDK is missing */
#if !MVGAL_HAVE_VULKAN

typedef int32_t VkResult;
typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef void*   VkInstance;
typedef void*   VkPhysicalDevice;
typedef void*   VkDevice;
typedef void*   VkQueue;
typedef void*   VkCommandBuffer;
typedef void*   VkDeviceMemory;
typedef uint64_t VkDeviceSize;
typedef const char* PFN_vkGetInstanceProcAddr;
typedef const char* PFN_vkGetDeviceProcAddr;

/* Minimal negotiate struct definition compatible with loader expectations.
 * Real Vulkan SDK defines VkNegotiateLayerInterface or VkNegotiateLayerInterfaceEXT
 * depending on loader version; this minimal struct exposes the fields we use.
 */
typedef struct VkNegotiateLayerInterface {
    uint32_t sType;
    uint32_t loaderLayerInterfaceVersion;
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   pfnGetDeviceProcAddr;
} VkNegotiateLayerInterface;

/* Common return code used by the loader */
#define VK_SUCCESS 0

/* Calling convention macro (no-op for most platforms) */
#ifndef VKAPI_CALL
#  define VKAPI_CALL
#endif

#else
/* When Vulkan headers are present, use the real negotiate struct name if available.
 * Many loader examples accept (VkNegotiateLayerInterface*) which is compatible.
 * We still declare VKAPI_CALL in case the header didn't provide it.
 */
#ifndef VKAPI_CALL
#  define VKAPI_CALL
#endif
#endif /* MVGAL_HAVE_VULKAN */


/* Simple sentinel symbol for test harnesses to verify the shared object */
MVGAL_EXPORT void MVGAL_vulkan_layer_sentinel(void)
{
    /* intentionally empty - presence is the check */
}

/* Forward declarations for the functions we export */
MVGAL_EXPORT VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct);

MVGAL_EXPORT void* VKAPI_CALL
vkGetInstanceProcAddr(void* instance, const char* pName);

MVGAL_EXPORT void* VKAPI_CALL
vkGetDeviceProcAddr(void* device, const char* pName);

/* Implementation notes:
 *
 * - vkNegotiateLoaderLayerInterfaceVersion:
 *   The loader calls this during layer loading to negotiate an interface
 *   version and to obtain the layer's getProcAddr hooks. We provide a
 *   conservative implementation that reports a loader interface version
 *   (2 is commonly supported) and exports pointers to our local
 *   vkGetInstanceProcAddr / vkGetDeviceProcAddr implementations so the
 *   loader can query layer-managed entrypoints.
 *
 * - vkGetInstanceProcAddr / vkGetDeviceProcAddr:
 *   For this minimal stub we do not implement any real dispatching. These
 *   functions print a debug message and return NULL for all Vulkan symbols.
 *   The Vulkan loader will continue its normal lookup process and chain to
 *   the next layer or ICD if needed.
 *
 * This design is intentionally non-invasive: it lets the layer be present
 * in the loader chain while avoiding any incorrect forwarding or ABI
 * mismatches during early development.
 */

/* Simple safe string comparison helper */
static int name_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

MVGAL_EXPORT VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct)
{
    if (pVersionStruct == NULL) {
        fprintf(stderr, "MVGAL: vkNegotiateLoaderLayerInterfaceVersion called with NULL\n");
        return (VkResult)-1;
    }

    /* Prefer a conservative loader interface version; 2 is commonly supported.
     * If a future loader uses a different struct layout this function may be
     * adapted to detect and honor the requested version.
     */
    pVersionStruct->loaderLayerInterfaceVersion = 2;

    /* Provide the loader with pointers to our getProcAddr hooks so it can
     * forward instance/device proc resolution through the layer.
     * Cast to the generic function pointer types used by the loader.
     */
    pVersionStruct->pfnGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr   = (PFN_vkGetDeviceProcAddr)vkGetDeviceProcAddr;

    fprintf(stderr, "MVGAL: negotiated loader interface version %u\n",
            pVersionStruct->loaderLayerInterfaceVersion);

    return VK_SUCCESS;
}

MVGAL_EXPORT void* VKAPI_CALL
vkGetInstanceProcAddr(void* instance, const char* pName)
{
    /* This stub intentionally does not implement dispatch wrappers.
     * We log a lightweight message for visibility during testing and return NULL
     * to allow the loader to resolve the requested symbol from below layers/ICDs.
     */
    if (pName) {
        fprintf(stderr, "MVGAL: vkGetInstanceProcAddr request for '%s' (instance=%p)\n",
                pName, instance);
    } else {
        fprintf(stderr, "MVGAL: vkGetInstanceProcAddr request for NULL name (instance=%p)\n",
                instance);
    }

    /* No layer-local entrypoints implemented in this stub -> return NULL */
    return NULL;
}

MVGAL_EXPORT void* VKAPI_CALL
vkGetDeviceProcAddr(void* device, const char* pName)
{
    if (pName) {
        fprintf(stderr, "MVGAL: vkGetDeviceProcAddr request for '%s' (device=%p)\n",
                pName, device);
    } else {
        fprintf(stderr, "MVGAL: vkGetDeviceProcAddr request for NULL name (device=%p)\n",
                device);
    }

    /* No layer-local device entrypoints implemented -> return NULL */
    return NULL;
}

/* Optional debug initializer - executed when the shared object is loaded. */
#if defined(__linux__) || defined(__APPLE__)
__attribute__((constructor)) static void mvgal_layer_init(void)
{
    fprintf(stderr, "MVGAL Vulkan layer (stub) initialized\n");
}
__attribute__((destructor)) static void mvgal_layer_fini(void)
{
    fprintf(stderr, "MVGAL Vulkan layer (stub) finalized\n");
}
#endif