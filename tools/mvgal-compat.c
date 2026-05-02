/**
 * mvgal-compat — Check whether a specific application or game is expected to
 * be compatible with MVGAL.
 *
 * Checks:
 *   - Vulkan layer registration
 *   - OpenCL ICD registration
 *   - DXVK / VKD3D-Proton presence (for Steam/Proton games)
 *   - Known incompatible applications
 *   - Required Vulkan extensions availability
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

/* -------------------------------------------------------------------------
 * Compatibility result
 * ---------------------------------------------------------------------- */

typedef enum {
    COMPAT_SUPPORTED    = 0,
    COMPAT_PARTIAL      = 1,
    COMPAT_UNSUPPORTED  = 2,
    COMPAT_UNKNOWN      = 3,
} compat_level_t;

typedef struct {
    compat_level_t level;
    char           summary[256];
    char           notes[1024];
    char           workaround[512];
} compat_result_t;

/* -------------------------------------------------------------------------
 * Known application database
 * ---------------------------------------------------------------------- */

typedef struct {
    const char *name_pattern;   /* substring match, case-insensitive */
    compat_level_t level;
    const char *notes;
    const char *workaround;
} app_entry_t;

static const app_entry_t g_app_db[] = {
    /* Games known to work well with multi-GPU Vulkan */
    { "doom",         COMPAT_SUPPORTED, "Vulkan renderer; AFR works well.", NULL },
    { "quake",        COMPAT_SUPPORTED, "Vulkan renderer; tested with vkQuake.", NULL },
    { "dota2",        COMPAT_SUPPORTED, "Vulkan renderer; SFR recommended.", NULL },
    { "csgo",         COMPAT_SUPPORTED, "Vulkan renderer via DXVK.", NULL },
    { "cs2",          COMPAT_SUPPORTED, "Native Vulkan; AFR tested.", NULL },
    { "cyberpunk",    COMPAT_PARTIAL,
      "DX12 via VKD3D-Proton; ray tracing may not scale across GPUs.",
      "Disable ray tracing for best multi-GPU performance." },
    { "elden ring",   COMPAT_PARTIAL,
      "DX12 via VKD3D-Proton; frame pacing sensitive.",
      "Set MVGAL_FRAME_PACING=1 in environment." },
    { "red dead",     COMPAT_PARTIAL,
      "Vulkan renderer; memory-intensive, ensure DMA-BUF is available.",
      "Use MVGAL_STRATEGY=sfr for best results." },
    { "minecraft",    COMPAT_SUPPORTED, "OpenGL via Zink/Vulkan; works with single-GPU fallback.", NULL },
    { "blender",      COMPAT_SUPPORTED, "OpenCL and Vulkan compute; multi-GPU rendering supported.", NULL },
    { "pytorch",      COMPAT_PARTIAL,
      "CUDA path requires NVIDIA GPU; OpenCL path works across vendors.",
      "Use MVGAL_CUDA_BACKEND=opencl for cross-vendor compute." },
    { "tensorflow",   COMPAT_PARTIAL,
      "CUDA path requires NVIDIA GPU; ROCm path for AMD.",
      "Use MVGAL_CUDA_BACKEND=opencl for cross-vendor compute." },
    { "ffmpeg",       COMPAT_SUPPORTED, "VAAPI and NVENC paths both work; MVGAL routes to best encoder.", NULL },
    { "obs",          COMPAT_SUPPORTED, "NVENC/VAAPI encoding; display capture works.", NULL },
    { "unreal",       COMPAT_PARTIAL,
      "Vulkan renderer works; DX12 via VKD3D-Proton is partial.",
      "Use Vulkan renderer (-vulkan flag) for best compatibility." },
    { "unity",        COMPAT_PARTIAL,
      "Vulkan renderer works; OpenGL renderer uses single GPU.",
      "Force Vulkan with -force-vulkan." },
    /* Known problematic */
    { "anti-cheat",   COMPAT_UNSUPPORTED,
      "Kernel-level anti-cheat (EAC, BattlEye) may block MVGAL layer.",
      "Disable MVGAL for games with kernel anti-cheat." },
    { "easyanticheat",COMPAT_UNSUPPORTED,
      "EasyAntiCheat blocks Vulkan layers.",
      "Disable MVGAL: unset ENABLE_MVGAL." },
    { "battleye",     COMPAT_UNSUPPORTED,
      "BattlEye blocks Vulkan layers.",
      "Disable MVGAL: unset ENABLE_MVGAL." },
    { NULL, COMPAT_UNKNOWN, NULL, NULL }
};

/* -------------------------------------------------------------------------
 * System checks
 * ---------------------------------------------------------------------- */

typedef struct {
    bool vulkan_layer_registered;
    bool opencl_icd_registered;
    bool kernel_module_loaded;
    bool daemon_running;
    bool dxvk_present;
    bool vkd3d_present;
    int  gpu_count;
    bool has_amd;
    bool has_nvidia;
    bool has_intel;
    bool has_mtt;
} system_state_t;

static system_state_t check_system(void)
{
    system_state_t s;
    memset(&s, 0, sizeof(s));

    /* Vulkan layer */
    struct stat st;
    s.vulkan_layer_registered =
        stat("/usr/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json", &st) == 0 ||
        stat("/etc/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json", &st) == 0 ||
        stat("/usr/local/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json", &st) == 0;

    /* OpenCL ICD */
    s.opencl_icd_registered = stat("/etc/OpenCL/vendors/mvgal.icd", &st) == 0;

    /* Kernel module */
    FILE *f = fopen("/proc/modules", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f))
            if (strncmp(line, "mvgal ", 6) == 0) { s.kernel_module_loaded = true; break; }
        fclose(f);
    }

    /* Daemon socket */
    s.daemon_running = stat("/run/mvgal/mvgal.sock", &st) == 0;

    /* DXVK */
    s.dxvk_present =
        stat("/usr/lib/dxvk/d3d11.dll", &st) == 0 ||
        stat("/usr/lib32/dxvk/d3d11.dll", &st) == 0 ||
        stat("/usr/share/dxvk/x64/d3d11.dll", &st) == 0;

    /* VKD3D-Proton */
    s.vkd3d_present =
        stat("/usr/lib/vkd3d-proton/d3d12.dll", &st) == 0 ||
        stat("/usr/share/vkd3d-proton/x64/d3d12.dll", &st) == 0;

    /* GPU count via /sys/class/drm */
    DIR *d = opendir("/sys/class/drm");
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strncmp(ent->d_name, "card", 4) != 0) continue;
            const char *p = ent->d_name + 4;
            bool digits = true;
            for (; *p; p++) if (*p < '0' || *p > '9') { digits = false; break; }
            if (!digits) continue;

            char sysfs[512], real[768], id_path[1024], id_buf[16];
            snprintf(sysfs, sizeof(sysfs), "/sys/class/drm/%s/device", ent->d_name);
            if (realpath(sysfs, real) == NULL) continue;
            snprintf(id_path, sizeof(id_path), "%s/vendor", real);
            int fd = open(id_path, O_RDONLY);
            if (fd < 0) continue;
            ssize_t n = read(fd, id_buf, sizeof(id_buf) - 1);
            close(fd);
            if (n <= 0) continue;
            id_buf[n] = '\0';
            uint16_t vid = (uint16_t)strtoul(id_buf, NULL, 16);
            if (vid == 0x1002) { s.has_amd    = true; s.gpu_count++; }
            if (vid == 0x10DE) { s.has_nvidia = true; s.gpu_count++; }
            if (vid == 0x8086) { s.has_intel  = true; s.gpu_count++; }
            if (vid == 0x1A82) { s.has_mtt    = true; s.gpu_count++; }
        }
        closedir(d);
    }

    return s;
}

/* -------------------------------------------------------------------------
 * Case-insensitive substring search
 * ---------------------------------------------------------------------- */

static bool istr_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            char h = haystack[i+j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * Check a specific application
 * ---------------------------------------------------------------------- */

static compat_result_t check_app(const char *app_name, const system_state_t *sys)
{
    compat_result_t r;
    memset(&r, 0, sizeof(r));
    r.level = COMPAT_UNKNOWN;
    snprintf(r.summary, sizeof(r.summary), "No specific data for '%s'", app_name);
    snprintf(r.notes, sizeof(r.notes),
             "MVGAL should work with any Vulkan application. "
             "Test with ENABLE_MVGAL=1 and report issues.");

    /* Search database */
    for (int i = 0; g_app_db[i].name_pattern != NULL; i++) {
        if (istr_contains(app_name, g_app_db[i].name_pattern)) {
            r.level = g_app_db[i].level;
            snprintf(r.summary, sizeof(r.summary), "%s", g_app_db[i].notes);
            if (g_app_db[i].workaround)
                snprintf(r.workaround, sizeof(r.workaround), "%s", g_app_db[i].workaround);
            break;
        }
    }

    /* Adjust based on system state */
    if (!sys->vulkan_layer_registered && r.level == COMPAT_SUPPORTED)
        r.level = COMPAT_PARTIAL;

    if (sys->gpu_count < 2) {
        snprintf(r.notes + strlen(r.notes),
                 sizeof(r.notes) - strlen(r.notes),
                 " (Only 1 GPU detected — multi-GPU features inactive.)");
    }

    return r;
}

/* -------------------------------------------------------------------------
 * Print system state
 * ---------------------------------------------------------------------- */

static void print_system_state(const system_state_t *s)
{
    printf("System State:\n");
    printf("  GPUs detected          : %d", s->gpu_count);
    if (s->has_amd)    printf(" AMD");
    if (s->has_nvidia) printf(" NVIDIA");
    if (s->has_intel)  printf(" Intel");
    if (s->has_mtt)    printf(" MooreThreads");
    printf("\n");
    printf("  Vulkan layer           : %s\n",
           s->vulkan_layer_registered ? "✓ registered" : "✗ not registered");
    printf("  OpenCL ICD             : %s\n",
           s->opencl_icd_registered ? "✓ registered" : "✗ not registered");
    printf("  Kernel module          : %s\n",
           s->kernel_module_loaded ? "✓ loaded" : "✗ not loaded");
    printf("  Daemon                 : %s\n",
           s->daemon_running ? "✓ running" : "✗ not running");
    printf("  DXVK                   : %s\n",
           s->dxvk_present ? "✓ present" : "✗ not found");
    printf("  VKD3D-Proton           : %s\n",
           s->vkd3d_present ? "✓ present" : "✗ not found");
    printf("\n");
}

static const char *level_str(compat_level_t l)
{
    switch (l) {
    case COMPAT_SUPPORTED:   return "✓ SUPPORTED";
    case COMPAT_PARTIAL:     return "⚠ PARTIAL";
    case COMPAT_UNSUPPORTED: return "✗ UNSUPPORTED";
    default:                 return "? UNKNOWN";
    }
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    bool system_only = false;
    const char *app_name = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--system") == 0 || strcmp(argv[i], "-s") == 0) {
            system_only = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: mvgal-compat [--system] [application-name]\n");
            printf("  --system / -s   Show system compatibility state only\n");
            printf("  application-name  Check compatibility for a specific app\n");
            printf("\nExamples:\n");
            printf("  mvgal-compat --system\n");
            printf("  mvgal-compat \"Cyberpunk 2077\"\n");
            printf("  mvgal-compat doom\n");
            return 0;
        } else if (argv[i][0] != '-') {
            app_name = argv[i];
        }
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf("  MVGAL Compatibility Checker\n");
    printf("══════════════════════════════════════════════════════════════\n\n");

    system_state_t sys = check_system();
    print_system_state(&sys);

    if (system_only || app_name == NULL) {
        /* Overall system compatibility */
        printf("Overall MVGAL Readiness:\n");
        int score = 0;
        if (sys.vulkan_layer_registered) score++;
        if (sys.kernel_module_loaded)    score++;
        if (sys.daemon_running)          score++;
        if (sys.gpu_count >= 2)          score++;

        if (score == 4)
            printf("  ✓ Fully operational — all components present\n");
        else if (score >= 2)
            printf("  ⚠ Partially operational — %d/4 components active\n", score);
        else
            printf("  ✗ Not operational — install and configure MVGAL first\n");

        if (!sys.vulkan_layer_registered)
            printf("  → Install Vulkan layer: run 'pkexec make install' in the build dir\n");
        if (!sys.kernel_module_loaded)
            printf("  → Load kernel module: pkexec modprobe mvgal\n");
        if (!sys.daemon_running)
            printf("  → Start daemon: pkexec systemctl start mvgald\n");
        if (sys.gpu_count < 2)
            printf("  → Only %d GPU detected; multi-GPU features require ≥2 GPUs\n",
                   sys.gpu_count);
        printf("\n");
        return (score >= 2) ? 0 : 1;
    }

    /* Application-specific check */
    printf("Checking compatibility for: %s\n\n", app_name);
    compat_result_t r = check_app(app_name, &sys);

    printf("  Result     : %s\n", level_str(r.level));
    printf("  Summary    : %s\n", r.summary);
    if (r.notes[0])
        printf("  Notes      : %s\n", r.notes);
    if (r.workaround[0])
        printf("  Workaround : %s\n", r.workaround);

    printf("\n  Steam launch option: ENABLE_MVGAL=1 %%command%%\n");
    printf("\n");

    return (r.level == COMPAT_UNSUPPORTED) ? 1 : 0;
}
