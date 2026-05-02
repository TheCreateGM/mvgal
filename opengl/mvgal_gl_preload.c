/**
 * MVGAL OpenGL LD_PRELOAD shim
 *
 * Intercepts glXSwapBuffers and eglSwapBuffers to inject MVGAL frame pacing
 * and telemetry.  The actual OpenGL→Vulkan translation is handled by Zink
 * (Mesa) or the vendor's OpenGL driver.
 *
 * Build:
 *   gcc -shared -fPIC -o libmvgal_gl.so mvgal_gl_preload.c -ldl -lpthread
 *
 * Usage:
 *   LD_PRELOAD=/usr/lib/libmvgal_gl.so glxgears
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Minimal X11 / EGL type declarations (avoid pulling in full headers)
 * ---------------------------------------------------------------------- */

typedef void *Display;
typedef unsigned long Drawable;
typedef void *EGLDisplay;
typedef void *EGLSurface;

/* -------------------------------------------------------------------------
 * Original function pointers
 * ---------------------------------------------------------------------- */

typedef void (*pfn_glXSwapBuffers)(Display *dpy, Drawable drawable);
typedef unsigned int (*pfn_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);

static pfn_glXSwapBuffers  orig_glXSwapBuffers  = NULL;
static pfn_eglSwapBuffers  orig_eglSwapBuffers  = NULL;
static pthread_once_t      g_init_once = PTHREAD_ONCE_INIT;

/* -------------------------------------------------------------------------
 * Frame counter and timing
 * ---------------------------------------------------------------------- */

static uint64_t g_frame_count = 0;
static uint64_t g_last_swap_ns = 0;
static bool     g_debug = false;

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* -------------------------------------------------------------------------
 * Initialisation
 * ---------------------------------------------------------------------- */

static void init_once(void)
{
    orig_glXSwapBuffers = (pfn_glXSwapBuffers)dlsym(RTLD_NEXT, "glXSwapBuffers");
    orig_eglSwapBuffers = (pfn_eglSwapBuffers)dlsym(RTLD_NEXT, "eglSwapBuffers");
    g_debug = (getenv("MVGAL_GL_DEBUG") != NULL);

    if (g_debug) {
        fprintf(stderr, "[mvgal-gl] Initialised. glX=%p egl=%p\n",
                (void *)orig_glXSwapBuffers, (void *)orig_eglSwapBuffers);
    }
}

/* -------------------------------------------------------------------------
 * Frame pacing: notify MVGAL daemon (best-effort via env var check)
 * ---------------------------------------------------------------------- */

static void on_frame_presented(void)
{
    uint64_t now = mono_ns();
    uint64_t frame_id = __atomic_fetch_add(&g_frame_count, 1, __ATOMIC_RELAXED);

    if (g_debug && g_last_swap_ns > 0) {
        double frame_ms = (double)(now - g_last_swap_ns) / 1e6;
        fprintf(stderr, "[mvgal-gl] frame=%llu  dt=%.2f ms  fps=%.1f\n",
                (unsigned long long)frame_id,
                frame_ms,
                frame_ms > 0 ? 1000.0 / frame_ms : 0.0);
    }

    g_last_swap_ns = now;
}

/* -------------------------------------------------------------------------
 * Intercepted functions
 * ---------------------------------------------------------------------- */

void glXSwapBuffers(Display *dpy, Drawable drawable)
{
    pthread_once(&g_init_once, init_once);

    if (orig_glXSwapBuffers)
        orig_glXSwapBuffers(dpy, drawable);

    on_frame_presented();
}

unsigned int eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    pthread_once(&g_init_once, init_once);

    unsigned int result = 1; /* EGL_TRUE */
    if (orig_eglSwapBuffers)
        result = orig_eglSwapBuffers(dpy, surface);

    on_frame_presented();
    return result;
}
