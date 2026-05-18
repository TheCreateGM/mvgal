# Upstream Driver Audit for MVGAL

This document records the current driver-boundary assumptions used by MVGAL. It is intentionally scoped to interfaces that can be used by an above-driver aggregation layer without forking vendor stacks.

## Scope

Audited upstream projects:

- NVIDIA `open-gpu-kernel-modules`: Linux kernel modules, including `nvidia.ko`, `nvidia-drm.ko`, `nvidia-modeset.ko`, and `nvidia-uvm.ko`.
- AMD `AMDVLK`: userspace Vulkan driver stack built from LLVM, XGL, LLPC, GPURT, and PAL.
- Moore Threads `mtgpu-drv`: out-of-tree Linux driver mod with DKMS metadata.
- Intel `media-driver`: VA-API userspace media driver for decode, encode, and video processing.

## Findings

### NVIDIA

The open kernel module source is split between a Linux kernel interface layer and OS-agnostic NVIDIA code. The public tree documents `kernel-open/nvidia`, `kernel-open/nvidia-drm`, `kernel-open/nvidia-modeset`, and `kernel-open/nvidia-uvm`, with GSP firmware and matching userspace driver components required for normal operation.

MVGAL implication: use the NVIDIA stack as an owned native driver and coordinate above it. Do not attempt to re-submit native NVIDIA command buffers from MVGAL kernel code. UVM concepts are useful for residency tracking and fault-driven migration design, but they are not a portable cross-vendor UVM ABI.

### AMD

AMDVLK is a userspace Vulkan driver. Upstream states it is built from LLVM, XGL, LLPC, GPURT, and PAL. Kernel services are provided through the normal Linux AMDGPU/DRM path rather than this repository.

MVGAL implication: Vulkan aggregation must live in an ICD/layer/runtime path that can discover AMD devices through Vulkan/DRM and use DMA-BUF or host staging for data movement. PAL internals are not a stable cross-vendor scheduling interface.

### Moore Threads

The referenced `mtgpu-drv` repository is a small out-of-tree driver mod with DKMS files and is currently marked unmaintained by its maintainer.

MVGAL implication: MTT support must be optional and capability-probed. The MVGAL installer may help install DKMS bits, but runtime behavior must degrade cleanly when MTT driver nodes, DMA-BUF support, or telemetry are unavailable.

### Intel

The Intel media-driver is a VA-API user mode driver for hardware decode, encode, and video post-processing. It supports Intel graphics generations and depends on LibVA/GmmLib plus the relevant i915/xe kernel support. It is not a general Vulkan/OpenGL aggregation interface.

MVGAL implication: Intel media engines should be modeled as specialized capabilities. General graphics/compute aggregation should use Vulkan, OpenCL/SYCL, Level Zero, DRM render nodes, or Mesa/Intel graphics paths, while media-driver integration should target encode/decode offload.

## MVGAL Implementation Status

Implemented in this tree:

- Kernel Phase 1 UAPI: `/dev/mvgal0` exposes version, GPU count, per-GPU PCI topology, caps, stats, enable/disable, rescan, and hotplug generation.
- `mvgal-info`: now queries the MVGAL kernel UAPI first and falls back to `/sys/class/drm` if the module is absent.
- Runtime and tooling scaffolding: scheduler strategies, power manager, Vulkan layer/ICD scaffolding, Steam metadata, packaging, Polkit helper.

Not implemented as production behavior:

- Cross-vendor kernel DMA-BUF export/import ownership.
- Cross-vendor unified GPU virtual memory with coherent page migration.
- Transparent submission of native graphics command buffers across unrelated vendor drivers.
- Universal SLI/CrossFire-equivalent rendering for unmodified applications.

## Required Architecture Boundary

MVGAL should remain an above-driver coordinator:

1. Enumerate GPUs through `/dev/mvgal0`, DRM render nodes, Vulkan physical devices, and vendor runtime APIs.
2. Build a conservative capability intersection for graphics and a capability union for explicit compute/media offload.
3. Prefer DMA-BUF for shareable buffers when both endpoints support it.
4. Use explicit PCIe/host staging copies when DMA-BUF/P2P is unavailable.
5. Keep privileged actions in the Polkit/pkexec helper path; normal tools must run as regular users.
6. Report unsupported features plainly through caps and stats instead of silently emulating unavailable hardware behavior.

## References

- NVIDIA open-gpu-kernel-modules: https://github.com/NVIDIA/open-gpu-kernel-modules
- AMDVLK: https://github.com/GPUOpen-Drivers/AMDVLK
- mtgpu-drv: https://github.com/dixyes/mtgpu-drv
- Intel media-driver: https://github.com/intel/media-driver
