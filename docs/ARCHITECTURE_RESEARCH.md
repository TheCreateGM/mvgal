# MVGAL Architecture Research

## Multi-Vendor GPU Aggregation Layer for Linux

**Version:** 1.0 - Initial Research Phase  
**Date:** 2024  
**Status:** In Progress

---

## Table of Contents

1. [GPU Driver Architecture](#1-gpu-driver-architecture)
2. [Initialization Flow](#2-initialization-flow)
3. [Rendering/Workload Flow](#3-renderingworkload-flow)
4. [Memory & Data Flow](#4-memory--data-flow)
5. [Cross-Vendor Compatibility](#5-cross-vendor-compatibility)
6. [References](#6-references)

---

## 1. GPU Driver Architecture

### 1.1 Linux DRM/KMS Overview

The Linux Direct Rendering Manager (DRM) and Kernel Mode Setting (KMS) provide the foundation for GPU driver support in the Linux kernel.

#### DRM (Direct Rendering Manager)
- **Purpose:** Provide direct hardware access to user-space graphics drivers
- **Location:** `/drivers/gpu/drm/` in Linux kernel
- **Key Components:**
  - DRM core: `/drivers/gpu/drm/drm_*.c`
  - DRM bridge: Connects DRM core to display controllers
  - DRM connector: Represents physical display outputs
  - DRM encoder: Converts pixel data to display signals
  - DRM crtc: CRT Controller - controls display timing

#### KMS (Kernel Mode Setting)
- **Purpose:** Set display modes in kernel space (resolution, refresh rate)
- **Benefits:** Smoother mode switching, better suspend/resume
- **Components:**
  - Mode setting ioctls
  - Framebuffer management
  - Output configuration

#### Device Nodes
- `/dev/dri/cardX` - Primary render node (X = 0, 1, 2...)
  - Supports render operations
  - Used by Mesa, Vulkan, OpenCL
- `/dev/dri/renderDX` - Render-only node
  - No display access, render-only
  - Used by compute workloads
- `/dev/dri/controlDX` - Control node (rare)

### 1.2 Vendor-Specific Driver Architectures

#### 1.2.1 AMD (amdgpu)

**Open Source Stack:**
```
+------------------+     +------------------+
|   User Space    |     |   User Space    |
|------------------|     |------------------|
|   Mesa (OpenGL  |     |   ROCm (Compute)|
|   Vulkan drivers)|     |   HIP Runtime   |
|   Gallium3D     |     |   OpenCL        |
+--------+--------+     +--------+--------+
         |                      |
         +----------------------+
                        |
+----------------------------------------+
|                 Kernel Space            |
|----------------------------------------|
|            drm/amdgpu/                  |
|  +---------------------------+         |
|  |     amdgpu.ko            |         |
|  |  +---------------------+  |         |
|  |  |   DC (Display Core)|  |         |
|  |  +---------------------+  |         |
|  |  +---------------------+  |         |
|  |  |   GC (Graphics    |  |         |
|  |  |   Command Processor)| |         |
|  |  +---------------------+  |         |
|  |  +---------------------+  |         |
|  |  |   SDMA (System    |  |         |
|  |  |   DMA)           |  |         |
|  |  +---------------------+  |         |
|  +---------------------------+         |
|  +--------------------------------+   |
|  |      DRM Core (drm_*.ko)      |   |
|  +--------------------------------+   |
+----------------------------------------+
         |                        |
         v                        v
+------------------+     +------------------+
|   PCIe Device    |     |   PCIe Device    |
|   (Discrete GPU) |     |   (iGPU/APU)    |
+------------------+     +------------------+
```

**Key Components:**
- **amdgpu.ko**: Main kernel driver module
- **DC (Display Core)**: Modern display engine (replaces DAL)
- **GC (Graphics Command)**: Graphics command processor
- **SDMA**: System DMA for data transfers
- **UVML**: Unified Virtual Memory Layer

**Driver Files:**
- `/drivers/gpu/drm/amd/amdgpu/` - Main driver
- `/drivers/gpu/drm/amd/display/` - Display code

**Device Nodes:**
- `/dev/kfd` - HSA Kernel Fusion Driver (for ROCm)
- `/dev/dri/cardX` - Standard DRM nodes

**Capabilities:**
- Vulkan: RADV (Mesa), AMDVLK (AMD official)
- OpenGL: radeonsi (Mesa)
- Compute: ROCm (HIP, OpenCL)
- Video: VCN (Video Core Next)

**Memory Management:**
- GTT (Graphics Translation Table)
- VRAM management
- GART (Graphics Address Remapping Table)
- HMM (Heterogeneous Memory Management) for APUs

#### 1.2.2 NVIDIA

**Proprietary Stack:**
```
+------------------+     +------------------+
|   User Space    |     |   User Space    |
|------------------|     |------------------|
|   NVIDIA GLX    |     |   CUDA Runtime  |
|   OpenGL driver |     |   cu BLAS, etc.  |
|   libGLX_nvidia |     |   OpenCL        |
+--------+--------+     +--------+--------+
         |                      |
         +----------------------+
                        |
+----------------------------------------+
|                 Kernel Space            |
|----------------------------------------|
|            nvidia.ko                    |
|  +---------------------------+         |
|  |   NVIDIA Kernel Module   |         |
|  |  +---------------------+  |         |
|  |  |   NVKM (Nouveau    |  |         |
|  |  |   compatibility)   |  |         |
|  |  +---------------------+  |         |
|  |  +---------------------+  |         |
|  |  |   Mode Setting    |  |         |
|  |  +---------------------+  |         |
|  |  +---------------------+  |         |
|  |  |   Memory Mgmt     |  |         |
|  |  +---------------------+  |         |
|  +---------------------------+         |
|                                          |
|  +--------------------------------+   |
|  |      DRM Core (limited use)    |   |
|  +--------------------------------+   |
+----------------------------------------+
```

**Approach 1: Proprietary Driver (nvidia.ko)**
- **Module:** `nvidia.ko` (main), `nvidia-modeset.ko`, `nvidia-drm.ko`, `nvidia-uvm.ko`
- **Device Nodes:**
  - `/dev/nvidia0` - Primary GPU
  - `/dev/nvidia1` - Second GPU
  - `/dev/nvidiactl` - Control device
  - `/dev/nvidia-modeset` - Mode setting
  - `/dev/nvidia-uvm` - Unified Virtual Memory
  - `/dev/dri/cardX` - Limited DRM support

**Capabilities:**
- Vulkan: NVIDIA official driver
- OpenGL: NVIDIA official driver
- Compute: CUDA, OpenCL
- Video: NVDEC, NVENC

**Memory Management:**
- UVM (Unified Virtual Memory)
- Mapped memory via `mmap()` on device nodes
- Peer-to-peer memory access between GPUs
- Unified memory addressing

**Approach 2: Nouveau (Open Source)**
- **Module:** `nouveau.ko`
- **Status:** Reverse-engineered, limited support
- **Capabilities:**
  - Basic display output
  - Limited 3D acceleration
  - Performance issues with newer GPUs
  - No official Vulkan support (experimental via Mesa)

**Proprietary Driver Limitations for MVGAL:**
1. Closed source - cannot modify driver internals
2. Limited DRM integration - bypasses standard DRM/KMS for many operations
3. No kernel module linking - cannot hook into nvidia.ko
4. Requires user-space interception (LD_PRELOAD, Vulkan layers)
5. Memory not directly shareable via DMA-BUF without explicit support

#### 1.2.3 Intel

**Open Source Stack:**
```
+------------------+     +------------------+
|   User Space    |     |   User Space    |
|------------------|     |------------------|
|   Mesa (Intel   |     |   Intel Compute |
|   OpenGL/Vulkan)|     |   Runtime       |
|   ANV driver    |     |   oneAPI        |
|   (Vulkan)      |     |   OpenCL        |
+--------+--------+     +--------+--------+
         |                      |
         +----------------------+
                        |
+----------------------------------------+
|                 Kernel Space            |
|----------------------------------------|
|            drm/i915/ or xe/            |
|  +---------------------------+         |
|  |   i915.ko (legacy)       |         |
|  |   or xe.ko (new)         |         |
|  |  +---------------------+  |         |
|  |  |   GT (Graphics    |  |         |
|  |  |   Technology)     |  |         |
|  |  +---------------------+  |         |
|  |  +---------------------+  |         |
|  |  |   Display Engine |  |         |
|  |  +---------------------+  |         |
|  |  +---------------------+  |         |
|  |  |   Memory Mgmt     |  |         |
|  |  +---------------------+  |         |
|  +---------------------------+         |
|                                          |
+----------------------------------------+
```

**Driver Evolution:**
- **i915.ko**: Legacy driver for Intel GPUs up to Gen12 (Tiger Lake)
- **xe.ko**: New unified driver for Gen12+ (Alder Lake and newer)

**Key Components (i915):**
- Intel Graphics Technology (GT) - 3D/Media engine
- Display Engine - Display output
- GEM (Graphics Execution Manager) - Memory management

**Key Components (xe):**
- Unified GPU driver supporting display, compute, media
- Better support for modern features
- Improved memory management

**Device Nodes:**
- `/dev/dri/cardX` - Standard DRM nodes
- Full DRM/KMS support

**Capabilities:**
- Vulkan: ANV (Intel Vulkan driver in Mesa)
- OpenGL: iris (Mesa), i965 (legacy)
- Compute: OpenCL via beignet or neo, oneAPI
- Video: QuickSync (QSV), VAAPI

**Memory Management:**
- PPGTT (Per-Process Graphics Translation Tables)
- Full 48-bit address space
- SVGTT (Shared Virtual Graphics Translation Tables) for sharing

#### 1.2.4 Moore Threads (MUSA)

**Stack Overview:**
```
+------------------+     +------------------+
|   User Space    |     |   User Space    |
|------------------|     |------------------|
|   MUSA Runtime  |     |   MUSA Vulkan  |
|   (Compute)     |     |   Driver        |
|   MUSA OpenCL   |     +------------------+
+--------+--------+
         |
         |
+----------------------------------------+
|                 Kernel Space            |
|----------------------------------------|
|            musa.ko (proposed)           |
|  +---------------------------+         |
|  |   Moore Threads Driver   |         |
|  |  (work in progress)      |         |
|  +---------------------------+         |
|                                          |
|  +--------------------------------+   |
|  |      DRM Core                   |   |
|  +--------------------------------+   |
+----------------------------------------+
```

**Current Status (Research):**
- Moore Threads GPUs are x86 compatible with full GPU capabilities
- MUSA (Moore Threads Unified System Architecture) is the compute stack
- Vulkan support is in development
- Driver may use standard DRM/KMS or custom approach

**Expected Device Nodes:**
- Likely `/dev/dri/cardX` for graphics
- Custom nodes for compute (TBD)

**Key Challenges:**
- Limited public documentation
- Driver maturity
- Cross-vendor coordination

### 1.3 Common Abstraction Points

Based on the architecture analysis, here are the potential abstraction points for MVGAL:

#### 1.3.1 Kernel-Level Abstraction

| Abstraction Point | AMD | NVIDIA | Intel | Moore Threads | Feasibility |
|------------------|-----|--------|-------|----------------|-------------|
| DRM Device Node (`/dev/dri/cardX`) | ✅ | Limited | ✅ | Likely | Medium |
| DRM File Operations | ✅ | Partial | ✅ | Likely | Medium |
| DRM IOCTLs | ✅ | Partial | ✅ | Likely | Medium |
| DMA-BUF Export | ✅ | ✅ (limited) | ✅ | Likely | High |
| Memory Mapping | ✅ | ✅ | ✅ | Likely | High |
| Kernel Module Hooks | ✅ | ❌ (proprietary) | ✅ | TBD | Low |

#### 1.3.2 User-Space Abstraction

| Abstraction Point | AMD | NVIDIA | Intel | Moore Threads | Feasibility |
|------------------|-----|--------|-------|----------------|-------------|
| Vulkan API | ✅ | ✅ | ✅ | Planned | High |
| OpenCL API | ✅ | ✅ | ✅ | Planned | High |
| LD_PRELOAD Interception | ✅ | ✅ | ✅ | ✅ | High |
| Vulkan Layers | ✅ | ✅ | ✅ | ✅ | High |
| Wayland/Weston | ✅ | ✅ | ✅ | ✅ | Medium |

### 1.4 Recommended Abstraction Strategy

**Primary Approach: User-Space Interception with Kernel Support (Optional)**

1. **Layer 1: User-Space Interception (Mandatory)**
   - Vulkan Layer for graphics
   - OpenCL/SYCL interception via LD_PRELOAD
   - CUDA wrapper (if feasible)

2. **Layer 2: DRM-Based Abstraction (Recommended)**
   - Use `/dev/dri/cardX` for rendering (Intel, AMD, Moore Threads)
   - Use NVIDIA's custom nodes with wrapper for NVIDIA
   - DMA-BUF for memory sharing across vendors

3. **Layer 3: Kernel Module (Optional, Advanced)**
   - Hook into DRM core for command interception
   - Provide memory management coordination
   - Only feasible for open-source drivers (AMD, Intel, Moore Threads)

---

## 2. Initialization Flow

### 2.1 Standard Linux GPU Initialization

```
+------------------+     +------------------+
|   System Boot    |     |   PCIe Enum     |
+--------+--------+     +--------+--------+
         |                      |
         v                      v
+------------------+     +------------------+
|   Kernel:        |     |   Kernel:        |
|   PCI Subsystem  |     |   Device Tree    |
|   Detects GPU    |     +--------+--------+
+--------+--------+              |
         |                       |
         +-----------------------+
                         |
+------------------------+--------+
|           Kernel Module Load    |
|--------------------------------|
|  1. Module Probe                |
|  2. PCI Device Claim            |
|  3. MMIO Region Mapping         |
|  4. Firmware Load               |
|  5. Hardware Initialization     |
|  6. DRM Device Registration    |
|  7. Device Node Creation        |
+------------------------+--------+
                         |
+------------------------+--------+
|           User-Space Infrastructure  |
|--------------------------------------|
|  1. libudev detects device nodes     |
|  2. Mesa/Vulkan loads driver         |
|  3. Driver initializes context      |
|  4. Application can use GPU         |
+------------------------+--------+
```

### 2.2 Vendor-Specific Initialization

#### 2.2.1 AMD (amdgpu)

```
1. PCI: 01:00.0 -> Class: 0300 (VGA), Vendor: 1002 (AMD), Device: 73a0
2. Kernel: amdgpu.ko loads (module_init -> amdgpu_init)
3. Probe: amdgpu_probe() -> amdgpu_device_init()
4. Firmware: Request firmware files (e.g., amdgpu/vega20_sdma.bin)
5. MMIO: Map BAR regions (BAR0: MMIO, BAR2: VRAM)
6. DRM: drm_dev_register() -> /dev/dri/card0
7. KMS: Register connectors, encoders, crtcs
8. HSA: If ROCm enabled, kfd.ko loads -> /dev/kfd
9. User-space: libdrm detects card0, Mesa initializes
```

**Key Files:**
- Firmware: `/lib/firmware/amdgpu/`
- Module params: `/sys/module/amdgpu/parameters/`
- Device info: `/sys/kernel/debug/dri/0/`

#### 2.2.2 NVIDIA (Proprietary)

```
1. PCI: 01:00.0 -> Class: 0300 (VGA), Vendor: 10de (NVIDIA)
2. Kernel: nvidia.ko loads (nvidia_init_module)
3. Probe: nv_probe_devices() -> nv_register_device()
4. Device nodes: /dev/nvidia0, /dev/nvidiactl, /dev/nvidia-modeset
5. DRM: nvidia-drm.ko (optional) -> /dev/dri/card0 (limited)
6. UVM: nvidia-uvm.ko -> /dev/nvidia-uvm
7. User-space: libGLX_nvidia.so, libnvidia-ml.so
8. CUDA: libcuda.so loads, communicates with nvidia.ko
```

**Key Files:**
- Device nodes: `/dev/nvidia*`
- Libraries: `/usr/lib/nvidia/`, `/usr/lib/xorg/modules/drivers/`
- Config: `/etc/X11/xorg.conf`

#### 2.2.3 Intel (i915/xe)

```
1. PCI: 00:02.0 -> Class: 0300 (VGA), Vendor: 8086 (Intel)
2. Kernel: i915.ko or xe.ko loads
3. Probe: i915_probe() or xe_probe()
4. GT detection: Identify graphics generation
5. MMIO: Map register spaces
6. DRM: Register DRM device -> /dev/dri/card0
7. KMS: Setup display pipeline
8. GEM: Initialize memory manager
9. User-space: Mesa detects Intel GPU
```

**Key Files:**
- Debug: `/sys/kernel/debug/dri/0/`
- Module params: `/sys/module/i915/parameters/`

### 2.3 Unified Initialization Design

**Goal:** Detect all GPUs and build a capability profile for each.

```
+----------------------------------------+
|           MVGAL Initialization          |
|----------------------------------------|
|                                        |
|  1. Scan /sys/class/drm/               |
|     - Enumerate all cardX devices      |
|     - Read device info from sysfs     |
|                                        |
|  2. scan /dev/nvidia* (NVIDIA specific) |
|     - Check for proprietary nodes      |
|     - Detect NVIDIA GPUs               |
|                                        |
|  3. Query PCI bus                       |
|     - Get vendor/device IDs            |
|     - Identify GPU class codes         |
|                                        |
|  4. Load vendor-specific modules       |
|     - amdgpu for AMD                  |
|     - nvidia for NVIDIA               |
|     - i915/xe for Intel               |
|     - musa for Moore Threads          |
|                                        |
|  5. Create GPU Profile for each device:|
|     - Vendor ID                       |
|     - Device ID                       |
|     - Memory (VRAM, System)           |
|     - Compute Capabilities            |
|     - Graphics APIs supported         |
|     - Performance characteristics      |
|     - Interconnect type (PCIe gen)    |
|                                        |
|  6. Register GPUs into unified pool   |
|     - Assign unique MVGAL GPU IDs      |
|     - Setup memory sharing            |
|     - Initialize communication chan.  |
+----------------------------------------+
```

**Implementation Approach:**
```c
// Pseudocode for GPU detection
typedef struct {
    char vendor[64];           // "AMD", "NVIDIA", "Intel", "Moore Threads"
    char device[64];           // Device model
    uint64_t vram_total;       // Total VRAM in bytes
    uint64_t vram_free;        // Free VRAM in bytes
    uint32_t pcie_gen;         // PCIe generation (1-5)
    uint32_t pcie_lanes;       // Number of PCIe lanes
    bool vulkan_support;       // Supports Vulkan
    bool opencl_support;       // Supports OpenCL
    bool cuda_support;         // Supports CUDA (NVIDIA only)
    bool hip_support;          // Supports HIP (AMD only)
    char drm_node[64];         // /dev/dri/cardX
    char nvidia_node[64];      // /dev/nvidiaX
    float compute_score;       // Relative compute capability
    float graphics_score;      // Relative graphics capability
    uint32_t capabilities;     // Bitmask of supported features
} mvgal_gpu_profile_t;
```

---

## 3. Rendering/Workload Flow

### 3.1 Standard Rendering Pipeline

```
Application
    |
    v
+------------------+
|   API Layer      |  <- Vulkan, OpenGL, etc.
+------------------+
    |
    v
+------------------+
|   Driver         |  <- Mesa, NVIDIA, etc.
+------------------+
    |
    v
+------------------+
|   Kernel DRM     |  <- DRM/KMS
+------------------+
    |
    v
+------------------+
|   GPU Hardware   |
+------------------+
    |
    v
  Display Output
```

### 3.2 MVGAL Interception Architecture

```
Application
    |
    v
+------------------+
|   MVGAL         |  <- Intercept layer (Vulkan layer, LD_PRELOAD)
|   API Intercept  |
+------------------+
    |
    v
+------------------+
|   Workload       |  <- Translate to internal representation
|   Translator     |
+------------------+
    |
    v
+------------------+
|   Workload       |  <- Split across GPUs
|   Splitter       |
+------------------+
    |            
    +--+---------+---------+
       |         |         |
       v         v         v
+------+----+ +--+------+ +----+------+
| GPU 0   | | GPU 1  | | GPU 2   |  <- Individual GPUs
| (AMD)   | | (NV)   | | (Intel) |
+------+----+ +--+------+ +----+------+
       |         |         |
       +---------+---------+
                 |
                 v
          +--------------+
          |   Result     |  <- Synchronize and merge
          |   Merger     |
          +--------------+
                 |
                 v
          Application Result
```

### 3.3 Vulkan Interception Layer

**Vulkan Layer Architecture:**
```
+----------------------------------------+
|           Vulkan Application           |
+----------------------------------------+
                   |
                   v
+----------------------------------------+
|           VK_LAYER_MVGAL              |
|----------------------------------------|
|  vkCreateInstance()                   |
|    -> Detect available GPUs            |
|    -> Setup MVGAL context              |
|                                        |
|  vkCreateDevice()                      |
|    -> Create logical device spanning   |
|       multiple physical devices       |
|                                        |
|  vkCreateSwapchain()                   |
|    -> Distribution strategy decision   |
|    -> Allocate across GPUs             |
|                                        |
|  vkQueueSubmit()                       |
|    -> Split command buffers           |
|    -> Distribute to GPU queues         |
|    -> Synchronize execution            |
|                                        |
|  vkPresent()                           |
|    -> Collect results                  |
|    -> Composite final image            |
|    -> Present to display               |
+----------------------------------------+
                   |
                   v
+----------------------------------------+
|           Physical GPU Drivers          |
|  (Mesa, NVIDIA, Intel drivers)          |
+----------------------------------------+
```

**Vulkan Layer Implementation:**
```json
{
  "VK_LAYER_MVGAL": {
    "description": "Multi-Vendor GPU Aggregation Layer",
    "version": 1,
    "implementations": [
      {
        "library_path": "libVK_LAYER_MVGAL.so",
        "api_version": "1.3.0"
      }
    ],
    "intercept": [
      "vkCreateInstance",
      "vkCreateDevice",
      "vkCreateSwapchainKHR",
      "vkCreateCommandPool",
      "vkAllocateCommandBuffers",
      "vkQueueSubmit",
      "vkPresentKHR"
    ]
  }
}
```

### 3.4 Workload Distribution Strategies

#### 3.4.1 Frame-Level Parallelism (AFR - Alternate Frame Rendering)

```
Time:  |----Frame 0----|----Frame 1----|----Frame 2----|----Frame 3----|
GPU 0: |=======Render=====|                |=======Render=====|             
GPU 1: |                 |=======Render=====|                |=======Render=====
GPU 2: |                 |                |                 |=======Render=====

Pros:
- Simple to implement
- Low synchronization overhead
- Good for latency-tolerant applications

Cons:
- Not all GPUs used every frame
- Potential frame pacing issues
- Micro-stutter possible
```

#### 3.4.2 Tile-Based Rendering (Split Frame Rendering - SFR)

```
Frame N:
+--------+--------+--------+
|  GPU 0 |  GPU 1 |  GPU 2 |
|  Left  | Middle |  Right |
+--------+--------+--------+

Pros:
- All GPUs contribute to each frame
- Better load balancing possible
- Predictable performance

Cons:
- Requires geometry splitting
- Edge artifacts possible without careful handling
- Memory replication overhead
```

**Implementation Options:**
1. **Checkboard:** Alternating pixels (not recommended)
2. **Vertical Split:** Left/right regions
3. **Horizontal Split:** Top/bottom regions
4. **Grid Split:** NxN grid tiles
5. **Depth-based:** Front/back geometry

#### 3.4.3 Task-Based Rendering

```
Frame N:
+------------------+
|   Geometry Pass  | -> GPU 0 (fast at geometry)
+------------------+
|   Shadow Pass    | -> GPU 1 (fast at compute)
+------------------+
|   Lighting Pass  | -> GPU 2 (fast at shading)
+------------------+
|   Post-Process   | -> Any available GPU
+------------------+

Pros:
- Best utilization of GPU strengths
- Flexible scheduling
- Can optimize based on workload

Cons:
- High synchronization overhead
- Complex dependency management
- Requires workload analysis
```

#### 3.4.4 Compute Offloading

```
Graphics Pipeline:
+------------------+     +------------------+
|   Geometry      |     |   Physics        |
|   (GPU 0)       |     |   (GPU 1)        |
+------------------+     +------------------+
         |                       |
         v                       v
+------------------+     +------------------+
|   Rendering      |     |   AI Inference   |
|   (GPU 0)       |     |   (GPU 2)        |
+------------------+     +------------------+
         |                       |
         +-----------------------+
                 |
+------------------+     
|   Composition     |
|   (GPU 0 or CPU) |
+------------------+

Dedication:
- GPU 0: Primary rendering
- GPU 1: Physics, culling, pre-processing
- GPU 2: AI, post-processing, compute
```

### 3.5 Recommended Strategy: Hybrid Approach

**Adaptive Workload Distribution:**

```
1. Analyze incoming workload:
   - Graphics vs Compute ratio
   - Memory requirements
   - Dependency graph
   
2. Select distribution strategy:
   - Pure graphics: Tile-based or AFR
   - Mixed workload: Task-based + offloading
   - Compute-heavy: Dedicated compute GPUs
   
3. Optimize based on:
   - GPU capabilities (profile)
   - Real-time performance data
   - Application hints (if provided)
   - Historical patterns
```

---

## 4. Memory & Data Flow

### 4.1 The Cross-Vendor Memory Challenge

**Problem:** Each vendor's GPU has its own memory space that is not directly accessible by other vendors' GPUs.

```
GPU Memory Spaces:
+------------------+     +------------------+     +------------------+
|   AMD VRAM       |     |   NVIDIA VRAM   |     |   Intel VRAM    |
|  (HBM/GDDR)      |     |  (GDDR/HBM)     |     |  (Embedded/    |
|  8-24GB          |     |  6-24GB         |     |   Discrete)     |
+------------------+     +------------------+     +------------------+
        |                        |                       |
        v                        v                       v
+-----------------------------------------------------+
|                   System Memory (DRAM)                |
|                   16-128GB typical                   |
+-----------------------------------------------------+
```

**Existing Solutions:**

#### 4.1.1 DMA-BUF (Direct Memory Access Buffer)

Linux mechanism for sharing buffers between devices.

```
1. GPU A allocates memory
2. GPU A exports memory as DMA-BUF file descriptor
3. GPU A shares FD with GPU B via Unix domain socket or binder
4. GPU B imports DMA-BUF FD
5. GPU B maps DMA-BUF into its address space
6. GPU B can read (and sometimes write) the memory

Limitations:
- Not all drivers support DMA-BUF
- Performance depends on hardware (PCIe transfers)
- Some GPUs may need explicit synchronization
- Not all memory types can be shared (some require copy)
```

**DMA-BUF Support Matrix:**

| Vendor | Export | Import | Performance | Notes |
|--------|--------|--------|-------------|-------|
| AMD (amdgpu) | ✅ | ✅ | High (PCIe 4.0+) | Full support |
| Intel (i915/xe) | ✅ | ✅ | High | Full support |
| NVIDIA (proprietary) | ✅ | ✅ | Medium-High | Requires compatibility mode |
| NVIDIA (Nouveau) | ⚠️ | ⚠️ | Low | Limited support |
| Moore Threads | TBD | TBD | TBD | To be determined |

#### 4.1.2 PCIe Peer-to-Peer (P2P) Transfers

Direct GPU-to-GPU memory copies without CPU involvement.

```
Requirements:
1. GPUs must be on the same PCIe root complex
2. PCIe switch must support P2P
3. Drivers must support P2P
4. Memory must be page-aligned

Performance:
- PCIe 3.0 x16: ~16 GB/s (bidirectional)
- PCIe 4.0 x16: ~32 GB/s
- PCIe 5.0 x16: ~64 GB/s
- PCIe 6.0 x16: ~128 GB/s (theoretical)
```

**P2P Support Matrix:**

| Vendor | P2P Export | P2P Import | Notes |
|--------|------------|------------|-------|
| AMD | ✅ | ✅ | Requires same root complex |
| NVIDIA | ✅ | ✅ | Best between NVIDIA GPUs |
| Intel | ✅ | ✅ | Limited to Intel GPUs |
| Cross-Vendor | ⚠️ | ⚠️ | May require CPU copy fallback |

#### 4.1.3 Unified Virtual Memory (UVM)

```
NVIDIA UVM:
- Single virtual address space across CPU and GPU
- Page migration between CPU and GPU memory
- Demand paging
- Limited to NVIDIA GPUs

AMD HUMA (Heterogeneous Uniform Memory Access):
- Unified memory for APUs
- Limited cross-GPU support

Intel: Similar concepts in newer drivers
```

### 4.2 MVGAL Memory Architecture

```
+----------------------------------------+
|           Memory Abstraction Layer     |
|----------------------------------------|
|                                        |
|  +------------------+                  |
|  |   Allocator      |                  |
|  |------------------|                  |
|  |  - System RAM    |                  |
|  |  - GPU VRAM      |                  |
|  |  - Shared (DMA-BUF) |               |
|  +------------------+                  |
|                                        |
|  +------------------+                  |
|  |   Memory         |                  |
|  |   Strategy       |                  |
|  |------------------|                  |
|  |  - Replicate     |  <- All GPUs get copy |
|  |  - Share (DMA-BUF)|  <- Shared mapping |
|  |  - Transfer (P2P)|  <- Copy on demand |
|  +------------------+                  |
|                                        |
|  +------------------+                  |
|  |   Synchronization |                  |
|  |------------------|                  |
|  |  - Fences        |                  |
|  |  - Semaphores     |                  |
|  |  - Events         |                  |
|  +------------------+                  |
+----------------------------------------+
```

### 4.3 Memory Strategy Decision Tree

```
Allocate Memory for Workload:
    |
    v
+-------+-------+
| Data Size <    |   > Threshold?
| Threshold      |
+-------+-------+
    |           No
    v           v
+------+  +------------+
|Share |  |Replicate?   |
|via    |  +----+------+
|DMA-BUF|  |Read-|Write-|
|       |  |Only |Heavy |
|       |  +----+------+
+------+     |      |
             v      v
      +-----------+ +----------+
      |  Transfer  | |Replicate |
      |   (P2P)    | | (AllGPUs) |
      +-----------+ +----------+
```

### 4.4 DMA-BUF Benchmark Design

**Purpose:** Measure data transfer latency and bandwidth between GPUs.

**Test Setup:**
```
GPU A (AMD) --PCIe Switch--> GPU B (NVIDIA)
   |                              |
   +--------------PCIe Root Complex------+
```

**Benchmark Tests:**

1. **Write-Read Latency:**
   - GPU A writes 4KB buffer
   - Export to DMA-BUF
   - GPU B imports and reads
   - Measure end-to-end time

2. **Bandwidth Test:**
   - Transfer sizes: 4KB, 64KB, 1MB, 16MB, 256MB
   - GPU A -> GPU B via DMA-BUF
   - Measure throughput (MB/s)

3. **Bidirectional Test:**
   - Simultaneous transfers in both directions
   - Measure aggregate bandwidth

4. **Multi-GPU Test:**
   - One GPU writes, all others read
   - Measure fan-out performance

**Expected Results Table:**

| Source GPU | Dest GPU | Size | Latency (us) | Bandwidth (MB/s) |
|------------|----------|------|--------------|------------------|
| AMD        | NVIDIA   | 4KB  | TBD          | TBD              |
| AMD        | NVIDIA   | 1MB  | TBD          | TBD              |
| AMD        | Intel    | 4KB  | TBD          | TBD              |
| ...        | ...      | ...  | ...          | ...              |

---

## 5. Cross-Vendor Compatibility

### 5.1 Intermediate Representation

**SPIR-V (Standard Portable Intermediate Representation):**
- Open standard by Khronos Group
- Used by Vulkan, OpenCL, SYCL
- Can be generated by:
  - GLSL compiler (glslang)
  - LLVM (via SPIR-V tools)
  - Clang (with -S -emit-spirv)

** Shader Translation Path:**
```
Application Shader (GLSL/HLSL)
    |
    v
+------------------+
|   SPIR-V         |
|   (Cross-vendor) |
+------------------+
    |
    +--------+--------+
    v               v
+--------+    +--------+
| AMD    |    | NVIDIA |
| Shader |    | Shader |
| (ISA)  |    | (PTX)  |
+--------+    +--------+
    |               |
    v               v
+--------+    +--------+
| AMD    |    | NVIDIA |
| GPU    |    | GPU    |
+--------+    +--------+
```

### 5.2 SPIR-V Support Matrix

| Vendor | SPIR-V Ingestion | Shader Compiler | Notes |
|--------|------------------|-----------------|-------|
| AMD | ✅ (RADV, AMDVLK) | ✅ | Full support |
| NVIDIA | ✅ | ✅ | Full support |
| Intel | ✅ (ANV) | ✅ | Full support |
| Moore Threads | Planned | TBD | To be confirmed |

### 5.3 Shader Translation Challenges

| Translation Path | Feasibility | Complexity | Performance |
|-----------------|-------------|------------|-------------|
| SPIR-V -> AMD ISA | ✅ | Low | Native |
| SPIR-V -> NVIDIA PTX | ✅ | Low | Native |
| SPIR-V -> Intel GEN | ✅ | Low | Native |
| AMD ISA -> NVIDIA PTX | ❌ | Very High | N/A |
| NVIDIA PTX -> AMD ISA | ❌ | Very High | N/A |
| GLSL -> SPIR-V | ✅ | Low | Native |
| HLSL -> SPIR-V | ✅ (via glslang) | Medium | Native |

**Conclusion:** Use SPIR-V as the common intermediate representation.

### 5.4 Fallback Mechanisms

**When cross-vendor execute fails:**

1. **Software Fallback:**
   - Execute on CPU (via LLVM, OpenCL CPU device)
   - Use Mesa llvmpipe (software rasterizer)

2. **Single-GPU Fallback:**
   - Execute on most capable single GPU
   - Disable multi-GPU for this workload

3. **Partial Acceleration:**
   - Compute on one GPU, graphics on another
   - Skip unsupported features

4. **Transpile to Supported:**
   - Convert shader to equivalent supported feature
   - May have performance penalty

**Fallback Decision Tree:**
```
Can all GPUs execute workload?
    |
    v
+-------+-------+
| Yes           | No
+-------+-------+
    |           |
    v           v
+------+  +------------+
| Use  |  | Check:     |
| All  |  | Partial    |
| GPUs |  | execute?   |
+------+  +----+-------+
           |    |
           v    v
    +------------+------------+
    | Use CPU   | | Single-GPU    |
    | fallback | | fallback      |
    +------------+------------+
```

---

## 6. References

### 6.1 Kernel Documentation
- Linux DRM Documentation: https://www.kernel.org/doc/html/latest/gpu/drm-kms.html
- Linux DRM Developer Guide: https://drm.dbai.at/
- AMDGPU Driver Documentation: https://www.kernel.org/doc/html/latest/gpu/amdgpu.html
- Intel i915 Driver Documentation: https://www.kernel.org/doc/html/latest/gpu/i915.html

### 6.2 API Documentation
- Vulkan Specification: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/
- OpenCL Specification: https://www.khronos.org/registry/OpenCL/specs/3.0-unified/html/OpenCL_API.html
- SPIR-V Specification: https://www.khronos.org/registry/SPIR-V/specs/unified/1.0/SPIRV.html

### 6.3 Vendor Documentation
- AMD ROCm: https://rocm.docs.amd.com/
- NVIDIA CUDA: https://docs.nvidia.com/cuda/
- Intel oneAPI: https://www.intel.com/content/www/us/en/developer/tools/oneapi/onedev.html
- Mesa: https://docs.mesa3d.org/

### 6.4 Related Projects
- VirtIO-GPU: Virtual GPU for QEMU
- RenderNode: Distributed rendering
- Multi-GPU research papers (various academic sources)

---

**Document Status:** Initial Draft  
**Next Steps:** Prototype GPU detection module implementation  
**Owner:** AxoGM
