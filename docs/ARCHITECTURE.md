# MVGAL Architecture

**Version:** 0.2.0 | **Last Updated:** April 2026

---

## Overview

MVGAL (Multi-Vendor GPU Aggregation Layer for Linux) is a six-layer system that
presents two or more heterogeneous GPUs from different vendors as a single
logical device to all applications without requiring application changes.

```
┌─────────────────────────────────────────────────────────────────┐
│                        Applications                             │
│          (Games, Blender, PyTorch, OpenCL programs)             │
└────────────┬──────────────┬──────────────┬──────────────────────┘
             │ Vulkan        │ OpenCL        │ CUDA / SYCL
             ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Layer 3: API Interception                       │
│   VK_LAYER_MVGAL  │  libmvgal_opencl.so  │  libmvgal_cuda.so   │
└────────────────────────────┬────────────────────────────────────┘
                             │ IPC (Unix socket)
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│               Layer 2: User-Space Runtime Daemon                 │
│                         mvgald                                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────────────┐ │
│  │Scheduler │ │MemoryMgr │ │PowerMgr  │ │  MetricsCollector  │ │
│  └──────────┘ └──────────┘ └──────────┘ └────────────────────┘ │
└────────────────────────────┬────────────────────────────────────┘
                             │ ioctl / sysfs
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│               Layer 1: Kernel Abstraction Driver                 │
│                         mvgal.ko                                 │
│              /dev/mvgal0  (character device)                     │
└──────┬──────────────┬──────────────┬──────────────┬─────────────┘
       │              │              │              │
       ▼              ▼              ▼              ▼
  amdgpu.ko     nvidia.ko       i915/xe.ko    mtgpu-drv.ko
  (AMD GPU)    (NVIDIA GPU)    (Intel GPU)   (MTT GPU)
```

---

## Layer 1 — Kernel Abstraction Driver (mvgal.ko)

**File:** `src/kernel/mvgal_kernel.c`  
**Interface:** `/dev/mvgal0` character device  
**License:** GPL-2.0-only

### Responsibilities

- Register a character device `/dev/mvgal0` via `alloc_chrdev_region` and `cdev_add`.
- Enumerate all display-class PCI devices at module load and on hotplug events.
- Expose GPU topology, PCIe link information, and BAR sizes to user space via ioctls.
- Monitor PCI bus events via `bus_register_notifier` for hot-plug support.

### IOCTL Interface

Defined in `include/mvgal/mvgal_uapi.h`:

| IOCTL | Direction | Description |
|-------|-----------|-------------|
| `MVGAL_IOC_QUERY_VERSION` | Read | Protocol version and feature flags |
| `MVGAL_IOC_GET_GPU_COUNT` | Read | Number of detected GPUs |
| `MVGAL_IOC_GET_GPU_INFO` | Read/Write | Per-GPU descriptor by index |
| `MVGAL_IOC_GET_CAPS` | Read | Aggregate capability flags |
| `MVGAL_IOC_GET_STATS` | Read | Kernel-side statistics |
| `MVGAL_IOC_RESCAN` | Write | Trigger PCI rescan |
| `MVGAL_IOC_ENABLE` / `MVGAL_IOC_DISABLE` | Write | Enable/disable aggregation |
| `MVGAL_IOC_EXPORT_DMABUF` | Write | Export DMA-BUF (future) |
| `MVGAL_IOC_IMPORT_DMABUF` | Write | Import DMA-BUF (future) |

### Design Decisions

**Why a character device instead of a DRM driver?**  
The kernel module is intentionally minimal in Phase 1. It does not replace vendor
DRM drivers; it sits above them. A character device is sufficient for topology
queries and future DMA-BUF coordination. Full DRM registration (exposing
`/dev/dri/cardN`) is planned for Phase 2 of the kernel module.

**Why not use `pci_register_driver`?**  
MVGAL must not claim PCI devices away from vendor drivers. Using
`pci_get_device` in a scan loop (rather than `pci_register_driver`) allows
MVGAL to observe all GPUs without interfering with their existing drivers.

---

## Layer 2 — User-Space Runtime Daemon (mvgald)

**Files:** `src/userspace/daemon/`, `src/userspace/scheduler/`,
`src/userspace/memory/`, `src/userspace/execution/`  
**Language:** C17  
**IPC:** Unix domain socket at `/run/mvgal/mvgal.sock`

### Subsystems

#### GPU Manager (`daemon/gpu_manager.c`)
Detects physical GPUs by scanning `/sys/class/drm/`, `/dev/nvidia*`, and
`/sys/bus/pci/devices/`. Normalizes vendor-specific metadata into
`mvgal_gpu_descriptor_t`. Implements GPU health monitoring via a background
thread that polls sysfs thermal and utilization nodes.

#### Scheduler (`scheduler/scheduler.c`)
Implements seven workload distribution strategies:

| Strategy | Use Case |
|----------|----------|
| Single GPU | Fallback / single-GPU systems |
| Round-Robin | Even distribution across all GPUs |
| AFR (Alternate Frame Rendering) | Gaming: odd/even frames on different GPUs |
| SFR (Split Frame Rendering) | Gaming: horizontal/vertical tile split |
| Task-Based | Rendering: distribute by render pass type |
| Compute Offload | AI/HPC: route compute to best GPU |
| Hybrid Adaptive | Automatic selection based on workload metrics |

The scheduler uses a priority queue (0–100) and implements work stealing when
one GPU's queue depth exceeds a configurable threshold.

#### Memory Manager (`memory/`)
- `memory.c` — Public API, allocation routing, overflow to system RAM.
- `dmabuf.c` — DMA-BUF export/import, P2P viability check, staging buffer fallback.
- `allocator.c` — NUMA-aware slab allocator for host-side staging buffers.
- `sync.c` — Cross-GPU fence and semaphore primitives.

#### Execution Engine (`execution/execution.c`)
Manages frame sessions (begin/submit/present lifecycle), generates migration
plans for cross-GPU workload migration, and produces Steam/Proton scheduling
profiles.

#### IPC Server (`daemon/ipc.c`)
Unix domain socket server. Authenticates clients via `SCM_CREDENTIALS`
(only `video` group members or root may submit workloads). Message framing
uses a fixed header with magic number and version field.

### Rust Safety-Critical Subsystems (`safe/`)

Memory-safety-critical operations are implemented in Rust (edition 2021,
MSRV 1.75) and compiled as static libraries linked into the daemon:

| Crate | Responsibility |
|-------|---------------|
| `fence_manager` | Cross-device fence lifecycle, state machine, reference counting |
| `memory_safety` | Cross-GPU allocation tracking, DMA-BUF association, ref counting |
| `capability_model` | GPU capability normalization, aggregate profile computation, JSON serialization |

Each crate exposes a C FFI interface. `unsafe` blocks are used only at FFI
boundaries and each is annotated with a safety comment.

---

## Layer 3 — API Interception Layer

### Vulkan Layer (`src/userspace/intercept/vulkan/`)

`VK_LAYER_MVGAL` is a Vulkan explicit layer registered via
`/usr/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json`.

**Architecture:** The layer uses the Vulkan loader's dispatch chain pattern.
Each intercepted function looks up the next function pointer via
`vkGetInstanceProcAddr` / `vkGetDeviceProcAddr` and forwards after performing
MVGAL-specific logic (telemetry, submit counting, physical device property
caching).

**Current intercepted functions:**
- Instance: `vkCreateInstance`, `vkDestroyInstance`, `vkEnumeratePhysicalDevices`
- Device: `vkCreateDevice`, `vkDestroyDevice`, `vkGetDeviceQueue`, `vkGetDeviceQueue2`
- Queue: `vkQueueSubmit`, `vkQueueSubmit2`, `vkQueueSubmit2KHR`
- Physical device: `vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceFeatures`,
  `vkGetPhysicalDeviceMemoryProperties`, `vkGetPhysicalDeviceQueueFamilyProperties`,
  `vkGetPhysicalDeviceProperties2`, `vkGetPhysicalDeviceFeatures2`,
  `vkGetPhysicalDeviceMemoryProperties2`

**Design Decision — Layer vs ICD:**  
A Vulkan layer is used rather than a full ICD because it allows MVGAL to
intercept any application without requiring the application to select MVGAL
explicitly. The layer sits above all physical ICDs and can observe and modify
all Vulkan calls. A full ICD implementation is planned for Phase 5 to enable
true multi-GPU virtual device presentation.

### OpenCL Layer (`src/userspace/intercept/opencl/`)

LD_PRELOAD-based interception of OpenCL runtime calls. Exposes one MVGAL
platform containing all physical GPU devices. NDRange kernels are partitioned
across GPUs by splitting the global work size.

### CUDA Shim (`src/userspace/intercept/cuda/`)

LD_PRELOAD-based interception of 40+ CUDA Driver and Runtime API functions.
Translates CUDA kernel launches to MVGAL workload submissions. Falls back to
routing only to the NVIDIA GPU if present.

---

## Layer 4 — Unified Memory Manager

**Files:** `src/userspace/memory/`

### Allocation Policy

```
Allocation request
       │
       ├─ size < 64 MB ──► GPU with most free VRAM
       │
       ├─ render target ──► GPU that will write first (from workload history)
       │
       └─ large buffer ──► GPU most likely to use it (from access pattern)
```

### DMA-BUF Transfer Path

```
Source GPU export
       │
       ├─ P2P viable? ──► dma_buf_map_attachment (zero-copy)
       │
       └─ P2P not viable ──► mmap source DMA-BUF in user space
                              │
                              └─► DMA to staging buffer
                                  │
                                  └─► Upload to destination GPU
```

### Memory Mirroring

Read-only allocations accessed by multiple GPUs are replicated to each GPU's
local VRAM. The mirror controller tracks access patterns per allocation and
applies hysteresis to avoid thrashing.

---

## Layer 5 — Cross-Device Synchronization

**Files:** `src/userspace/memory/sync.c`, `safe/fence_manager/`

Timeline semaphores and fence objects are unified across vendor boundaries.
The Rust `fence_manager` crate provides a memory-safe state machine for fence
lifecycle (Pending → Submitted → Signalled → Reset).

---

## Layer 6 — Tooling and Monitoring

| Tool | Location | Description |
|------|----------|-------------|
| `mvgalctl` | `tools/mvgal.c` | CLI: list GPUs, set strategy, show stats |
| `mvgal-config` | `tools/mvgal-config.c` | Configuration management CLI |
| `mvgal-gui` | `src/gui/mvgal-gui.c` | GTK4 monitoring dashboard |
| `mvgal-tray` | `src/gui/mvgal-tray.c` | System tray indicator |
| Benchmarks | `misc/benchmarks/` | Synthetic, real-world, and stress benchmarks |

---

## Data Flow: Gaming Workload

```
Game (via Proton/DXVK)
  │
  │ vkQueueSubmit (frame N)
  ▼
VK_LAYER_MVGAL
  │ intercepts submit, logs telemetry
  │ forwards to next layer
  ▼
Physical ICD (AMD / NVIDIA / Intel)
  │
  │ (parallel) mvgald AFR scheduler
  │   ├─ Frame N   → GPU 0
  │   └─ Frame N+1 → GPU 1
  ▼
Frame compositor
  │ waits for both GPUs via timeline semaphore
  │ copies frame from rendering GPU to display GPU if needed
  ▼
vkQueuePresentKHR on display-connected GPU
```

## Data Flow: AI Compute Workload

```
PyTorch / TensorFlow
  │
  │ CUDA / OpenCL / SYCL kernel launch
  ▼
MVGAL CUDA shim / OpenCL layer
  │ translates to MVGAL workload submission
  ▼
mvgald Compute Offload scheduler
  │ shards batch dimension across N GPUs
  │ allocates input tensors via unified memory manager
  │ issues DMA-BUF transfers for cross-GPU data
  ▼
GPU 0 … GPU N-1 (parallel execution)
  │
  │ results collected via write-combined system
  ▼
Application receives aggregated result
```

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Character device instead of DRM driver (Phase 1) | Avoids claiming PCI devices from vendor drivers; simpler to maintain across kernel versions |
| Vulkan layer instead of full ICD (Phase 1) | Allows transparent interception without application changes; ICD planned for Phase 5 |
| C17 for userspace, Rust for safety-critical paths | C17 maximizes compatibility with existing GPU driver headers; Rust eliminates memory safety bugs in fence/memory tracking |
| Unix socket IPC with SCM_CREDENTIALS | Zero-dependency authentication; no D-Bus or systemd dependency required |
| DMA-BUF for cross-GPU transfers | Kernel-supported zero-copy path; works across all vendor drivers that support it |
| Sysfs polling for GPU metrics | No vendor-specific SDK required; works on all supported vendors |
