# MVGAL User Guide

**Version:** 0.2.2 | **Date:** May 2026

---

## Table of Contents

1. [Getting Started](#1-getting-started)
2. [Basic Usage](#2-basic-usage)
3. [Scheduling Strategies](#3-scheduling-strategies)
4. [GPU Management](#4-gpu-management)
5. [Gaming with MVGAL](#5-gaming-with-mvgal)
6. [AI/ML Workloads](#6-aiml-workloads)
7. [Monitoring and Diagnostics](#7-monitoring-and-diagnostics)
8. [Configuration Reference](#8-configuration-reference)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Getting Started

### 1.1 What is MVGAL?

MVGAL (Multi-Vendor GPU Aggregation Layer) combines multiple GPUs from different vendors (AMD, NVIDIA, Intel, Moore Threads) into a single logical device. Applications see one GPU with the combined capabilities of all physical GPUs.

### 1.2 When to Use MVGAL

| Use Case | Benefit |
|----------|---------|
| Gaming with mixed GPUs | AFR/SFR distributes frames across all GPUs |
| AI/ML training | Compute offload shards batches across GPUs |
| Video rendering | Task-based scheduling distributes workloads |
| Development/testing | Test multi-GPU behavior without homogeneous hardware |

### 1.3 When NOT to Use MVGAL

- Single GPU systems (no benefit)
- GPUs on different PCIe root complexes with no P2P (high latency)
- Applications that require direct GPU access (bypasses MVGAL)

### 1.4 Quick Start

```bash
# 1. Install (see INSTALL.md)
sudo dnf install mvgal mvgal-dkms

# 2. Load kernel module
sudo modprobe mvgal

# 3. Start daemon
sudo systemctl start mvgald

# 4. Check status
mvgal-status

# 5. Set strategy
mvgal-config strategy set auto
```

---

## 2. Basic Usage

### 2.1 Check GPU Status

```bash
# Quick status
mvgal-status

# Detailed info
mvgal-info

# JSON output
mvgal-info --json

# Compatibility check
mvgal-compat
```

### 2.2 Start/Stop Daemon

```bash
# Start
sudo mvgal start

# Stop
sudo mvgal stop

# Restart
sudo mvgal restart

# Status
sudo mvgal status
```

### 2.3 View Logs

```bash
# Daemon logs
journalctl -u mvgald -f

# Log levels: debug, info, warn, error
sudo mvgal-config log-level set debug
```

---

## 3. Scheduling Strategies

### 3.1 Available Strategies

| Strategy | Best For | Description |
|----------|----------|-------------|
| `auto` | General use | System selects based on workload history |
| `afr` | Gaming | Alternate Frame Rendering: even frames -> GPU 0, odd -> GPU 1 |
| `sfr` | High-res gaming | Split Frame Rendering: tiles distributed across GPUs |
| `task` | Mixed workloads | Route by type: graphics vs compute |
| `compute_offload` | AI/ML | Route compute to highest-FLOPS GPU |
| `hybrid` | Variable workloads | Adaptive selection based on metrics |
| `single_gpu` | Debugging | Use only primary GPU |

### 3.2 Set Strategy

```bash
# Set globally
mvgal-config strategy set afr

# Set for specific application
mvgal-config strategy set compute_offload --app "python train.py"

# View current
mvgal-config strategy get
```

### 3.3 Strategy Recommendations

| Scenario | Recommended Strategy |
|----------|---------------------|
| Gaming (1080p/1440p) | `afr` |
| Gaming (4K+) | `sfr` |
| AI training | `compute_offload` |
| Video encoding | `task` |
| Mixed gaming + streaming | `hybrid` |
| Debugging | `single_gpu` |
| Unsure | `auto` |

### 3.4 Frame Pacing (Gaming)

MVGAL includes a frame pacer for smooth AFR gaming:

```ini
# /etc/mvgal/mvgal.conf
[scheduler]
frame_timeout_ms = 16    # 60 FPS target
migration_threshold = 3  # Migrate after 3 frames of imbalance
```

---

## 4. GPU Management

### 4.1 List GPUs

```bash
mvgal-info
```

Example output:
```
GPU 0: AMD Radeon RX 7900 XTX (RDNA 3)
  VRAM: 24576 MB | Bandwidth: 960 GB/s
  Compute Units: 96 | PCIe: 0000:03:00.0
  Status: Enabled | Power: Auto

GPU 1: NVIDIA GeForce RTX 4090 (Ada)
  VRAM: 24576 MB | Bandwidth: 1008 GB/s
  CUDA Cores: 16384 | PCIe: 0000:0a:00.0
  Status: Enabled | Power: Auto
```

### 4.2 Enable/Disable GPUs

```bash
# Disable GPU 1
mvgal-config gpu disable 1

# Enable GPU 1
mvgal-config gpu enable 1

# Check enabled GPUs
mvgal-config gpu list
```

### 4.3 Power Management

```bash
# Set power state
mvgal-config power set gpu 0 state on
mvgal-config power set gpu 1 state auto

# View power status
mvgal-config power status
```

### 4.4 Hot-Plug Support

MVGAL detects GPU hot-plug events automatically:

```bash
# Manual rescan
echo 1 | sudo tee /sys/class/mvgal/mvgal0/rescan

# Check topology generation
cat /sys/class/mvgal/mvgal0/topology_generation
```

---

## 5. Gaming with MVGAL

### 5.1 Steam Integration

```bash
# Install Steam compatibility tool
cp -r steam ~/.steam/root/compatibilitytools.d/mvgal

# In Steam:
# 1. Right-click game -> Properties
# 2. Compatibility -> Force compatibility tool
# 3. Select "MVGAL Frame Pacer"
```

### 5.2 Vulkan Games

Vulkan games automatically use MVGAL when the implicit layer is installed:

```bash
# Verify layer is active
vulkaninfo | grep -i mvgal

# Enable debug logging
export VK_LAYER_MVGAL_DEBUG=1
```

### 5.3 OpenGL Games

```bash
# Preload OpenGL shim
LD_PRELOAD=/usr/lib/mvgal/libmvgal_gl.so ./game

# Or add to /etc/ld.so.preload for system-wide
```

### 5.4 DirectX Games (via Proton)

DirectX games through Proton use the Vulkan layer automatically:

```
Game -> DXVK/VKD3D -> Vulkan -> MVGAL Layer -> Physical GPUs
```

### 5.5 Performance Tips

1. **Use AFR for most games**: Best performance for frame-based rendering
2. **Enable frame pacer**: Prevents stuttering from uneven frame times
3. **Match GPU capabilities**: Similar GPUs perform better together
4. **P2P support**: GPUs on same PCIe root complex have lower latency

---

## 6. AI/ML Workloads

### 6.1 CUDA Workloads

```bash
# Enable CUDA wrapper
echo "/usr/lib/mvgal/libmvgal_cuda.so" | sudo tee -a /etc/ld.so.preload

# Run training
python train.py

# Or per-application
LD_PRELOAD=/usr/lib/mvgal/libmvgal_cuda.so python train.py
```

### 6.2 OpenCL Workloads

```bash
# Register ICD
echo "/usr/lib/mvgal/libmvgal_opencl.so" | sudo tee /etc/OpenCL/vendors/mvgal.icd

# Run application
./opencl_app

# Verify
clinfo | grep -i mvgal
```

### 6.3 Compute Offload Strategy

For AI/ML workloads, use the compute offload strategy:

```bash
mvgal-config strategy set compute_offload
```

This routes compute kernels to the GPU with the highest FLOPS, while keeping display output on the primary GPU.

### 6.4 Memory Management for AI

Large model training benefits from MVGAL's unified memory:

```ini
# /etc/mvgal/mvgal.conf
[memory]
allocation_policy = best_fit
transfer_policy = dma_buf
staging_buffer_size_mb = 512  # Increase for large models
```

---

## 7. Monitoring and Diagnostics

### 7.1 Real-Time Monitoring

```bash
# Watch mode (updates every 1s)
mvgal-status --watch

# JSON output for scripting
mvgal-status --json

# Benchmark
mvgal-bench
```

### 7.2 Prometheus Metrics

```bash
# Start exporter
mvgal_exporter --port 9100

# Scrape endpoint
curl http://localhost:9100/metrics

# Grafana dashboard
# Import dashboard ID: XXXXX (coming soon)
```

### 7.3 REST API

```bash
# Start REST server
mvgal_rest_server

# Query status
curl http://localhost:7474/api/v1/status

# Query GPU info
curl http://localhost:7474/api/v1/gpus

# Query metrics
curl http://localhost:7474/api/v1/metrics
```

### 7.4 Qt Dashboard

```bash
# Launch dashboard
mvgal-dashboard

# Tabs:
# - Overview: GPU status, utilization, temperature
# - Scheduler: Current strategy, workload distribution
# - Logs: Real-time log viewer
# - Config: Live configuration editor
```

### 7.5 Diagnostic Commands

```bash
# Full system check
mvgal-compat

# Kernel module info
mvgal-info --kernel

# Daemon info
mvgal-info --daemon

# All info as JSON
mvgal-info --json --all
```

---

## 8. Configuration Reference

### 8.1 Configuration File Location

- **System-wide**: `/etc/mvgal/mvgal.conf`
- **User override**: `~/.config/mvgal/mvgal.conf`

### 8.2 All Options

```ini
[daemon]
log_level = info              # debug, info, warn, error
log_file = /var/log/mvgal/mvgald.log
pid_file = /var/run/mvgal/mvgald.pid
ipc_socket = /var/run/mvgal/mvgald.sock

[scheduler]
strategy = auto               # auto, afr, sfr, task, compute_offload, hybrid, single_gpu
frame_timeout_ms = 16         # Target frame time (ms)
migration_threshold = 3       # Frames before migration consideration
work_stealing = true          # Enable work stealing between GPUs

[memory]
allocation_policy = best_fit  # best_fit, first_fit, round_robin
transfer_policy = dma_buf     # dma_buf, p2p, staging
staging_buffer_size_mb = 256  # Host staging buffer size

[power]
idle_timeout_ms = 5000        # Time before idle detection
sustained_timeout_ms = 30000  # Time before sustained power state
park_timeout_ms = 60000       # Time before GPU parking
dvfs_enabled = true           # Dynamic voltage/frequency scaling
thermal_threshold_c = 85      # Thermal throttling threshold

[metrics]
poll_interval_ms = 1000       # Metrics polling interval
telemetry_enabled = true      # Enable workload telemetry
prometheus_enabled = true     # Enable Prometheus exporter
prometheus_port = 9100        # Prometheus scrape port

[rest]
enabled = true                # Enable REST API
port = 7474                   # REST API port
bind = 127.0.0.1              # REST API bind address
```

### 8.3 Environment Variables

| Variable | Description |
|----------|-------------|
| `MVGAL_CONFIG` | Override config file path |
| `MVGAL_LOG_LEVEL` | Override log level |
| `MVGAL_STRATEGY` | Override scheduling strategy |
| `VK_LAYER_MVGAL_DEBUG` | Enable Vulkan layer debug logging |
| `MVGAL_DISABLE` | Set to `1` to disable MVGAL entirely |

---

## 9. Troubleshooting

### 9.1 Common Issues

| Problem | Solution |
|---------|----------|
| No GPUs detected | Check `lspci`, verify vendor drivers loaded |
| Daemon won't start | Check `journalctl -u mvgald`, verify socket not in use |
| Vulkan layer not working | Verify JSON in `/etc/vulkan/implicit_layer.d/` |
| CUDA wrapper not intercepting | Check `/etc/ld.so.preload`, verify library exists |
| Poor gaming performance | Try `afr` strategy, enable frame pacer |
| High latency | Check P2P support, verify GPUs on same root complex |
| Memory errors | Increase `staging_buffer_size_mb`, check VRAM |

### 9.2 Log Analysis

```bash
# View recent errors
journalctl -u mvgald --priority err

# View debug logs
sudo mvgal-config log-level set debug
journalctl -u mvgald -f

# Search for specific issues
journalctl -u mvgald | grep -i "error\|fail\|warn"
```

### 9.3 Performance Debugging

```bash
# Run benchmark suite
mvgal-bench

# Check memory bandwidth
mvgal-bench --memory

# Check compute performance
mvgal-bench --compute

# Check latency
mvgal-bench --latency
```

### 9.4 Getting Help

- **Documentation**: `docs/` directory
- **Issues**: GitHub Issues
- **Discussions**: GitHub Discussions
- **Security**: `SECURITY.md` for vulnerability disclosure
