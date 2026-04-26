# MVGAL Gaming Integration Guide

**Version:** 0.2.0 | **Last Updated:** April 2026

---

## Overview

MVGAL integrates with Steam, Proton, DXVK, and VKD3D-Proton to enable
multi-GPU rendering for Linux games. This guide covers configuration,
per-game profiles, and known compatibility notes.

---

## Steam Detection

MVGAL detects Steam at daemon startup by checking:

1. The `STEAM_RUNTIME` environment variable.
2. The presence of a `steam` process (`/proc/*/comm`).
3. The Steam runtime directory (`~/.steam/root/`).

When Steam is detected, MVGAL automatically activates the **gaming profile**:
- Scheduling mode: AFR (Alternate Frame Rendering) preferred.
- Frame pacing enforcement: enabled.
- Latency-minimizing mode: enabled.
- Idle timeout: reduced to 1 second (faster wake-up).

To manually force the gaming profile:

```bash
mvgalctl set-profile gaming
```

To disable automatic Steam detection:

```ini
# /etc/mvgal/mvgal.conf
[gaming]
auto_detect_steam = false
```

---

## Proton Integration

MVGAL provides a Vulkan layer (`VK_LAYER_MVGAL`) that is automatically
injected into Proton sessions when `MVGAL_VULKAN_ENABLE=1` is set.

### Enabling for All Steam Games

```bash
# Add to Steam launch options for a game:
MVGAL_VULKAN_ENABLE=1 %command%

# Or enable globally via environment:
echo 'MVGAL_VULKAN_ENABLE=1' >> ~/.config/environment.d/mvgal.conf
```

### Enabling for a Specific Game

In Steam, right-click the game → Properties → Launch Options:

```
MVGAL_VULKAN_ENABLE=1 MVGAL_STRATEGY=afr %command%
```

### Proton Version Compatibility

| Proton Version | Status | Notes |
|----------------|--------|-------|
| Proton 9.0+ | ✅ Supported | Recommended |
| Proton 8.0 | ✅ Supported | |
| Proton 7.0 | ⚠️ Partial | Frame pacing may stutter |
| Proton-GE | ✅ Supported | |

---

## Rendering Modes

### Alternate Frame Rendering (AFR)

In AFR mode, odd frames are rendered on GPU 0 and even frames on GPU 1
(extendable to N GPUs). The frame compositor synchronizes present timing
to maintain consistent inter-frame intervals.

```bash
# Enable AFR
mvgalctl set-strategy afr

# Or via environment variable
MVGAL_STRATEGY=afr game_executable
```

**Best for:** Games with consistent frame times, high-FPS targets.  
**Avoid for:** Games with heavy inter-frame dependencies (motion blur, TAA).

### Split Frame Rendering (SFR)

In SFR mode, each frame is divided into horizontal or vertical tiles, with
each GPU rendering a portion. The compositor assembles the final frame.

```bash
# Enable SFR (horizontal split)
mvgalctl set-strategy sfr

# Configure split direction
mvgalctl set-sfr-direction horizontal  # or: vertical
```

**Best for:** Games with uniform workload distribution across the screen.  
**Avoid for:** Games with heavy UI elements concentrated in one screen region.

### Hybrid Adaptive

The hybrid strategy automatically selects between AFR and SFR based on
real-time workload metrics.

```bash
mvgalctl set-strategy hybrid
```

---

## Per-Game Profiles

MVGAL maintains a profile database at `/etc/mvgal/profiles/` and
`~/.config/mvgal/profiles/`. Profiles are matched by executable name and
Vulkan application name.

### Creating a Profile

```ini
# ~/.config/mvgal/profiles/cyberpunk2077.conf
[profile]
name = Cyberpunk 2077
executable = Cyberpunk2077.exe
vulkan_app_name = Cyberpunk 2077

[scheduling]
strategy = afr
frame_pacing = true
idle_timeout_ms = 500

[memory]
prefer_gpu = 0
mirror_textures = true
```

### Listing Available Profiles

```bash
mvgalctl list-profiles
```

### Applying a Profile Manually

```bash
mvgalctl apply-profile cyberpunk2077
```

---

## DXVK Integration

MVGAL's Vulkan layer correctly reports `VkPhysicalDeviceGroupProperties` so
DXVK can optionally use explicit multi-GPU. When `VK_LAYER_MVGAL` is active,
DXVK sees MVGAL's unified device rather than individual physical devices.

### DXVK Configuration

```ini
# dxvk.conf
# No special configuration needed when VK_LAYER_MVGAL is active.
# DXVK will automatically use the MVGAL unified device.
```

### DXVK Version Compatibility

| DXVK Version | Status |
|--------------|--------|
| 2.3+ | ✅ Supported |
| 2.0–2.2 | ✅ Supported |
| 1.x | ⚠️ Limited (no device group support) |

---

## VKD3D-Proton Integration

VKD3D-Proton uses Vulkan device groups for multi-GPU. MVGAL's layer exposes
the correct `VkPhysicalDeviceGroupProperties` so VKD3D-Proton can use all
GPUs in the MVGAL pool.

```bash
# Enable VKD3D multi-GPU (experimental)
VKD3D_CONFIG=dxr11,dxr MVGAL_VULKAN_ENABLE=1 %command%
```

---

## Frame Pacing

MVGAL enforces frame pacing to prevent stuttering in AFR mode. If GPU A
renders frame N two milliseconds late, frame N+1 on GPU B is delayed to
maintain consistent inter-frame timing.

```bash
# Configure frame pacing target (milliseconds)
mvgalctl set-frame-pacing-target 16.67  # 60 FPS

# Disable frame pacing (not recommended for gaming)
mvgalctl set-frame-pacing off
```

---

## Display Output

MVGAL detects the display-connected GPU at startup and always routes
`vkQueuePresentKHR` through that GPU. If the rendering GPU differs from the
display GPU, MVGAL copies the composited frame via DMA-BUF.

```bash
# Show which GPU is display-connected
mvgalctl status --show-display-gpu

# Override display GPU (advanced)
mvgalctl set-display-gpu 1
```

---

## Laptop (PRIME) Configuration

On laptops with Intel iGPU + discrete dGPU:

1. The iGPU drives the display (PRIME render offload).
2. The dGPU renders via PRIME.
3. MVGAL includes both GPUs in the pool.

```bash
# Check PRIME configuration
mvgalctl status --prime

# Force PRIME offload for a game
DRI_PRIME=1 MVGAL_VULKAN_ENABLE=1 %command%
```

---

## Known Compatibility Issues

| Game / Engine | Issue | Workaround |
|---------------|-------|------------|
| Games using DLSS | DLSS requires NVIDIA GPU; MVGAL routes DLSS calls to NVIDIA only | Set `MVGAL_DLSS_GPU=nvidia` |
| Games with anti-cheat (EAC, BattlEye) | Anti-cheat may detect MVGAL layer | Disable layer: `MVGAL_VULKAN_ENABLE=0` |
| Unreal Engine 4 (older) | UE4 may not enumerate device groups | Use `MVGAL_STRATEGY=single` |
| Unity (older) | Unity may not support multi-GPU | Use `MVGAL_STRATEGY=single` |
| Vulkan ray tracing | RT requires all GPUs to support VK_KHR_ray_tracing_pipeline | MVGAL reports intersection of features |

---

## Debugging

```bash
# Enable verbose Vulkan layer logging
MVGAL_VULKAN_DEBUG=1 MVGAL_VULKAN_LOG_PATH=/tmp/mvgal-vk.log game_executable

# Show real-time GPU utilization during gaming
mvgalctl monitor --interval 500

# Show frame timing statistics
mvgalctl stats --frames
```
