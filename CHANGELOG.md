# Changelog

All notable changes to MVGAL (Multi-Vendor GPU Aggregation Layer) are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Remote GPU network pooling: UDP/TCP peer discovery, heartbeat health tracking,
  RemoteGpu integration with DeviceRegistry, latency-weighted scheduler scoring
- AI-driven scheduler: `mvgal_ai.h` model inference C API (`load_model`, `predict`),
  `[ai_scheduler]` config section, runtime enable/disable toggle
- Cross-crate Rust FFI integration test suite (`safe/ffi_tests/`) — 9 tests
  validating fence, memory, and capability lifecycle across C FFI boundaries

### Changed
- Rust FFI audit: `panic::catch_unwind` wrappers on all 19 `extern "C"` functions
  across `capability_model`, `fence_manager`, `memory_safety`
- Metrics collector expansion with additional GPU telemetry fields

### Fixed
- SDL error handling in benchmarks
- `mvgal_resolve` type mismatch in stress benchmark
- Unused variable warnings in stress benchmark
- Missing CMake include paths for `mvgald` target

### Removed
- Stale GitHub Actions CI/CD workflows (ci.yml, copr.yml)

## [v0.2.1] — 2026-05-01

### Added
- RPM/COPR packaging: spec file fixes for Fedora, conditional OpenCL, debuginfo
- Vulkan ICD with device, memory, command buffer, and sync entry points
- Real GPU querying for Vulkan ICD, stress benchmark cleanup
- Vulkan API interception tests
- Fedora COPR package badge and install instructions
- Configurable GPU selection strategies: AFR, single, all-device modes
- Automatic frame rendering (AFR) support for aggregated Vulkan devices
- Dynamic load balancing with mutex lifecycle fix in Vulkan device group emulator
- GPU driver telemetry integration into load balancer
- Cross-vendor semaphore synchronization and tile assembly logic
- Kernel stub ABI, Vulkan layer updates, pkexec privilege helper
- Steam/Proton compatibility layer

### Changed
- CMake: use GNUInstallDirs for proper lib64 detection, compatibility aliases
- RPM spec: remove non-ASCII chars for RPM 6.x compatibility
- Project homepage URL updated
- Documentation rewritten based on full code analysis

### Fixed
- Vulkan 1.4 header incompatibilities and duplicate declarations
- CMake nesting error in Vulkan detection logic
- Manual Vulkan header search fallback, path syntax
- GCC 15+ implicit-declaration error in `mvgal_rewrite_update_gpu_utilization`
- Missing MVGAL_INCLUDE_DIRS on Vulkan layer target
- Dead `mvgal_rewrite_update_gpu_utilization` call removed
- Forward declaration for `mvgal_get_queue_family_properties` in device_group.c
- Function name mismatch (`mvgal_gpu_` → `mvgal_`) in device_group.c
- Build fixes for openSUSE, Mageia 8 compat

## [0.2.1-5..0.2.1-12] — 2026-04/05

### Added
- Moore Threads Driver Installer with Loginwall Support
- Multi-Vendor Vulkan Device Group Emulation
- Dynamic Workload Rebalancing Engine
- Security Policies (Polkit)
- Weighted dynamic load balancing for SFR
- Moore Threads vendor ID (0x1ED5)

### Fixed
- RPM debuginfo package conflict
- Spec file to match actual build outputs

## [0.2.0-1] — 2026-04-21

### Added
- GPU enumeration and capability discovery
- Device group creation and management
- Basic workload distribution across GPUs
- Initial Vulkan device group emulation
- Compute kernel dispatch infrastructure

## [0.1.0-1] — 2026-04-20

### Added
- Initial project structure and build system
- Multi-vendor GPU abstraction layer design
- Basic memory management for cross-device buffers
- Fence synchronization primitives
- Core API specification

[v0.2.1]: https://github.com/TheCreateGM/mvgal/releases/tag/v0.2.1
