/**
 * mvgal-status — Show real-time GPU utilization, memory usage, and scheduler
 * decisions.  Polls the daemon socket or sysfs at a configurable interval.
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
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAX_GPUS 16
#define DAEMON_SOCKET "/run/mvgal/mvgal.sock"

/* -------------------------------------------------------------------------
 * Signal handling
 * ---------------------------------------------------------------------- */

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* -------------------------------------------------------------------------
 * sysfs helpers
 * ---------------------------------------------------------------------- */

static int sysfs_read_str(const char *path, char *buf, size_t sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, (ssize_t)(sz - 1));
    close(fd);
    if (n <= 0) return -1;
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) n--;
    buf[n] = '\0';
    return 0;
}

static long long sysfs_read_ll(const char *path)
{
    char buf[64];
    if (sysfs_read_str(path, buf, sizeof(buf)) < 0) return -1;
    return strtoll(buf, NULL, 0);
}

/* -------------------------------------------------------------------------
 * GPU state
 * ---------------------------------------------------------------------- */

typedef struct {
    char     name[128];
    char     pci_slot[64];
    uint16_t vendor_id;
    char     drm_node[64];
    int      utilization_pct;
    long long vram_total;
    long long vram_used;
    int      temperature_c;
    int      power_mw;
    int      clock_mhz;
    bool     valid;
} gpu_status_t;

static int g_gpu_count = 0;
static gpu_status_t g_gpus[MAX_GPUS];

static const char *vendor_name(uint16_t vid)
{
    switch (vid) {
    case 0x1002: return "AMD";
    case 0x10DE: return "NVIDIA";
    case 0x8086: return "Intel";
    case 0x1A82: return "MTT";
    default:     return "???";
    }
}

static void discover_gpus(void)
{
    g_gpu_count = 0;
    DIR *d = opendir("/sys/class/drm");
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && g_gpu_count < MAX_GPUS) {
        if (strncmp(ent->d_name, "card", 4) != 0) continue;
        const char *p = ent->d_name + 4;
        bool digits = true;
        for (; *p; p++) if (*p < '0' || *p > '9') { digits = false; break; }
        if (!digits) continue;

        char sysfs[512];
        snprintf(sysfs, sizeof(sysfs), "/sys/class/drm/%s/device", ent->d_name);
        char real[768];
        if (realpath(sysfs, real) == NULL) continue;

        char id_path[1024], id_buf[16];
        snprintf(id_path, sizeof(id_path), "%s/vendor", real);
        if (sysfs_read_str(id_path, id_buf, sizeof(id_buf)) < 0) continue;
        uint16_t vid = (uint16_t)strtoul(id_buf, NULL, 16);
        if (vid != 0x1002 && vid != 0x10DE && vid != 0x8086 && vid != 0x1A82) continue;

        gpu_status_t *g = &g_gpus[g_gpu_count++];
        memset(g, 0, sizeof(*g));
        g->vendor_id = vid;
        g->valid = true;

        char *slot = strrchr(real, '/');
        if (slot) strncpy(g->pci_slot, slot + 1, sizeof(g->pci_slot) - 1);
        snprintf(g->drm_node, sizeof(g->drm_node), "/dev/dri/%.50s", ent->d_name);
        {
            const char *vn = vendor_name(vid);
            char pci_copy[64];
            strncpy(pci_copy, g->pci_slot, sizeof(pci_copy) - 1);
            pci_copy[sizeof(pci_copy) - 1] = '\0';
            snprintf(g->name, sizeof(g->name), "%.20s [%.40s]", vn, pci_copy);
        }
    }
    closedir(d);
}

static void refresh_gpu_status(void)
{
    for (int i = 0; i < g_gpu_count; i++) {
        gpu_status_t *g = &g_gpus[i];
        char base[512];
        snprintf(base, sizeof(base), "/sys/class/drm/card%d/device", i);
        char real[768];
        if (realpath(base, real) == NULL) {
            /* Try by PCI slot */
            snprintf(real, sizeof(real), "/sys/bus/pci/devices/%s", g->pci_slot);
        }

        char path[800];

        /* Utilization */
        snprintf(path, sizeof(path), "%s/gpu_busy_percent", real);
        long long u = sysfs_read_ll(path);
        g->utilization_pct = (u >= 0) ? (int)u : -1;

        /* VRAM */
        snprintf(path, sizeof(path), "%s/mem_info_vram_total", real);
        g->vram_total = sysfs_read_ll(path);
        snprintf(path, sizeof(path), "%s/mem_info_vram_used", real);
        g->vram_used = sysfs_read_ll(path);

        /* Temperature via hwmon */
        char hwmon_base[800];
        snprintf(hwmon_base, sizeof(hwmon_base), "%s/hwmon", real);
        DIR *hw = opendir(hwmon_base);
        if (hw) {
            struct dirent *he;
            while ((he = readdir(hw)) != NULL) {
                if (strncmp(he->d_name, "hwmon", 5) != 0) continue;
                char tp[1200];
                snprintf(tp, sizeof(tp), "%s/%s/temp1_input", hwmon_base, he->d_name);
                long long t = sysfs_read_ll(tp);
                if (t > 0) g->temperature_c = (int)(t / 1000);

                /* Power */
                snprintf(tp, sizeof(tp), "%s/%s/power1_average", hwmon_base, he->d_name);
                long long pw = sysfs_read_ll(tp);
                if (pw > 0) g->power_mw = (int)(pw / 1000);

                /* Clock */
                snprintf(tp, sizeof(tp), "%s/%s/freq1_input", hwmon_base, he->d_name);
                long long clk = sysfs_read_ll(tp);
                if (clk > 0) g->clock_mhz = (int)(clk / 1000000);
                break;
            }
            closedir(hw);
        }
    }
}

/* -------------------------------------------------------------------------
 * Daemon socket query (best-effort)
 * ---------------------------------------------------------------------- */

static bool query_daemon_status(char *out_buf, size_t out_sz)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DAEMON_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    /* Send a minimal status request (magic + type=GET_STATISTICS) */
    uint8_t req[28];
    memset(req, 0, sizeof(req));
    /* magic = 'MVGL' */
    req[0] = 0x4D; req[1] = 0x56; req[2] = 0x47; req[3] = 0x4C;
    /* version = 1 */
    req[4] = 1;
    /* messageType = 50 (GET_STATISTICS) */
    req[8] = 50;
    /* flags = REQUEST (1) */
    req[20] = 1;

    write(fd, req, sizeof(req));

    ssize_t n = read(fd, out_buf, (ssize_t)(out_sz - 1));
    close(fd);
    if (n > 0) { out_buf[n] = '\0'; return true; }
    return false;
}

/* -------------------------------------------------------------------------
 * Display
 * ---------------------------------------------------------------------- */

static void clear_screen(void)
{
    /* ANSI escape: clear screen and move cursor to top-left */
    printf("\033[2J\033[H");
}

static void print_bar(int pct, int width)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = (pct * width) / 100;
    putchar('[');
    for (int i = 0; i < width; i++)
        putchar(i < filled ? '#' : ' ');
    putchar(']');
}

static void print_status(bool continuous)
{
    if (continuous) clear_screen();

    /* Timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    printf("MVGAL Status  [%s]  (press Ctrl-C to quit)\n", ts);
    printf("══════════════════════════════════════════════════════════════\n");

    /* Daemon status */
    char daemon_buf[256];
    bool daemon_up = query_daemon_status(daemon_buf, sizeof(daemon_buf));
    printf("Daemon: %s\n", daemon_up ? "running" : "not running");

    /* Kernel module */
    bool module_loaded = false;
    FILE *f = fopen("/proc/modules", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f))
            if (strncmp(line, "mvgal ", 6) == 0) { module_loaded = true; break; }
        fclose(f);
    }
    printf("Kernel module: %s\n\n", module_loaded ? "loaded" : "not loaded");

    if (g_gpu_count == 0) {
        printf("No GPUs detected.\n");
        return;
    }

    refresh_gpu_status();

    for (int i = 0; i < g_gpu_count; i++) {
        const gpu_status_t *g = &g_gpus[i];
        printf("GPU %d  %s\n", i, g->name);
        printf("  DRM node : %s\n", g->drm_node);

        /* Utilization bar */
        printf("  Util     : ");
        if (g->utilization_pct >= 0) {
            print_bar(g->utilization_pct, 30);
            printf(" %3d%%\n", g->utilization_pct);
        } else {
            printf("n/a\n");
        }

        /* VRAM bar */
        printf("  VRAM     : ");
        if (g->vram_total > 0) {
            int vram_pct = (int)((g->vram_used * 100) / g->vram_total);
            print_bar(vram_pct, 30);
            printf(" %.1f / %.1f GiB\n",
                   (double)g->vram_used  / (1024.0*1024.0*1024.0),
                   (double)g->vram_total / (1024.0*1024.0*1024.0));
        } else {
            printf("n/a\n");
        }

        if (g->temperature_c > 0)
            printf("  Temp     : %d °C\n", g->temperature_c);
        if (g->power_mw > 0)
            printf("  Power    : %.1f W\n", (double)g->power_mw / 1000.0);
        if (g->clock_mhz > 0)
            printf("  Clock    : %d MHz\n", g->clock_mhz);
        printf("\n");
    }
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int interval_ms = 1000;
    bool continuous = false;
    bool once = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--watch") == 0 || strcmp(argv[i], "-w") == 0) {
            continuous = true;
        } else if (strcmp(argv[i], "--once") == 0 || strcmp(argv[i], "-1") == 0) {
            once = true;
        } else if ((strcmp(argv[i], "--interval") == 0 || strcmp(argv[i], "-i") == 0)
                   && i + 1 < argc) {
            interval_ms = atoi(argv[++i]);
            if (interval_ms < 100) interval_ms = 100;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: mvgal-status [--watch] [--once] [--interval MS]\n");
            printf("  --watch / -w       Continuously refresh (default interval 1000 ms)\n");
            printf("  --once  / -1       Print once and exit\n");
            printf("  --interval MS      Refresh interval in milliseconds\n");
            return 0;
        }
    }

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    discover_gpus();

    if (once || !continuous) {
        print_status(false);
        return 0;
    }

    /* Continuous watch mode */
    while (g_running) {
        print_status(true);
        struct timespec ts = {
            .tv_sec  = interval_ms / 1000,
            .tv_nsec = (long)(interval_ms % 1000) * 1000000L,
        };
        nanosleep(&ts, NULL);
    }

    printf("\nmvgal-status: exiting.\n");
    return 0;
}
