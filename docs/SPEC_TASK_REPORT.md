# MVGAL Full Specification — Alignment and Task Report

**Date:** May 2026  
**Audience:** Architects, distro packagers, and contributors assessing scope versus the “full stack” specification.

---

## Executive summary

The specification describes a multi-year, cross-vendor GPU aggregation platform (kernel DRM shims, HMM-backed unified addressing, heterogeneous Vulkan logical devices, DXVK interception, DLSS/FSR/XeSS fusion, NCCL-aware unified devices, etc.). The **mvgal** tree already contains a layered architecture (kernel module stubs, daemon, Vulkan layer, schedulers, memory helpers, Steam tool metadata, Polkit actions, DKMS installer script, packaging). Components span **research-grade scaffolding** through **working user-space primitives**; several bullet points remain **fundamentally limited by upstream driver boundaries** or require hardware labs to validate honest “single logical GPU” semantics.

This report maps deliverables to the repository, documents design choices, calls out residual gaps honestly, and records changes made during the latest consolidation pass.

---

## Repository layout (high level)

| Area | Location | Role |
|------|-----------|------|
| Kernel | `kernel/` | GPL-2.0 module, vendor hooks (`vendors/`). |
| Daemon | `runtime/daemon/` C++ | `mvgald` orchestration (IPC, registry, scheduler hooks). |
| Userspace core | `src/userspace/` | GPU registry, scheduler strategies, memory, execution engine. |
| Vulkan | `src/userspace/intercept/vulkan/vk_layer.c` | Implicit layer: dispatch chain, **`vkEnumeratePhysicalDeviceGroups`** virtualization. |
| Device-group emulator | `src/userspace/vulkan_icd/device_group.c` | Aggregation of properties/features/heaps for ICD-style paths. |
| Rust safety | `safe/` | Fences, memory tracking, capability model. |
| Polkit | `config/org.freedesktop.policykit.mvgal.policy` | `com.mvgal.*` actions. |
| Privilege helper | `config/mvgal-pkexec-helper.sh` | Single entrypoint for module load, layer install, **MTT installer**. |
| MTT installer | `scripts/mtt-dkms-installer.sh` | GitHub / optional portal download, DKMS, **re-elevates via `pkexec` when non-root**. |
| Steam | `steam/` | Compatibility tool manifests, pacing helpers. |
| Packaging | `packaging/` | deb, rpm, PKGBUILD. |

---

## Security: Polkit and `pkexec`

**Requirement:** Administrative tasks use Polkit-backed `pkexec`, not raw `sudo` in MVGAL tooling.

**Implementation:**

- Policy definitions live in `config/org.freedesktop.policykit.mvgal.policy` (`com.mvgal.driver.load`, `unload`, `vulkan.layer.register`, `power.configure`, `installer.mtt`, plus scheduler/memory helpers).
- `config/mvgal-pkexec-helper.sh` runs only as root after Polkit prompts; it invokes the MTT DKMS installer at `/usr/share/mvgal/scripts/mtt-dkms-installer.sh`.
- Documentation and scripts (e.g. `CONTRIBUTING.md`, `scripts/install_dependencies.sh`) state **pkexec-first** conventions.

**Important distinction:** Installing distro packages (`dnf`, `apt`) remains the user/distro administrator’s workflow; MVGAL’s own privileged operations aim to go through PolicyKit helpers where applicable.

---

## Moore Threads (“loginwall”) installer

**Path:** `scripts/mtt-dkms-installer.sh`

**Behavior:**

- Checks for optional portal authentication; falls back to **public clone/tarball** from `mtgpu-drv` (referenced mirror).
- On non-root invocation, **`exec pkexec`** re-runs itself (no `sudo`).
- DKMS lifecycle: add/build/install, optional `modprobe`, udev and modprobe snippets.

**Honest caveat:** Bypassing arbitrary vendor licence walls automatically is neither reliable nor ethically/legally specifiable here. The script supports credential env vars / root-only config file placeholders; proprietary blobs remain the vendor’s contractual domain.

---

## Multi-vendor Vulkan device groups

Two complementary mechanisms exist:

1. **Implicit layer (`vk_layer.c`)** — `vkEnumeratePhysicalDeviceGroups` exposes **one synthetic group** containing every tracked physical device for the instance (capped by `VK_MAX_DEVICE_GROUP_SIZE`). Suitable for loaders and apps that query groups without going through MVGAL’s ICD.
2. **ICD-style emulator (`vulkan_icd/device_group.c`)** — deterministic virtual UUIDs, aggregated **VkPhysicalDeviceProperties**, **VkPhysicalDeviceFeatures** (feature intersection semantics), aggregated memory heaps, presentation caps scaffolding.

**Build integration:** `device_group.c` is compiled into **`mvgal_vulkan_icd`** (`src/userspace/vulkan_icd/CMakeLists.txt`).

**Concurrency fix (this pass):** `mvgal_device_group_create()` previously **`memset` the entire global struct**, destroying an active `pthread_mutex_t` — undefined behavior on Linux. Reset now **destroys** the mutex, then reallocates/recreates it before repopulating the group.

**Vulkan API alignment:** `VkPhysicalDeviceProperties` only exposes **`pipelineCacheUUID`** at the core level; fictitious **`driverUUID` / `deviceUUID`** assignments were removed. Driver and device identifiers remain available via `mvgal_device_group_get_uuid()` and Vulkan 1.1+ **ID properties** chains when ICD entry points advertise them consistently.

**Limitation:** A true heterogeneous **single logical Vulkan device** with honest peer memory semantics across NVIDIA/AMD/Intel/MTT is not provided by commodity stacks today; MVGAL documents this in research notes (`docs/research/`). Aggregation remains best-effort and policy gated.

---

## Dynamic workload rebalancing engine

**Path:** `src/userspace/scheduler/load_balancer.c`

**Previously:** imbalance detection logged only.

**Now:** When `dynamic_load_balance` is enabled and `max(util) − min(util)` exceeds `load_balance_threshold`, the balancer **adjusts scheduler weights** (`gpu->priority` and `config.gpu_priorities[]`) toward the coolest and hottest GPUs respectively (step-clamped within [10, 100]). This steers **`mvgal_scheduler_calculate_score()`** preferences for **new work**.

**Explicitly out of scope for this tier:** rewriting in-flight Vulkan command buffers, relocating render passes mid-frame, or migrating active CUDA kernels without cooperative support from vendor runtimes.

---

## Memory paths (DMA-BUF, P2P, HMM)

User-space scaffolding (`memory/dmabuf.c`, daemon memory manager): documents three-tier fallback (dma-buf, P2P, bounce). Production **zero-copy cross-vendor** paths depend on kernel + driver negotiated modifiers and topology discovery; reviewers should validate claims against Integration tests on chosen hardware matrices.

---

## Gaming / Steam / Proton / vendor upsampling

Steam compatibility assets and docs exist (`steam/README.md`, `docs/STEAM.md`). **DLSS + FSR + XeSS concurrently** implies multiple proprietary SDKs bound to disparate devices; exposing a fused feature set through one virtual device is not trivially truthful without negotiated capability reporting per physical device.

---

## HPC/MPI/PyTorch/TensorFlow “single logical CUDA device”

Would require NVIDIA’s stack to expose a cooperating UVM surface across non-NVIDIA devices, which CUDA does not support. Practical direction documented in `professional/` is **multi-process / multi-backend** pooling, not a literal single `/dev/nvidia0` abstraction.

---

## Testing approach recommended

| Layer | Tests |
|--------|-------|
| Rust | `cargo test` (`safe/` crates). |
| C unit/integration | CMake `src/tests/` targets. |
| Vulkan | Vulkaninfo with `VK_LAYER_MVGAL` after enabling env per manifest. |
| Polkit | `pkaction \| grep com.mvgal`. |
| MTT installer | Dry-run parse on CI image without hardware; full DKMS test on Moore Threads SKU. |

---

## Changes in latest implementation pass

1. **`device_group.c`**: safe mutex teardown/recreate across group rebuild (`mvgal_device_group_create`); MVGAL error enums and logging calls aligned with **`mvgal_types.h`** / **`mvgal_log.h`**; **`pipelineCacheUUID`**-only path for aggregated `VkPhysicalDeviceProperties`.
2. **`vulkan_icd/CMakeLists.txt`**: **`device_group.c`** added to ICD sources so the emulator is compiled and linked.
3. **`load_balancer.c`**: priority-based steering when imbalance crosses threshold (scheduler-tier “dynamic rebalancing engine”).

Refer to git history after commit for precise diffs.

---

## Bottom line for stakeholders

Treat MVGAL as an **integrated framework and experimentation platform**: strong fit for interception, scheduling policy, daemon lifecycle, packaging, Polkit-aligned ops, documentation, and **honest disclaimers**. Treat **kernel-level heterogeneous command routing and vendor-faithful Vulkan memory unification** as **ongoing systems research**, not solved by headers alone — even when the codebase structure presents them as stubs or aggregators ready for iterative hardening.
