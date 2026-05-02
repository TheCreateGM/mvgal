# MVGAL Architecture

**Version:** 0.2.1 | **Last Updated:** May 2026

---

## Overview

MVGAL (Multi-Vendor GPU Aggregation Layer for Linux) is a six-layer system that presents two or more heterogeneous GPUs from different vendors as a single logical device to all applications without requiring application changes.

```
┌─────────────────────────────────────────────────────────────────┐
│                        Applications                             │
│          (Games, Blender, PyTorch, OpenCL programs)             │
└────────────┬──────────────┬──────────────┬──────────────────────┘
             │ Vulkan        │ OpenCL        │ CUDA / OpenGL
             ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Layer 5: API Interception                       │
│  VK_LAYER_MVGAL │ libmvgal_opencl.so │ libmvgal_cuda.so        │
│  libmvgal_gl.so (OpenGL LD_PRELOAD)                             │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│                  Layer 4: Execution Engine                       │
│  Frame sessions · Migration plans · Steam/Proton profiles        │
│  (src/userspace/execution/execution.c)                          │
└────────────────────────────┬────────────────────────────────────┘
                             │ IPC (Unix socket /run/mvgal/mvgal.sock)
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│               Layer 3: User-Space Runtime Daemon                 │
│                         mvgald (C++20)                           │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────────────┐ │
│  │Scheduler │ │MemoryMgr │ │PowerMgr  │ │  MetricsCollector  │ │
│  └──────────┘ └──────────┘ └──────────┘ └────────────────────┘ │
└────────────────────────────┬────────────────────────────────────┘
                             │
┌────────────────────────────▼────────────────────────────────────┐
│               Layer 2: Rust Safety Subsystems                    │
│  fence_manager · memory_safety · capability_model               │
│  (safe/ — Rust, edition 2021, MSRV 1.75)                        │
└────────────────────────────┬────────────────────────────────────┘
                             │ ioctl / sysfs
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│               Layer 1: Kernel Abstraction Driver                 │
│                         mvgal.ko (GPL-2.0)                       │
│              /dev/mvgal0  (character device)                     │
└──────┬──────────────┬──────────────┬──────────────┬─────────────┘
       │              │              │              │
       ▼              ▼              ▼              ▼
  amdgpu.ko     nvidia.ko       i915/xe.ko    mtgpu-drv.ko
  (AMD GPU)    (NVIDIA GPU)    (Intel GPU)   (MTT GPU)
```

---

## Layer 1 — Kernel Module (`mvgal.ko`)

**Source:** `kernel/`  
**License:** GPL-2.0-only  
**Device node:** `/dev/mvgal0`

### Responsibilities

- Registers a character device `/dev/mvgal0` via `alloc_chrdev_region` and `cdev_add`.
- Enumerates all display-class PCI devices at module load and on hotplug events.
- Exposes GPU topology, PCIe link information, and BAR sizes to user space via ioctls.
- Manages cross-GPU DMA-BUF export/import and unified virtual address space.
- Implements kernel-side workload queue with 16 priority levels.
- Provides cross-vendor fence and timeline synchronization primitives.

### Module Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `enable_debug` | bool | false | Enable verbose kernel log output |

### IOCTL Interface

Defined in `include/mvgal/mvgal_uapi.h`:

| IOCTL | Direction | Description |
|-------|-----------|-------------|
| `MVGAL_IOCTL_QUERY_DEVICES` | Read | Number and descriptors of detected GPUs |
| `MVGAL_IOCTL_QUERY_CAPABILITIES` | Read | Aggregate capability profile |
| `MVGAL_IOCTL_SUBMIT_WORKLOAD` | Write | Submit a workload to the scheduler |
| `MVGAL_IOCTL_ALLOC_MEMORY` | Read/Write | Allocate unified virtual memory |
| `MVGAL_IOCTL_FREE_MEMORY` | Write | Free a unified memory allocation |
| `MVGAL_IOCTL_IMPORT_DMABUF` | Write | Import a DMA-BUF file descriptor |
| `MVGAL_IOCTL_EXPORT_DMABUF` | Read | Export a buffer as DMA-BUF |
| `MVGAL_IOCTL_WAIT_FENCE` | Write | Wait for a fence to be signaled |
| `MVGAL_IOCTL_SIGNAL_FENCE` | Write | Signal a fence |
| `MVGAL_IOCTL_SET_GPU_AFFINITY` | Write | Pin a context to specific GPUs |

### Vendor Operations (`struct mvgal_vendor_ops`)

Each vendor implements the same interface:

```c
struct mvgal_vendor_ops {
    int  (*init)(struct mvgal_gpu_device *dev);
    void (*fini)(struct mvgal_gpu_device *dev);
    int  (*submit_cs)(struct mvgal_gpu_device *dev, struct mvgal_workload *wl);
    int  (*alloc_vram)(struct mvgal_gpu_device *dev, size_t size, uint64_t *addr);
    void (*free_vram)(struct mvgal_gpu_device *dev, uint64_t addr);
    int  (*wait_idle)(struct mvgal_gpu_device *dev, uint32_t timeout_ms);
    int  (*set_power_state)(struct mvgal_gpu_device *dev, enum mvgal_power_state);
    struct dma_buf *(*export_dmabuf)(struct mvgal_gpu_device *dev, uint64_t addr, size_t size);
    int  (*import_dmabuf)(struct mvgal_gpu_device *dev, struct dma_buf *buf, uint64_t *addr);
    int  (*query_utilization)(struct mvgal_gpu_device *dev, uint32_t *util_percent);
};
```

Vendor implementations: `kernel/vendors/mvgal_amd.c`, `mvgal_nvidia.c`, `mvgal_intel.c`, `mvgal_mtt.c`.

### Design Decisions

**Character device instead of DRM driver (Phase 1):** The kernel module does not replace vendor DRM drivers. It sits above them. A character device is sufficient for topology queries and DMA-BUF coordination. Full DRM registration is planned for Phase 2.

**No `pci_register_driver`:** MVGAL must not claim PCI devices away from vendor drivers. Using `pci_get_device` in a scan loop allows MVGAL to observe all GPUs without interfering with their existing drivers.

---

## Layer 2 — Rust Safety Subsystems (`safe/`)

Three Rust crates (edition 2021, MSRV 1.75) provide memory-safe implementations of critical subsystems. Each exposes a C FFI interface with `#[no_mangle] extern "C"` functions.

### `fence_manager` (~248 LOC)

Cross-device fence lifecycle management.

**State machine:** `Pending → Submitted → Signalled → Reset`

**C FFI:**
```c
uint64_t mvgal_fence_create(uint32_t gpu_index);
void     mvgal_fence_submit(uint64_t handle);
void     mvgal_fence_signal(uint64_t handle);
uint32_t mvgal_fence_state(uint64_t handle);   // 0=Pending 1=Submitted 2=Signalled 3=Reset
void     mvgal_fence_reset(uint64_t handle);
void     mvgal_fence_destroy(uint64_t handle);
```

### `memory_safety` (~230 LOC)

Safe wrappers for cross-GPU memory allocation tracking with reference counting.

**Placements:** `SystemRam`, `GpuVram`, `Mirrored`

**C FFI:**
```c
uint64_t mvgal_mem_track(uint64_t size, uint32_t placement);
void     mvgal_mem_retain(uint64_t handle);
void     mvgal_mem_release(uint64_t handle);
void     mvgal_mem_set_dmabuf(uint64_t handle, int32_t fd);
uint64_t mvgal_mem_size(uint64_t handle);
uint32_t mvgal_mem_placement(uint64_t handle);
uint64_t mvgal_mem_total_system_bytes(void);
uint64_t mvgal_mem_total_gpu_bytes(void);
```

### `capability_model` (~260 LOC)

GPU capability normalization, aggregate profile computation, and JSON serialization.

**Tiers:** `Full` (all GPUs same API set), `ComputeOnly`, `Mixed`

**C FFI:**
```c
uint64_t    mvgal_cap_compute(const GpuCapability *caps, uint32_t count);
void        mvgal_cap_free(uint64_t handle);
uint64_t    mvgal_cap_total_vram(uint64_t handle);
uint32_t    mvgal_cap_tier(uint64_t handle);
const char *mvgal_cap_to_json(uint64_t handle);
```

---

## Layer 3 — Runtime Daemon (`mvgald`)

**Source:** `runtime/daemon/` (C++20)  
**Socket:** `/run/mvgal/mvgal.sock`  
**PID file:** `/etc/mvgal/mvgald.pid`

### Subsystems

#### DeviceRegistry (`device_registry.cpp`)

Enumerates GPUs via `/sys/class/drm/cardN/device/` and PCI bus scan. Normalizes vendor-specific metadata into `GpuDevice` objects with:
- PCI slot, vendor/device IDs, DRM node path
- Capabilities: VRAM size/free, bandwidth, compute units, API flags, PCIe gen/lanes, NUMA node
- State: utilization %, memory %, temperature, power draw, clock speed, power state

#### Scheduler (`scheduler.cpp`)

Three scheduling modes:
- `STATIC_PARTITIONING` — divide workload by static weights
- `DYNAMIC_LOAD_BALANCING` — route to GPU with most available capacity
- `APPLICATION_PROFILE` — pre-configured profiles for known applications

Priority queue with 16 levels. Work-stealing when one GPU's queue depth exceeds threshold.

#### MemoryManager (`memory_manager.cpp`)

Coordinates cross-GPU memory allocation. Tracks allocations per GPU, implements DMA-BUF transfer path, PCIe P2P fallback, and host-RAM staging.

#### PowerManager (`power_manager.cpp`)

Power states: `ACTIVE → SUSTAINED → IDLE → PARK`

Configurable timeouts:
- `idleTimeoutMs` (default: 5000 ms) — time without workload before going idle
- `sustainedTimeoutMs` — time in idle before sustained
- `parkTimeoutMs` — time in sustained before parking

DVFS: adjusts GPU clock based on utilization. Thermal throttling at configurable threshold.

#### MetricsCollector (`metrics_collector.cpp`)

Polls sysfs at configurable interval (default: 1000 ms). Collects:
- GPU utilization, memory utilization, memory bandwidth (read/write)
- Temperature, power draw, clock speed, queue depth
- Submit latency, execution time, wait time

Exposes telemetry subscription API for clients.

#### IpcServer (`ipc_server.cpp`)

Unix domain socket server. Message format:

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  magic   │ version  │  type    │ reqId    │ payloadSz│  flags   │ reserved │
│ 'MVGL'   │    1     │ uint32   │ uint32   │ uint32   │ uint32   │ uint32   │
│ 4 bytes  │ 4 bytes  │ 4 bytes  │ 4 bytes  │ 4 bytes  │ 4 bytes  │ 4 bytes  │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

Message types: `HELLO`, `GOODBYE`, `QUERY_DEVICES`, `QUERY_DEVICE_CAPABILITIES`, `QUERY_UNIFIED_CAPABILITIES`, `ALLOC_MEMORY`, `FREE_MEMORY`, `IMPORT_DMABUF`, `EXPORT_DMABUF`, `SUBMIT_WORKLOAD`, `WAIT_WORKLOAD`, `SET_SCHEDULING_MODE`, `SET_GPU_PRIORITY`, `SET_GPU_ENABLED`, `GET_STATISTICS`, `SUBSCRIBE_TELEMETRY`, `UNSUBSCRIBE_TELEMETRY`, `GET_CONFIG`, `SET_CONFIG`, `LOAD_CONFIG`, `SAVE_CONFIG`, `ERROR`.

Authentication: `SCM_CREDENTIALS` — only `video` group members or root may submit workloads.

---

## Layer 4 — Execution Engine

**Source:** `src/userspace/execution/execution.c` (~882 LOC)

Translates intercepted API calls into scheduler workloads. Manages frame sessions (begin → submit → present lifecycle), generates cross-GPU migration plans, and produces Steam/Proton scheduling profiles.

### Frame Session Lifecycle

```
mvgal_execution_begin_frame()   → allocates frame_id, records API/strategy/app
mvgal_execution_submit()        → creates execution plan, selects GPUs, chooses migration path
mvgal_execution_present()       → finalizes frame, triggers frame pacer if needed
mvgal_execution_get_frame_stats() → returns timing, GPU list, bytes migrated
```

### Migration Path Selection

```
1. DMA-BUF zero-copy   (MVGAL_EXECUTION_MIGRATION_STREAM)
2. PCIe P2P            (MVGAL_EXECUTION_MIGRATION_STREAM with P2P)
3. Host-RAM staging    (MVGAL_EXECUTION_MIGRATION_EVICT)
4. Mirror/replicate    (MVGAL_EXECUTION_MIGRATION_MIRROR)
```

---

## Layer 5 — API Interception

### Vulkan Layer (`src/userspace/intercept/vulkan/`)

`VK_LAYER_MVGAL` is a Vulkan explicit layer registered via `/usr/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json`.

Uses the Vulkan loader dispatch-chain pattern. Each intercepted function looks up the next function pointer via `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr`.

**Intercepted functions:**
- Instance: `vkCreateInstance`, `vkDestroyInstance`, `vkEnumeratePhysicalDevices`
- Device: `vkCreateDevice`, `vkDestroyDevice`, `vkGetDeviceQueue`, `vkGetDeviceQueue2`
- Queue: `vkQueueSubmit`, `vkQueueSubmit2`, `vkQueueSubmit2KHR`
- Physical device: `vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceFeatures`, `vkGetPhysicalDeviceMemoryProperties`, `vkGetPhysicalDeviceQueueFamilyProperties`, `vkGetPhysicalDeviceProperties2`, `vkGetPhysicalDeviceFeatures2`, `vkGetPhysicalDeviceMemoryProperties2`

**Global state** (`g_mvgal_layer_state`): linked lists of instance, device, queue, and physical device dispatch tables. Protected by `pthread_mutex_t`. Atomic submit counter.

**Debug:** `MVGAL_VULKAN_DEBUG=1` enables logging. `MVGAL_VULKAN_LOG_PATH=<path>` redirects to file.

### OpenCL Layer (`src/userspace/intercept/opencl/`)

LD_PRELOAD-based interception. Presents all GPUs as a single MVGAL OpenCL platform. NDRange kernels are partitioned across GPUs by splitting the global work size.

Registered via `/etc/OpenCL/vendors/mvgal.icd`.

### CUDA Shim (`src/userspace/intercept/cuda/`)

LD_PRELOAD-based interception of 40+ CUDA Driver and Runtime API functions. Intercepts:
- `cuLaunchKernel`, `cudaLaunchKernel` — kernel launches
- `cuMemAlloc`, `cudaMalloc`, `cuMemFree`, `cudaFree` — memory management
- `cuMemcpy*`, `cudaMemcpy*` — memory transfers
- Cross-GPU copy detection and memory tracking per GPU

6 workload distribution strategies: round-robin, AFR, SFR, single, hybrid, custom.

### OpenGL Preload (`opengl/mvgal_gl_preload.c`)

LD_PRELOAD shim intercepting `glXSwapBuffers` and `eglSwapBuffers`. Injects frame pacing telemetry. Actual OpenGL→Vulkan translation is handled by Zink (Mesa).

---

## Layer 6 — Tooling

### CLI Tools (`tools/`)

| Tool | Key Functions |
|------|--------------|
| `mvgal-info` | `enumerate_drm_gpus()`, reads `/sys/class/drm/cardN/device/`, JSON output |
| `mvgal-status` | `discover_gpus()`, `refresh_gpu_status()`, ANSI progress bars, daemon socket check |
| `mvgal-bench` | `bench_memory_bandwidth()`, `bench_compute()`, `bench_scheduling_latency()`, `bench_sync_overhead()` |
| `mvgal-compat` | `check_system()`, `check_app()`, 15+ app database, system readiness scoring |
| `mvgal-config` | `list_gpus()`, `set_strategy()`, `set_gpu_enabled()`, `show_stats()` |

### Qt Dashboard (`ui/`)

- `mvgal_dashboard.cpp` — Qt5/Qt6 main window with 4 tabs: Overview, Scheduler, Logs, Config
- `GpuWidget` — per-GPU utilization/VRAM progress bars, temperature, power, clock, workload type
- `mvgal_rest_server.go` — Go HTTP server on `:7474`

REST endpoints:
- `GET /api/v1/gpus` — all GPU metrics
- `GET /api/v1/gpus/{id}` — single GPU
- `GET /api/v1/scheduler` — current mode and GPU count
- `GET /api/v1/stats` — aggregate stats (total VRAM, avg utilization, daemon status)
- `GET /api/v1/logs` — last 100 lines of daemon log

---

## Data Flow: Gaming Workload (AFR)

```
Game (via Proton/DXVK)
  │
  │ vkQueueSubmit (frame N)
  ▼
VK_LAYER_MVGAL
  │ intercepts submit, increments atomic counter, logs telemetry
  │ forwards to next layer in dispatch chain
  ▼
Physical ICD (AMD / NVIDIA / Intel)
  │
  │ (parallel) mvgald AFR scheduler
  │   ├─ Frame N   → GPU 0  (even)
  │   └─ Frame N+1 → GPU 1  (odd)
  ▼
Frame pacer (steam/mvgal_frame_pacer.c)
  │ ring buffer depth 8, background thread
  │ sleep_until_ns(next_vsync_boundary)
  ▼
vkQueuePresentKHR on display-connected GPU
```

## Data Flow: AI Compute Workload

```
PyTorch / TensorFlow
  │
  │ CUDA kernel launch (cudaLaunchKernel)
  ▼
MVGAL CUDA shim (LD_PRELOAD)
  │ intercepts, translates to MVGAL workload submission
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

## Memory Architecture

### Allocation Policy

```
Request size < 64 MB  →  GPU with most free VRAM
Render target         →  GPU that will write first (from workload history)
Large buffer          →  GPU most likely to use it (from access pattern)
```

### Transfer Path

```
Source GPU export
  │
  ├─ DMA-BUF viable?  →  dma_buf_map_attachment (zero-copy, kernel-supported)
  │
  └─ DMA-BUF not viable
       │
       ├─ PCIe P2P viable?  →  pci_p2pdma (requires same root complex, kernel 5.10+)
       │
       └─ Fallback  →  mmap source + DMA to host staging buffer + upload to dest GPU
```

### Memory Flags

| Flag | Value | Description |
|------|-------|-------------|
| `HOST_VALID` | `1<<0` | CPU can access (mapped) |
| `GPU_VALID` | `1<<1` | GPUs can access |
| `CPU_CACHED` | `1<<2` | CPU cached |
| `CPU_UNCACHED` | `1<<3` | CPU uncached (write-combined) |
| `SHARED` | `1<<4` | Shared across GPUs |
| `DMA_BUF` | `1<<5` | Use DMA-BUF |
| `P2P` | `1<<6` | Enable P2P transfers |
| `REPLICATED` | `1<<7` | Mirror across all GPUs |
| `PERSISTENT` | `1<<8` | Persistent CPU mapping |
| `LAZY_ALLOCATE` | `1<<9` | Defer physical allocation |
| `ZERO_INITIALIZED` | `1<<10` | Zero on allocation |

---

## Scheduling Strategies

| Strategy | Enum | Description |
|----------|------|-------------|
| Round-robin | `MVGAL_STRATEGY_ROUND_ROBIN` | Even distribution across all GPUs |
| AFR | `MVGAL_STRATEGY_AFR` | Even frames → GPU 0, odd frames → GPU 1 |
| SFR | `MVGAL_STRATEGY_SFR` | Screen split horizontally or vertically |
| Task-based | `MVGAL_STRATEGY_TASK` | Route by workload type (graphics vs compute) |
| Compute offload | `MVGAL_STRATEGY_COMPUTE_OFFLOAD` | Route compute to highest-FLOPS GPU |
| Hybrid | `MVGAL_STRATEGY_HYBRID` | Adaptive — selects best strategy per workload |
| Single GPU | `MVGAL_STRATEGY_SINGLE_GPU` | Use only the primary GPU |
| Auto | `MVGAL_STRATEGY_AUTO` | System selects based on workload history |
| Custom | `MVGAL_STRATEGY_CUSTOM` | User-provided splitter callback |

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Character device instead of DRM driver (Phase 1) | Avoids claiming PCI devices from vendor drivers; simpler to maintain across kernel versions |
| Vulkan layer instead of full ICD (Phase 1) | Allows transparent interception without application changes; full ICD planned for Phase 5 |
| C17 for userspace, C++20 for daemon, Rust for safety-critical paths | C17 maximizes compatibility with GPU driver headers; C++20 provides modern abstractions for daemon; Rust eliminates memory safety bugs in fence/memory tracking |
| Unix socket IPC with `SCM_CREDENTIALS` | Zero-dependency authentication; no D-Bus or systemd dependency required |
| DMA-BUF for cross-GPU transfers | Kernel-supported zero-copy path; works across all vendor drivers that support it |
| Sysfs polling for GPU metrics | No vendor-specific SDK required; works on all supported vendors |
| `pkexec` for all privileged operations | Policy-compliant privilege escalation; no `sudo` dependency |
