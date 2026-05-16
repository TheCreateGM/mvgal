# MVGAL Developer Guide

**Version:** 0.2.2 | **Date:** May 2026

---

## Table of Contents

1. [Project Structure](#1-project-structure)
2. [Build System](#2-build-system)
3. [Coding Standards](#3-coding-standards)
4. [Kernel Module Development](#4-kernel-module-development)
5. [Userspace Development](#5-userspace-development)
6. [Testing](#6-testing)
7. [Debugging](#7-debugging)
8. [Contributing](#8-contributing)
9. [Release Process](#9-release-process)
10. [Architecture Decisions](#10-architecture-decisions)

---

## 1. Project Structure

```
mvgal/
├── kernel/                    # Linux kernel module (mvgal.ko)
│   ├── mvgal_main.c           # PCI enumeration, cdev, sysfs
│   ├── mvgal_core.c           # DRM registration, module init
│   ├── mvgal_device.c         # Logical device, GPU management
│   ├── mvgal_memory.c         # Memory allocation, DMA-BUF
│   ├── mvgal_scheduler.c      # Workload scheduling
│   ├── mvgal_sync.c           # Cross-vendor fences
│   ├── mvgal_power.c          # Power management
│   ├── mvgal_uapi.h           # User-space API definitions
│   ├── mvgal_types.h          # Internal types
│   ├── mvgal.h                # Main header
│   ├── Makefile               # Kbuild makefile
│   ├── dkms.conf              # DKMS configuration
│   └── vendors/               # Per-vendor driver integration
│       ├── mvgal_amd.c        # AMD amdgpu ops
│       ├── mvgal_nvidia.c     # NVIDIA open-kernel ops
│       ├── mvgal_intel.c      # Intel i915/xe ops
│       └── mvgal_mtt.c        # Moore Threads mtgpu ops
│
├── safe/                      # Rust safety crates
│   ├── Cargo.toml             # Workspace manifest
│   ├── fence_manager/         # Cross-device fence lifecycle
│   ├── memory_safety/         # Allocation tracking, ref counting
│   ├── capability_model/      # GPU capability normalization
│   └── ffi_tests/             # Cross-crate FFI tests
│
├── runtime/                   # C++20 runtime daemon
│   └── daemon/                # mvgald implementation
│       ├── main.cpp           # Entry point
│       ├── daemon.cpp/hpp     # Orchestrator
│       ├── device_registry.cpp/hpp
│       ├── scheduler.cpp/hpp
│       ├── memory_manager.cpp/hpp
│       ├── power_manager.cpp/hpp
│       ├── metrics_collector.cpp/hpp
│       └── ipc_server.cpp/hpp
│
├── src/userspace/             # Userspace library and tools
│   ├── include/               # Public headers
│   │   ├── mvgal.h            # Main API header
│   │   ├── mvgal_types.h      # Type definitions
│   │   ├── mvgal_gpu.h        # GPU descriptors
│   │   └── mvgal_error.h      # Error codes
│   ├── api/                   # Core API implementation
│   ├── daemon/                # Daemon implementation (C version)
│   ├── execution/             # Frame sessions, migration plans
│   ├── memory/                # Memory management
│   ├── scheduler/             # Scheduling strategies
│   ├── intercept/             # API interception layers
│   │   ├── vk_layer.c         # Vulkan implicit layer
│   │   ├── cl_intercept.c     # OpenCL ICD
│   │   ├── cuda_wrapper.c     # CUDA LD_PRELOAD wrapper
│   │   ├── d3d_wrapper.c      # Direct3D wrapper
│   │   ├── metal_wrapper.c    # Metal wrapper
│   │   └── webgpu_wrapper.c   # WebGPU wrapper
│   └── vulkan_icd/            # Vulkan ICD implementation
│
├── tools/                     # CLI tools
│   ├── mvgal.c                # Main CLI
│   ├── mvgal-info.c           # GPU information
│   ├── mvgal-status.c         # Real-time status
│   ├── mvgal-bench.c          # Benchmark suite
│   ├── mvgal-compat.c         # Compatibility checker
│   ├── mvgal-config.c         # Configuration tool
│   └── mvgal_exporter.go      # Prometheus exporter
│
├── steam/                     # Steam/Proton compatibility
├── opengl/                    # OpenGL preload shim
├── ui/                        # Qt dashboard + REST server
├── compat/                    # Compatibility layers
├── bindings/                  # Language bindings (7 languages)
├── packaging/                 # Package build scripts
├── docs/                      # Documentation
├── tests/                     # Test suite
├── CMakeLists.txt             # CMake build
├── meson.build                # Meson build
├── build.zig                  # Zig build
└── README.md
```

---

## 2. Build System

### 2.1 CMake (Primary)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 2.2 Meson (Alternative)

```bash
meson setup build --buildtype=release
ninja -C build
```

### 2.3 Zig (Alternative)

```bash
zig build -Doptimize=ReleaseSafe
```

### 2.4 Kernel Module

```bash
cd kernel
make
sudo make install
```

### 2.5 Rust Crates

```bash
cd safe
cargo build --release
cargo test
```

### 2.6 Adding New Components

**CMake**: Add to root `CMakeLists.txt`:
```cmake
add_subdirectory(new_component)
```

**Meson**: Add to root `meson.build`:
```meson
subdir('new_component')
```

**Zig**: Add to `build.zig`:
```zig
const new_component = b.addExecutable(.{ ... });
```

---

## 3. Coding Standards

### 3.1 C (Kernel and Userspace)

- **Standard**: C11
- **Style**: Linux kernel style for kernel/, Google style for userspace/
- **Naming**: `snake_case` for functions, `UPPER_SNAKE_CASE` for macros
- **Prefix**: `mvgal_` for all public symbols
- **Error handling**: Return negative errno values (kernel), `mvgal_error_t` (userspace)
- **Comments**: Kernel-doc format for kernel, Doxygen for userspace

```c
/**
 * mvgal_gpu_enable - Enable a GPU for aggregation
 * @dev: Main MVGAL device
 * @gpu_index: Index of GPU to enable
 *
 * Returns 0 on success, negative errno on failure.
 */
int mvgal_gpu_enable(struct mvgal_device *dev, u32 gpu_index)
{
    if (!dev || gpu_index >= MVGAL_UAPI_MAX_GPUS)
        return -EINVAL;
    // ...
}
```

### 3.2 C++ (Runtime Daemon)

- **Standard**: C++20
- **Style**: Google C++ style
- **Naming**: `snake_case` for functions, `CamelCase` for classes
- **Headers**: `.hpp` for headers, `.cpp` for implementation
- **RAII**: Use smart pointers, avoid raw `new`/`delete`
- **Exceptions**: Use for error handling in daemon

```cpp
class DeviceRegistry {
 public:
  DeviceRegistry();
  ~DeviceRegistry();

  std::vector<GpuInfo> EnumerateGpus();
  bool RegisterGpu(const GpuInfo& info);

 private:
  std::mutex mutex_;
  std::vector<GpuInfo> gpus_;
};
```

### 3.3 Rust (Safety Crates)

- **Edition**: 2021
- **Style**: `rustfmt` defaults
- **Naming**: `snake_case` for functions, `CamelCase` for types
- **Safety**: Minimize `unsafe` blocks, document safety invariants
- **Testing**: Unit tests in same file, integration tests in `tests/`

```rust
/// Manages cross-device fence lifecycle.
pub struct FenceManager {
    fences: HashMap<u64, FenceState>,
    next_id: AtomicU64,
}

impl FenceManager {
    /// Creates a new fence manager.
    pub fn new() -> Self {
        Self {
            fences: HashMap::new(),
            next_id: AtomicU64::new(1),
        }
    }
}
```

### 3.4 Go (REST Server, Exporter)

- **Version**: 1.21+
- **Style**: `gofmt` defaults
- **Naming**: `CamelCase` for exported, `camelCase` for unexported
- **Errors**: Return errors, don't panic

---

## 4. Kernel Module Development

### 4.1 Adding a New Vendor Driver

1. Create `kernel/vendors/mvgal_<vendor>.c`
2. Implement `struct mvgal_vendor_ops`:

```c
static const struct mvgal_vendor_ops mvgal_<vendor>_ops = {
    .init = mvgal_<vendor>_init,
    .fini = mvgal_<vendor>_fini,
    .submit_cs = mvgal_<vendor>_submit_cs,
    .alloc_vram = mvgal_<vendor>_alloc_vram,
    .free_vram = mvgal_<vendor>_free_vram,
    .wait_idle = mvgal_<vendor>_wait_idle,
    .set_power_state = mvgal_<vendor>_set_power_state,
    .export_dmabuf = mvgal_<vendor>_export_dmabuf,
    .import_dmabuf = mvgal_<vendor>_import_dmabuf,
    .query_utilization = mvgal_<vendor>_query_utilization,
};
```

3. Register in `mvgal_device.c`:

```c
case PCI_VENDOR_ID_<VENDOR>:
    dev->ops = &mvgal_<vendor>_ops;
    break;
```

### 4.2 Adding a New IOCTL

1. Define ioctl number in `kernel/mvgal_uapi.h`:

```c
#define MVGAL_IOCTL_NEW_FEATURE \
    _IOWR(MVGAL_IOCTL_MAGIC, 15, struct mvgal_new_feature)
```

2. Define data structure:

```c
struct mvgal_new_feature {
    __u32 param1;
    __u64 param2;
    __u32 result;
};
```

3. Implement handler in `kernel/mvgal_main.c`:

```c
case MVGAL_IOCTL_NEW_FEATURE:
    return mvgal_ioctl_new_feature(dev, (void __user *)arg);
```

4. Add userspace wrapper in `src/userspace/api/mvgal_api.c`

### 4.3 Adding Sysfs Attributes

```c
static ssize_t new_attr_show(struct device *dev,
                             struct device_attribute *attr,
                             char *buf)
{
    struct mvgal_device *mvgal = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%u\n", mvgal->new_value);
}

static DEVICE_ATTR_RO(new_attr);

// Register in mvgal_sysfs_init:
device_create_file(mvgal->class_dev, &dev_attr_new_attr);
```

---

## 5. Userspace Development

### 5.1 Adding a Scheduling Strategy

1. Create `src/userspace/scheduler/strategy/<name>.c`
2. Implement strategy functions:

```c
int mvgal_scheduler_<name>_init(struct mvgal_scheduler *sched);
int mvgal_scheduler_<name>_submit(struct mvgal_scheduler *sched,
                                   struct mvgal_workload *wl);
int mvgal_scheduler_<name>_fini(struct mvgal_scheduler *sched);
```

3. Register in `src/userspace/scheduler/scheduler.c`:

```c
case MVGAL_STRATEGY_<NAME>:
    return mvgal_scheduler_<name>_init(sched);
```

### 5.2 Adding an API Interception Layer

1. Create `src/userspace/intercept/<api>_intercept.c`
2. Implement interception functions:

```c
// LD_PRELOAD approach
API_RETURN api_function_intercept(API_ARGS) {
    // Pre-processing
    API_RETURN result = original_api_function(API_ARGS);
    // Post-processing
    return result;
}
```

3. Build as shared library
4. Document in `docs/`

### 5.3 Adding a CLI Tool

1. Create `tools/mvgal-<name>.c`
2. Use existing patterns from `tools/mvgal-info.c`
3. Add to `tools/CMakeLists.txt`

---

## 6. Testing

### 6.1 C Unit Tests

```bash
cd tests
make test
```

Tests are in `tests/unit/` and `tests/integration/`.

### 6.2 Rust Tests

```bash
cd safe
cargo test
```

### 6.3 Benchmarks

```bash
cd tests/benchmarks
make
./run_benchmarks.sh
```

### 6.4 Adding Tests

**C tests**: Add to `tests/unit/test_<module>.c`:

```c
static void test_new_function(void) {
    // Arrange
    // Act
    // Assert
    CU_ASSERT_TRUE(result);
}
```

**Rust tests**: Add to same file:

```rust
#[cfg(test)]
mod tests {
    #[test]
    fn test_new_function() {
        // Arrange
        // Act
        // Assert
    }
}
```

---

## 7. Debugging

### 7.1 Kernel Module

```bash
# Enable debug logging
sudo modprobe mvgal debug=1

# View kernel messages
dmesg | grep mvgal

# Use ftrace
echo function > /sys/kernel/debug/tracing/current_tracer
echo mvgal_* > /sys/kernel/debug/tracing/set_ftrace_filter
```

### 7.2 Userspace

```bash
# Run daemon with debug
sudo mvgald --foreground --log-level debug

# Use gdb
gdb --args mvgald --foreground

# Use valgrind
valgrind --leak-check=full mvgald --foreground
```

### 7.3 Vulkan Layer

```bash
export VK_LAYER_MVGAL_DEBUG=1
export VK_LAYER_MVGAL_LOG_FILE=/tmp/mvgal_vulkan.log
```

### 7.4 IPC Debugging

```bash
# Check socket
ls -l /var/run/mvgal/mvgald.sock

# Test IPC
mvgal-config strategy get --debug
```

---

## 8. Contributing

### 8.1 Workflow

1. Fork the repository
2. Create a feature branch
3. Make changes following coding standards
4. Add tests for new functionality
5. Submit a pull request

### 8.2 Pull Request Requirements

- [ ] Code follows project coding standards
- [ ] Tests pass (`make test`, `cargo test`)
- [ ] Documentation updated
- [ ] CHANGELOG.md updated
- [ ] Commit messages follow Conventional Commits

### 8.3 Commit Message Format

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

---

## 9. Release Process

### 9.1 Version Numbering

`MAJOR.MINOR.PATCH`

- **MAJOR**: Breaking changes
- **MINOR**: New features (backward compatible)
- **PATCH**: Bug fixes

### 9.2 Release Steps

1. Update version in `CMakeLists.txt`, `meson.build`, `build.zig`
2. Update `CHANGELOG.md`
3. Update `docs/STATUS.md`
4. Tag release: `git tag -a v0.2.2 -m "Release v0.2.2"`
5. Push tag: `git push origin v0.2.2`
6. Build packages: `make packages`
7. Upload to package repositories

### 9.3 Package Building

```bash
# Debian/Ubuntu
dpkg-buildpackage -us -uc

# Fedora/RHEL
rpmbuild -ba packaging/mvgal.spec

# Arch Linux
makepkg -si
```

---

## 10. Architecture Decisions

### 10.1 Kernel Module as Observer, Not Virtualizer

**Decision**: The kernel module observes GPU topology and coordinates policy, but does not virtualize GPU commands.

**Rationale**: Avoids claiming PCI devices from vendor drivers, maintains compatibility with existing vendor drivers, reduces kernel complexity.

**Alternatives considered**: Full GPU virtualization (like SR-IOV), command interception in kernel.

### 10.2 Userspace Scheduling

**Decision**: All scheduling logic runs in userspace (mvgald).

**Rationale**: Easier to update, debug, and extend. Kernel module remains stable and minimal.

### 10.3 Implicit Vulkan Layer

**Decision**: Use Vulkan implicit layer for automatic interception.

**Rationale**: No application changes required, works with all Vulkan applications, standard Vulkan mechanism.

### 10.4 LD_PRELOAD for API Wrapping

**Decision**: Use LD_PRELOAD for CUDA, OpenCL, OpenGL interception.

**Rationale**: No application changes required, works with existing binaries, simple implementation.

### 10.5 Rust for Safety-Critical Components

**Decision**: Use Rust for fence management, memory safety, and capability modeling.

**Rationale**: Memory safety guarantees, strong type system, excellent FFI with C.
