---
title: Configuration
---

# Configuration

MVGAL is configured via `/etc/mvgal/mvgal.conf` (INI format).

## Core Settings

```ini
[core]
enabled = true
debug_level = info
default_strategy = round_robin
enable_dmabuf = true
```

## Per-GPU Settings

```ini
[gpu_0]
priority = 0
enabled = true

[gpu_1]
priority = 1
enabled = true
```

## AFR (Alternate Frame Rendering)

```ini
[afr]
enable_sync = true
sync_timeout_ms = 16
```

## AI Scheduler

```ini
[ai_scheduler]
enabled = false
model_path = /etc/mvgal/models/scheduler.onnx
inference_timeout_ms = 5
fallback_on_error = true
confidence_threshold = 0.6
enable_stats = true
```

## Network Pooling

```ini
[network]
enabled = false
discovery_port = 42069
listen_addr = 0.0.0.0
listen_port = 0
broadcast_addr = 255.255.255.255
heartbeat_interval_s = 5
peer_timeout_s = 30
```
