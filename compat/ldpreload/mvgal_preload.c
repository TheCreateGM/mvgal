// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * @file mvgal_preload.c
 * @brief LD_PRELOAD shim for applications using dlopen for GPU APIs
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This shim intercepts dlopen() calls for GPU API libraries and redirects
 * them to MVGAL's interception layers. It is loaded via LD_PRELOAD.
 *
 * Usage:
 *   LD_PRELOAD=/usr/lib/libmvgal_preload.so application
 */

/* _GNU_SOURCE must come before any system header */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original dlopen function pointer.
 * dlsym returns void* but we store it as a function pointer.
 * The cast via union is the portable way to silence -Wpedantic. */
typedef void *(*dlopen_fn_t)(const char *filename, int flags);

static dlopen_fn_t real_dlopen = NULL;

/* MVGAL library paths */
#define MVGAL_CUDA_LIB   "/usr/lib/libmvgal_cuda.so"
#define MVGAL_OPENCL_LIB "/usr/lib/libmvgal_opencl.so"

/* Libraries to intercept */
static const struct {
    const char *original;
    const char *replacement;
} intercept_map[] = {
    /* CUDA runtime */
    { "libcuda.so",        MVGAL_CUDA_LIB },
    { "libcuda.so.1",      MVGAL_CUDA_LIB },
    { "libcudart.so",      MVGAL_CUDA_LIB },
    { "libcudart.so.12",   MVGAL_CUDA_LIB },
    /* OpenCL */
    { "libOpenCL.so",      MVGAL_OPENCL_LIB },
    { "libOpenCL.so.1",    MVGAL_OPENCL_LIB },
};

#define INTERCEPT_COUNT (sizeof(intercept_map) / sizeof(intercept_map[0]))

/**
 * @brief Initialize the preload shim.
 *
 * Called automatically when the shared library is loaded.
 */
__attribute__((constructor))
static void mvgal_preload_init(void)
{
    const char *debug = getenv("MVGAL_PRELOAD_DEBUG");

    /* Resolve the real dlopen before we intercept it.
     * POSIX allows casting void* from dlsym to a function pointer via union. */
    union { void *obj; dlopen_fn_t fn; } sym;
    sym.obj = dlsym(RTLD_NEXT, "dlopen");
    real_dlopen = sym.fn;
    if (real_dlopen == NULL) {
        fprintf(stderr, "[mvgal-preload] WARNING: could not resolve real dlopen\n");
        return;
    }

    if (debug != NULL && debug[0] != '\0' && debug[0] != '0') {
        fprintf(stderr, "[mvgal-preload] initialized, intercepting %zu libraries\n",
                INTERCEPT_COUNT);
    }
}

/**
 * @brief Intercept dlopen() and redirect GPU API libraries to MVGAL.
 *
 * If the requested library is a known GPU API library and the corresponding
 * MVGAL replacement exists, the replacement is loaded instead.
 */
void *dlopen(const char *filename, int flags)
{
    const char *debug = getenv("MVGAL_PRELOAD_DEBUG");
    size_t i;

    if (real_dlopen == NULL) {
        /* Fallback: resolve now (should not happen after constructor).
         * Use union cast to avoid -Wpedantic void*-to-function-pointer warning. */
        union { void *obj; dlopen_fn_t fn; } sym2;
        sym2.obj = dlsym(RTLD_NEXT, "dlopen");
        real_dlopen = sym2.fn;
        if (real_dlopen == NULL) {
            return NULL;
        }
    }

    if (filename == NULL) {
        return real_dlopen(filename, flags);
    }

    /* Check if this is a library we should intercept */
    for (i = 0; i < INTERCEPT_COUNT; i++) {
        const char *orig = intercept_map[i].original;
        const char *repl = intercept_map[i].replacement;

        /* Match on basename or full path suffix */
        const char *basename = strrchr(filename, '/');
        const char *name = (basename != NULL) ? basename + 1 : filename;

        if (strcmp(name, orig) == 0) {
            if (debug != NULL && debug[0] != '\0' && debug[0] != '0') {
                fprintf(stderr,
                        "[mvgal-preload] redirecting dlopen(\"%s\") -> \"%s\"\n",
                        filename, repl);
            }

            /* Try to load the MVGAL replacement */
            void *handle = real_dlopen(repl, flags);
            if (handle != NULL) {
                return handle;
            }

            /* Replacement not available; fall through to original */
            if (debug != NULL && debug[0] != '\0' && debug[0] != '0') {
                fprintf(stderr,
                        "[mvgal-preload] replacement \"%s\" not found, "
                        "falling back to \"%s\"\n",
                        repl, filename);
            }
            break;
        }
    }

    return real_dlopen(filename, flags);
}
