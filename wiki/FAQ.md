---
title: FAQ
---

# FAQ

## What does MVGAL do?

MVGAL combines multiple GPUs from different vendors into one logical device. Applications see it as a single GPU without modification.

## Does it work with any GPU?

Any GPU with a Linux driver — AMD (amdgpu), NVIDIA (nvidia-open/proprietary), Intel (i915/xe), Moore Threads (mtgpu-drv).

## Do I need to modify my applications?

No. MVGAL intercepts GPU API calls transparently via Vulkan layers, LD_PRELOAD shims, and a kernel module.

## Does it work with Steam/Proton?

Yes. Add `ENABLE_MVGAL=1 MVGAL_STRATEGY=afr %command%` to game launch options, or use MVGAL as a compatibility tool.

## Can GPUs from different vendors work together?

Yes. An AMD + NVIDIA + Intel setup works. MVGAL normalizes capabilities through its Rust capability model.

## Can I pool GPUs across machines?

Yes (added in v0.2.2). Nodes on the same LAN can share GPUs via UDP discovery and TCP connections.

## Does it support AI/ML workloads?

The AI-driven scheduler learns workload patterns to route compute optimally. CUDA interception works with most ML frameworks.

## Is there a performance overhead?

Vulkan layer: <5µs dispatch overhead. CUDA shim: <10µs per call. DMA-BUF zero-copy has no additional copy overhead.

## Where are logs?

`journalctl -u mvgald` or `/var/log/mvgal/`.
