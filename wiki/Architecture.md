---
title: Architecture
---

# Architecture

MVGAL is a six-layer stack:

```
┌─────────────────────────────────────────────┐
│         Your Application / Game             │
├─────────────────────────────────────────────┤
│ Layer 6: CLI Tools & Dashboard              │
│  mvgal-info, mvgal-status, Qt dashboard     │
├─────────────────────────────────────────────┤
│ Layer 5: API Interception                   │
│  Vulkan layer, OpenCL ICD, CUDA shim        │
├─────────────────────────────────────────────┤
│ Layer 4: Execution Engine                   │
│  Frame sessions, migration plans            │
├─────────────────────────────────────────────┤
│ Layer 3: Runtime Daemon (mvgald)            │
│  Scheduler, memory mgr, IPC, metrics        │
├─────────────────────────────────────────────┤
│ Layer 2: Rust Safety Subsystems             │
│  Fence manager, memory safety, capabilities │
├─────────────────────────────────────────────┤
│ Layer 1: Kernel Module                      │
│  DRM meta-driver, /dev/mvgal0               │
└─────────────────────────────────────────────┘
```

## Data Flow

```
Application → API Interception → Unix Socket → mvgald → GPU Drivers
```

All API calls are intercepted transparently via:
- **Vulkan**: `VK_LAYER_MVGAL` validation layer
- **OpenCL**: LD_PRELOAD ICD loader shim
- **CUDA**: LD_PRELOAD driver API interception
- **OpenGL**: LD_PRELOAD `libGL.so` shim

## Daemon Architecture

The `mvgald` daemon runs as a system service and consists of:

| Component | Description |
|-----------|-------------|
| Scheduler | Distributes work across GPUs (7 strategies) |
| DeviceRegistry | Manages local + remote GPU state |
| MemoryManager | DMA-BUF, P2P, staging allocations |
| PowerManager | Idle detection, GPU parking |
| MetricsCollector | GPU telemetry (temp, util, VRAM) |
| IPC Server | Unix domain socket + network discovery |

## Remote GPU Pooling

Nodes on the same network can pool GPUs:

```
Node A (AMD + NVIDIA) ←→ Node B (Intel Arc)
         ↓                    ↓
   Local GPUs           Remote GPUs
         ↓                    ↓
   └─────┴──── Unified Scheduler ────┘
```

UDP broadcast discovery + TCP health heartbeat.
