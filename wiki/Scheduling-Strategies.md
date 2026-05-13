---
title: Scheduling Strategies
---

# Scheduling Strategies

MVGAL provides 7+ scheduling strategies for distributing workloads across GPUs.

## Standard Strategies

| Strategy | Flag | Description | Best For |
|----------|------|-------------|----------|
| Round-robin | `round_robin` | Distributes frames evenly across GPUs | General compute, balanced workloads |
| AFR | `afr` | Odd/even frames on alternating GPUs | Gaming, rendering |
| SFR | `sfr` | Splits frame into horizontal/vertical tiles | Gaming (proton), rendering |
| Task-based | `task` | Routes individual draw calls/compute tasks | Mixed graphics + compute |
| Compute offload | `compute_offload` | Routes compute to best GPU for number-crunching | AI/HPC training |
| Hybrid | `hybrid` | Auto-select strategy based on runtime metrics | General-purpose |
| Single GPU | `single` | Uses one GPU (fallback/compat) | Compatibility mode |

## AI-Driven Strategy

When enabled, the AI scheduler learns workload patterns:

```
Workload features → mvgal_ai_model_predict() → recommended GPU
```

Configured via `[ai_scheduler]` section in `mvgal.conf`.

## Network Pooling Strategy

Remote GPUs discovered on the network are scored by:

```
score = vramScore + computeBonus - latencyPenalty
```

Higher score → preferred for scheduling. Configured via `[network]`.

## Runtime Switching

```bash
# Via CLI
mvgal-config set-strategy afr

# Via REST API
curl -X PUT http://localhost:7474/api/v1/scheduler \
  -d '{"strategy": "hybrid"}'
```
