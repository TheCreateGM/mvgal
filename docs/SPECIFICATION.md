# MVGAL Specification Traceability

**Project:** Multi-Vendor GPU Aggregation Layer for Linux  
**Version:** 0.2.2  
**Status:** Research prototype with production-oriented packaging

This document maps the project specification to implementation locations, build artifacts, and known limitations.

---

## 1. Multi-Vendor GPU Support

| Requirement | Implementation | Notes |
|-------------|----------------|-------|
| AMD GPUs | `kernel/vendors/mvgal_amd.c`, `gpu_manager.c` PCI `0x1002` | amdgpu DRM + sysfs metrics |
| NVIDIA GPUs | `kernel/vendors/mvgal_nvidia.c`, CUDA shim | nvidia-open / proprietary bridge |
| Intel GPUs | `kernel/vendors/mvgal_intel.c` | i915 / xe |
| Moore Threads | `kernel/vendors/mvgal_mtt.c`, `scripts/mtt-dkms-installer.sh` | mtgpu-drv DKMS installer |
| Any architecture | Capability model in `safe/capability_model/`, Vulkan ICD | Intersection/union of API features |

---

## 2. Unified Workload Distribution

| Component | Path |
|-----------|------|
| Scheduler (7 strategies) | `src/userspace/scheduler/` |
| Execution engine | `src/userspace/execution/execution.c` |
| Vulkan device group | `src/userspace/vulkan_icd/device_group.c` |
| Vulkan implicit layer | `src/userspace/intercept/vulkan/` |
| Load balancer | `src/userspace/scheduler/load_balancer.c` |

Strategies: round-robin, AFR, SFR, task, compute-offload, hybrid, single-GPU (`config/mvgal.conf`).

---

## 3. Driver Architecture

```
Application → API interception → mvgald → ioctl/UAPI → mvgal.ko → vendor drivers
```

| Layer | Artifact | Source |
|-------|----------|--------|
| Kernel (Phase 1) | `mvgal.ko` | `kernel/mvgal_main.c` |
| Kernel (full stack, optional) | + vendor shims | `MVGAL_BUILD_FULL_STACK=1` in `kernel/Kbuild` |
| Daemon | `mvgald` | `runtime/daemon/` |
| Library | `libmvgal.so`, `libmvgal_core.a` | `src/userspace/` |

Reference research: `docs/REFERENCE_RESEARCH.md`, `docs/DRIVER_INTEGRATION.md`, `docs/UPSTREAM_DRIVER_AUDIT.md`.

---

## 4. Initialization Flow

1. **Kernel:** PCI display-class scan → sysfs `/sys/class/mvgal/mvgal0/` → `/dev/mvgal0` UAPI (`kernel/mvgal_main.c`).
2. **Daemon:** `mvgald` reads config, opens UAPI, enumerates DRM nodes (`runtime/daemon/device_registry.cpp`).
3. **Userspace:** `mvgal-info` and `mvgal-probe` query `/dev/mvgal0` first; tools fall back to `/sys/class/drm` when the MVGAL kernel module is absent.
4. **Runtime:** `gpu_manager.c` normalizes sysfs/PCI into `mvgal_gpu_descriptor_t`, builds logical devices.

```bash
pkexec systemctl start mvgald
mvgal-info
```

---

## 5. Rendering / Workload Flow

| API | Interception | Distribution |
|-----|--------------|--------------|
| Vulkan | `VK_LAYER_MVGAL`, ICD | Device group + command-buffer rewrite |
| OpenCL | ICD loader | Scheduler dispatch |
| CUDA | `libmvgal_cuda.so` | Compute-offload strategy |
| D3D (Wine) | `d3d_wrapper.c` | DXVK/VKD3D path |
| OpenGL | `libmvgal_gl.so` | LD_PRELOAD |

Steam integration: `tools/mvgal-steam-setup.c`, `steam/mvgal_frame_pacer.c`.

---

## 6. Memory & Data Flow

| Mechanism | Location |
|-----------|----------|
| DMA-BUF | `kernel/mvgal_memory.c`, vendor `export_dmabuf` ops |
| UVA space | `mvgal_uva_init()` in `mvgal_memory.c` |
| P2P matrix | `mvgal_detect_all_p2p()` in `mvgal_device.c` |
| Rust safety | `safe/memory_safety/`, `safe/fence_manager/` |

Cross-vendor zero-copy is best-effort; PCIe P2P requires matching vendor pairs.

---

## 7. Optimization Features

| Feature | Location |
|---------|----------|
| Idle GPU parking | `runtime/daemon/power_manager.cpp` |
| Power curves | `PowerManager::applyPowerCurves()`, `[power]` in `config/mvgal.conf` |
| PSU headroom | `PowerManager::updatePsuHeadroom()` |
| DVFS / thermal | Kernel `mvgal_power.c`, daemon `power_manager.cpp` |
| Gaming | AFR/SFR strategies, Proton compat in `compat/windows/` |

Privileged operations use **pkexec** + Polkit (`config/mvgal-pkexec-helper.sh`, `com.mvgal.*` actions).

---

## 8. Languages & Frameworks

| Language | Bindings / usage |
|----------|------------------|
| C / C++ | Core (`src/userspace/`, `kernel/`, `runtime/`) |
| Rust | `safe/`, `runtime/Cargo.toml` (fence, memory, capability) |
| Go | `tools/mvgal_exporter.go`, `ui/mvgal_rest_server.go` |
| Python, Java, D, etc. | `bindings/` |

Build: CMake (primary), optional Meson/Nix (`packaging/nix/`), COPR RPM/DEB.

---

## 9. System Compatibility

| Distro support | Mechanism |
|----------------|-----------|
| Fedora/RHEL | COPR `axogm/mvgal` |
| Debian/Ubuntu | `src/pkg/debian/` |
| Arch | `packaging/arch/` PKGBUILD |
| Generic | `scripts/install_dependencies.sh`, DKMS |

Laptop/iGPU: PRIME detection in `config/mvgal.conf` `[dri] enable_prime`.

---

## 10. Windows Compatibility (Optional)

| Component | Path |
|-----------|------|
| NTSYNC / futex bridge | `compat/windows/mvgal_ntsync.c` |
| D3D interception | `src/userspace/intercept/d3d/d3d_wrapper.c` |
| WoW64 | Wine/Proton stack (not forked; integrated via layer) |

---

## 11. Deliverables Checklist

| Deliverable | Status |
|-------------|--------|
| Linux kernel module | ✅ Phase 1 (`mvgal_main.c`); experimental stack optional |
| Userspace library + daemon | ✅ Builds on Fedora 44 |
| Packaging (DEB/RPM/COPR/Nix) | ✅ |
| Architecture documentation | ✅ `docs/ARCHITECTURE.md`, this file |
| Multi-vendor aggregation | ⚠️ Prototype — per-app validation required |
| Transparent SLI/CrossFire equivalent | ⚠️ AFR/SFR for supported Vulkan paths only |

---

## 12. Build Commands

```bash
# Userspace + daemon + tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build

# Kernel module (Phase 1 — default)
cmake -S . -B build-kernel -DMVGAL_BUILD_KERNEL=ON \
  -DMVGAL_BUILD_RUNTIME=OFF -DMVGAL_BUILD_API=OFF
cmake --build build-kernel

# Full aggregation stack (experimental — link incomplete; vendor/DMA-BUF stubs)
# make -C /lib/modules/$(uname -r)/build M=$PWD/kernel MVGAL_BUILD_FULL_STACK=1 modules
```

---

## 13. Known Limitations

1. **Not a replacement for vendor drivers** — MVGAL coordinates above amdgpu/nvidia/i915/mtgpu.
2. **Graphics aggregation** requires application support for multi-GPU Vulkan or MVGAL interception.
3. **Anti-cheat** may block Vulkan layers.
4. **NVLink / SLI hardware** is not emulated; scheduling is software-based.
5. **Kernel DRM registration** is disabled by default; use Phase 1 char device UAPI.

---

## 14. References

- NVIDIA open-gpu-kernel-modules  
- AMDVLK / ROCK-Kernel-Driver  
- intel/media-driver  
- dixyes/mtgpu-drv  
- NCCL, MPI, PyTorch Distributed (scheduling inspiration)

See `docs/REFERENCE_RESEARCH.md` for detailed analysis.
