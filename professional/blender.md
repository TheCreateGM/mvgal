# MVGAL + Blender Multi-GPU Rendering

## Overview

Blender supports multi-GPU rendering via its Cycles renderer using OpenCL,
CUDA, HIP (AMD ROCm), and Metal backends.  MVGAL enhances this by:

1. Presenting all GPUs as a unified OpenCL platform (via `libmvgal_opencl.so`).
2. Routing Cycles tile renders across GPUs via the MVGAL scheduler.
3. Providing a Vulkan compute path for Blender 4.x EEVEE Next.

## Prerequisites

- Blender 3.6 LTS or 4.x
- MVGAL installed with OpenCL ICD registered (`/etc/OpenCL/vendors/mvgal.icd`)
- At least 2 GPUs with OpenCL 1.2+ support

## Configuration

### OpenCL (Cycles)

1. Open Blender → Edit → Preferences → System → Cycles Render Devices.
2. Select **OpenCL** as the compute device type.
3. You should see **MVGAL Unified Platform** with all GPUs listed.
4. Enable all GPUs in the list.
5. In Render Properties → Performance → Tiles, set tile size to a multiple of
   the GPU count (e.g., 256×256 with 2 GPUs → 2 tiles rendered in parallel).

### Vulkan (EEVEE Next, Blender 4.x)

MVGAL's Vulkan layer (`VK_LAYER_MVGAL`) is an implicit layer and activates
automatically.  No additional configuration is needed.

Launch Blender with debug logging:
```bash
MVGAL_VULKAN_DEBUG=1 blender
```

### Environment Variables

```bash
# Use MVGAL for this Blender session
ENABLE_MVGAL=1 blender

# Force specific strategy for Cycles compute
MVGAL_STRATEGY=compute_offload blender

# Limit to specific GPUs (bitmask: GPU 0 + GPU 1 = 0x3)
MVGAL_GPU_MASK=0x3 blender
```

## Expected Performance

| Configuration | Relative Render Time |
|---------------|---------------------|
| 1× GPU (baseline) | 1.0× |
| 2× GPU (MVGAL AFR) | ~1.7–1.9× faster |
| 3× GPU (MVGAL) | ~2.4–2.7× faster |

Actual speedup depends on scene complexity, VRAM requirements, and PCIe
bandwidth between GPUs.

## Known Limitations

- Blender's built-in multi-GPU support (selecting multiple devices in
  Preferences) and MVGAL should not be used simultaneously — use one or the
  other.
- Very large scenes that exceed the VRAM of a single GPU require MVGAL's
  unified memory manager to stage data through host RAM, which reduces speedup.
- EEVEE (non-Next) uses OpenGL and benefits from MVGAL only via the Zink
  OpenGL→Vulkan translation layer.

## Testing

Run the automated render test:
```bash
./professional/test_blender_render.sh /path/to/test_scene.blend
```

The script renders the scene with 1 GPU and with all GPUs, then reports the
speedup ratio.
