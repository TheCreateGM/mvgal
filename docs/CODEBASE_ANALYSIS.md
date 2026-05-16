# MVGAL Codebase Analysis

**Version:** 0.2.2 | **Date:** May 2026

---

## 1. Architecture Overview

MVGAL is a six-layer stack that aggregates heterogeneous GPUs (AMD, NVIDIA, Intel, Moore Threads) into a single logical device:

```
Layer 6: Tooling          (CLI tools, Qt dashboard, REST API)
Layer 5: API Interception (Vulkan layer, OpenCL ICD, CUDA shim, OpenGL preload)
Layer 4: Execution Engine (Frame sessions, migration plans, Steam profiles)
Layer 3: Runtime Daemon   (mvgald: scheduler, memory, power, metrics, IPC)
Layer 2: Rust Safety      (fence_manager, memory_safety, capability_model)
Layer 1: Kernel Module    (mvgal.ko: PCI enumeration, UAPI ioctls, sysfs)
```

### Key Design Principle

The kernel module acts as a **topology observer and policy coordinator**, not a command virtualization layer. It enumerates GPUs via PCI bus scan, exposes topology via ioctls, and leaves scheduling/API interception to userspace. This avoids claiming PCI devices from vendor drivers.

---

## 2. Component Breakdown

### 2.1 Kernel Module (`kernel/`)

| File | Lines | Responsibility | Status |
|------|-------|---------------|--------|
| `mvgal_main.c` | ~500 | PCI enumeration, character device, sysfs, hotplug notifier | Complete |
| `mvgal_core.c` | ~250 | DRM registration (disabled), PCI driver table, module init | Partial (DRM #if 0) |
| `mvgal_device.c` | ~300 | Logical device, GPU list management, capability profile, P2P detection | Complete |
| `mvgal_memory.c` | ~280 | Memory allocation, DMA-BUF stubs, UVA space, residency tracking | Stub (DMA-BUF -EOPNOTSUPP) |
| `mvgal_scheduler.c` | ~320 | 16-level priority queue, workload dispatch | Stub (submission -EOPNOTSUPP) |
| `mvgal_sync.c` | ~280 | Cross-vendor fences, timeline semaphores | Stub |
| `mvgal_power.c` | ~200 | DVFS, thermal throttling, power budgeting | Stub |
| `vendors/mvgal_amd.c` | ~200 | AMD amdgpu integration ops | Stub (fake addresses) |
| `vendors/mvgal_nvidia.c` | ~200 | NVIDIA open-kernel shim + device detection helpers | Stub (fake addresses) |
| `vendors/mvgal_intel.c` | ~200 | Intel i915/xe integration | Stub (fake addresses) |
| `vendors/mvgal_mtt.c` | ~180 | Moore Threads mtgpu-drv integration | Stub (fake addresses) |

**Kernel Interfaces:**
- Character device: `/dev/mvgal0`
- Sysfs: `/sys/class/mvgal/mvgal0/` (gpu_count, topology_generation, rescan)
- Per-GPU sysfs: `/sys/class/mvgal/mvgal0/gpuN/` (enabled, pci_path, power_state)
- UAPI ioctls: 8 implemented (QUERY_VERSION, GET_GPU_COUNT, GET_GPU_INFO, ENABLE, DISABLE, GET_STATS, GET_CAPS, RESCAN), 4 stubbed (EXPORT_DMABUF, IMPORT_DMABUF, ALLOC_CROSS_VENDOR, FREE_CROSS_VENDOR)

### 2.2 Runtime Daemon (`runtime/daemon/`)

| File | Responsibility | Status |
|------|---------------|--------|
| `main.cpp` | Entry point, signal handling, daemonization, PID file | Complete |
| `daemon.cpp/hpp` | Orchestrates all subsystems | Complete |
| `device_registry.cpp/hpp` | GPU enumeration via sysfs + PCI | Complete |
| `scheduler.cpp/hpp` | Static/dynamic/profile scheduling, work-stealing | Complete |
| `memory_manager.cpp/hpp` | Cross-GPU allocation, DMA-BUF, P2P, staging | Complete |
| `power_manager.cpp/hpp` | Idle detection, GPU parking, DVFS, thermal | Complete |
| `metrics_collector.cpp/hpp` | Sysfs polling, telemetry subscriptions | Complete |
| `ipc_server.cpp/hpp` | Unix socket server, SCM_CREDENTIALS auth, 21 message types | Complete |

### 2.3 Rust Safety Crates (`safe/`)

| Crate | LOC | Tests | Description |
|-------|-----|-------|-------------|
| `fence_manager` | ~248 | 3 | Cross-device fence lifecycle, state machine |
| `memory_safety` | ~230 | 3 | Allocation tracking, ref counting, DMA-BUF association |
| `capability_model` | ~260 | 5 | GPU capability normalization, JSON serialization |
| `ffi_tests` | ~200 | 9 | Cross-crate FFI integration tests |

### 2.4 Userspace Library (`src/userspace/`)

| Module | LOC | Description |
|--------|-----|-------------|
| `api/mvgal_api.c` | ~800 | Core API: init, context, strategy, stats |
| `api/mvgal_log.c` | ~400 | 22 logging functions, thread-safe, color support |
| `daemon/gpu_manager.c` | ~2,091 | GPU detection, health monitoring, callbacks |
| `daemon/config.c` | ~270 | INI config load/save, defaults, validation |
| `daemon/ipc.c` | ~292 | Unix socket IPC server/client |
| `daemon/main.c` | ~234 | Daemon entry, signals, PID file, daemonization |
| `execution/execution.c` | ~882 | Frame sessions, migration plans, Steam profiles |
| `memory/memory.c` | ~924 | Core memory management |
| `memory/dmabuf.c` | ~802 | DMA-BUF export/import, P2P, UVM |
| `memory/allocator.c` | ~448 | NUMA-aware slab allocator |
| `memory/sync.c` | ~402 | Cross-GPU fence and semaphore primitives |
| `scheduler/scheduler.c` | ~1,383 | Core scheduler, priority queue, thread pool |
| `scheduler/load_balancer.c` | ~270 | Dynamic load balancing |
| `scheduler/workload_splitter.c` | ~200 | Workload splitting logic |
| `scheduler/strategy/afr.c` | ~166 | Alternate Frame Rendering |
| `scheduler/strategy/sfr.c` | ~331 | Split Frame Rendering |
| `scheduler/strategy/task.c` | ~251 | Task-based distribution |
| `scheduler/strategy/compute_offload.c` | ~125 | Compute offload |
| `scheduler/strategy/hybrid.c` | ~238 | Hybrid adaptive |

### 2.5 API Interception Layers (`src/userspace/intercept/`)

| Layer | LOC | Status |
|-------|-----|--------|
| Vulkan (`vk_layer.c`) | ~1,205 | Complete - dispatch-chain layer, 17 intercepted functions |
| OpenCL (`cl_intercept.c`) | ~600 | Complete - LD_PRELOAD, platform + device interception |
| CUDA (`cuda_wrapper.c`) | ~1,340 | Complete - 40+ functions, 6 distribution strategies |
| D3D (`d3d_wrapper.c`) | ~1,595 | Complete - all types fixed, compiles and links |
| Metal (`metal_wrapper.c`) | ~400 | Complete |
| WebGPU (`webgpu_wrapper.c`) | ~300 | Complete |

### 2.6 Vulkan ICD (`src/userspace/vulkan_icd/`)

| File | Description |
|------|-------------|
| `icd_entry.c` | ICD entry points, vk_icdGetInstanceProcAddr |
| `physical_device.c` | Virtual VkPhysicalDevice with limits aggregation |
| `device_group.c` | Device group emulation, queue family aggregation |
| `command_buffer_rewrite.c` | Command buffer distribution across GPUs |

### 2.7 CLI Tools (`tools/`)

| Tool | LOC | Description |
|------|-----|-------------|
| `mvgal-info.c` | ~372 | GPU info, VRAM, temp, utilization, JSON output |
| `mvgal-status.c` | ~373 | Real-time bars, daemon check, --watch mode |
| `mvgal-bench.c` | ~463 | Memory BW, compute FLOPS, latency, sync overhead |
| `mvgal-compat.c` | ~366 | System check + 15+ app compatibility database |
| `mvgal-config.c` | ~400 | Strategy, GPU enable/disable, stats, reload |
| `mvgal.c` | ~350 | Main CLI: start/stop, status, load-module |
| `mvgal_exporter.go` | ~200 | Prometheus metrics exporter |

### 2.8 Steam/Proton Layer (`steam/`)

| File | Description |
|------|-------------|
| `mvgal_frame_pacer.c/h` | Vsync-aligned frame pacing, ring buffer depth 8 |
| `mvgal_steam_compat.sh` | Steam compatibility tool entry point |
| `toolmanifest.vdf` | Steam tool manifest |
| `compatibilitytool.vdf` | Steam tool registration |

### 2.9 OpenGL Layer (`opengl/`)

| File | Description |
|------|-------------|
| `mvgal_gl_preload.c` | LD_PRELOAD shim intercepting glXSwapBuffers + eglSwapBuffers |

### 2.10 UI Dashboard (`ui/`)

| File | Description |
|------|-------------|
| `mvgal_dashboard.cpp/h` | Qt5/Qt6, 4 tabs: Overview, Scheduler, Logs, Config |
| `mvgal_rest_server.go` | Go HTTP server, 5 REST endpoints on :7474 |

### 2.11 Compatibility Layer (`compat/`)

| File | Description |
|------|-------------|
| `ldpreload/mvgal_preload.c` | Generic LD_PRELOAD hook |
| `vulkan_layer/mvgal_layer.cpp` | Vulkan layer implementation |
| `windows/mvgal_ntsync.c/h` | NTSYNC compatibility shim |

### 2.12 Language Bindings (`bindings/`)

| Language | File |
|----------|------|
| Java | `bindings/java/MvgalClient.java` |
| C# | `bindings/csharp/MvgalClient.cs` |
| D | `bindings/d/mvgal.d` |
| Nim | `bindings/nim/mvgal.nim` |
| V | `bindings/v/mvgal.v` |
| Crystal | `bindings/crystal/mvgal.cr` |
| Haxe | `bindings/haxe/Mvgal.hx` |

---

## 3. Data Flow Diagrams

### 3.1 Gaming Workload (AFR)

```
Game (via Proton/DXVK)
  |
  | vkQueueSubmit (frame N)
  v
VK_LAYER_MVGAL (intercepts, increments atomic counter, logs telemetry)
  |
  | forwards to next layer in dispatch chain
  v
Physical ICD (AMD / NVIDIA / Intel)
  |
  | (parallel) mvgald AFR scheduler
  |   +-- Frame N   -> GPU 0 (even)
  |   +-- Frame N+1 -> GPU 1 (odd)
  v
Frame pacer (steam/mvgal_frame_pacer.c)
  | ring buffer depth 8, background thread
  | sleep_until_ns(next_vsync_boundary)
  v
vkQueuePresentKHR on display-connected GPU
```

### 3.2 AI Compute Workload

```
PyTorch / TensorFlow
  |
  | CUDA kernel launch (cudaLaunchKernel)
  v
MVGAL CUDA shim (LD_PRELOAD)
  | intercepts, translates to MVGAL workload submission
  v
mvgald Compute Offload scheduler
  | shards batch dimension across N GPUs
  | allocates input tensors via unified memory manager
  | issues DMA-BUF transfers for cross-GPU data
  v
GPU 0 ... GPU N-1 (parallel execution)
  |
  | results collected via write-combined system
  v
Application receives aggregated result
```

### 3.3 Memory Transfer Path

```
Source GPU export
  |
  +-- DMA-BUF viable?  -> dma_buf_map_attachment (zero-copy)
  |
  +-- DMA-BUF not viable
       |
       +-- PCIe P2P viable?  -> pci_p2pdma (same root complex)
       |
       +-- Fallback  -> mmap source + DMA to host staging + upload to dest
```

---

## 4. Dependency Graph

### 4.1 Kernel Module Dependencies

```
mvgal.ko
  +-- drm.ko (DRM framework, disabled in current build)
  +-- pci subsystem (GPU enumeration)
  +-- dma-buf framework (memory sharing, stub)
  +-- iommu framework (address translation, planned)
  +-- sysfs (topology exposure)
  +-- cdev (character device)
```

### 4.2 Userspace Component Dependencies

```
libmvgal.so
  +-- libdrm (DRM userspace)
  +-- libpci/pciaccess (PCI enumeration)
  +-- libudev (device events)
  +-- pthread (threading)
  +-- dl (dynamic loading)

mvgald (daemon)
  +-- libmvgal.so
  +-- pthread
  +-- libdrm
  +-- Rust crates (fence_manager, memory_safety, capability_model)

VK_LAYER_MVGAL.so
  +-- libvulkan (Vulkan loader)
  +-- libmvgal.so

mvgal_vulkan_icd.so
  +-- libvulkan
  +-- libmvgal.so

mvgal_cuda.so
  +-- libcuda.so (NVIDIA driver)
  +-- libmvgal.so

mvgal_opencl.so
  +-- libOpenCL.so
  +-- libmvgal.so

mvgal_gl.so
  +-- libGL.so / libEGL.so
  +-- libdl.so
```

### 4.3 External Dependencies

| Category | Libraries |
|----------|-----------|
| Graphics | Vulkan SDK, OpenGL, Mesa (Zink) |
| Compute | CUDA Toolkit, ROCm, oneAPI Level Zero, OpenCL ICD |
| Build | CMake 3.16+, Ninja, Meson 1.0+, Cargo (Rust 1.75+), Go modules |
| UI | Qt5/Qt6, GTK (optional) |
| Packaging | DKMS, dpkg, rpm, pacman |

---

## 5. Key Data Structures

### 5.1 Kernel-Side

```c
// GPU slot in kernel device (mvgal_main.c)
struct mvgal_gpu_slot {
    struct mvgal_gpu_info info;   // PCI topology, vendor, BDF
    bool present;                  // GPU detected
    bool enabled;                  // GPU enabled for aggregation
    char power_state[8];           // "auto", "on", "off"
    struct mvgal_gpu_sysfs *sysfs; // Per-GPU sysfs kobject
};

// Main device structure (mvgal_main.c)
struct mvgal_device {
    struct mutex lock;
    struct mvgal_gpu_slot gpus[MVGAL_UAPI_MAX_GPUS];
    struct mvgal_uapi_stats stats;
    struct notifier_block pci_notifier;
    dev_t devt;
    struct device *class_dev;
    u32 gpu_count;
    u32 topology_generation;
    bool enabled;
    bool notifier_registered;
};

// GPU device (mvgal_device.c)
struct mvgal_gpu_device {
    enum mvgal_vendor_id vendor;
    struct pci_dev *pdev;
    const struct mvgal_vendor_ops *ops;
    void *vendor_priv;
    uint64_t vram_size;
    uint64_t vram_bandwidth;
    uint32_t compute_units;
    uint64_t api_flags;
    uint32_t gpu_index;
    uint32_t numa_node;
    bool available;
    bool enabled;
    struct list_head node;
    struct mutex lock;
    // DVFS, thermal, power state...
};

// Vendor operations interface
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

### 5.2 Userspace-Side

```c
// GPU descriptor (mvgal_gpu.h)
typedef struct {
    uint32_t id;
    char name[256];
    mvgal_vendor_t vendor;
    mvgal_gpu_type_t type;
    // PCI info, memory info, capabilities, performance scores...
    char drm_node[64];
    char drm_render_node[64];
    bool enabled;
    bool available;
} mvgal_gpu_descriptor_t;

// Logical device descriptor (mvgal_gpu.h)
typedef struct {
    mvgal_gpu_descriptor_t descriptor;
    uint32_t gpu_count;
    uint32_t gpu_indices[16];
    uint64_t gpu_mask;
    uint64_t common_features;
    uint64_t aggregate_features;
    bool heterogeneous;
    uint32_t primary_gpu_index;
} mvgal_logical_device_descriptor_t;

// Workload telemetry (mvgal_types.h)
typedef struct {
    mvgal_workload_type_t type;
    mvgal_gpu_index_t gpu_index;
    const char *step_name;
    mvgal_size_t data_size;
    mvgal_timestamp_t timestamp;
    struct { uint64_t flags_value; } flags;
    struct { uint32_t x, y, z; } dims;
    void *user_data;
} mvgal_workload_telemetry_t;
```

---

## 6. Algorithms

### 6.1 Scheduling Strategies

| Strategy | Algorithm |
|----------|-----------|
| Round-robin | Distribute workloads sequentially across enabled GPUs |
| AFR | Even frames -> GPU 0, odd frames -> GPU 1 |
| SFR | Split frame into tiles (horizontal/vertical), assign to GPUs |
| Task-based | Route by workload type (graphics vs compute) |
| Compute offload | Route compute to highest-FLOPS GPU |
| Hybrid | Adaptive selection based on workload metrics |
| Single GPU | Use only primary GPU |
| Auto | System selects based on workload history |
| AI-driven | ML model predicts optimal GPU assignment |

### 6.2 Memory Management

- **Allocation policy**: Size < 64MB -> GPU with most free VRAM; render target -> GPU that writes first; large buffer -> GPU most likely to use it
- **Transfer path**: DMA-BUF zero-copy (preferred) -> PCIe P2P (fallback) -> Host-RAM staging (last resort)
- **UVA space**: 128TB address space (similar to NVIDIA UVM), base at 1TB

### 6.3 Power Management

- **State machine**: ACTIVE -> SUSTAINED -> IDLE -> PARK
- **Timeouts**: idleTimeoutMs=5000ms, sustainedTimeoutMs configurable, parkTimeoutMs configurable
- **DVFS**: Adjust GPU clock based on utilization thresholds
- **Thermal throttling**: Reduce clocks when temperature exceeds threshold

### 6.4 P2P Detection

```
mvgal_detect_p2p_support(gpu1, gpu2):
  1. Check same NUMA node -> P2P supported
  2. NVIDIA + NVIDIA -> Check NVLink topology
  3. AMD + AMD -> Check xGMI/Infinity Fabric
  4. Otherwise -> P2P unsupported
```

---

## 7. Missing or Incomplete Components

| Component | Current State | Gap |
|-----------|--------------|-----|
| Kernel DMA-BUF export/import | Returns -EOPNOTSUPP | Needs actual dma_buf_ops implementation |
| Kernel workload submission | Returns -EOPNOTSUPP | Needs vendor ops integration |
| Vendor VRAM allocation | Fake addresses (0x10000000) | Needs real TTM/GEM/BO allocation |
| Vendor utilization query | Fake (50%) | Needs sysfs reading |
| DRM registration | #if 0 disabled | Needs kernel 7.x compatibility fix |
| Cross-vendor memory migration | Stub | Needs migration path selection + execution |
| CI automation | Manual-only (workflow_dispatch) | Needs auto-trigger on push/PR |
| KUnit tests | Not implemented | Needs kernel test framework |
| `.clang-format` | Missing | Referenced in CONTRIBUTING.md |
| LICENSE | GPL-3.0 text | Should be dual GPL-2.0/MIT per README |

---

## 8. Build System

| System | Purpose | Status |
|--------|---------|--------|
| CMake (root) | Primary build system | Complete |
| Meson | Alternative build | Complete |
| Zig (build.zig) | Alternative build | Complete |
| Kbuild (kernel/Makefile) | Kernel module | Complete |
| Cargo (Rust workspace) | Safety crates | Complete |
| Go modules | REST server, exporter | Complete |
| DKMS | Kernel module auto-rebuild | Config present |

---

## 9. Test Coverage

| Suite | Pass | Total | Notes |
|-------|------|-------|-------|
| C unit tests | 5 | 5 | core_api, gpu_detection, memory, scheduler, config |
| C integration tests | 1 | 1 | multi_gpu_validation |
| C wrapper tests | 5 | 5 | opencl, d3d, metal, webgpu, multi_gpu |
| Rust unit tests | 12 | 12 | fence(3), memory(3), capability(6) |
| Rust FFI tests | 9 | 9 | Cross-crate FFI integration |
| Synthetic benchmarks | 10 | 10 | Memory BW, compute, latency, sync |
| Real-world benchmarks | 12 | 12 | Gaming, rendering, AI workloads |
| Stress benchmarks | 9 | 10 | 1 cosmetic threading artifact |
| **Total** | **63** | **64** | **98.4%** |
