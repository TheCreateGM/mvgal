/**
 * @file mvgal_prometheus.c
 * @brief Prometheus Metrics Endpoint Implementation
 *
 * Lightweight embedded HTTP server exposing MVGAL metrics
 * in Prometheus text-based exposition format.
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include "mvgal_prometheus.h"
#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define PROMETHEUS_MAX_METRICS    256
#define PROMETHEUS_BUF_SIZE       (64 * 1024)
#define PROMETHEUS_RESPONSE_SIZE  (128 * 1024)
#define PROMETHEUS_MAX_LABELS     8
#define PROMETHEUS_MAX_NAME       128

/* ============================================================================
 * Metric Entry
 * ============================================================================ */

typedef struct {
    char        name[PROMETHEUS_MAX_NAME];
    char        help[256];
    mvgal_metric_type_t type;
    uint32_t    num_labels;
    char        label_names[PROMETHEUS_MAX_LABELS][64];
    double      value;
    double      label_values[PROMETHEUS_MAX_LABELS];
    bool        used;
} metric_entry_t;

/* ============================================================================
 * Internal State
 * ============================================================================ */

static metric_entry_t g_metrics[PROMETHEUS_MAX_METRICS];
static uint32_t g_num_metrics = 0;
static pthread_mutex_t g_metrics_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t g_http_thread;
static bool g_http_running = false;
static int g_http_sock = -1;
static char g_listen_addr[64] = "127.0.0.1";
static uint16_t g_listen_port = 9100;

/* ============================================================================
 * Built-in GPU Metrics
 * ============================================================================ */

static void register_builtin_metrics(void) {
    /* GPU utilization */
    static const char *gpu_labels[] = {"gpu", "vendor"};
    mvgal_metric_desc_t desc;

    desc = (mvgal_metric_desc_t){
        "mvgal_gpu_utilization_percent", "GPU utilization percentage",
        MVGAL_METRIC_GAUGE, gpu_labels, 2
    };
    mvgal_prometheus_register_metric(&desc);

    desc = (mvgal_metric_desc_t){
        "mvgal_gpu_vram_total_bytes", "Total VRAM per GPU",
        MVGAL_METRIC_GAUGE, gpu_labels, 2
    };
    mvgal_prometheus_register_metric(&desc);

    desc = (mvgal_metric_desc_t){
        "mvgal_gpu_vram_used_bytes", "Used VRAM per GPU",
        MVGAL_METRIC_GAUGE, gpu_labels, 2
    };
    mvgal_prometheus_register_metric(&desc);

    desc = (mvgal_metric_desc_t){
        "mvgal_gpu_temperature_celsius", "GPU temperature in Celsius",
        MVGAL_METRIC_GAUGE, gpu_labels, 2
    };
    mvgal_prometheus_register_metric(&desc);

    desc = (mvgal_metric_desc_t){
        "mvgal_gpu_power_watts", "GPU power draw in watts",
        MVGAL_METRIC_GAUGE, gpu_labels, 2
    };
    mvgal_prometheus_register_metric(&desc);

    desc = (mvgal_metric_desc_t){
        "mvgal_gpu_clock_mhz", "GPU core clock in MHz",
        MVGAL_METRIC_GAUGE, gpu_labels, 2
    };
    mvgal_prometheus_register_metric(&desc);

    /* Scheduler / system metrics */
    desc = (mvgal_metric_desc_t){
        "mvgal_scheduler_frames_submitted", "Total frames submitted",
        MVGAL_METRIC_COUNTER, NULL, 0
    };
    mvgal_prometheus_register_metric(&desc);

    desc = (mvgal_metric_desc_t){
        "mvgal_scheduler_queued_workloads", "Currently queued workloads",
        MVGAL_METRIC_GAUGE, NULL, 0
    };
    mvgal_prometheus_register_metric(&desc);

    desc = (mvgal_metric_desc_t){
        "mvgal_memory_dmabuf_active", "Active DMA-BUF allocations",
        MVGAL_METRIC_GAUGE, NULL, 0
    };
    mvgal_prometheus_register_metric(&desc);

    /* Daemon health */
    desc = (mvgal_metric_desc_t){
        "mvgal_daemon_up", "Whether the MVGAL daemon is running",
        MVGAL_METRIC_GAUGE, NULL, 0
    };
    mvgal_prometheus_register_metric(&desc);
}

/* ============================================================================
 * Metric Registration
 * ============================================================================ */

mvgal_error_t mvgal_prometheus_register_metric(const mvgal_metric_desc_t *desc) {
    if (!desc) return MVGAL_ERROR_INVALID_ARGUMENT;

    pthread_mutex_lock(&g_metrics_lock);
    if (g_num_metrics >= PROMETHEUS_MAX_METRICS) {
        pthread_mutex_unlock(&g_metrics_lock);
        return MVGAL_ERROR_OUT_OF_MEMORY;
    }

    metric_entry_t *m = &g_metrics[g_num_metrics++];
    memset(m, 0, sizeof(*m));
    strncpy(m->name, desc->name, sizeof(m->name) - 1);
    if (desc->help) strncpy(m->help, desc->help, sizeof(m->help) - 1);
    m->type = desc->type;
    m->num_labels = desc->num_labels;
    for (uint32_t i = 0; i < desc->num_labels && i < PROMETHEUS_MAX_LABELS; i++) {
        strncpy(m->label_names[i], desc->label_names[i], sizeof(m->label_names[i]) - 1);
    }
    m->used = true;

    pthread_mutex_unlock(&g_metrics_lock);
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Gauge / Counter / Histogram helpers
 * ============================================================================ */

static metric_entry_t *find_metric(const char *name) {
    for (uint32_t i = 0; i < g_num_metrics; i++) {
        if (g_metrics[i].used && strcmp(g_metrics[i].name, name) == 0) {
            return &g_metrics[i];
        }
    }
    return NULL;
}

mvgal_error_t mvgal_prometheus_gauge_set(
    const char *name, double value, const char *const *label_values)
{
    pthread_mutex_lock(&g_metrics_lock);
    metric_entry_t *m = find_metric(name);
    if (!m) {
        pthread_mutex_unlock(&g_metrics_lock);
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    m->value = value;
    if (label_values) {
        for (uint32_t i = 0; i < m->num_labels; i++) {
            m->label_values[i] = label_values[i] ? atof(label_values[i]) : 0.0;
        }
    }
    pthread_mutex_unlock(&g_metrics_lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_prometheus_counter_add(
    const char *name, double value, const char *const *label_values)
{
    pthread_mutex_lock(&g_metrics_lock);
    metric_entry_t *m = find_metric(name);
    if (!m || m->type != MVGAL_METRIC_COUNTER) {
        pthread_mutex_unlock(&g_metrics_lock);
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    m->value += value;
    (void)label_values;
    pthread_mutex_unlock(&g_metrics_lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_prometheus_histogram_observe(
    const char *name, double value, const char *const *label_values)
{
    (void)label_values;
    /* Simplified: histograms not yet bucketed */
    pthread_mutex_lock(&g_metrics_lock);
    metric_entry_t *m = find_metric(name);
    if (!m) {
        pthread_mutex_unlock(&g_metrics_lock);
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    m->value = value;
    pthread_mutex_unlock(&g_metrics_lock);
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * Generate /metrics Output
 * ============================================================================ */

mvgal_error_t mvgal_prometheus_generate(char *buffer, size_t capacity, size_t *written) {
    if (!buffer || !written) return MVGAL_ERROR_INVALID_ARGUMENT;

    pthread_mutex_lock(&g_metrics_lock);

    char *ptr = buffer;
    size_t remaining = capacity;

    /* Update GPU metrics from live data */
    int32_t gpu_count = mvgal_gpu_get_count();
    if (gpu_count < 0) gpu_count = 0;

    /* Daemon uptime */
    metric_entry_t *up = find_metric("mvgal_daemon_up");
    if (up) up->value = 1.0;

    /* Per-GPU metrics */
    for (int32_t g = 0; g < gpu_count; g++) {
        mvgal_gpu_descriptor_t info;
        if (mvgal_gpu_get_descriptor((uint32_t)g, &info) != MVGAL_SUCCESS)
            continue;

        char gpu_idx[16];
        snprintf(gpu_idx, sizeof(gpu_idx), "%d", g);

        /* Utilization */
        metric_entry_t *util = find_metric("mvgal_gpu_utilization_percent");
        if (util) {
            snprintf(ptr, remaining,
                     "mvgal_gpu_utilization_percent{gpu=\"%s\",vendor=\"%u\"} %u\n",
                     gpu_idx, (unsigned)info.vendor, (unsigned)info.utilization_percent);
            size_t len = strlen(ptr);
            ptr += len; remaining -= len;
        }

        /* VRAM */
        metric_entry_t *vram_t = find_metric("mvgal_gpu_vram_total_bytes");
        if (vram_t) {
            snprintf(ptr, remaining,
                     "mvgal_gpu_vram_total_bytes{gpu=\"%s\",vendor=\"%u\"} %lu\n",
                     gpu_idx, (unsigned)info.vendor, (unsigned long)info.vram_total);
            size_t len = strlen(ptr);
            ptr += len; remaining -= len;
        }

        metric_entry_t *vram_u = find_metric("mvgal_gpu_vram_used_bytes");
        if (vram_u) {
            snprintf(ptr, remaining,
                     "mvgal_gpu_vram_used_bytes{gpu=\"%s\",vendor=\"%u\"} %lu\n",
                     gpu_idx, (unsigned)info.vendor, (unsigned long)info.vram_used);
            size_t len = strlen(ptr);
            ptr += len; remaining -= len;
        }

        /* Temperature */
        metric_entry_t *temp = find_metric("mvgal_gpu_temperature_celsius");
        if (temp) {
            snprintf(ptr, remaining,
                     "mvgal_gpu_temperature_celsius{gpu=\"%s\",vendor=\"%u\"} %d\n",
                     gpu_idx, (unsigned)info.vendor, (int)info.temperature_celsius);
            size_t len = strlen(ptr);
            ptr += len; remaining -= len;
        }

        /* Power */
        metric_entry_t *power = find_metric("mvgal_gpu_power_watts");
        if (power) {
            snprintf(ptr, remaining,
                     "mvgal_gpu_power_watts{gpu=\"%s\",vendor=\"%u\"} %d\n",
                     gpu_idx, (unsigned)info.vendor, (int)info.current_power_w);
            size_t len = strlen(ptr);
            ptr += len; remaining -= len;
        }

        /* Clock */
        metric_entry_t *clock = find_metric("mvgal_gpu_clock_mhz");
        if (clock) {
            /* Use bandwidth as a placeholder if core_clock_mhz is missing */
            snprintf(ptr, remaining,
                     "mvgal_gpu_clock_mhz{gpu=\"%s\",vendor=\"%u\"} %u\n",
                     gpu_idx, (unsigned)info.vendor, (unsigned)info.memory_bandwidth_gbps);
            size_t len = strlen(ptr);
            ptr += len; remaining -= len;
        }
    }

    /* Counter metrics: show with TYPE and HELP headers */
    for (uint32_t i = 0; i < g_num_metrics && remaining > 128; i++) {
        if (!g_metrics[i].used) continue;

        int n;
        /* HELP */
        if (g_metrics[i].help[0]) {
            n = snprintf(ptr, remaining, "# HELP %s %s\n",
                         g_metrics[i].name, g_metrics[i].help);
            ptr += n; remaining -= (size_t)(n > 0 ? n : 0);
        }

        /* TYPE */
        const char *type_str = "untyped";
        switch (g_metrics[i].type) {
            case MVGAL_METRIC_GAUGE:     type_str = "gauge";    break;
            case MVGAL_METRIC_COUNTER:   type_str = "counter";  break;
            case MVGAL_METRIC_HISTOGRAM: type_str = "histogram"; break;
            case MVGAL_METRIC_SUMMARY:   type_str = "summary";  break;
        }
        n = snprintf(ptr, remaining, "# TYPE %s %s\n",
                     g_metrics[i].name, type_str);
        ptr += n; remaining -= (size_t)(n > 0 ? n : 0);

        /* Value (with labels if present) */
        if (g_num_metrics > 0 && g_metrics[i].num_labels > 0) {
            /* Labeled metrics were already emitted inline above */
            continue;
        }

        n = snprintf(ptr, remaining, "%s %g\n",
                     g_metrics[i].name, g_metrics[i].value);
        ptr += n; remaining -= (size_t)(n > 0 ? n : 0);
    }

    pthread_mutex_unlock(&g_metrics_lock);

    *written = (size_t)(ptr - buffer);
    return MVGAL_SUCCESS;
}

/* ============================================================================
 * HTTP Server
 * ============================================================================ */

static void handle_http_request(int client_fd) {
    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buf[n] = '\0';

    /* Check for GET /metrics */
    if (strstr(buf, "GET /metrics") == NULL && strstr(buf, "GET /") != NULL) {
        const char *root_resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "MVGAL Prometheus Metrics\n"
            "GET /metrics for Prometheus-formatted output\n";
        write(client_fd, root_resp, strlen(root_resp));
        close(client_fd);
        return;
    }

    /* Generate /metrics */
    char response[PROMETHEUS_RESPONSE_SIZE];
    size_t written = 0;

    mvgal_prometheus_generate(response, sizeof(response), &written);
    
    char header[256];
    int header_len = snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
                             "Content-Length: %zu\r\n"
                             "Connection: close\r\n\r\n",
                             written);
    write(client_fd, header, (size_t)header_len);
    write(client_fd, response, written);
    close(client_fd);
}

static void *http_server_thread(void *arg) {
    (void)arg;
    struct sockaddr_in addr;
    int opt = 1;

    g_http_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_http_sock < 0) {
        mvgal_log_error("prometheus: socket() failed");
        return NULL;
    }

    setsockopt(g_http_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_listen_port);
    inet_pton(AF_INET, g_listen_addr, &addr.sin_addr);

    if (bind(g_http_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mvgal_log_error("prometheus: bind() failed on %s:%u", g_listen_addr, g_listen_port);
        close(g_http_sock);
        g_http_sock = -1;
        return NULL;
    }

    if (listen(g_http_sock, 10) < 0) {
        mvgal_log_error("prometheus: listen() failed");
        close(g_http_sock);
        g_http_sock = -1;
        return NULL;
    }

    mvgal_log_info("prometheus: listening on %s:%u",
              g_listen_addr, g_listen_port);

    while (g_http_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(g_http_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            if (!g_http_running) break;
            mvgal_log_error("prometheus: accept() failed");
            continue;
        }

        handle_http_request(client_fd);
    }

    return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

mvgal_error_t mvgal_prometheus_start(const char *listen_addr, uint16_t port) {
    if (!listen_addr) return MVGAL_ERROR_INVALID_ARGUMENT;

    pthread_mutex_lock(&g_metrics_lock);
    if (g_http_running) {
        pthread_mutex_unlock(&g_metrics_lock);
        return MVGAL_SUCCESS;
    }

    strncpy(g_listen_addr, listen_addr, sizeof(g_listen_addr) - 1);
    g_listen_port = port;
    g_http_running = true;

    register_builtin_metrics();

    if (pthread_create(&g_http_thread, NULL, http_server_thread, NULL) != 0) {
        g_http_running = false;
        pthread_mutex_unlock(&g_metrics_lock);
        mvgal_log_error("prometheus: thread creation failed");
        return MVGAL_ERROR_INITIALIZATION;
    }

    pthread_mutex_unlock(&g_metrics_lock);
    return MVGAL_SUCCESS;
}

mvgal_error_t mvgal_prometheus_stop(void) {
    pthread_mutex_lock(&g_metrics_lock);
    if (!g_http_running) {
        pthread_mutex_unlock(&g_metrics_lock);
        return MVGAL_SUCCESS;
    }

    g_http_running = false;
    if (g_http_sock >= 0) {
        shutdown(g_http_sock, SHUT_RDWR);
        close(g_http_sock);
        g_http_sock = -1;
    }
    pthread_mutex_unlock(&g_metrics_lock);

    pthread_join(g_http_thread, NULL);
    mvgal_log_info("prometheus: stopped");
    return MVGAL_SUCCESS;
}

bool mvgal_prometheus_is_running(void) {
    return g_http_running;
}
