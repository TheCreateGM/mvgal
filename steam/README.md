# MVGAL Steam and Proton Compatibility Layer

This directory contains the Steam and Proton integration components for MVGAL.

## Overview

MVGAL integrates with Steam via two mechanisms:

1. **Vulkan implicit layer** (`VK_LAYER_MVGAL`) — automatically active for all
   Vulkan applications, including those launched through Proton.
2. **Steam launch option** — `ENABLE_MVGAL=1 %command%` enables MVGAL for a
   specific game and activates the AFR scheduler.

## Files

| File | Purpose |
|------|---------|
| `mvgal_steam_compat.sh` | Steam compatibility tool entry point |
| `mvgal_proton_hook.sh` | Proton pre-launch hook that sets MVGAL env vars |
| `mvgal_frame_pacer.c` | Frame pacing queue implementation |
| `mvgal_afr.c` | Alternate Frame Rendering coordinator |
| `toolmanifest.vdf` | Steam compatibility tool manifest |
| `compatibilitytool.vdf` | Steam compatibility tool registration |

## Usage

### Per-game launch option
Add to Steam game properties → Launch Options:
```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr %command%
```

### Environment variables

| Variable | Values | Description |
|----------|--------|-------------|
| `ENABLE_MVGAL` | `0` / `1` | Enable/disable MVGAL for this launch |
| `MVGAL_STRATEGY` | `afr`, `sfr`, `hybrid`, `single` | Scheduling strategy |
| `MVGAL_FRAME_PACING` | `0` / `1` | Enable frame pacing correction |
| `MVGAL_GPU_MASK` | hex bitmask | Which GPUs to use (e.g. `0x3` = GPU 0+1) |
| `MVGAL_VULKAN_DEBUG` | `0` / `1` | Enable Vulkan layer debug logging |
| `MVGAL_VULKAN_LOG_PATH` | path | Write Vulkan layer log to file |

## DXVK Compatibility

DXVK 2.x translates Direct3D 9/10/11 to Vulkan. MVGAL's Vulkan layer sits
above DXVK in the layer stack and intercepts `vkQueueSubmit` calls. No special
configuration is needed — DXVK games work automatically.

Tested DXVK versions: 2.3, 2.4

## VKD3D-Proton Compatibility

VKD3D-Proton translates Direct3D 12 to Vulkan. Same as DXVK — MVGAL intercepts
at the Vulkan queue submission level. Frame pacing is more important for DX12
titles due to their use of explicit synchronisation.

Recommended: `MVGAL_FRAME_PACING=1`

## Frame Pacing

Multi-GPU AFR can cause microstutter if frames are delivered to the display at
uneven intervals. MVGAL's frame pacing queue holds completed frames and releases
them at the correct display refresh interval (vsync-aligned).

The frame pacer runs as a background thread in `mvgald` and communicates with
the Vulkan layer via the IPC socket.

## Alternate Frame Rendering (AFR)

- Even frames (0, 2, 4, …) → GPU 0
- Odd frames (1, 3, 5, …) → GPU 1
- Frame N+1 is not presented until frame N is complete (dependency enforced via
  Vulkan timeline semaphore)
- If GPU 1 finishes before GPU 0, it waits in the frame pacing queue

## Known Limitations

- Kernel-level anti-cheat (EasyAntiCheat, BattlEye) blocks Vulkan layers.
  Disable MVGAL for affected games.
- Some DX12 titles with aggressive GPU synchronisation may not benefit from AFR.
  Use `MVGAL_STRATEGY=single` as fallback.
- Ray tracing workloads are not split across GPUs in the current implementation.
