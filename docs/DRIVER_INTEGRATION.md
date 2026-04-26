# MVGAL Driver Integration Guide

**Version:** 0.2.0 | **Last Updated:** April 2026

This document describes how MVGAL integrates with each vendor's GPU driver,
which kernel interfaces are used, and known limitations.

---

## AMD (amdgpu)

### Required Driver
- **Kernel driver:** `amdgpu` (in-tree, `drivers/gpu/drm/amd/`)
- **Minimum version:** Linux 5.15 (amdgpu with DMA-BUF peer-to-peer support)
- **Recommended:** Linux 6.1+ for full DMA-BUF heap support

### Kernel Interfaces Used

| Interface | Purpose |
|-----------|---------|
| `/sys/class/drm/card*/device/vendor` | Vendor identification (0x1002) |
| `/sys/class/drm/card*/device/mem_info_vram_total` | VRAM size |
| `/sys/class/drm/card*/device/mem_info_vram_used` | VRAM usage |
| `/sys/class/drm/card*/device/gpu_busy_percent` | GPU utilization |
| `/sys/class/drm/card*/device/hwmon/*/temp1_input` | Temperature (millidegrees) |
| `/sys/class/drm/card*/device/power_dpm_force_performance_level` | DVFS control |
| `/dev/dri/renderD*` | DRM render node for command submission |
| `/dev/kfd` | AMD KFD for compute (ROCm) |
| DMA-BUF via `dma_buf_export` / `dma_buf_attach` | Cross-GPU memory sharing |

### DMA-BUF Integration

AMD GPUs support DMA-BUF export via the `amdgpu_dma_buf_*` functions in the
kernel driver. MVGAL uses the `/dev/dma_heap/system` heap for host-side staging
buffers and the amdgpu DRM render node for GPU-side allocations.

For P2P transfers between two AMD GPUs on the same PCIe root complex, MVGAL
checks `p2pdma_distance()` viability before attempting direct DMA-BUF attachment.

### Reference Repository

- **AMDVLK (historical):** https://github.com/GPUOpen-Drivers/AMDVLK
- **Mesa RADV (current):** https://gitlab.freedesktop.org/mesa/mesa (src/amd/vulkan/)
- **ROCK Kernel Driver:** https://github.com/GPUOpen-Drivers/ROCK-Kernel-Driver

Files studied: `drivers/gpu/drm/amd/amdgpu/amdgpu_dma_buf.c`,
`amdgpu_ttm.c`, `amdgpu_ring.c`, `amdgpu_device.c`.

### Known Limitations

- APU (integrated AMD GPU) shares system RAM; VRAM size reported as 0 or small.
  MVGAL detects this via the `MVGAL_UAPI_GPU_FLAG_INTEGRATED_GUESS` flag.
- `power_dpm_force_performance_level` requires the `video` group or root.
- Some older amdgpu versions do not support DMA-BUF P2P; MVGAL falls back to
  staging buffer copy automatically.

---

## NVIDIA (nvidia / nvidia-drm)

### Required Driver
- **Kernel driver:** `nvidia`, `nvidia-drm` (open-gpu-kernel-modules preferred)
- **Minimum version:** NVIDIA open kernel modules 535+ (R535)
- **Recommended:** Latest open kernel modules release

### Kernel Interfaces Used

| Interface | Purpose |
|-----------|---------|
| `/dev/nvidia0`, `/dev/nvidia1`, … | Per-GPU character devices |
| `/dev/nvidiactl` | Control device |
| `/dev/nvidia-uvm` | Unified Virtual Memory |
| `/sys/class/drm/card*/device/vendor` | Vendor identification (0x10DE) |
| `nvidia-smi` (user space) | Power, temperature, utilization queries |
| NVML library (`libnvidia-ml.so`) | Programmatic metrics access |
| DMA-BUF via `nvidia-drm` | Cross-GPU memory sharing (requires `NVreg_EnableGpuFirmware=1`) |

### Integration Approach

NVIDIA's open kernel module exposes limited inter-driver interfaces. MVGAL
uses a **user-space shim** approach for NVIDIA:

1. **Kernel module component:** PCIe detection, power state via ACPI, DMA-BUF
   import/export via `nvidia-drm`'s `drm_gem_object` interface.
2. **User-space component:** Full command submission via `nvUvm` and `nvOs`
   ioctls on `/dev/nvidia*` character devices.

The `nvidia-drm` module must be loaded (`modprobe nvidia-drm modeset=1`) for
DMA-BUF support. MVGAL checks for this at daemon startup.

### Reference Repository

- **Open GPU Kernel Modules:** https://github.com/nvidia/open-gpu-kernel-modules

Files studied: `kernel-open/nvidia-drm/nv-dmabuf.c`,
`kernel-open/nvidia-drm/nvidia-drm-gem.c`,
`src/nvidia/src/kernel/gpu/mem_mgr/`,
`src/nvidia/interface/nvos.h`.

### Known Limitations

- DMA-BUF P2P between NVIDIA and AMD requires both drivers to support
  `p2pdma` and the GPUs to be on the same PCIe root complex.
- CUDA interception is best-effort; CUDA-exclusive operations that cannot be
  translated to Vulkan compute are routed only to the NVIDIA GPU.
- `nvidia-drm` DMA-BUF support requires `NVreg_EnableGpuFirmware=1` on some
  driver versions.
- Proprietary NVIDIA driver (non-open) has more restricted inter-driver
  interfaces; the open kernel module is strongly preferred.

---

## Intel (i915 / Xe)

### Required Driver
- **Kernel driver:** `i915` (Gen 1–12) or `xe` (Gen 12.5+, Arc GPUs)
- **Minimum version:** Linux 5.15 for i915; Linux 6.2 for xe
- **Recommended:** Linux 6.6+ for xe with full DMA-BUF support

### Kernel Interfaces Used

| Interface | Purpose |
|-----------|---------|
| `/sys/class/drm/card*/device/vendor` | Vendor identification (0x8086) |
| `/sys/class/drm/card*/gt/gt0/rps_cur_freq_mhz` | Current GPU frequency |
| `/sys/class/drm/card*/gt/gt0/rps_min_freq_mhz` | Minimum frequency |
| `/sys/class/drm/card*/gt/gt0/rps_max_freq_mhz` | Maximum frequency |
| `/sys/class/drm/card*/device/hwmon/*/temp1_input` | Temperature |
| `/dev/dri/renderD*` | DRM render node |
| GEM buffer objects via `DRM_IOCTL_I915_GEM_CREATE` | Memory allocation |
| `execbuf2` (`DRM_IOCTL_I915_GEM_EXECBUFFER2`) | Command submission (i915) |
| `xe_exec` (`DRM_IOCTL_XE_EXEC`) | Command submission (xe) |
| DMA-BUF via `i915_gem_prime_export` | Cross-GPU memory sharing |

### Driver Detection

MVGAL detects which Intel driver is active at runtime:

```c
// Check for xe driver
if (access("/sys/module/xe", F_OK) == 0)
    intel_driver = INTEL_DRIVER_XE;
else
    intel_driver = INTEL_DRIVER_I915;
```

### Reference Repositories

- **Intel LTS Kernel:** https://github.com/intel/linux-intel-lts
- **Intel DRM (upstream):** https://gitlab.freedesktop.org/drm/intel
- **Intel Media Driver:** https://github.com/intel/media-driver

Files studied: `drivers/gpu/drm/i915/gem/i915_gem_object.c`,
`drivers/gpu/drm/i915/i915_gem_execbuffer.c`,
`drivers/gpu/drm/xe/xe_bo.c`, `drivers/gpu/drm/xe/xe_exec.c`.

### Known Limitations

- Intel integrated GPUs (iGPU) share system RAM; VRAM is reported as 0.
  MVGAL detects iGPU via `MVGAL_UAPI_GPU_FLAG_INTEGRATED_GUESS`.
- On laptops with Intel iGPU + discrete dGPU, MVGAL uses PRIME render offload:
  the iGPU drives the display, the dGPU renders.
- The `xe` driver API is still evolving; MVGAL checks the kernel version and
  driver module presence before using xe-specific ioctls.
- Arc GPU (discrete) requires `xe` driver on Linux 6.2+.

---

## Moore Threads (mtgpu-drv)

### Required Driver
- **Kernel driver:** `mtgpu` (out-of-tree community driver)
- **Source:** https://github.com/dixyes/mtgpu-drv
- **Status:** Community-maintained; official MTT Linux support is limited.

### Kernel Interfaces Used

| Interface | Purpose |
|-----------|---------|
| `/sys/class/drm/card*/device/vendor` | Vendor identification (0x1ED5) |
| `/dev/dri/renderD*` | DRM render node |
| DRM GEM objects | Memory allocation |
| DMA-BUF (if supported by mtgpu version) | Cross-GPU memory sharing |

### Integration Approach

The MTT integration is **compile-time optional**. If `mtgpu-drv` headers are
not available at build time, MVGAL logs a warning at module load and skips MTT
GPU initialization. The build system checks for MTT headers via:

```cmake
find_path(MTT_INCLUDE_DIR mtgpu_drm.h
    PATHS /usr/include/mtgpu /usr/local/include/mtgpu)
if(MTT_INCLUDE_DIR)
    target_compile_definitions(mvgal_core PRIVATE MVGAL_ENABLE_MTT=1)
endif()
```

### Reference Repository

- **Community mtgpu driver:** https://github.com/dixyes/mtgpu-drv

Files studied: `src/mtgpu_drv.c` (probe function, DRM registration),
`src/mtgpu_gem.c` (GEM buffer management),
`src/mtgpu_submit.c` (command submission path).

### Known Limitations

- Official MTT Linux driver support is limited; behavior may differ from the
  community driver.
- DMA-BUF P2P with MTT GPUs is not guaranteed; MVGAL always falls back to
  staging buffer copy for MTT.
- MTT GPU metrics (temperature, utilization) may not be available via standard
  sysfs paths; MVGAL uses best-effort detection.

---

## Cross-Vendor DMA-BUF Compatibility Matrix

| Source \ Dest | AMD | NVIDIA | Intel | MTT |
|---------------|-----|--------|-------|-----|
| **AMD**       | P2P (same root complex) | Staging | DMA-BUF heap | Staging |
| **NVIDIA**    | Staging | P2P (NVLink if available) | Staging | Staging |
| **Intel**     | DMA-BUF heap | Staging | P2P (same root complex) | Staging |
| **MTT**       | Staging | Staging | Staging | Staging |

**P2P** = Direct peer-to-peer DMA (zero system RAM copy)  
**DMA-BUF heap** = Via `/dev/dma_heap/system` (one system RAM copy)  
**Staging** = Export → mmap → copy → import (two system RAM copies)

---

## Adding a New Vendor

To add support for a new GPU vendor:

1. Add the PCI vendor ID to `src/kernel/mvgal_kernel.c` in `mvgal_vendor_name()`
   and `mvgal_vendor_mask()`.
2. Add vendor detection in `src/userspace/daemon/gpu_manager.c` in the
   `mvgal_gpu_detect_vendor()` function.
3. Add sysfs paths for metrics (temperature, utilization, VRAM) in
   `gpu_manager.c`.
4. Add a vendor-specific ops struct in the kernel module (optional, for
   kernel-level integration).
5. Add compile-time guard: `#ifdef MVGAL_ENABLE_<VENDOR>`.
6. Document the vendor in this file.
7. Add a test case in `src/tests/tests/unit/test_gpu_detection.c`.
