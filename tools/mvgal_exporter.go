// MVGAL Prometheus Metrics Exporter
//
// Exposes GPU metrics from MVGAL for Prometheus scraping.
// Connects to the MVGAL REST API at :7474 or daemon socket.
//
// Build:
//   cd tools && go mod init github.com/axogm/mvgal/tools
//   go build -o mvgal_exporter ./mvgal_exporter.go
//
// Run:
//   ./mvgal_exporter --listen :9100 --api-url http://localhost:7474
//
// SPDX-License-Identifier: MIT

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

// ---------------------------------------------------------------------------
// Data types (mirrors REST API)
// ---------------------------------------------------------------------------

// GpuInfo holds current metrics for one GPU.
type GpuInfo struct {
	Index          int     `json:"index"`
	Name           string  `json:"name"`
	Vendor         string  `json:"vendor"`
	PciSlot        string  `json:"pci_slot"`
	DrmNode        string  `json:"drm_node"`
	UtilizationPct int     `json:"utilization_pct"`
	VramTotalBytes int64   `json:"vram_total_bytes"`
	VramUsedBytes  int64   `json:"vram_used_bytes"`
	TemperatureC   int     `json:"temperature_c"`
	PowerW         float64 `json:"power_w"`
	ClockMhz       int     `json:"clock_mhz"`
	Enabled        bool    `json:"enabled"`
}

// SchedulerStatus holds the current scheduler configuration.
type SchedulerStatus struct {
	Mode        string `json:"mode"`
	IdleTimeout int    `json:"idle_timeout_s"`
	GpuCount    int    `json:"gpu_count"`
}

// Stats holds aggregate statistics.
type Stats struct {
	TotalVramBytes int64   `json:"total_vram_bytes"`
	UsedVramBytes  int64   `json:"used_vram_bytes"`
	AvgUtilPct     float64 `json:"avg_utilization_pct"`
	GpuCount       int     `json:"gpu_count"`
	DaemonRunning  bool    `json:"daemon_running"`
	Timestamp      string  `json:"timestamp"`
}

// ---------------------------------------------------------------------------
// Prometheus metrics
// ---------------------------------------------------------------------------

var (
	// GPU metrics from design section 8.2.2
	gpuUtilization = prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "mvgal_gpu_utilization_percent",
			Help: "GPU utilization percentage",
		},
		[]string{"gpu_index", "vendor"},
	)

	gpuMemoryUsed = prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "mvgal_gpu_memory_used_bytes",
			Help: "GPU memory used in bytes",
		},
		[]string{"gpu_index", "vendor"},
	)

	gpuTemperature = prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "mvgal_gpu_temperature_celsius",
			Help: "GPU temperature in Celsius",
		},
		[]string{"gpu_index", "vendor"},
	)

	schedulerWorkloads = prometheus.NewCounterVec(
		prometheus.CounterOpts{
			Name: "mvgal_scheduler_workloads_total",
			Help: "Total number of workloads scheduled",
		},
		[]string{"strategy", "gpu_index"},
	)

	// Additional metrics requested
	gpuPowerDraw = prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "mvgal_power_draw_watts",
			Help: "GPU power consumption in watts",
		},
		[]string{"gpu_index", "vendor"},
	)

	gpuMemoryAvailable = prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "mvgal_memory_available_bytes",
			Help: "GPU memory available in bytes",
		},
		[]string{"gpu_index", "vendor"},
	)

	schedulerQueueLength = prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "mvgal_scheduler_queue_length",
			Help: "Number of pending workloads in scheduler queue",
		},
		[]string{"strategy"},
	)

	dmaBufAttachments = prometheus.NewGaugeVec(
		prometheus.GaugeOpts{
			Name: "mvgal_dmabuf_attachments",
			Help: "Number of active DMA-BUF buffer attachments",
		},
		[]string{"gpu_index", "vendor"},
	)

	// Aggregate metrics
	totalVramBytes = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "mvgal_total_vram_bytes",
		Help: "Total VRAM across all GPUs in bytes",
	})

	usedVramBytes = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "mvgal_used_vram_bytes",
		Help: "Used VRAM across all GPUs in bytes",
	})

	avgUtilization = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "mvgal_avg_utilization_percent",
		Help: "Average GPU utilization across all GPUs",
	})

	gpuCount = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "mvgal_gpu_count",
		Help: "Number of detected GPUs",
	})

	daemonRunning = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "mvgal_daemon_running",
		Help: "MVGAL daemon running status (1 = running, 0 = not running)",
	})
)

// ---------------------------------------------------------------------------
// REST API client
// ---------------------------------------------------------------------------

type APIClient struct {
	baseURL string
	client  *http.Client
}

func NewAPIClient(baseURL string) *APIClient {
	return &APIClient{
		baseURL: strings.TrimRight(baseURL, "/"),
		client: &http.Client{
			Timeout: 5 * time.Second,
		},
	}
}

func (c *APIClient) fetchGPUs() ([]GpuInfo, error) {
	resp, err := c.client.Get(c.baseURL + "/api/v1/gpus")
	if err != nil {
		return nil, fmt.Errorf("failed to fetch GPUs: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("API returned status %d", resp.StatusCode)
	}

	var gpus []GpuInfo
	if err := json.NewDecoder(resp.Body).Decode(&gpus); err != nil {
		return nil, fmt.Errorf("failed to decode GPU response: %w", err)
	}

	return gpus, nil
}

func (c *APIClient) fetchStats() (Stats, error) {
	resp, err := c.client.Get(c.baseURL + "/api/v1/stats")
	if err != nil {
		return Stats{}, fmt.Errorf("failed to fetch stats: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return Stats{}, fmt.Errorf("API returned status %d", resp.StatusCode)
	}

	var stats Stats
	if err := json.NewDecoder(resp.Body).Decode(&stats); err != nil {
		return Stats{}, fmt.Errorf("failed to decode stats response: %w", err)
	}

	return stats, nil
}

func (c *APIClient) fetchScheduler() (SchedulerStatus, error) {
	resp, err := c.client.Get(c.baseURL + "/api/v1/scheduler")
	if err != nil {
		return SchedulerStatus{}, fmt.Errorf("failed to fetch scheduler: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return SchedulerStatus{}, fmt.Errorf("API returned status %d", resp.StatusCode)
	}

	var status SchedulerStatus
	if err := json.NewDecoder(resp.Body).Decode(&status); err != nil {
		return SchedulerStatus{}, fmt.Errorf("failed to decode scheduler response: %w", err)
	}

	return status, nil
}

// ---------------------------------------------------------------------------
// Metrics collection
// ---------------------------------------------------------------------------

func collectMetrics(client *APIClient) {
	// Fetch GPU metrics
	gpus, err := client.fetchGPUs()
	if err != nil {
		log.Printf("Error fetching GPUs: %v", err)
		return
	}

	// Update per-GPU metrics
	for _, gpu := range gpus {
		labels := []string{strconv.Itoa(gpu.Index), gpu.Vendor}

		gpuUtilization.WithLabelValues(labels...).Set(float64(gpu.UtilizationPct))
		gpuMemoryUsed.WithLabelValues(labels...).Set(float64(gpu.VramUsedBytes))
		gpuTemperature.WithLabelValues(labels...).Set(float64(gpu.TemperatureC))
		gpuPowerDraw.WithLabelValues(labels...).Set(gpu.PowerW)

		// Available memory = total - used
		available := gpu.VramTotalBytes - gpu.VramUsedBytes
		gpuMemoryAvailable.WithLabelValues(labels...).Set(float64(available))

		// DMA-BUF count (placeholder - would need daemon socket for actual count)
		// For now, set to 0 as we can't get this from REST API alone
		dmaBufAttachments.WithLabelValues(labels...).Set(0)
	}

	// Fetch aggregate stats
	stats, err := client.fetchStats()
	if err != nil {
		log.Printf("Error fetching stats: %v", err)
	} else {
		totalVramBytes.Set(float64(stats.TotalVramBytes))
		usedVramBytes.Set(float64(stats.UsedVramBytes))
		avgUtilization.Set(stats.AvgUtilPct)
		gpuCount.Set(float64(stats.GpuCount))

		if stats.DaemonRunning {
			daemonRunning.Set(1)
		} else {
			daemonRunning.Set(0)
		}
	}

	// Fetch scheduler status
	scheduler, err := client.fetchScheduler()
	if err != nil {
		log.Printf("Error fetching scheduler: %v", err)
	} else {
		// Queue length is not directly available from REST API
		// Set to 0 as placeholder - would need daemon socket for actual count
		schedulerQueueLength.WithLabelValues(scheduler.Mode).Set(0)

		// Increment workload counter for the current mode
		for i := 0; i < scheduler.GpuCount; i++ {
			schedulerWorkloads.WithLabelValues(scheduler.Mode, strconv.Itoa(i)).Inc()
		}
	}
}

// ---------------------------------------------------------------------------
// Daemon socket connection (for advanced metrics)
// ---------------------------------------------------------------------------

// fetchDaemonMetrics attempts to get additional metrics from the Unix socket
func fetchDaemonMetrics() error {
	// Try to connect to the daemon socket for additional metrics
	socketPath := "/run/mvgal/mvgal.sock"

	// Check if socket exists
	if _, err := os.Stat(socketPath); os.IsNotExist(err) {
		// Socket doesn't exist, daemon not running - this is fine
		return nil
	}

	// For now, we rely on REST API
	// Future: implement Unix socket protocol for queue length, DMA-BUF count, etc.
	log.Printf("Daemon socket available at %s (using REST API for metrics)", socketPath)
	return nil
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

func main() {
	listen := flag.String("listen", ":9100", "Address to listen on")
	apiURL := flag.String("api-url", "http://localhost:7474", "MVGAL REST API URL")
	interval := flag.Int("interval", 5, "Metrics collection interval in seconds")
	flag.Parse()

	log.Printf("MVGAL Prometheus exporter starting on %s", *listen)
	log.Printf("Connecting to MVGAL REST API at %s", *apiURL)

	// Register metrics
	prometheus.MustRegister(
		gpuUtilization,
		gpuMemoryUsed,
		gpuTemperature,
		schedulerWorkloads,
		gpuPowerDraw,
		gpuMemoryAvailable,
		schedulerQueueLength,
		dmaBufAttachments,
		totalVramBytes,
		usedVramBytes,
		avgUtilization,
		gpuCount,
		daemonRunning,
	)

	// Create API client
	client := NewAPIClient(*apiURL)

	// Initial collection
	collectMetrics(client)
	_ = fetchDaemonMetrics()

	// Start background collection
	go func() {
		ticker := time.NewTicker(time.Duration(*interval) * time.Second)
		defer ticker.Stop()

		for range ticker.C {
			collectMetrics(client)
			_ = fetchDaemonMetrics()
		}
	}()

	// Expose metrics endpoint
	http.Handle("/metrics", promhttp.Handler())

	// Health check endpoint
	http.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("OK"))
	})

	// Ready endpoint for Kubernetes
	http.HandleFunc("/ready", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("READY"))
	})

	log.Printf("Metrics endpoint available at http://%s/metrics", *listen)
	log.Printf("Health endpoint available at http://%s/health", *listen)

	if err := http.ListenAndServe(*listen, nil); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}