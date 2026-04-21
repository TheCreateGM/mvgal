# Research Task 1: Existing Multi-GPU Framework Survey

This document covers the research prerequisite for the first implementation slice of UMGAL/MVGAL. The immediate goal is not to claim that the project already solves heterogeneous device aggregation. The goal is to understand why previous multi-GPU systems succeeded only in narrow scenarios and why a Vulkan-layer-first milestone is the least risky place to start.

## Summary

Older consumer multi-GPU systems such as NVIDIA SLI and AMD CrossFire were built around driver-managed rendering heuristics. They worked best when the driver vendor controlled the full stack: the GPUs were usually homogeneous, the rendering mode was mostly alternate-frame rendering (AFR), and game-specific profiles were required to avoid corruption or negative scaling. Both vendors later shifted responsibility toward explicit application-controlled multi-GPU in low-level APIs. NVIDIA states that with DirectX 12 and Vulkan-era engines, native game integration is the preferred model and that no new SLI driver profiles would be added for RTX 20 series and earlier GPUs starting January 1, 2021. AMD’s CrossFire support article makes the same architectural point from the other side: in DirectX 12 and Vulkan applications, multi-GPU is “exclusively handled by the application.” Those statements matter because they explain why a transparent cross-vendor driver-profile solution is no longer credible as the main design.

Vulkan device groups are the modern explicit multi-GPU baseline. Khronos documents device groups as a way to represent multiple physical devices as one logical device, but the model is still oriented around a cooperating set of devices that the implementation already knows how to group. The Vulkan Guide also frames device groups around single-vendor systems, vendor bridge links, or tightly integrated hardware configurations. Device groups are therefore useful as a capability model and as an execution primitive, but not as a complete answer for arbitrary AMD + NVIDIA + Intel combinations.

Academic and HPC runtimes, especially StarPU and Legion, provide the second important lesson. They do not try to pretend that heterogeneous accelerators are identical. Instead, they formalize scheduling, placement, and data movement. StarPU explicitly presents itself as a runtime for heterogeneous multicore architectures and focuses on task dependencies, optimized heterogeneous scheduling, and optimized data transfers and replication between host memory and discrete memories. Legion takes a region and task model, where locality and data movement are first-class concerns. These systems scale because they treat heterogeneity as a scheduling and memory-placement problem, not as a magical “single device” illusion at every level of the stack.

## What Worked

SLI and CrossFire worked when all of the following were true:

- The GPUs were from the same vendor and usually the same family.
- The rendering mode matched the engine’s frame dependency pattern.
- The driver shipped a title-specific profile or the game contained explicit support.
- Output routing was constrained, often through a primary adapter or bridge topology.

These systems were effective for a limited class of gaming workloads where frames could be split or alternated without too much inter-frame dependency. AMD’s support article still describes AFR-oriented modes and frame pacing features, which confirms the design center.

Vulkan device groups improved on this by giving the application explicit control over queueing, memory, and presentation behavior. The Khronos guidance explicitly mentions AFR-style presentation and management of multiple sub-devices behind one logical device. This is the first practical entry point for UMGAL because it allows the runtime to remain in user space, intercept submission, and start learning how real applications behave before any kernel virtualization claims are made.

Academic runtimes worked because they accepted overhead where it was necessary and optimized the expensive paths: data placement, dependency analysis, and asynchronous execution. StarPU’s emphasis on optimized transfers and replication is directly relevant to UMGAL’s future unified-memory design. Legion’s focus on locality and independence is directly relevant to any future workload classifier or logical-device scheduler.

## What Failed

Implicit driver-managed gaming multi-GPU failed for structural reasons:

- AFR increased latency and frame pacing problems.
- Driver teams had to maintain per-title profiles.
- Modern engines use temporal techniques, history buffers, and frame-to-frame feedback that make naive AFR fragile.
- Consumer developers stopped investing in vendor-specific multi-GPU paths because the install base was too small.

The official vendor shift to explicit APIs is effectively an admission that generic driver heuristics do not scale well to modern engines.

Vulkan device groups also have hard limits for UMGAL:

- They assume the Vulkan implementation already exposes a useful group.
- They do not solve translation across vendor-specific memory models by themselves.
- They are not a transparent solution for OpenGL, CUDA, OpenCL, or DRM clients.

Academic runtimes are also not a direct drop-in solution:

- They usually assume application cooperation.
- They target HPC or task-based programming models, not arbitrary games.
- They do not need to preserve desktop graphics semantics or transparent compatibility with Steam/Proton.

## Design Implications for UMGAL

The research points to a staged architecture:

1. Start with a Vulkan layer, not a kernel meta-driver. This matches the current industry direction and avoids pretending we can hook proprietary drivers safely.
2. Treat “single logical GPU” as a compatibility illusion built on top of explicit scheduling and memory routing, not as a literal universal hardware abstraction from day one.
3. Borrow concepts from HPC runtimes for scheduling, telemetry, and data placement rather than copying old gaming-era AFR systems.
4. Keep heterogeneous support policy-driven. Homogeneous or near-homogeneous device groups can use more aggressive modes; cross-vendor pools need conservative fallbacks.

## Sources

- Khronos Vulkan Guide, Device Groups: https://github.khronos.org/Vulkan-Site/guide/latest/extensions/device_groups.html
- NVIDIA SLI transition notice: https://nvidia.custhelp.com/app/answers/detail/a_id/5082/~/nvidia-sli-support-transitioning-to-native-game-integrations
- AMD CrossFire support article: https://www.amd.com/en/resources/support-articles/faqs/DH-018.html
- StarPU project overview: https://starpu.gitlabpages.inria.fr/
- Legion publications and runtime overview: https://legion.stanford.edu/publications/index.html
