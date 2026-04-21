# Research Task 3: Vulkan Explicit Multi-GPU API

This document summarizes the Vulkan constructs that are relevant to Milestone 1 and to the later cross-device memory work. The immediate implementation in this repository is intentionally smaller than the full research target: the code now establishes a real Vulkan layer that can intercept submission. The design choices below explain why that is the correct first foundation.

## Core Model

Khronos positions device groups as Vulkan’s explicit multi-GPU mechanism. The Vulkan Guide describes a device group as multiple physical devices represented as a single logical device. That is the right conceptual model for UMGAL’s internal “logical GPU,” but there is an important limitation: device groups are defined by the implementation. In practice, they are most natural for same-vendor or tightly coupled hardware. That means UMGAL can borrow the device-group programming model without assuming the platform will expose a cross-vendor device group for us.

For UMGAL, the value of the device-group model is the decomposition it implies:

- one logical device view presented upward,
- multiple execution targets downward,
- explicit control over memory placement,
- explicit control over synchronization,
- explicit control over presentation ownership.

That decomposition is exactly what a Vulkan layer needs if it will eventually translate one application-facing device into multiple vendor-facing backends.

## External Memory and Synchronization

The Vulkan external-memory guidance is the bridge from submission interception to actual cross-device cooperation. Khronos documents `VK_KHR_external_memory` as the umbrella for external handle types and documents `VK_KHR_external_memory_fd` as the POSIX file-descriptor path on Linux. This is directly relevant to UMGAL because Linux cross-device sharing will likely route through DMA-BUF-backed file descriptors where supported.

The external synchronization guidance matters just as much. Memory sharing without an explicit fence or semaphore story is not usable. Vulkan’s external fence and semaphore families, plus timeline semaphores in core Vulkan 1.2+, provide the synchronization vocabulary needed to hand work across queues and, later, across devices. Even when the final heterogeneous path has to involve user-space copies or host staging, the synchronization model should still look like Vulkan timeline progression instead of ad hoc sleeps or polling loops.

## Why Milestone 1 Starts With `vkQueueSubmit`

The user specification requires Vulkan first, and that is correct. `vkQueueSubmit` is the narrowest place where useful work begins:

- It sits after application command recording.
- It exposes the submission cadence of real applications.
- It is late enough to capture queueing behavior.
- It is early enough to accumulate telemetry before memory virtualization is implemented.

Interception at `vkQueueSubmit` therefore gives UMGAL a loader-compliant foothold without yet rewriting device enumeration, swapchains, or memory allocation. This is the minimal reliable point to collect workload traces for future AFR, SFR, and compute partitioning experiments.

## Two-GPU Rendering Pipeline Pseudocode

The following pseudocode is not the current code path in the repository. It is the design reference for a future explicit two-device frame pipeline using Vulkan concepts:

```text
create instance
enumerate physical devices
select gpu_a and gpu_b
query device-group compatibility

create logical device abstraction
create queues for graphics/transfer on both devices

for each frame:
  classify frame workload
  choose strategy:
    if frame-level parallel and latency budget allows:
      use AFR
    else if render target can be partitioned:
      use SFR / tiling
    else:
      use single-GPU fallback

  ensure shared resources are resident:
    textures/static buffers -> imported via external memory fd or mirrored
    transient render targets -> allocate per device

  record command buffers:
    cmd_a targets gpu_a work partition
    cmd_b targets gpu_b work partition

  submit cmd_a and cmd_b
  signal timeline semaphore values:
    sem_a = frame_id * 2 + 0
    sem_b = frame_id * 2 + 1

  wait for both semaphore values in compose queue
  compose or copy final image into presentable image
  present on display owner queue
```

The important point is that the logical abstraction is built from explicit per-device work and explicit synchronization. The layer should not hide that complexity from itself. It only hides it from the application.

## Risks

Three risks stand out:

- Device groups are not a cross-vendor guarantee.
- External-memory support is extension- and driver-dependent.
- Submission interception alone cannot infer safe frame partitioning; temporal dependencies and vendor extension mismatches will require conservative heuristics.

Because of those risks, Milestone 1 should gather queue telemetry and validate loader behavior before it attempts logical-device enumeration tricks.

## Sources

- Vulkan specification: https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
- Vulkan Guide, Device Groups: https://github.khronos.org/Vulkan-Site/guide/latest/extensions/device_groups.html
- Vulkan Guide, External Memory and Synchronization: https://github.khronos.org/Vulkan-Site/guide/latest/extensions/external.html
- `VK_KHR_external_semaphore_fd` reference: https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_external_semaphore_fd.html
