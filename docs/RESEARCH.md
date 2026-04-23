# MVGAL Research

This document records the Phase 1 research pass for MVGAL and the design decisions that fall out of actual upstream source, not aspirational architecture notes.

## Scope of this pass

- audit the current repository and treat it as a prototype scaffold,
- study the four upstream vendor projects named in the task,
- define a minimal ioctl/UAPI for `/dev/mvgal0`,
- implement only the trustworthy Phase 2 kernel skeleton: PCI enumeration, capability query, topology tracking, and hotplug-aware rescan.

This pass does **not** claim transparent logical-GPU aggregation, cross-vendor command submission, unified VRAM, or production Vulkan device virtualization.

## Repository reality check

The local tree contains substantial placeholder code and status documents that overstate implementation maturity. The credible baseline in this repository is still:

- a prototype userspace stack,
- a loader-compliant Vulkan layer foundation,
- incomplete kernel/userspace integration.

The kernel module prior to this pass was not a safe basis for MVGAL. It:

- scanned `/dev/dri/card*` from kernel space,
- inferred vendors heuristically,
- hard-coded fake VRAM sizes,
- exposed internal kernel structs directly through ioctls.

That has been replaced in this pass by a shared UAPI and a PCI-backed enumeration path.

## Upstream repositories studied

### NVIDIA open-gpu-kernel-modules

- Local clone: `/tmp/mvgal-upstream/nvidia-open`
- Commit: `db0c4e65c8e34c678d745ddb1317f53f90d1072b`
- Date: `2026-03-24`
- Release: `595.58.03`

Key source anchors:

- `README.md`
- `src/nvidia/arch/nvalloc/unix/include/nv-ioctl-numbers.h`
- `src/nvidia/arch/nvalloc/unix/include/nv-ioctl.h`
- `kernel-open/nvidia-drm/nvidia-drm-gem-dma-buf.c`

Findings:

1. The open kernel modules are still tied to a matching NVIDIA user-mode release.
   - `README.md` states the kernel modules must be used with the corresponding user-space driver release and GSP firmware.
   - This means MVGAL cannot treat the NVIDIA kernel repo as an independent, fully open stack.

2. NVIDIA exposes private RM ioctl surfaces in addition to DRM integration.
   - `nv-ioctl-numbers.h` defines `NV_ESC_REGISTER_FD`, `NV_ESC_IOCTL_XFER_CMD`, and `NV_ESC_EXPORT_TO_DMABUF_FD`.
   - `nv-ioctl.h` defines `nv_ioctl_export_to_dma_buf_fd_t`.
   - This is a strong signal that NVIDIA integration for MVGAL will need a user-space shim path, not only a generic DRM hook.

3. DMA-BUF interop exists, but through NVIDIA-owned paths.
   - `nvidia-drm-gem-dma-buf.c` implements PRIME import/export plumbing and memory export through NVKMS-backed helpers.
   - That is useful for explicit sharing, but it is not equivalent to a generic cross-vendor submission surface.

Implication for MVGAL:

- Do not try to hook NVIDIA submission in a generic kernel meta-driver.
- Use standard dma-buf/sync fd interop where available.
- Plan for NVIDIA user-space interception around Vulkan/CUDA/OpenGL entry points and private RM/NVKMS behavior.

### AMDVLK and XGL

- Local clone: `/tmp/mvgal-upstream/amdvlk`
- AMDVLK commit: `0f7b1d76a13bb5e299856a38eecbe15167cd30ca`
- Date: `2025-09-15`
- Local clone: `/tmp/mvgal-upstream/xgl`
- XGL commit: `e9782eb33ce5e5e4ed2e339542a28c1b933624b4`
- Date: `2025-04-29`

Key source anchors:

- `AMDVLK/README.md`
- `AMDVLK/default.xml`
- `xgl/README.md`
- `xgl/icd/api/vk_instance.cpp`
- `xgl/icd/api/vk_memory.cpp`
- `xgl/icd/api/vk_device.cpp`
- `xgl/icd/api/vk_semaphore.cpp`
- `xgl/icd/api/vk_physical_device.cpp`

Findings:

1. The top-level AMDVLK repository is a manifest, not the whole driver.
   - `default.xml` pulls `xgl`, `pal`, `llpc`, `gpurt`, and `llvm-project`.
   - The Linux Vulkan ICD implementation lives in `xgl`.

2. AMD already exposes Vulkan device-group support inside its ICD.
   - `vk_instance.cpp` implements `Instance::EnumeratePhysicalDeviceGroups`.
   - That path groups PAL devices known to the same AMD stack.
   - This is useful for same-vendor multi-GPU, not heterogeneous aggregation.

3. External memory and semaphore fd support are explicit ICD features.
   - `vk_memory.cpp` handles `VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR`.
   - `vkGetMemoryFdKHR` and `vkGetMemoryFdPropertiesKHR` support `OPAQUE_FD` and `DMA_BUF`.
   - `vk_semaphore.cpp` and `vk_device.cpp` support timeline semaphores and fd import/export.

4. There is a hard limitation around split-instance peer binding.
   - `vk_physical_device.cpp` rejects `VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT` with a comment that single images bound to multiple peer allocations are not implemented due to page table support issues.

5. Peer memory features depend on vendor-specific multi-GPU fabric support.
   - `vk_device.cpp` exposes richer peer features only when `GetMultiGpuCompatibility` reports peer transfer read support.
   - The code comments call out XGMI as the performant case.

Implication for MVGAL:

- AMD’s ICD is a strong reference for same-vendor explicit multi-GPU primitives.
- It is not evidence that heterogeneous logical-device aggregation can be done below the ICD.
- MVGAL should sit above vendor ICDs and use external-memory/external-semaphore fd primitives as a cross-vendor coordination layer.

### Intel media-driver

- Local clone: `/tmp/mvgal-upstream/intel-media-driver`
- Commit: `838be2418698c79b46175960f4c687eabd37e554`
- Date: `2026-04-22`
- Release bump in tree: `26.2.0`

Key source anchors:

- `README.md`
- `media_driver/linux/common/ddi/media_libva.cpp`
- `media_driver/linux/common/ddi/media_libva_interface.cpp`
- `media_driver/linux/common/ddi/media_libva_caps.cpp`

Findings:

1. This repository is a VA-API media driver, not Intel’s general Vulkan/compute driver.
   - `README.md` describes it as the Intel Media Driver for VAAPI.
   - It targets decode, encode, and video post-processing.

2. It does expose dma-buf based surface export/import paths.
   - `media_libva.cpp` implements `DdiMedia_ExportSurfaceHandle`.
   - That function accepts `VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2` and `DRM_PRIME_3`.
   - It calls `mos_bo_export_to_prime(...)` and returns DRM PRIME file descriptors.

3. Capability tables advertise DRM PRIME memory types broadly.
   - `media_libva_caps*.cpp` files expose `VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_*`.

Implication for MVGAL:

- Intel media-driver is relevant for video surface sharing and media workloads.
- It is **not** sufficient as the primary Intel backend for graphics or general compute.
- For graphics/compute aggregation MVGAL must target Intel’s DRM render nodes and Vulkan/Level Zero/OpenCL stacks; media-driver remains a useful side path for VA-API workloads.

### Moore Threads / MTT

- Local clone: `/tmp/mvgal-upstream/mtgpu-drv`
- Commit: `9647512612dd60ca50c5b0d543a005a34eef170a`
- Date: `2026-01-16`

Key source anchors:

- `README.md`
- `inc/mtgpu-next/uapi/mtgpu_drm.h`
- `src/mtgpu/mtgpu_drm_gem.c`
- `src/common/os-interface-drm.c`
- `src/pvr/pvr_drm.c`
- `inc/mtgpu/mtgpu_defs.h`

Findings:

1. The public mirror is explicitly unmaintained.
   - `README.md` marks the project unmaintained.
   - Any integration against undocumented behavior must be treated as fragile.

2. The driver exposes a real DRM-style ioctl surface.
   - `mtgpu_drm.h` defines `DRM_MTGPU_QUERY_INFO`, `DRM_MTGPU_JOB_SUBMIT`, timeline, fence, semaphore, DMA transfer, and object-management ioctls.

3. The driver has dma-buf and PRIME interop paths.
   - `mtgpu_drm_gem.c` defines `dma_buf_ops` and uses `drm_gem_dmabuf_export`.
   - `os-interface-drm.c` wraps `drm_gem_prime_import`.

4. Sync fd style primitives are present.
   - `pvr_drm.c` registers `MTGPU_FENCE_TO_FD` and `MTGPU_SEMAPHORE_TO_FD`.

5. The Moore Threads PCI vendor ID in this driver is `0x1ED5`.
   - `inc/mtgpu/mtgpu_defs.h` defines `PCI_VENDOR_ID_MT (0x1ED5)`.
   - The local MVGAL tree previously used `0x1EAC`, which was wrong and is corrected in this pass.

Implication for MVGAL:

- MTT is closer to AMD and Intel than NVIDIA in terms of DRM-style integration surfaces.
- The unmaintained status means MVGAL should prefer stable DRM/dma-buf/syncfd interfaces and avoid undocumented bridge ioctls unless absolutely necessary.

## Cross-vendor conclusions

### 1. The kernel module must start as topology and policy, not command virtualization

None of the upstream trees provide a safe, vendor-neutral kernel submission API that MVGAL can hook uniformly. The correct Phase 2 kernel scope is:

- enumerate candidate GPUs,
- track topology changes,
- expose a minimal query interface,
- leave scheduling, API interception, and cross-vendor composition in user space.

### 2. Cross-vendor memory sharing must use the lowest common explicit primitives

The realistic common denominator is:

- dma-buf or opaque fd backed external memory,
- sync fd or external semaphore fd,
- host-memory staging when peer access is absent.

Anything stronger than that becomes vendor-pair specific.

### 3. Same-vendor device groups are not evidence for heterogeneous logical devices

AMD’s device-group support is useful, but it depends on the vendor stack owning the whole memory and peer model. MVGAL should not advertise a synthetic heterogeneous Vulkan physical device until it can honestly emulate:

- memory type enumeration,
- queue family behavior,
- external synchronization,
- residency and failure semantics.

### 4. Intel media-driver is a side integration, not the core Intel render backend

The media-driver is useful for VA-API and DMA-BUF export, but the main MVGAL render/compute plan for Intel still has to sit on top of i915/xe plus ANV/Level Zero/OpenCL user-space components.

## Chosen design for this phase

### Shared ioctl/UAPI

Implemented in `include/mvgal/mvgal_uapi.h`.

Implemented ioctls:

- `MVGAL_IOC_QUERY_VERSION`
- `MVGAL_IOC_GET_GPU_COUNT`
- `MVGAL_IOC_GET_GPU_INFO`
- `MVGAL_IOC_ENABLE`
- `MVGAL_IOC_DISABLE`
- `MVGAL_IOC_GET_STATS`
- `MVGAL_IOC_GET_CAPS`
- `MVGAL_IOC_RESCAN`

Defined but intentionally not implemented yet:

- `MVGAL_IOC_EXPORT_DMABUF`
- `MVGAL_IOC_IMPORT_DMABUF`
- `MVGAL_IOC_ALLOC_CROSS_VENDOR`
- `MVGAL_IOC_FREE_CROSS_VENDOR`

The current kernel module returns `-EOPNOTSUPP` for the future memory-sharing commands. This keeps the ABI direction visible without pretending the path works.

### Kernel module scope

Implemented in `src/kernel/mvgal_kernel.c`.

The kernel module now:

- enumerates PCI display/3D controllers instead of opening `/dev/dri/*` from kernel space,
- records vendor/device/subsystem IDs, BDF, bound driver name, NUMA node, PCIe link state, and BAR visibility windows,
- rescans on PCI bus add/remove/bind/unbind notifications,
- exposes a topology generation counter,
- tracks only read-only skeleton stats.

Not implemented in kernel:

- DRM render-node virtualization,
- command submission,
- dma-buf export/import mediation,
- VRAM accounting,
- unified scheduling.

## Risks and unresolved questions

1. NVIDIA remains the hardest backend.
   - The open kernel tree helps with topology and dma-buf awareness, but real submission remains tied to NVIDIA-owned user-mode behavior.

2. Vendor-neutral VRAM reporting is not trustworthy without vendor hooks.
   - The current kernel module reports BAR visibility windows, not total VRAM.

3. Hot-unplug safety for active workloads is not solved.
   - This pass only rescans topology.
   - Graceful drain/migration belongs to the runtime daemon once real workload tracking exists.

4. Vulkan extension intersection is still future work.
   - Before any logical-device emulation, MVGAL must measure the real extension intersection across:
     - NVIDIA Vulkan,
     - AMD XGL/RADV or AMDVLK deployment target,
     - Intel ANV,
     - MTT Vulkan stack if present.

## Immediate next steps

1. Add a small userspace probe tool that exercises the new UAPI and prints topology.
2. Extend the Vulkan layer from submission logging to explicit physical-device mediation.
3. Prototype one concrete cross-vendor memory-sharing pair using:
   - dma-buf fd export/import,
   - semaphore fd import/export,
   - host fallback.
4. Replace remaining optimistic docs with milestone-based status documents.
