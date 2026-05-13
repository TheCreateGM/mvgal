---
title: CLI Tools
---

# CLI Tools

| Tool | Description |
|------|-------------|
| `mvgal-info` | List all detected GPUs, VRAM, temperature, utilization |
| `mvgal-status` | Real-time GPU utilization with bars (`--watch` to auto-refresh) |
| `mvgal-bench` | Memory bandwidth, compute FLOPS, scheduling latency |
| `mvgal-compat` | System readiness check + per-app compatibility database |
| `mvgal-config` | Configure scheduler mode, GPU enable/disable |
| `mvgal` | Main CLI: start/stop daemon, set strategy, show stats |

## Examples

```bash
# List GPUs with JSON output
mvgal-info --json

# Real-time monitoring
mvgal-status --watch --interval 500

# Run all benchmarks
mvgal-bench all

# Check if a game is compatible
mvgal-compat "Cyberpunk 2077"

# Switch scheduler strategy
mvgal-config set-strategy afr
```

## REST API

```bash
# Start the REST API server
mvgal-rest-server --listen :7474

# Query endpoints
curl http://localhost:7474/api/v1/gpus
curl http://localhost:7474/api/v1/stats
curl http://localhost:7474/api/v1/scheduler
curl -X PUT http://localhost:7474/api/v1/scheduler \
  -d '{"strategy": "hybrid"}'
curl http://localhost:7474/api/v1/logs
```
