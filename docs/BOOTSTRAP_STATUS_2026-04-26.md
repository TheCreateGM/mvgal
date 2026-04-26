# Bootstrap Status - 2026-04-26

## Summary

This pass extended the Milestone 1 Vulkan layer foundation established on
2026-04-21 with physical device enumeration mediation and property forwarding.
All 8 tests pass. The CI workflow is now in place.

## Completed In This Pass

### Vulkan Layer (Milestone 2 foundation)

- Extended `vk_layer.h` dispatch structs with physical device property/feature/
  memory/queue-family function pointers.
- Added `physical_device_count` atomic counter to layer state.
- Added `properties_cached` field and `VkPhysicalDeviceProperties` cache to
  `mvgal_physical_device_dispatch_t` — the layer now remembers device names and
  vendor IDs without calling back into the driver on every submit.
- Added `mvgal_vk_cache_physical_device_properties()` which logs device name,
  vendor ID, and device ID on first observation.
- Added forwarding interceptors for:
  - `vkGetPhysicalDeviceProperties`
  - `vkGetPhysicalDeviceFeatures`
  - `vkGetPhysicalDeviceMemoryProperties`
  - `vkGetPhysicalDeviceQueueFamilyProperties`
  - `vkGetPhysicalDeviceProperties2`
  - `vkGetPhysicalDeviceFeatures2`
  - `vkGetPhysicalDeviceMemoryProperties2`
- Updated `vkEnumeratePhysicalDevices` to log topology and trigger property
  caching for each newly registered physical device.
- Updated `vkGetInstanceProcAddr` to expose all new interceptors.
- Updated layer description from "Milestone 1" to production description.
- Updated manifest `implementation_version` from 1 to 2.

### Build System

- Added `scheduler/workload_splitter.c` to both `mvgal_core` and `mvgal` shared
  library targets in `src/userspace/CMakeLists.txt` (the file existed but was
  not compiled).

### CI

- Added `.github/workflows/ci.yml` with:
  - Build matrix: Ubuntu 22.04 + 24.04, GCC + Clang
  - Unit test run via CTest
  - Vulkan layer smoke test (lavapipe ICD)
  - clang-tidy check on Vulkan layer sources
  - clang-format check on Vulkan layer sources
  - Rust clippy + rustfmt (conditional on Cargo.toml)
  - Packaging check (Debian control, PKGBUILD syntax)
  - shellcheck on all shell scripts

### Verified

- `cmake --build build_ci` completes with 63/63 targets built, zero errors.
- `ctest` reports 8/8 tests PASS including `test_vulkan_layer_submit`.
- `libVK_LAYER_MVGAL.so` exports all required Vulkan layer entry points plus
  the new physical device interceptors.

## Not Completed

- No logical-device aggregation is exposed to applications yet. The layer
  observes and forwards; it does not yet synthesize a virtual device.
- No DMA-BUF, external-memory import/export, or cross-vendor synchronization
  path is implemented in the layer.
- No kernel `mvgal.ko` DRM registration (the kernel module is a character
  device skeleton, not a DRM driver).
- The old stub files (`vk_instance.c`, `vk_device.c`, `vk_queue.c`,
  `vk_command.c`) remain in the tree but are not compiled. They reference
  undefined symbols from a previous design and should be removed or rewritten
  as proper chain-forwarding files in a future pass.

## Recommended Next Steps

1. Remove or rewrite `vk_instance.c`, `vk_device.c`, `vk_queue.c`,
   `vk_command.c` to use the loader-compliant dispatch pattern from `vk_layer.c`
   rather than the old fake-handle approach.
2. Add `vkEnumeratePhysicalDevices` mediation: intercept the result and
   optionally present a synthesized aggregate device alongside real devices.
3. Add external-memory and timeline-semaphore tracing to the layer so future
   cross-device memory sharing paths have an observation point.
4. Extend the kernel module to register as a DRM driver (not just a character
   device) so `/dev/mvgal0` appears in the DRM subsystem.
5. Wire `mvgal-probe` output into the daemon's startup log so the daemon
   reports kernel-detected topology at startup.
