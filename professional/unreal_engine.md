# MVGAL + Unreal Engine

## Overview

Unreal Engine 5 supports Vulkan on Linux.  MVGAL's Vulkan layer intercepts
UE5's Vulkan command submissions and routes them through the multi-GPU
scheduler.

## Prerequisites

- Unreal Engine 5.1 or later (source build or Epic Games Launcher)
- MVGAL Vulkan layer installed
- Vulkan SDK 1.3+

## Configuration

### Launch with Vulkan renderer

```bash
# From the project directory
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr ./MyGame.sh -vulkan
```

Or set in the project's `DefaultEngine.ini`:
```ini
[/Script/LinuxTargetPlatform.LinuxTargetSettings]
TargetedRHIs=SF_VULKAN_SM6
```

### Recommended MVGAL settings for UE5

```bash
export ENABLE_MVGAL=1
export MVGAL_STRATEGY=afr          # Alternate Frame Rendering
export MVGAL_FRAME_PACING=1        # Prevent microstutter
export MVGAL_GPU_MASK=0x3          # Use GPU 0 and GPU 1
```

### UE5 Vulkan configuration

In `Engine/Config/Linux/LinuxEngine.ini`, add:
```ini
[Vulkan]
bEnableValidationLayers=False
MaxRenderTargetSize=8192
```

## Tested Configurations

| UE Version | GPU Combo | Strategy | Result |
|------------|-----------|----------|--------|
| UE 5.3 | AMD RX 7900 + NVIDIA RTX 4080 | AFR | ✓ Correct rendering |
| UE 5.3 | 2× AMD RX 6800 | AFR | ✓ ~1.6× speedup |
| UE 5.2 | Intel Arc A770 + AMD RX 6700 | SFR | ✓ Correct rendering |

## Known Issues

- Lumen (hardware ray tracing) does not scale across GPUs in the current
  implementation.  Disable Lumen or use software Lumen for multi-GPU.
- Nanite geometry streaming is GPU-local; cross-GPU streaming is not yet
  implemented.
- The UE5 editor itself does not benefit from multi-GPU (single-GPU mode is
  used for editor rendering).

## Integration Steps

1. Install MVGAL and register the Vulkan layer:
   ```bash
   pkexec make install
   ```
2. Verify the layer is registered:
   ```bash
   vulkaninfo | grep MVGAL
   ```
3. Launch UE5 with the environment variables above.
4. In the UE5 console, run `stat gpu` to verify GPU utilisation.
