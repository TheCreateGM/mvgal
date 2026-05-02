# MVGAL Gaming Guide

**Version:** 0.2.1

---

## Overview

MVGAL provides first-class gaming support through:
- **Vulkan implicit layer** — active for all Vulkan games automatically
- **AFR (Alternate Frame Rendering)** — even/odd frames on different GPUs
- **SFR (Split Frame Rendering)** — screen tiles on different GPUs
- **Frame pacing** — vsync-aligned delivery to prevent microstutter
- **Steam compatibility tool** — one-click enable per game
- **DXVK + VKD3D-Proton** — D3D9/10/11/12 games via Proton work automatically

---

## Quick Setup for Gaming

### Per-game (Steam launch option)

```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr %command%
```

### Global (all Vulkan games)

The Vulkan layer `VK_LAYER_MVGAL` is an implicit layer — it activates for every Vulkan application once installed. No per-game configuration needed.

---

## Scheduling Strategies for Gaming

### AFR — Alternate Frame Rendering

Best for: games with consistent frame times, high GPU utilization.

```
Frame 0 → GPU 0
Frame 1 → GPU 1
Frame 2 → GPU 0
Frame 3 → GPU 1
...
```

Frame N+1 is not presented until frame N is complete (enforced via Vulkan timeline semaphore). This prevents out-of-order presentation.

```bash
MVGAL_STRATEGY=afr
```

### SFR — Split Frame Rendering

Best for: games with heavy per-pixel shading, high resolution.

```
Left half  → GPU 0
Right half → GPU 1
Composite → display GPU
```

```bash
MVGAL_STRATEGY=sfr
```

### Hybrid

Best for: mixed workloads, unknown games. MVGAL selects AFR or SFR automatically based on workload metrics.

```bash
MVGAL_STRATEGY=hybrid
```

---

## Frame Pacing

Multi-GPU AFR can cause microstutter if frames arrive at uneven intervals. MVGAL's frame pacer (`steam/mvgal_frame_pacer.c`) holds completed frames in a ring buffer (depth 8) and releases them at vsync-aligned intervals.

The pacer runs as a background thread in `mvgald`. It uses `clock_gettime(CLOCK_MONOTONIC)` and `nanosleep` for nanosecond-precision timing.

Enable explicitly:
```bash
MVGAL_FRAME_PACING=1
```

The frame pacer is enabled by default when `MVGAL_STRATEGY=afr`.

---

## Environment Variables

| Variable | Values | Default | Description |
|----------|--------|---------|-------------|
| `ENABLE_MVGAL` | `0` / `1` | `0` | Enable MVGAL for this launch |
| `MVGAL_STRATEGY` | `afr`, `sfr`, `hybrid`, `single`, `round_robin` | `hybrid` | Scheduling strategy |
| `MVGAL_FRAME_PACING` | `0` / `1` | `1` (with AFR) | Enable frame pacing |
| `MVGAL_GPU_MASK` | hex bitmask | `0xFF` (all) | Which GPUs to use |
| `MVGAL_VULKAN_DEBUG` | `0` / `1` | `0` | Vulkan layer debug logging |
| `MVGAL_VULKAN_LOG_PATH` | path | stderr | Redirect Vulkan log to file |

---

## Steam Compatibility Tool

MVGAL registers as a Steam compatibility tool. To use:

1. Right-click game → Properties → Compatibility
2. Check "Force the use of a specific Steam Play compatibility tool"
3. Select **MVGAL 0.2.1**

The compatibility tool script (`steam/mvgal_steam_compat.sh`) automatically sets `ENABLE_MVGAL=1`, `MVGAL_STRATEGY=afr`, and `MVGAL_FRAME_PACING=1`.

---

## DXVK Compatibility

DXVK translates Direct3D 9/10/11 to Vulkan. MVGAL's Vulkan layer sits above DXVK in the layer stack and intercepts `vkQueueSubmit` calls. No special configuration needed.

Tested DXVK versions: 2.3, 2.4

**Recommended settings for DXVK games:**
```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr MVGAL_FRAME_PACING=1 %command%
```

---

## VKD3D-Proton Compatibility

VKD3D-Proton translates Direct3D 12 to Vulkan. Same as DXVK — MVGAL intercepts at the Vulkan queue submission level.

DX12 titles use explicit synchronization, making frame pacing more important:
```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr MVGAL_FRAME_PACING=1 %command%
```

---

## Game Compatibility Database

`mvgal-compat` includes a built-in database of known games:

```bash
mvgal-compat "doom"
mvgal-compat "Cyberpunk 2077"
mvgal-compat "Elden Ring"
```

| Game | Status | Notes |
|------|--------|-------|
| DOOM (Vulkan) | ✅ Supported | AFR works well |
| Quake (vkQuake) | ✅ Supported | Tested with vkQuake |
| Dota 2 | ✅ Supported | SFR recommended |
| CS2 | ✅ Supported | Native Vulkan, AFR tested |
| Cyberpunk 2077 | ⚠️ Partial | DX12 via VKD3D-Proton; disable ray tracing for best multi-GPU |
| Elden Ring | ⚠️ Partial | Frame pacing sensitive; use `MVGAL_FRAME_PACING=1` |
| Red Dead Redemption 2 | ⚠️ Partial | Memory-intensive; ensure DMA-BUF available |
| Minecraft | ✅ Supported | OpenGL via Zink/Vulkan |

---

## Known Limitations

### Anti-cheat

Kernel-level anti-cheat (EasyAntiCheat, BattlEye) blocks Vulkan layers. Disable MVGAL for affected games:
```
ENABLE_MVGAL=0 %command%
```

### Ray tracing

Ray tracing workloads are GPU-local and do not scale across GPUs in the current implementation. Disable ray tracing for best multi-GPU performance.

### Single-GPU fallback

MVGAL automatically falls back to single-GPU mode when:
- Multi-GPU latency exceeds single-GPU latency by >20%
- A GPU becomes unavailable during a session
- The game uses features incompatible with multi-GPU (e.g., certain DX12 synchronization patterns)

Force single-GPU:
```bash
MVGAL_STRATEGY=single
```

---

## Performance Expectations

| Configuration | Expected Improvement |
|---------------|---------------------|
| 2× same-vendor GPUs (AFR) | 1.6–1.9× frame rate |
| 2× mixed-vendor GPUs (AFR) | 1.4–1.7× frame rate |
| 3× GPUs (AFR) | 2.0–2.5× frame rate |
| SFR (high resolution) | 1.5–1.8× frame rate |

Actual improvement depends on:
- PCIe bandwidth between GPUs
- Game's CPU bottleneck
- Frame time consistency
- VRAM requirements vs available VRAM per GPU

---

## Monitoring During Gaming

```bash
# Watch GPU utilization in real time
mvgal-status --watch --interval 200

# Check frame pacer stats (via REST API)
curl http://localhost:7474/api/v1/stats
```

---

## Troubleshooting

**Microstutter despite frame pacing:**
- Ensure `MVGAL_FRAME_PACING=1`
- Check display refresh rate matches game frame rate
- Try `MVGAL_STRATEGY=sfr` instead of `afr`

**Lower performance than single GPU:**
- PCIe bandwidth may be the bottleneck
- Try `MVGAL_STRATEGY=single` to confirm
- Check `mvgal-status` for PCIe utilization

**Game crashes on launch:**
- Disable MVGAL: `ENABLE_MVGAL=0`
- Check `MVGAL_VULKAN_DEBUG=1` output
- Verify Vulkan layer is correctly installed: `vulkaninfo | grep MVGAL`
