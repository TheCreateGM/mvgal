# MVGAL Steam & Proton Integration

**Version:** 0.2.1

---

## Overview

MVGAL integrates with Steam via two mechanisms:

1. **Vulkan implicit layer** (`VK_LAYER_MVGAL`) — automatically active for all Vulkan applications, including those launched through Proton. No configuration needed.
2. **Steam compatibility tool** — select MVGAL in game Properties → Compatibility for per-game control.

---

## Installation as Steam Compatibility Tool

Copy the Steam compatibility tool files to Steam's compatibility tools directory:

```bash
# User-level (recommended)
mkdir -p ~/.steam/root/compatibilitytools.d/MVGAL
cp steam/toolmanifest.vdf steam/compatibilitytool.vdf steam/mvgal_steam_compat.sh \
   ~/.steam/root/compatibilitytools.d/MVGAL/

# Or system-level
pkexec cp -r steam/ /usr/share/steam/compatibilitytools.d/MVGAL/
```

Restart Steam. The tool appears as **MVGAL 0.2.1** in the compatibility tool selector.

---

## Per-Game Launch Option

The simplest way to enable MVGAL for a specific game:

1. Right-click game in Steam library → **Properties**
2. In **Launch Options**, enter:

```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr %command%
```

---

## Environment Variables

| Variable | Values | Default | Description |
|----------|--------|---------|-------------|
| `ENABLE_MVGAL` | `0` / `1` | `0` | Enable MVGAL for this launch |
| `MVGAL_STRATEGY` | `afr`, `sfr`, `hybrid`, `single`, `round_robin`, `compute_offload` | `hybrid` | Scheduling strategy |
| `MVGAL_FRAME_PACING` | `0` / `1` | `1` (with AFR) | Enable vsync-aligned frame pacing |
| `MVGAL_GPU_MASK` | hex bitmask | `0xFF` | Which GPUs to use (e.g. `0x3` = GPU 0+1) |
| `MVGAL_VULKAN_DEBUG` | `0` / `1` | `0` | Enable Vulkan layer debug logging |
| `MVGAL_VULKAN_LOG_PATH` | file path | stderr | Redirect Vulkan layer log to file |

---

## Frame Pacing

Multi-GPU AFR can cause microstutter if frames arrive at uneven intervals. The frame pacer (`steam/mvgal_frame_pacer.c`) holds completed frames in a ring buffer (depth 8) and releases them at vsync-aligned intervals.

**Implementation details:**
- Background thread using `CLOCK_MONOTONIC` for nanosecond precision
- `sleep_until_ns()` using `nanosleep()` for accurate timing
- Ring buffer: 8 slots, head/tail pointers, mutex-protected
- Statistics: `frames_paced`, `frames_dropped`, `avg_jitter_us`

**API:**
```c
mvgal_frame_pacer_t *mvgal_fp_create(uint32_t refresh_hz);
void                 mvgal_fp_destroy(mvgal_frame_pacer_t *fp);
int                  mvgal_fp_submit_frame(mvgal_frame_pacer_t *fp,
                                            uint64_t frame_id,
                                            uint32_t gpu_index);
void                 mvgal_fp_get_stats(const mvgal_frame_pacer_t *fp,
                                         uint64_t *frames_paced,
                                         uint64_t *frames_dropped,
                                         double   *avg_jitter_us);
void                 mvgal_fp_set_refresh_hz(mvgal_frame_pacer_t *fp, uint32_t hz);
```

---

## DXVK Compatibility

DXVK 2.x translates Direct3D 9/10/11 to Vulkan. MVGAL's Vulkan layer sits above DXVK in the layer stack and intercepts `vkQueueSubmit` calls. No special configuration is needed.

**Tested:** DXVK 2.3, 2.4

**Recommended launch option:**
```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr MVGAL_FRAME_PACING=1 %command%
```

---

## VKD3D-Proton Compatibility

VKD3D-Proton translates Direct3D 12 to Vulkan. Same as DXVK — MVGAL intercepts at the Vulkan queue submission level.

DX12 titles use explicit GPU synchronization, making frame pacing more important:
```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr MVGAL_FRAME_PACING=1 %command%
```

---

## Steam Profile Generation

MVGAL can generate optimized Steam/Proton profiles via the execution engine:

```c
mvgal_steam_profile_request_t req = {
    .app_name       = "MyGame",
    .strategy       = MVGAL_STRATEGY_AFR,
    .steam_mode     = true,
    .proton_mode    = true,
    .vulkan_mode    = true,
    .low_latency    = true,
};

mvgal_steam_profile_t profile;
mvgal_execution_get_steam_profile(&req, &profile);
// profile.launch_options contains the recommended env vars
// profile.gpu_list contains the recommended GPU indices
```

---

## Known Limitations

| Issue | Workaround |
|-------|-----------|
| EasyAntiCheat blocks Vulkan layers | `ENABLE_MVGAL=0 %command%` |
| BattlEye blocks Vulkan layers | `ENABLE_MVGAL=0 %command%` |
| Ray tracing doesn't scale across GPUs | Disable ray tracing in game settings |
| Some DX12 titles with aggressive sync | Use `MVGAL_STRATEGY=single` |

---

## Debugging

```bash
# Enable Vulkan layer debug output
MVGAL_VULKAN_DEBUG=1 ENABLE_MVGAL=1 %command%

# Log to file
MVGAL_VULKAN_LOG_PATH=/tmp/mvgal_vk.log ENABLE_MVGAL=1 %command%
cat /tmp/mvgal_vk.log

# Check layer is in the chain
vulkaninfo 2>/dev/null | grep -A2 MVGAL

# Check daemon is running
mvgal-compat --system
```
