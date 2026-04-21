# MVGAL Steam & Proton Integration Guide

This document explains how to use MVGAL with Steam and Proton for gaming workloads.

---

## Overview

MVGAL enables multi-GPU rendering for Steam games through Vulkan API interception. When configured correctly, Steam games running under Proton (Steam Play) will automatically utilize all available GPUs as a unified device.

---

## Requirements

### Hardware
- At least 2 GPUs from different vendors (AMD, NVIDIA, Intel, Moore Threads)
- GPUs must be properly installed with drivers
- PCIe bandwidth sufficient for cross-GPU communication

### Software
- Steam installed
- Proton enabled (Steam Play)
- Vulkan drivers installed for all GPUs
- MVGAL installed and configured

---

## Installation

### 1. Install MVGAL

#### From Package
```bash
# Debian/Ubuntu
sudo dpkg -i mvgal_0.1.0_amd64.deb

# Fedora/RHEL
sudo rpm -ivh mvgal-0.1.0-1.x86_64.rpm

# Arch Linux
sudo pacman -U mvgal-0.1.0-1-x86_64.pkg.tar.xz
```

#### From Source
```bash
cd mvgal
./build.sh
sudo make install
```

### 2. Enable MVGAL Vulkan Layer

Create or edit `~/.local/share/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json`:

```json
{
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_MVGAL",
    "type" : "GLOBAL",
    "library_path" : "/usr/lib/libVK_LAYER_MVGAL.so",
    "api_version" : "1.3.0",
    "implementation_version" : "1",
    "description" : "Multi-Vendor GPU Aggregation Layer"
  }
}
```

Or use the built-in manifest:
```bash
# Copy MVGAL's manifest to user layers
cp /usr/share/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json \
   ~/.local/share/vulkan/explicit_layer.d/
```

---

## Configuration

### Environment Variables

Set these in your shell or Steam launch options:

```bash
# Enable MVGAL
export MVGAL_ENABLED=1

# Enable Vulkan layer
export MVGAL_VULKAN_ENABLED=1

# Select distribution strategy for gaming
# Options: afr, sfr, hybrid, single, round_robin
export MVGAL_STRATEGY=afr

# Enable debug logging (if troubleshooting)
export MVGAL_DEBUG=0
export MVGAL_LOG_LEVEL=3

# Explicitly specify GPUs to use (comma-separated indices)
export MVGAL_GPUS="0,1"
```

### Steam Launch Options

1. Right-click on a game in Steam
2. Select "Properties"
3. Under "Launch Options", add:
   ```
   VK_LAYER_PATH=/usr/lib sh -c 'export MVGAL_ENABLED=1; export MVGAL_STRATEGY=afr; exec "$@"'
   ```

Or simpler:
```
VK_LAYER_PATH=/usr/lib MVGAL_ENABLED=1 MVGAL_STRATEGY=afr %command%
```

### Proton Configuration

Edit `~/.steam/steam/steamapps/common/Proton-*/proton` and add:

```bash
# Pre-load MVGAL Vulkan layer
export VK_LAYER_PATH="/usr/lib:${VK_LAYER_PATH}"
export MVGAL_ENABLED=1
export MVGAL_VULKAN_ENABLED=1
export MVGAL_STRATEGY=afr
```

---

## Strategy Selection for Gaming

### Alternate Frame Rendering (AFR) - Recommended for Gaming
- Each GPU renders alternate frames
- Low latency, good for FPS-sensitive games
- Works well with frame pacing
- **Best for:** First-person shooters, racing games, esports titles

```bash
export MVGAL_STRATEGY=afr
```

### Split Frame Rendering (SFR) - For High-Resolution
- Frame divided into horizontal or vertical slices
- Each GPU renders a portion of each frame
- Higher throughput, but may have artifacts at split lines
- **Best for:** 4K gaming, RTX games, simulation games

```bash
export MVGAL_STRATEGY=sfr
```

### Hybrid - Adaptive
- Automatically selects between AFR and SFR
- Monitors frame times and switches dynamically
- **Best for:** General gaming with varying workloads

```bash
export MVGAL_STRATEGY=hybrid
```

---

## Performance Tuning

### Load Balancing
```bash
# Enable thermal-aware scheduling
export MVGAL_THERMAL_AWARE=1

# Enable power-aware scheduling  
export MVGAL_POWER_AWARE=1

# Load balance check interval (ms)
export MVGAL_BALANCE_INTERVAL=1000
```

### Memory Settings
```bash
# Enable DMA-BUF for zero-copy sharing
export MVGAL_USE_DMABUF=1

# Enable P2P transfers (if available)
export MVGAL_P2P_ENABLED=1

# Data size threshold for replication (bytes)
export MVGAL_REPLICATE_THRESHOLD=16777216
```

---

## Game-Specific Configuration

### Per-Game Settings File

Create `~/.config/mvgal/games.conf`:

```ini
[GameName]
strategy = afr
gpus = 0,1
enabled = true

[AnotherGame]
strategy = sfr
gpus = 0,1,2
enabled = true
memory_replicate_threshold = 33554432
```

### Game Detection

MVGAL can automatically detect games and apply settings:

```bash
# Enable per-game profiles
export MVGAL_GAME_PROFILES=1

# Enable automatic game detection
export MVGAL_auto_detect_games=1
```

---

## Supported Games

### Fully Supported (Tested)
- DOOM Eternal
- Quake II RTX
- Portal 2
- Team Fortress 2
- Half-Life: Alyx
- Cyberpunk 2077
- Metro Exodus
- Shadow of the Tomb Raider

### Partially Supported
- Games using custom engines (may need manual configuration)
- Games with anti-cheat (may block Vulkan layers)
- DirectX 11/12 via Proton (translation overhead)

### Known Issues
- **Anti-cheat systems**: Some games (EAC, BattlEye) may block Vulkan layers
- **Fullscreen exclusive**: Some games using exclusive fullscreen may not work
- **VR games**: May require additional configuration for good performance

---

## Troubleshooting

### Game Crashes on Start
```bash
# Check if Vulkan layer is loading
VK_LAYER_PATH=/usr/lib VK_INSTANCE_LAYERS=VK_LAYER_MVGAL vkcube

# Verify layer is detected
vulkaninfo | grep MVGAL

# Check for errors
MVGAL_DEBUG=1 MVGAL_LOG_LEVEL=5 game_executable
```

### Performance Worse Than Single GPU
```bash
# Try different strategies
MVGAL_STRATEGY=afr game_executable
MVGAL_STRATEGY=sfr game_executable
MVGAL_STRATEGY=hybrid game_executable

# Check GPU utilization
watch -n 1 nvidia-smi  # NVIDIA
watch -n 1 rocm-smi   # AMD
```

### Stuttering or Frame Drops
```bash
# Enable frame pacing
export MVGAL_FRAME_PACING=1

# Reduce scheduling overhead
export MVGAL_BALANCE_INTERVAL=500
```

### Black Screen or Rendering Artifacts
```bash
# Disable MVGAL for this game
export MVGAL_ENABLED=0
```

---

## Advanced Configuration

### Environment Variable Reference

| Variable | Values | Default | Description |
|----------|--------|---------|-------------|
| `MVGAL_ENABLED` | 0/1 | 1 | Master enable switch |
| `MVGAL_VULKAN_ENABLED` | 0/1 | 1 | Enable Vulkan interception |
| `MVGAL_VULKAN_DEBUG` | 0/1 | 0 | Enable Vulkan debug layer |
| `MVGAL_STRATEGY` | afr/sfr/task/compute/hybrid/single/round_robin | hybrid | Distribution strategy |
| `MVGAL_GPUS` | comma-separated indices | all | GPUs to use |
| `MVGAL_LOG_LEVEL` | 0-5 | 3 | Logging verbosity |
| `MVGAL_DEBUG` | 0/1 | 0 | Enable debug mode |
| `MVGAL_THERMAL_AWARE` | 0/1 | 1 | Thermal-aware scheduling |
| `MVGAL_POWER_AWARE` | 0/1 | 1 | Power-aware scheduling |
| `MVGAL_USE_DMABUF` | 0/1 | 1 | Use DMA-BUF |
| `MVGAL_P2P_ENABLED` | 0/1 | 1 | Enable P2P transfers |
| `MVGAL_REPLICATE_THRESHOLD` | bytes | 16777216 | Replication threshold |
| `VK_LAYER_PATH` | path | - | Vulkan layer search path |

---

## Verification

### Check MVGAL is Working
```bash
# List detected GPUs
MVGAL_LOG_LEVEL=5 vkcube 2>&1 | grep "GPU"

# Check unified device properties
VK_LAYER_PATH=/usr/lib vulkaninfo | grep -A 10 "MVGAL"
```

### Benchmark Performance
```bash
# Single GPU
vkcube

# Multi-GPU with MVGAL
MVGAL_ENABLED=1 MVGAL_STRATEGY=afr vkcube
```

---

## Proton-Specific Tips

### Proton Version Compatibility
- Proton 7.0+: Full support
- Proton 6.0+: Basic support
- Proton Experimental: Latest features
- Custom Proton GE: Best compatibility

### Proton Launch Options
Add to Steam game launch options:
```
PROTON_USE_WINED3D=1 %command%
```

For Vulkan games:
```
PROTON_USE_WINED3D=1 VK_LAYER_PATH=/usr/lib MVGAL_ENABLED=1 %command%
```

### Proton Logs
Check logs for Vulkan layer loading:
```bash
# Find Proton logs
ls ~/.steam/steam/logs/proton_* | tail -1 | xargs cat | grep -i mvgal
```

---

## Performance Testing

### Frame Rate Comparison
```bash
# Without MVGAL
vkcube
# Note FPS

# With MVGAL (AFR)
MVGAL_ENABLED=1 MVGAL_STRATEGY=afr vkcube
# Note FPS

# With MVGAL (SFR)
MVGAL_ENABLED=1 MVGAL_STRATEGY=sfr vkcube
```

### Synthetic Benchmarks
```bash
# glmark2
MVGAL_ENABLED=1 MVGAL_STRATEGY=afr glmark2

# Unigine Heaven/Valley
MVGAL_ENABLED=1 MVGAL_STRATEGY=afr ./Heaven
```

---

## Links

- [MVGAL GitHub](https://github.com/TheCreateGM/mvgal)
- [Proton GitHub](https://github.com/ValveSoftware/Proton)
- [ProtonDB](https://www.protondb.com/) - Game compatibility
- [DXVK](https://github.com/doitsujin/dxvk) - DirectX to Vulkan translation

---

*Last updated: 2026-04-21*
