# MVGAL OpenGL Translation Layer

Translates OpenGL calls to Vulkan and routes them through the MVGAL Vulkan ICD.

## Approach

MVGAL uses the **Zink** Mesa driver as the OpenGL→Vulkan translation layer.
Zink is a Mesa Gallium driver that implements OpenGL on top of Vulkan.  When
combined with MVGAL's Vulkan layer, all OpenGL applications automatically
benefit from multi-GPU aggregation.

## Alternative: LD_PRELOAD shim

For applications that cannot use Zink, `mvgal_gl_preload.c` provides a minimal
LD_PRELOAD shim that intercepts `glXSwapBuffers` and `eglSwapBuffers` to inject
MVGAL frame pacing.

## Files

| File | Purpose |
|------|---------|
| `mvgal_gl_preload.c` | LD_PRELOAD shim for frame pacing injection |
| `mvgal_gl_preload.h` | Public header |
| `CMakeLists.txt` | Build configuration |

## Usage

### Via Zink (recommended)

```bash
# Force Zink for any OpenGL application
MESA_LOADER_DRIVER_OVERRIDE=zink ENABLE_MVGAL=1 glxgears
```

### Via LD_PRELOAD shim

```bash
LD_PRELOAD=/usr/lib/libmvgal_gl.so glxgears
```

## Limitations

- Full OpenGL state machine emulation is provided by Zink/Mesa, not MVGAL.
- MVGAL only adds multi-GPU routing at the Vulkan layer level.
- OpenGL applications that use vendor-specific extensions not supported by Zink
  may fall back to single-GPU operation.
