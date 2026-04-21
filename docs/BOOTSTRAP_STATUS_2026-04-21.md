# Bootstrap Status - 2026-04-21

This repository already contained substantial placeholder code and optimistic status documents. The current working assumption for UMGAL/MVGAL is:

- the existing tree is a prototype scaffold, not a nearly complete production system,
- the first trustworthy milestone is a real Vulkan layer foundation,
- kernel virtualization, cross-vendor memory migration, and transparent logical-device enumeration remain future work.

## Completed In This Bootstrap Pass

- Added Milestone 1 research documents for:
  - existing multi-GPU framework survey,
  - Vulkan explicit multi-GPU API,
  - Vulkan layer development.
- Replaced the previous fake Vulkan layer with a loader-compliant minimal layer.
- Switched the Vulkan manifest to a proper implicit-layer format with environment gating.
- Added an integration smoke test that performs a real `vkQueueSubmit` against a software ICD and verifies that the layer logged the submission.

## Not Completed

- No logical-device aggregation is exposed to applications yet.
- No physical-device enumeration rewrite is attempted yet.
- No DMA-BUF, external-memory import/export, or cross-vendor synchronization path is implemented in the layer.
- No kernel `umgal.ko` or `mvgal.ko` design claims should be treated as validated based on the current tree.

## Recommended Next Steps

1. Extend the Vulkan layer from submission telemetry to physical-device enumeration mediation.
2. Add a trace format for queue, command-buffer, and fence behavior.
3. Research and prototype external-memory and timeline-semaphore sharing on one known-good vendor pair before touching heterogeneous translation.
4. Audit or remove legacy documentation that still claims near-complete project status.
