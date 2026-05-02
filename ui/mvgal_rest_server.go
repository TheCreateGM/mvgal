// MVGAL REST API Server
//
// Provides a JSON REST API consumed by the Qt dashboard and CLI tools.
// Reads GPU metrics from sysfs and the MVGAL daemon socket.
//
// Build:
//   go build -o mvgal-rest-server ./mvgal_rest_server.go
//
// Run:
//   ./mvgal-rest-server --listen :7474
//
// SPDX-License-Identifier: MIT

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// ---------------------------------------------------------------------------
// Data types
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
// sysfs helpers
// ---------------------------------------------------------------------------

func readSysfsStr(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(data)), nil
}

func readSysfsInt64(path string) (int64, error) {
	s, err := readSysfsStr(path)
	if err != nil {
		return 0, err
	}
	return strconv.ParseInt(s, 0, 64)
}

func vendorName(vid uint64) string {
	switch vid {
	case 0x1002:
		return "AMD"
	case 0x10DE:
		return "NVIDIA"
	case 0x8086:
		return "Intel"
	case 0x1A82:
		return "MooreThreads"
	default:
		return "Unknown"
	}
}

// ---------------------------------------------------------------------------
// GPU enumeration
// ---------------------------------------------------------------------------

func enumerateGPUs() ([]GpuInfo, error) {
	var gpus []GpuInfo

	entries, err := os.ReadDir("/sys/class/drm")
	if err != nil {
		return nil, fmt.Errorf("cannot read /sys/class/drm: %w", err)
	}

	idx := 0
	for _, e := range entries {
		name := e.Name()
		if !strings.HasPrefix(name, "card") {
			continue
		}
		// Skip cardN-* connector entries
		num := name[4:]
		if _, err := strconv.Atoi(num); err != nil {
			continue
		}

		devPath := filepath.Join("/sys/class/drm", name, "device")
		realPath, err := filepath.EvalSymlinks(devPath)
		if err != nil {
			continue
		}

		vendorStr, err := readSysfsStr(filepath.Join(realPath, "vendor"))
		if err != nil {
			continue
		}
		vid, err := strconv.ParseUint(vendorStr, 0, 64)
		if err != nil {
			continue
		}
		if vid != 0x1002 && vid != 0x10DE && vid != 0x8086 && vid != 0x1A82 {
			continue
		}

		g := GpuInfo{
			Index:   idx,
			Vendor:  vendorName(vid),
			DrmNode: filepath.Join("/dev/dri", name),
			Enabled: true,
		}

		// PCI slot
		parts := strings.Split(realPath, "/")
		if len(parts) > 0 {
			g.PciSlot = parts[len(parts)-1]
		}
		g.Name = fmt.Sprintf("%s GPU [%s]", g.Vendor, g.PciSlot)

		// Utilization
		if u, err := readSysfsInt64(filepath.Join(realPath, "gpu_busy_percent")); err == nil {
			g.UtilizationPct = int(u)
		}

		// VRAM
		if v, err := readSysfsInt64(filepath.Join(realPath, "mem_info_vram_total")); err == nil {
			g.VramTotalBytes = v
		}
		if v, err := readSysfsInt64(filepath.Join(realPath, "mem_info_vram_used")); err == nil {
			g.VramUsedBytes = v
		}

		// Temperature and power via hwmon
		hwmonBase := filepath.Join(realPath, "hwmon")
		hwmonEntries, _ := os.ReadDir(hwmonBase)
		for _, he := range hwmonEntries {
			if !strings.HasPrefix(he.Name(), "hwmon") {
				continue
			}
			hwmonPath := filepath.Join(hwmonBase, he.Name())
			if t, err := readSysfsInt64(filepath.Join(hwmonPath, "temp1_input")); err == nil {
				g.TemperatureC = int(t / 1000)
			}
			if p, err := readSysfsInt64(filepath.Join(hwmonPath, "power1_average")); err == nil {
				g.PowerW = float64(p) / 1e6
			}
			if c, err := readSysfsInt64(filepath.Join(hwmonPath, "freq1_input")); err == nil {
				g.ClockMhz = int(c / 1e6)
			}
			break
		}

		gpus = append(gpus, g)
		idx++
	}

	return gpus, nil
}

// ---------------------------------------------------------------------------
// Daemon status check
// ---------------------------------------------------------------------------

func isDaemonRunning() bool {
	conn, err := net.DialTimeout("unix", "/run/mvgal/mvgal.sock", 200*time.Millisecond)
	if err != nil {
		return false
	}
	conn.Close()
	return true
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

func writeJSON(w http.ResponseWriter, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	if err := json.NewEncoder(w).Encode(v); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}

func handleGPUs(w http.ResponseWriter, r *http.Request) {
	gpus, err := enumerateGPUs()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	writeJSON(w, gpus)
}

func handleGPU(w http.ResponseWriter, r *http.Request) {
	// Extract index from URL: /api/v1/gpus/{id}
	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/v1/gpus/"), "/")
	if len(parts) == 0 {
		http.Error(w, "missing GPU index", http.StatusBadRequest)
		return
	}
	idx, err := strconv.Atoi(parts[0])
	if err != nil {
		http.Error(w, "invalid GPU index", http.StatusBadRequest)
		return
	}

	gpus, err := enumerateGPUs()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	for _, g := range gpus {
		if g.Index == idx {
			writeJSON(w, g)
			return
		}
	}
	http.Error(w, "GPU not found", http.StatusNotFound)
}

func handleScheduler(w http.ResponseWriter, r *http.Request) {
	gpus, _ := enumerateGPUs()
	status := SchedulerStatus{
		Mode:        "dynamic",
		IdleTimeout: 5,
		GpuCount:    len(gpus),
	}
	writeJSON(w, status)
}

func handleStats(w http.ResponseWriter, r *http.Request) {
	gpus, _ := enumerateGPUs()

	var totalVram, usedVram int64
	var totalUtil int
	for _, g := range gpus {
		totalVram += g.VramTotalBytes
		usedVram += g.VramUsedBytes
		totalUtil += g.UtilizationPct
	}

	avgUtil := 0.0
	if len(gpus) > 0 {
		avgUtil = float64(totalUtil) / float64(len(gpus))
	}

	stats := Stats{
		TotalVramBytes: totalVram,
		UsedVramBytes:  usedVram,
		AvgUtilPct:     avgUtil,
		GpuCount:       len(gpus),
		DaemonRunning:  isDaemonRunning(),
		Timestamp:      time.Now().UTC().Format(time.RFC3339),
	}
	writeJSON(w, stats)
}

func handleLogs(w http.ResponseWriter, r *http.Request) {
	// Try to read from the daemon log file
	logPath := "/var/log/mvgal/mvgald.log"
	data, err := os.ReadFile(logPath)
	if err != nil {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{
			"error": "log file not found: " + logPath,
		})
		return
	}

	lines := strings.Split(string(data), "\n")
	// Return last 100 lines
	if len(lines) > 100 {
		lines = lines[len(lines)-100:]
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"lines": lines,
	})
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------

func router() http.Handler {
	mux := http.NewServeMux()

	mux.HandleFunc("/api/v1/gpus", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/api/v1/gpus" || r.URL.Path == "/api/v1/gpus/" {
			handleGPUs(w, r)
		} else {
			handleGPU(w, r)
		}
	})
	mux.HandleFunc("/api/v1/gpus/", handleGPU)
	mux.HandleFunc("/api/v1/scheduler", handleScheduler)
	mux.HandleFunc("/api/v1/stats", handleStats)
	mux.HandleFunc("/api/v1/logs", handleLogs)

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]string{
			"service": "MVGAL REST API",
			"version": "0.2.1",
			"endpoints": "/api/v1/gpus, /api/v1/scheduler, /api/v1/stats, /api/v1/logs",
		})
	})

	return mux
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

func main() {
	listen := flag.String("listen", ":7474", "Address to listen on")
	flag.Parse()

	log.Printf("MVGAL REST API server starting on %s", *listen)

	srv := &http.Server{
		Addr:         *listen,
		Handler:      router(),
		ReadTimeout:  5 * time.Second,
		WriteTimeout: 10 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	if err := srv.ListenAndServe(); err != nil {
		log.Fatalf("Server error: %v", err)
	}
}
