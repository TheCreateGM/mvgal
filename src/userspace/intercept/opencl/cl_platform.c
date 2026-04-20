/**
 * @file cl_platform.c
 * @brief OpenCL Platform API Interception
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This file implements interception of OpenCL platform API functions.
 * Uses dlsym(RTLD_NEXT) to chain to the real OpenCL library.
 */

#include <dlfcn.h>
#include <stddef.h>
#include <stdbool.h>

// We need to define these to avoid including CL/cl.h
// These are the actual OpenCL function signatures

// Opaque types (we don't need the actual definitions)
typedef void* cl_platform_id;
typedef void* cl_device_id;

// Platform info enumerations
typedef enum {
    CL_PLATFORM_PROFILE = 0x0900,
    CL_PLATFORM_VERSION = 0x0901,
    CL_PLATFORM_NAME = 0x0902,
    CL_PLATFORM_VENDOR = 0x0903,
    CL_PLATFORM_EXTENSIONS = 0x0904
} cl_platform_info;

// Function pointer types
typedef int (*clGetPlatformIDsFunc)(unsigned int, void*, unsigned int*);
typedef int (*clGetPlatformInfoFunc)(void*, unsigned int, size_t, void*, size_t*);

// Global state
static bool g_initialized = false;
static void* g_libopencl = NULL;

// Function pointers
static clGetPlatformIDsFunc g_orig_clGetPlatformIDs = NULL;
static clGetPlatformInfoFunc g_orig_clGetPlatformInfo = NULL;

// Initialization
static void ensure_initialized(void) {
    if (g_initialized) return;
    g_initialized = true;
    g_libopencl = dlopen("libOpenCL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!g_libopencl) {
        g_libopencl = dlopen("libOpenCL.so", RTLD_LAZY | RTLD_GLOBAL);
    }
    
    if (g_libopencl) {
        g_orig_clGetPlatformIDs = (clGetPlatformIDsFunc)dlsym(g_libopencl, "clGetPlatformIDs");
        g_orig_clGetPlatformInfo = (clGetPlatformInfoFunc)dlsym(g_libopencl, "clGetPlatformInfo");
    }
}

// clGetPlatformIDs
int clGetPlatformIDs(unsigned int num_entries, void* platforms, unsigned int* num_platforms) {
    ensure_initialized();
    
    if (g_orig_clGetPlatformIDs) {
        return g_orig_clGetPlatformIDs(num_entries, platforms, num_platforms);
    }
    
    // Fallback: return 0 platforms
    if (num_platforms) *num_platforms = 0;
    return 0;
}

// clGetPlatformInfo
int clGetPlatformInfo(void* platform, unsigned int param_name, size_t param_value_size, void* param_value, size_t* param_value_size_ret) {
    ensure_initialized();
    
    if (g_orig_clGetPlatformInfo) {
        return g_orig_clGetPlatformInfo(platform, param_name, param_value_size, param_value, param_value_size_ret);
    }
    
    return -1; // CL_INVALID_VALUE
}
