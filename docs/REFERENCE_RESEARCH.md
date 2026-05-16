# MVGAL Reference Research

**Version:** 0.2.2 | **Date:** May 2026

---

## 1. NVIDIA Open GPU Kernel Modules

**Repository:** `github.com/NVIDIA/open-gpu-kernel-modules`

### Key Findings

- **License**: Dual MIT/GPL-2.0 (compatible with MVGAL's kernel module)
- **Architecture**: Two kernel modules:
  - `nvidia.ko` - Proprietary user-space driver companion
  - `nvidia-open.ko` - Fully open-source kernel module
- **Memory Management**: Uses NVIDIA's proprietary memory manager internally; open module exposes limited interfaces
- **DMA-BUF**: Supports dma-buf export/import for PRIME interoperability
- **Multi-GPU**: No built-in multi-GPU aggregation; SLI/NVLink handled at application level

### Relevance to MVGAL

- NVIDIA's open kernel module provides the `nvidia-vgpu-mgr` interface that could be leveraged for VRAM allocation
- DMA-BUF support is essential for cross-vendor memory sharing
- The open module's limited API means MVGAL must rely on userspace CUDA shim for compute workloads

---

## 2. AMDVLK (AMD Vulkan Driver)

**Repository:** `github.com/GPUOpen-Drivers/AMDVLK`

### Key Findings

- **License**: MIT
- **Architecture**: Full Vulkan driver with PAL (Platform Abstraction Layer)
- **Multi-GPU**: Supports explicit multi-GPU via Vulkan device groups
- **Memory Management**: Uses AMDGPU kernel driver (amdgpu.ko) for VRAM allocation
- **Cross-GPU**: Supports DMA-BUF for PRIME offloading

### Relevance to MVGAL

- AMDVLK's PAL layer demonstrates how to abstract GPU-specific operations
- The driver's multi-GPU support via Vulkan device groups is a model for MVGAL's Vulkan ICD
- AMDGPU kernel driver provides sysfs interfaces that MVGAL already reads for power/thermal

---

## 3. Moore Threads mtgpu-drv

**Repository:** `github.com/mthreads/mtgpu-drv`

### Key Findings

- **License**: GPL-2.0
- **Architecture**: DRM-based kernel driver for Moore Threads S-series GPUs
- **Memory Management**: Uses DRM GEM for buffer management
- **Multi-GPU**: No explicit multi-GPU support in driver
- **Status**: Actively maintained, supports S2000/S3000/S4000

### Relevance to MVGAL

- mtgpu-drv provides the kernel interface for Moore Threads GPUs
- MVGAL's `mvgal_mtt.c` vendor ops integrate with this driver
- The driver's GEM-based memory management is compatible with MVGAL's DMA-BUF approach

---

## 4. Intel Media Driver / Xe Driver

**Repository:** `github.com/intel/media-driver`, `github.com/intel/intel-graphics-compiler`

### Key Findings

- **License**: MIT (media-driver), Apache 2.0 (IGC)
- **Architecture**: Two-tier:
  - `i915.ko` - Legacy Intel graphics driver (Gen 8-11)
  - `xe.ko` - New Xe driver (Gen 12+, Arc discrete GPUs)
- **Memory Management**: Uses DRM GEM with TTM for discrete GPUs
- **Multi-GPU**: Supports Vulkan device groups for multi-GPU

### Relevance to MVGAL

- Intel's transition from i915 to xe mirrors MVGAL's need to support both driver generations
- The media driver's VA-API integration is relevant for MVGAL's video encoding/decoding support
- Intel's oneAPI Level Zero provides compute abstraction that MVGAL's CUDA shim could target

---

## 5. NCCL (NVIDIA Collective Communications Library)

**Repository:** `github.com/NVIDIA/nccl`

### Key Findings

- **License**: BSD-3-Clause
- **Purpose**: High-performance multi-GPU communication for AI/ML workloads
- **Multi-GPU Support**:
  - NVLink: Direct GPU-to-GPU memory access
  - PCIe P2P: Peer-to-peer DMA transfers
  - Network: InfiniBand/RoCE for multi-node
- **Collective Operations**: AllReduce, AllGather, Broadcast, Reduce, etc.
- **Topology Awareness**: Automatically detects GPU topology and optimizes communication paths

### Relevance to MVGAL

- NCCL's topology detection algorithm is a model for MVGAL's P2P detection
- The library's fallback strategy (NVLink -> PCIe P2P -> Host staging) matches MVGAL's memory transfer path
- NCCL's collective operations could be exposed through MVGAL's compute API for AI workloads

---

## 6. SLI / CrossFire Legacy Technologies

### SLI (NVIDIA)

- **Technology**: Links 2-4 identical NVIDIA GPUs via NVLink bridge
- **Modes**: AFR (alternate frame rendering), SFR (split frame rendering), SLI AA
- **Limitations**: Requires identical GPUs, application support, deprecated in RTX 40-series
- **Relevance**: MVGAL's AFR/SFR strategies are directly inspired by SLI

### CrossFire (AMD)

- **Technology**: Links 2-4 AMD GPUs via PCIe or XDMA bridge
- **Modes**: AFR, SuperTile, Scissor
- **Limitations**: Requires identical GPUs, deprecated in RDNA 2+
- **Relevance**: MVGAL's SFR (tile-based) strategy mirrors CrossFire's SuperTile

### Key Insight

Both SLI and CrossFire required **identical GPUs** and **application-specific profiles**. MVGAL's innovation is supporting **heterogeneous GPUs** (different vendors, different capabilities) through capability normalization and adaptive scheduling.

---

## 7. Compatibility Layers

### 7.1 Proton (Valve)

**Repository:** `github.com/ValveSoftware/Proton`

- **Purpose**: Run Windows games on Linux via Wine + DXVK + VKD3D
- **Multi-GPU**: No built-in multi-GPU support; relies on underlying Vulkan driver
- **Relevance**: MVGAL's Steam integration layer works alongside Proton to distribute frames across GPUs

### 7.2 DXVK

**Repository:** `github.com/doitsujin/dxvk`

- **Purpose**: Translate Direct3D 9/10/11 to Vulkan
- **Multi-GPU**: Supports Vulkan device groups for multi-GPU
- **Relevance**: MVGAL's Vulkan ICD presents as a single device to DXVK, which then distributes to physical GPUs

### 7.3 VKD3D-Proton

**Repository:** `github.com/HansKristian-Work/vkd3d-proton`

- **Purpose**: Translate Direct3D 12 to Vulkan
- **Multi-GPU**: Supports Vulkan device groups
- **Relevance**: Same as DXVK; MVGAL's aggregation is transparent to VKD3D

### 7.4 NTSYNC

**Repository:** `github.com/zfigura/ntsync`

- **Purpose**: Kernel module for Windows NT synchronization primitives
- **Relevance**: MVGAL's `compat/windows/mvgal_ntsync.c` provides similar functionality for cross-GPU synchronization

---

## 8. Multi-GPU Framework Survey

### 8.1 Vulkan Device Groups

- **API**: `vkEnumeratePhysicalDeviceGroups`, `vkGetDeviceGroupPeerMemoryFeatures`
- **Capability**: Present different images from different GPUs, share memory via external memory handles
- **Limitation**: Requires application to explicitly manage multi-GPU

### 8.2 CUDA Multi-GPU

- **API**: `cudaSetDevice`, `cudaMemcpyPeer`, `cudaMemcpyPeerAsync`
- **Capability**: Explicit multi-GPU programming with peer-to-peer memory access
- **Limitation**: Requires application to manage device selection and data movement

### 8.3 OpenCL Multi-Device

- **API**: `clCreateContext` with multiple devices, `clEnqueueMigrateMemObjects`
- **Capability**: Context spans multiple devices, memory migration between devices
- **Limitation**: No automatic load balancing; application must manage distribution

### 8.4 MVGAL's Approach

MVGAL sits **above** all these frameworks, intercepting API calls and distributing workloads transparently. The key differentiators:

1. **Heterogeneous support**: Different vendors, different capabilities
2. **Transparent aggregation**: Applications see a single logical device
3. **Adaptive scheduling**: Strategies change based on workload characteristics
4. **Memory management**: Automatic cross-GPU memory placement and migration

---

## 9. Kernel Module Development Patterns

### 9.1 DRM Driver Registration

- **Standard approach**: Register with DRM subsystem via `drm_dev_alloc`, `drm_dev_register`
- **Issue**: MVGAL's DRM registration is disabled (`#if 0`) due to kernel 7.x compatibility
- **Alternative**: Character device (`/dev/mvgal0`) provides equivalent functionality

### 9.2 DMA-BUF Framework

- **Export**: `dma_buf_export()` with `dma_buf_ops` (map, unmap, release)
- **Import**: `dma_buf_attach()`, `dma_buf_map_attachment()`
- **Status**: MVGAL's kernel module has stub implementations returning `-EOPNOTSUPP`

### 9.3 PCI Hotplug Notifier

- **Mechanism**: `blocking_notifier_chain_register` with `BUS_NOTIFY_ADD_DEVICE`
- **Status**: MVGAL implements this in `mvgal_main.c` for dynamic GPU detection

### 9.4 Sysfs Integration

- **Class device**: `class_create("mvgal")`, `device_create()`
- **Attributes**: `DEVICE_ATTR_RO`, `DEVICE_ATTR_RW`
- **Status**: MVGAL implements full sysfs hierarchy under `/sys/class/mvgal/`

---

## 10. Build System Patterns

### 10.1 Kbuild (Kernel Module)

```makefile
obj-m += mvgal.o
mvgal-y := mvgal_main.o mvgal_core.o mvgal_device.o mvgal_memory.o \
           mvgal_scheduler.o mvgal_power.o mvgal_sync.o
mvgal-y += vendors/mvgal_amd.o vendors/mvgal_nvidia.o \
           vendors/mvgal_intel.o vendors/mvgal_mtt.o
```

### 10.2 CMake (Userspace)

- Options: `MVGAL_BUILD_KERNEL`, `MVGAL_BUILD_RUNTIME`, `MVGAL_BUILD_API`, `MVGAL_BUILD_GAMING`, `MVGAL_BUILD_TESTS`, `MVGAL_BUILD_RUST`, `MVGAL_BUILD_ZIG`
- Conditional compilation based on available dependencies

### 10.3 Cargo (Rust)

- Workspace with 4 crates: `mvgal-scheduler`, `mvgal-capability-model`, `mvgal-config`, `mvgal-logging`
- FFI bindings via `#[repr(C)]` structs and `extern "C"` functions

---

## 11. Security Considerations

### 11.1 Privilege Escalation

- **Polkit**: `pkexec` for module load/unload, power management
- **Policy file**: `config/org.freedesktop.policykit.mvgal.policy`
- **Actions**: `org.mvgal.module.load`, `org.mvgal.module.unload`, `org.mvgal.power.manage`

### 11.2 IPC Security

- **Unix socket**: `SCM_CREDENTIALS` for peer authentication
- **PID validation**: Daemon verifies client UID/GID
- **Authorization**: Configurable access control

### 11.3 Memory Safety

- **Rust crates**: `fence_manager`, `memory_safety` provide safe wrappers around unsafe FFI
- **Bounds checking**: All array accesses validated
- **Reference counting**: DMA-BUF handles tracked with atomic refcounts

---

## 12. Performance Considerations

### 12.1 Zero-Copy Memory Sharing

- **DMA-BUF**: Preferred path for cross-GPU memory sharing
- **PCIe P2P**: Fallback when DMA-BUF not available
- **Host staging**: Last resort, incurs copy overhead

### 12.2 Scheduling Overhead

- **Frame pacing**: Ring buffer depth 8, vsync-aligned presentation
- **Work-stealing**: Dynamic load balancing with minimal synchronization
- **Thread pool**: Configurable worker count, lock-free queues

### 12.3 Power Management

- **GPU parking**: Disable unused GPUs to save power
- **DVFS**: Dynamic voltage/frequency scaling based on utilization
- **Thermal throttling**: Reduce clocks when temperature exceeds threshold
