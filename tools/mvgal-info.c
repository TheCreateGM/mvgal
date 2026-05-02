/**
 * mvgal-info — Print all detected GPUs, their capabilities, and the current
 * logical device configuration.
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
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/utsname.h>

/* -------------------------------------------------------------------------
 * Helpers: sysfs reading
 * ---------------------------------------------------------------------- */

static int read_sysfs_str(const char *path, char *buf, size_t bufsz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, (ssize_t)(bufsz - 1));
    close(fd);
    if (n <= 0) return -1;
    /* strip trailing newline */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) n--;
    buf[n] = '\0';
    return 0;
}

static long long read_sysfs_ll(const char *path)
{
    char buf[64];
    if (read_sysfs_str(path, buf, sizeof(buf)) < 0) return -1;
    return strtoll(buf, NULL, 0);
}

/* -------------------------------------------------------------------------
 * GPU vendor detection from PCI vendor ID
 * ---------------------------------------------------------------------- */

static const char *vendor_name(uint16_t vid)
{
    switch (vid) {
    case 0x1002: return "AMD";
    case 0x10DE: return "NVIDIA";
    case 0x8086: return "Intel";
    case 0x1A82: return "Moore Threads";
    default:     return "Unknown";
    }
}

/* -------------------------------------------------------------------------
 * GPU info structure
 * ---------------------------------------------------------------------- */

#define MAX_GPUS 16

typedef struct {
    char     pci_slot[64];   /* e.g. 0000:01:00.0 */
    uint16_t vendor_id;
    uint16_t device_id;
    char     name[128];
    char     drm_node[64];   /* e.g. /dev/dri/card0 */
    long long vram_total_bytes;
    long long vram_used_bytes;
    int      temperature_c;
    int      utilization_pct;
    bool     display_connected;
    bool     enabled;
} gpu_info_t;

static int g_gpu_count = 0;
static gpu_info_t g_gpus[MAX_GPUS];

/* -------------------------------------------------------------------------
 * Enumerate GPUs via /sys/class/drm
 * ---------------------------------------------------------------------- */

static void enumerate_drm_gpus(void)
{
    DIR *d = opendir("/sys/class/drm");
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && g_gpu_count < MAX_GPUS) {
        /* We want cardN entries (not renderD or connectors) */
        if (strncmp(ent->d_name, "card", 4) != 0) continue;
        /* Skip cardN-* connector entries */
        const char *p = ent->d_name + 4;
        bool digits_only = true;
        for (; *p; p++) {
            if (*p < '0' || *p > '9') { digits_only = false; break; }
        }
        if (!digits_only) continue;

        char sysfs_path[512];
        snprintf(sysfs_path, sizeof(sysfs_path),
                 "/sys/class/drm/%s/device", ent->d_name);

        /* Resolve symlink to get PCI slot */
        char real_path[512];
        if (realpath(sysfs_path, real_path) == NULL) continue;

        /* Extract PCI slot (last component) */
        char *slot = strrchr(real_path, '/');
        if (!slot) continue;
        slot++;

        /* Read vendor/device IDs */
        char id_path[768];
        char id_buf[16];

        snprintf(id_path, sizeof(id_path), "%s/vendor", real_path);
        if (read_sysfs_str(id_path, id_buf, sizeof(id_buf)) < 0) continue;
        uint16_t vid = (uint16_t)strtoul(id_buf, NULL, 16);

        snprintf(id_path, sizeof(id_path), "%s/device", real_path);
        if (read_sysfs_str(id_path, id_buf, sizeof(id_buf)) < 0) continue;
        uint16_t did = (uint16_t)strtoul(id_buf, NULL, 16);

        /* Only accept known GPU vendors */
        if (vid != 0x1002 && vid != 0x10DE && vid != 0x8086 && vid != 0x1A82)
            continue;

        gpu_info_t *g = &g_gpus[g_gpu_count++];
        memset(g, 0, sizeof(*g));
        strncpy(g->pci_slot, slot, sizeof(g->pci_slot) - 1);
        g->vendor_id = vid;
        g->device_id = did;
        snprintf(g->drm_node, sizeof(g->drm_node), "/dev/dri/%.50s", ent->d_name);
        g->enabled = true;

        /* Try to read a human-readable name */
        snprintf(id_path, sizeof(id_path), "%s/label", real_path);
        if (read_sysfs_str(id_path, g->name, sizeof(g->name)) < 0) {
            snprintf(g->name, sizeof(g->name), "%s GPU [%04x:%04x]",
                     vendor_name(vid), vid, did);
        }

        /* VRAM (AMD: mem_info_vram_total / mem_info_vram_used) */
        snprintf(id_path, sizeof(id_path), "%s/mem_info_vram_total", real_path);
        g->vram_total_bytes = read_sysfs_ll(id_path);
        snprintf(id_path, sizeof(id_path), "%s/mem_info_vram_used", real_path);
        g->vram_used_bytes = read_sysfs_ll(id_path);

        /* Temperature (hwmon) */
        char hwmon_base[768];
        snprintf(hwmon_base, sizeof(hwmon_base), "%s/hwmon", real_path);
        DIR *hw = opendir(hwmon_base);
        if (hw) {
            struct dirent *he;
            while ((he = readdir(hw)) != NULL) {
                if (strncmp(he->d_name, "hwmon", 5) != 0) continue;
                char temp_path[2048];
                snprintf(temp_path, sizeof(temp_path),
                         "%s/%s/temp1_input", hwmon_base, he->d_name);
                long long t = read_sysfs_ll(temp_path);
                if (t > 0) g->temperature_c = (int)(t / 1000);
                break;
            }
            closedir(hw);
        }

        /* GPU utilization (AMD: gpu_busy_percent) */
        snprintf(id_path, sizeof(id_path), "%s/gpu_busy_percent", real_path);
        long long util = read_sysfs_ll(id_path);
        if (util >= 0) g->utilization_pct = (int)util;
    }
    closedir(d);
}

/* -------------------------------------------------------------------------
 * Print functions
 * ---------------------------------------------------------------------- */

static void print_separator(char c, int width)
{
    for (int i = 0; i < width; i++) putchar(c);
    putchar('\n');
}

static void print_header(void)
{
    printf("\n");
    print_separator('=', 72);
    printf("  MVGAL — Multi-Vendor GPU Aggregation Layer  |  mvgal-info\n");
    print_separator('=', 72);

    struct utsname uts;
    if (uname(&uts) == 0) {
        printf("  Kernel : %s %s\n", uts.sysname, uts.release);
        printf("  Machine: %s\n", uts.machine);
    }

    /* Check if mvgal.ko is loaded */
    bool module_loaded = false;
    FILE *f = fopen("/proc/modules", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "mvgal ", 6) == 0) { module_loaded = true; break; }
        }
        fclose(f);
    }
    printf("  mvgal.ko: %s\n", module_loaded ? "loaded" : "not loaded");

    /* Check for /dev/mvgal0 */
    struct stat st;
    printf("  /dev/mvgal0: %s\n",
           stat("/dev/mvgal0", &st) == 0 ? "present" : "absent");
    print_separator('-', 72);
}

static void print_gpu(int idx, const gpu_info_t *g)
{
    printf("\n  GPU %d — %s\n", idx, g->name);
    print_separator('-', 60);
    printf("    PCI slot   : %s\n", g->pci_slot);
    printf("    Vendor ID  : 0x%04X  (%s)\n", g->vendor_id, vendor_name(g->vendor_id));
    printf("    Device ID  : 0x%04X\n", g->device_id);
    printf("    DRM node   : %s\n", g->drm_node);
    printf("    Status     : %s\n", g->enabled ? "enabled" : "disabled");

    if (g->vram_total_bytes > 0) {
        double total_gb = (double)g->vram_total_bytes / (1024.0 * 1024.0 * 1024.0);
        double used_gb  = (double)g->vram_used_bytes  / (1024.0 * 1024.0 * 1024.0);
        printf("    VRAM       : %.2f GiB total", total_gb);
        if (g->vram_used_bytes >= 0)
            printf(", %.2f GiB used (%.0f%%)",
                   used_gb,
                   total_gb > 0 ? (used_gb / total_gb * 100.0) : 0.0);
        printf("\n");
    } else {
        printf("    VRAM       : unknown\n");
    }

    if (g->temperature_c > 0)
        printf("    Temperature: %d °C\n", g->temperature_c);
    if (g->utilization_pct >= 0)
        printf("    Utilization: %d %%\n", g->utilization_pct);
}

static void print_logical_device(void)
{
    printf("\n");
    print_separator('=', 72);
    printf("  Logical MVGAL Device\n");
    print_separator('=', 72);
    printf("  Physical GPUs aggregated : %d\n", g_gpu_count);

    long long total_vram = 0;
    for (int i = 0; i < g_gpu_count; i++)
        if (g_gpus[i].vram_total_bytes > 0)
            total_vram += g_gpus[i].vram_total_bytes;

    if (total_vram > 0)
        printf("  Aggregate VRAM           : %.2f GiB\n",
               (double)total_vram / (1024.0 * 1024.0 * 1024.0));

    /* Capability tier */
    bool has_amd = false, has_nvidia = false, has_intel = false, has_mtt = false;
    for (int i = 0; i < g_gpu_count; i++) {
        switch (g_gpus[i].vendor_id) {
        case 0x1002: has_amd    = true; break;
        case 0x10DE: has_nvidia = true; break;
        case 0x8086: has_intel  = true; break;
        case 0x1A82: has_mtt    = true; break;
        }
    }

    printf("  Vendors present          :");
    if (has_amd)    printf(" AMD");
    if (has_nvidia) printf(" NVIDIA");
    if (has_intel)  printf(" Intel");
    if (has_mtt)    printf(" MooreThreads");
    if (!has_amd && !has_nvidia && !has_intel && !has_mtt) printf(" none");
    printf("\n");

    int vendor_count = (has_amd ? 1 : 0) + (has_nvidia ? 1 : 0) +
                       (has_intel ? 1 : 0) + (has_mtt ? 1 : 0);
    const char *tier = (vendor_count > 1) ? "Mixed (heterogeneous)" :
                       (g_gpu_count > 1)  ? "Homogeneous multi-GPU" :
                                            "Single GPU";
    printf("  Capability tier          : %s\n", tier);

    /* Vulkan layer registration */
    struct stat st;
    bool vk_layer = (stat("/usr/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json", &st) == 0) ||
                    (stat("/etc/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json", &st) == 0);
    printf("  Vulkan layer registered  : %s\n", vk_layer ? "yes" : "no");

    /* OpenCL ICD */
    bool ocl_icd = (stat("/etc/OpenCL/vendors/mvgal.icd", &st) == 0);
    printf("  OpenCL ICD registered    : %s\n", ocl_icd ? "yes" : "no");

    /* Daemon socket */
    bool daemon_sock = (stat("/run/mvgal/mvgal.sock", &st) == 0);
    printf("  Daemon socket            : %s\n",
           daemon_sock ? "/run/mvgal/mvgal.sock (present)" : "absent");
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    bool json_output = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0)
            json_output = true;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: mvgal-info [--json]\n");
            printf("  Print all detected GPUs and the MVGAL logical device configuration.\n");
            printf("  --json   Output in JSON format\n");
            return 0;
        }
    }

    enumerate_drm_gpus();

    if (json_output) {
        printf("{\n");
        printf("  \"gpu_count\": %d,\n", g_gpu_count);
        printf("  \"gpus\": [\n");
        for (int i = 0; i < g_gpu_count; i++) {
            const gpu_info_t *g = &g_gpus[i];
            printf("    {\n");
            printf("      \"index\": %d,\n", i);
            printf("      \"name\": \"%s\",\n", g->name);
            printf("      \"pci_slot\": \"%s\",\n", g->pci_slot);
            printf("      \"vendor_id\": \"0x%04X\",\n", g->vendor_id);
            printf("      \"device_id\": \"0x%04X\",\n", g->device_id);
            printf("      \"vendor\": \"%s\",\n", vendor_name(g->vendor_id));
            printf("      \"drm_node\": \"%s\",\n", g->drm_node);
            printf("      \"vram_total_bytes\": %lld,\n", g->vram_total_bytes);
            printf("      \"vram_used_bytes\": %lld,\n", g->vram_used_bytes);
            printf("      \"temperature_c\": %d,\n", g->temperature_c);
            printf("      \"utilization_pct\": %d,\n", g->utilization_pct);
            printf("      \"enabled\": %s\n", g->enabled ? "true" : "false");
            printf("    }%s\n", (i < g_gpu_count - 1) ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
        return 0;
    }

    print_header();

    if (g_gpu_count == 0) {
        printf("\n  No GPUs detected via /sys/class/drm.\n");
        printf("  Ensure GPU drivers (amdgpu, nvidia, i915/xe, mtgpu) are loaded.\n\n");
        return 1;
    }

    printf("\n  Detected %d GPU(s):\n", g_gpu_count);
    for (int i = 0; i < g_gpu_count; i++)
        print_gpu(i, &g_gpus[i]);

    print_logical_device();
    printf("\n");

    return 0;
}
