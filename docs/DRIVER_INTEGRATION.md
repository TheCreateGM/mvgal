# MVGAL Driver Integration Guide

This document describes how MVGAL integrates with each GPU vendor's driver
stack, including the specific integration method, limitations, and workarounds.

---

## AMD (amdgpu)

### Supported Architectures
- RDNA 1 (Navi 10/12/14): RX 5000 series
- RDNA 2 (Navi 21/22/23/24): RX 6000 series
- RDNA 3 (Navi 31/32/33): RX 7000 series
- GCN (Vega, Polaris): RX 400/500/Vega series
- AMD APU (integrated Vega, RDNA): Ryzen iGPU

### Integration Method

AMD GPUs use the open-source `amdgpu` kernel driver.  MVGAL integrates at
three levels:

1. **Kernel level** (`kernel/vendors/mvgal_amd.c`): Uses DRM GEM objects and
   the amdgpu command submission interface (`DRM_IOCTL_AMDGPU_CS`).
2. **User-space level** (`src/userspace/daemon/gpu_manager.c`): Reads GPU
   metrics from sysfs (`/sys/class/drm/cardN/device/`).
3. **Vulkan level**: AMDVLK or RADV (Mesa) ICD; MVGAL layer sits above.

### DMA-BUF Support
AMD GPUs fully support DMA-BUF export/import via the `amdgpu` driver.
Zero-copy cross-GPU transfers are possible between two AMD GPUs.

### sysfs Metrics
| Path | Metric |
|------|--------|
| `device/gpu_busy_percent` | GPU utilization (0–100%) |
| `device/mem_info_vram_total` | Total VRAM in bytes |
| `device/mem_info_vram_used` | Used VRAM in bytes |
| `device/hwmon/hwmonN/temp1_input` | Temperature in millidegrees C |
| `device/hwmon/hwmonN/power1_average` | Power draw in microwatts |

### APU Special Handling
AMD APUs share system RAM as VRAM.  MVGAL detects this by checking if
`mem_info_vram_total` equals the system RAM size.  In this case, DMA-BUF
transfers to/from the APU are zero-copy (same physical memory).

---

## NVIDIA (nvidia / nvidia-open)

### Supported Architectures
- Turing (RTX 20xx, GTX 16xx): SM 7.5
- Ampere (RTX 30xx, A-series): SM 8.0/8.6
- Ada Lovelace (RTX 40xx): SM 8.9
- Pascal (GTX 10xx): SM 6.x (proprietary driver only)
- Maxwell (GTX 9xx): SM 5.x (proprietary driver only)

### Integration Method

NVIDIA GPUs use either the open kernel modules (`nvidia-open`) or the
proprietary driver.  MVGAL uses a user-space bridge approach:

1. **Open kernel modules** (`kernel/vendors/mvgal_nvidia.c`): Uses the
   `nvidia-drm` DRM interface for command submission and DMA-BUF.
2. **Proprietary driver**: User-space shim via ioctl to `/dev/nvidiactl` and
   `/dev/nvidia0`.
3. **CUDA shim** (`src/userspace/intercept/cuda/cuda_wrapper.c`): Intercepts
   CUDA API calls via LD_PRELOAD.

### DMA-BUF Support
NVIDIA open kernel modules (525+) support DMA-BUF export via
`NV_DRM_IOCTL_GEM_EXPORT_DMABUF_FD`.  The proprietary driver requires
`nvidia-drm.modeset=1` kernel parameter.

### sysfs Metrics
NVIDIA does not expose metrics via standard sysfs.  MVGAL uses:
- `nvidia-smi` (if available) for temperature and power
- `/proc/driver/nvidia/gpus/*/information` for basic info
- NVML library (if available) for detailed metrics

### Limitations
- Kernel-level anti-cheat (EAC, BattlEye) may block MVGAL's Vulkan layer.
- CUDA P2P transfers require both GPUs to be NVIDIA.
- NVLink is not supported in the current implementation.

---

## Intel (i915 / xe)

### Supported Architectures
- Gen 9 (Skylake, Kaby Lake): HD/UHD 600 series
- Gen 11 (Ice Lake): Iris Plus
- Gen 12 (Tiger Lake, Alder Lake): Iris Xe, UHD 700 series
- Xe (Arc A-series): Arc A380, A580, A750, A770

### Integration Method

Intel GPUs use the `i915` (Gen 9–12) or `xe` (Xe/Arc) kernel driver.

1. **Kernel level** (`kernel/vendors/mvgal_intel.c`): Uses DRM GEM objects
   and the `i915_gem_execbuffer2` / `xe_exec` submission interface.
2. **User-space level**: Reads metrics from sysfs and `intel_gpu_top`.
3. **Vulkan level**: Intel ANV (Mesa) or Intel Arc Vulkan driver.

### DMA-BUF Support
Intel GPUs fully support DMA-BUF.  The `i915` driver exports GEM objects as
DMA-BUF file descriptors.

### sysfs Metrics
| Path | Metric |
|------|--------|
| `device/drm/cardN/gt/gt0/rps_cur_freq_mhz` | Current clock (MHz) |
| `device/hwmon/hwmonN/temp1_input` | Temperature |
| `device/hwmon/hwmonN/power1_average` | Power draw |

### Integrated GPU Handling
Intel iGPUs share system RAM.  MVGAL treats them similarly to AMD APUs —
DMA-BUF transfers are zero-copy when the iGPU and CPU share the same memory.

---

## Moore Threads (mtgpu-drv)

### Supported Architectures
- MTT S60: Entry-level discrete GPU
- MTT S80: Mid-range discrete GPU
- MTT S2000: Workstation GPU

### Integration Method

Moore Threads GPUs use the `mtgpu-drv` kernel driver.

1. **Kernel level** (`kernel/vendors/mvgal_mtt.c`): Uses the mtgpu DRM
   interface for command submission.
2. **User-space level**: Reads metrics from sysfs where available.
3. **Vulkan level**: Moore Threads Vulkan ICD.

### DMA-BUF Support
Moore Threads GPUs support DMA-BUF export/import via the mtgpu driver.
Cross-vendor DMA-BUF with AMD/NVIDIA/Intel requires kernel 6.2+ with
`dma_buf_map_attachment` support.

### Limitations
- CUDA is not supported on Moore Threads GPUs.
- OpenCL support is limited to OpenCL 1.2.
- Some Vulkan extensions may not be available.

---

## Cross-Vendor Synchronisation

### DMA-BUF Fences
Linux DMA-BUF fences (`sync_file`) provide cross-driver synchronisation.
MVGAL uses `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` and `DMA_BUF_IOCTL_IMPORT_SYNC_FILE`
to synchronise between vendor drivers.

### Vulkan Timeline Semaphores
For Vulkan workloads, MVGAL uses `VkSemaphoreTypeTimeline` semaphores to
coordinate frame ordering across GPUs.

### PCIe P2P
Direct PCIe peer-to-peer transfers between GPUs require:
- Both GPUs on the same PCIe root complex
- Kernel 5.10+ with `pci_p2pdma` support
- Both drivers supporting P2P DMA

MVGAL falls back to host-RAM staging when P2P is not available.

---

## PCIe Bandwidth Measurements

MVGAL measures PCIe bandwidth at startup for each GPU pair:

```
GPU 0 (AMD) ↔ GPU 1 (NVIDIA): ~12 GB/s (PCIe 4.0 x16)
GPU 0 (AMD) ↔ GPU 2 (Intel):  ~6 GB/s  (PCIe 3.0 x8)
```

These measurements inform the scheduler's decision to avoid unnecessary
cross-GPU transfers.

---

## Vendor-Specific Vulkan Extensions

| Extension | AMD | NVIDIA | Intel | MTT |
|-----------|-----|--------|-------|-----|
| `VK_KHR_device_group` | ✓ | ✓ | ✓ | ✓ |
| `VK_KHR_external_memory_fd` | ✓ | ✓ | ✓ | ✓ |
| `VK_EXT_external_memory_dma_buf` | ✓ | ✓ | ✓ | ✓ |
| `VK_KHR_timeline_semaphore` | ✓ | ✓ | ✓ | ✓ |
| `VK_AMD_shader_core_properties` | ✓ | ✗ | ✗ | ✗ |
| `VK_NV_device_diagnostic_checkpoints` | ✗ | ✓ | ✗ | ✗ |
| `VK_INTEL_performance_query` | ✗ | ✗ | ✓ | ✗ |

MVGAL's Vulkan layer reports the **intersection** of extensions by default,
ensuring all applications work correctly.  Applications can opt into the
**union** by setting `MVGAL_VK_EXTENSION_MODE=union`.
