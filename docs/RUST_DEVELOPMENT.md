# MVGAL Rust Development Guide

![Rust](https://img.shields.io/badge/Language-Rust-%23DEA584.svg?style=for-the-badge&logo=rust&logoColor=white)
![Status](https://img.shields.io/badge/status-100%25_Complete-%234CAF50?style=for-the-badge)
![Version](https://img.shields.io/badge/version-0.2.0-%2376B900?style=for-the-badge)

**Project:** Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
**Document Version:** 1.0
**Last Updated:** May 01, 2026

---

## 📋 Overview

MVGAL includes **Rust-based safety-critical components** that provide memory-safe abstractions for cross-GPU operations. These components are organized as a **Cargo workspace** with fully-featured C FFI (Foreign Function Interface) interfaces, allowing them to be called from the C-based MVGAL core.

### Why Rust?

1. **Memory Safety**: Rust's ownership model prevents common bugs like use-after-free, buffer overflows, and data races
2. **Thread Safety**: Compile-time guarantees for thread-safe operations
3. **Performance**: Zero-cost abstractions with performance comparable to C
4. **Interoperability**: Excellent C FFI support via `extern "C"` and `#[no_mangle]`
5. **Testing**: Built-in test framework with `cargo test`

### Rust Components Summary

| Component | Lines | Purpose | Status |
|-----------|-------|---------|--------|
| **fence_manager** | ~248 | Cross-device fence lifecycle management | ✅ 100% |
| **memory_safety** | ~230 | Safe memory allocation tracking | ✅ 100% |
| **capability_model** | ~260 | GPU capability normalization | ✅ 100% |
| **Total** | **~748+** | Safety-critical subsystems | ✅ 100% |

---

## 🚀 Quick Start

### Prerequisites

```bash
# Install Rust (minimum version 1.75)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

# Verify installation
rustc --version  # Should be >= 1.75
cargo --version

# Install required dependencies
# Ubuntu/Debian
sudo apt install cmake gcc pkg-config libssl-dev

# Fedora/RHEL
sudo dnf install cmake gcc pkg-config openssl-devel

# Arch Linux
sudo pacman -S cmake gcc pkgconf openssl
```

### Build All Rust Components

```bash
# From the project root
cd /path/to/mvgal

# Build all Rust crates in the workspace
cd safe
cargo build --release

# Or build individual crates
cargo build --release -p fence_manager
cargo build --release -p memory_safety
cargo build --release -p capability_model

# Run tests
cargo test
cargo test --release
```

### Build with Rust Integration (Full Project)

```bash
# The main build.sh script handles Rust components
cd mvgal
./build.sh

# Or manually with CMake
mkdir -p build && cd build
cmake .. -DWITH_RUST=ON -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 🏗️ Workspace Structure

### Directory Layout

```
mvgal/
├── Cargo.toml                     # Workspace root manifest
├── Cargo.lock                     # Workspace lockfile
│
├── safe/                           # Safety-critical crates (workspace members)
│   ├── fence_manager/
│   │   ├── Cargo.toml            # Fence manager crate manifest
│   │   └── src/
│   │       └── lib.rs           # Main implementation
│   │
│   ├── memory_safety/
│   │   ├── Cargo.toml            # Memory safety crate manifest
│   │   └── src/
│   │       └── lib.rs           # Main implementation
│   │
│   └── capability_model/
│       ├── Cargo.toml            # Capability model crate manifest
│       └── src/
│           └── lib.rs           # Main implementation
│
└── runtime/
    └── safe/                      # Runtime bindings
        ├── Cargo.toml            # Runtime crate manifest
        ├── lib.rs               # Runtime entry point
        ├── fence_manager.rs     # Fence manager bindings
        ├── memory_safety.rs      # Memory safety bindings
        └── capability_model.rs   # Capability model bindings
```

### Workspace Configuration

**Root `Cargo.toml`:**
```toml
[workspace]
members = [
    "safe/fence_manager",
    "safe/memory_safety",
    "safe/capability_model",
    "runtime/safe",
]
resolver = "2"

[workspace.package]
version = "0.2.1"
edition = "2021"
rust-version = "1.75"
authors = ["MVGAL Contributors"]
license = "MIT OR Apache-2.0"
repository = "https://github.com/TheCreateGM/mvgal"
description = "Multi-Vendor GPU Aggregation Layer for Linux — Rust safety-critical subsystems"

[workspace.dependencies]
tokio = { version = "1", features = ["full"] }
serde = { version = "1", features = ["derive"] }
serde_json = "1"
cbindgen = "0.26"
```

Each crate has its own `Cargo.toml` specifying its specific dependencies.

---

## 📦 Component Documentation

### 1. Fence Manager (`safe/fence_manager`)

**Purpose:** Cross-device fence lifecycle management for MVGAL.

This crate provides memory-safe wrappers for cross-GPU fence operations, exposed via a C FFI interface for use by the C++ runtime daemon.

#### Features

- **Fence Creation**: Create fences associated with specific GPUs
- **State Management**: State machine with 5 states (Pending, Submitted, Signalled, TimedOut, Reset)
- **Timestamp Tracking**: Monotonic nanosecond timestamps for creation and signaling
- **Thread Safety**: Mutex-protected registry with atomic ID generation
- **FFI Interface**: Full C interface for integration with C code

#### Types

```rust
/// Opaque fence handle returned to C callers.
pub type MvgalFenceHandle = u64;

/// Fence state machine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
pub enum FenceState {
    Pending   = 0,
    Submitted = 1,
    Signalled = 2,
    TimedOut  = 3,
    Reset     = 4,
}
```

#### FFI Functions

```rust
// All functions are marked with #[no_mangle] and extern "C"

/// Allocate a new fence for the given GPU index.
/// Returns a non-zero handle on success, 0 on failure.
pub extern "C" fn mvgal_fence_create(gpu_index: u32) -> MvgalFenceHandle

/// Submit a fence (Pending → Submitted).
/// Returns 1 on success, 0 otherwise.
pub extern "C" fn mvgal_fence_submit(handle: MvgalFenceHandle) -> i32

/// Signal a fence (Submitted/Pending → Signalled).
/// Returns 1 on success, 0 otherwise.
pub extern "C" fn mvgal_fence_signal(handle: MvgalFenceHandle) -> i32

/// Query the state of a fence.
/// Returns the FenceState value, or -1 if the handle is invalid.
pub extern "C" fn mvgal_fence_state(handle: MvgalFenceHandle) -> i32

/// Reset a signalled fence.
/// Returns 1 on success, 0 otherwise.
pub extern "C" fn mvgal_fence_reset(handle: MvgalFenceHandle) -> i32

/// Destroy a fence and release its resources.
/// Returns 1 on success, 0 if the handle was not found.
pub extern "C" fn mvgal_fence_destroy(handle: MvgalFenceHandle) -> i32
```

#### Usage from C

```c
#include <stdint.h>

uint64_t handle = mvgal_fence_create(0);  // GPU 0
if (handle != 0) {
    // Submit the fence
    mvgal_fence_submit(handle);
    
    // Signal the fence
    mvgal_fence_signal(handle);
    
    // Check state
    int state = mvgal_fence_state(handle);
    // state will be: 0=Pending, 1=Submitted, 2=Signalled, 3=TimedOut, 4=Reset
    
    // Reset the fence
    mvgal_fence_reset(handle);
    
    // Destroy the fence
    mvgal_fence_destroy(handle);
}
```

#### Unit Tests

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fence_lifecycle() {
        let h = mvgal_fence_create(0);
        assert_ne!(h, 0);
        assert_eq!(mvgal_fence_state(h), FenceState::Pending as i32);
        assert_eq!(mvgal_fence_submit(h), 1);
        assert_eq!(mvgal_fence_state(h), FenceState::Submitted as i32);
        assert_eq!(mvgal_fence_signal(h), 1);
        assert_eq!(mvgal_fence_state(h), FenceState::Signalled as i32);
        assert_eq!(mvgal_fence_reset(h), 1);
        assert_eq!(mvgal_fence_state(h), FenceState::Reset as i32);
        assert_eq!(mvgal_fence_destroy(h), 1);
        assert_eq!(mvgal_fence_state(h), -1);
    }

    #[test]
    fn test_invalid_handle() {
        assert_eq!(mvgal_fence_state(0xDEADBEEF), -1);
        assert_eq!(mvgal_fence_signal(0xDEADBEEF), 0);
        assert_eq!(mvgal_fence_destroy(0xDEADBEEF), 0);
    }

    #[test]
    fn test_multiple_fences() {
        let h1 = mvgal_fence_create(0);
        let h2 = mvgal_fence_create(1);
        assert_ne!(h1, h2);
        assert_eq!(mvgal_fence_signal(h1), 1);
        assert_eq!(mvgal_fence_state(h1), FenceState::Signalled as i32);
        assert_eq!(mvgal_fence_state(h2), FenceState::Pending as i32);
        mvgal_fence_destroy(h1);
        mvgal_fence_destroy(h2);
    }
}
```

---

### 2. Memory Safety (`safe/memory_safety`)

**Purpose:** Safe wrappers for cross-GPU memory operations in MVGAL.

This crate provides memory-safe tracking of allocations across multiple GPUs, with reference counting and placement tracking.

#### Features

- **Allocation Tracking**: Track memory allocations with unique handles
- **Reference Counting**: Automatic cleanup when reference count reaches zero
- **Placement Tracking**: Support for System RAM, GPU VRAM, and Mirrored placements
- **DMA-BUF Association**: Associate DMA-BUF file descriptors with allocations
- **Statistics**: Total bytes tracking per placement type
- **Thread Safety**: Mutex-protected registry with atomic ID generation

#### Types

```rust
pub type MvgalAllocHandle = u64;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
pub enum MemoryPlacement {
    SystemRam = 0,
    GpuVram   = 1,
    Mirrored  = 2,
}
```

#### FFI Functions

```rust
/// Track a new cross-GPU memory allocation.
/// Returns 0 on invalid placement, non-zero handle on success.
pub extern "C" fn mvgal_mem_track(
    size_bytes: u64,
    placement: u32,      // 0=SystemRam, 1=GpuVram, 2=Mirrored
    gpu_index: u32
) -> MvgalAllocHandle

/// Increment the reference count.
/// Returns 1 on success, 0 on invalid handle.
pub extern "C" fn mvgal_mem_retain(handle: MvgalAllocHandle) -> i32

/// Decrement the reference count. Frees the record when it reaches zero.
/// Returns 1 on success, 0 on invalid handle.
pub extern "C" fn mvgal_mem_release(handle: MvgalAllocHandle) -> i32

/// Associate a DMA-BUF fd with an allocation. Pass -1 to clear.
/// Returns 1 on success, 0 on invalid handle.
pub extern "C" fn mvgal_mem_set_dmabuf(
    handle: MvgalAllocHandle,
    fd: i32
) -> i32

/// Query the size in bytes of a tracked allocation. Returns 0 if invalid.
pub extern "C" fn mvgal_mem_size(handle: MvgalAllocHandle) -> u64

/// Query the placement. Returns -1 if the handle is invalid.
pub extern "C" fn mvgal_mem_placement(handle: MvgalAllocHandle) -> i32

/// Return total bytes tracked in system RAM.
pub extern "C" fn mvgal_mem_total_system_bytes() -> u64

/// Return total bytes tracked in GPU VRAM.
pub extern "C" fn mvgal_mem_total_gpu_bytes() -> u64
```

#### Usage from C

```c
#include <stdint.h>

// Allocate 1MB in GPU VRAM for GPU 0
uint64_t mem_handle = mvgal_mem_track(1024 * 1024, 1, 0);

if (mem_handle != 0) {
    // Increment reference count
    mvgal_mem_retain(mem_handle);
    
    // Get size
    uint64_t size = mvgal_mem_size(mem_handle);
    // size == 1024 * 1024
    
    // Associate DMA-BUF fd
    int fd = open("/dev/dma_heap/system", O_RDWR);
    mvgal_mem_set_dmabuf(mem_handle, fd);
    
    // Release references
    mvgal_mem_release(mem_handle);
    mvgal_mem_release(mem_handle);  // This will free the allocation
    
    // Check totals
    uint64_t system_bytes = mvgal_mem_total_system_bytes();
    uint64_t gpu_bytes = mvgal_mem_total_gpu_bytes();
}
```

#### Unit Tests

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_alloc_lifecycle() {
        let h = mvgal_mem_track(1024 * 1024, 1, 0);
        assert_ne!(h, 0);
        assert_eq!(mvgal_mem_size(h), 1024 * 1024);
        assert_eq!(mvgal_mem_placement(h), MemoryPlacement::GpuVram as i32);
        assert_eq!(mvgal_mem_retain(h), 1);
        assert_eq!(mvgal_mem_release(h), 1);
        assert_eq!(mvgal_mem_release(h), 1);  // Final release frees
        assert_eq!(mvgal_mem_size(h), 0);  // Invalid after free
    }

    #[test]
    fn test_dmabuf_association() {
        let h = mvgal_mem_track(4096, 1, 1);
        assert_ne!(h, 0);
        assert_eq!(mvgal_mem_set_dmabuf(h, 5), 1);
        assert_eq!(mvgal_mem_set_dmabuf(h, -1), 1);  // Clear
        mvgal_mem_release(h);
    }

    #[test]
    fn test_invalid_placement() {
        let h = mvgal_mem_track(4096, 99, 0);  // Invalid placement
        assert_eq!(h, 0);
    }
}
```

---

### 3. Capability Model (`safe/capability_model`)

**Purpose:** GPU capability normalization and comparison for MVGAL.

This crate provides functionality to normalize and compare GPU capabilities across different vendors, compute aggregate capabilities for multi-GPU systems, and serialize capability information to JSON.

#### Features

- **Vendor Enumeration**: Support for AMD, NVIDIA, Intel, Moore Threads
- **Capability Aggregation**: Combine capabilities from multiple GPUs
- **Tier Classification**: Classify systems as Full, ComputeOnly, or Mixed
- **API Flags**: Track supported APIs (Vulkan, OpenCL, CUDA, SYCL, OpenGL)
- **JSON Serialization**: Serialize capability information to JSON strings
- **Thread Safety**: Designed for thread-safe operation

#### Types

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(C)]
pub enum GpuVendor {
    Unknown      = 0,
    Amd          = 1,
    Nvidia       = 2,
    Intel        = 3,
    MooreThreads = 4,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(C)]
pub enum CapabilityTier {
    Full        = 0,    // All GPUs support graphics and compute
    ComputeOnly = 1,    // All GPUs support compute only
    Mixed       = 2,    // Mixed capabilities
}

pub mod api_flags {
    pub const VULKAN: u32 = 1 << 0;
    pub const OPENCL: u32 = 1 << 1;
    pub const CUDA:   u32 = 1 << 2;
    pub const SYCL:   u32 = 1 << 3;
    pub const OPENGL: u32 = 1 << 4;
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[repr(C)]
pub struct GpuCapability {
    pub vendor:             GpuVendor,
    pub device_id:          u32,
    pub vram_bytes:         u64,
    pub vram_bandwidth_gbps: f32,
    pub compute_units:      u32,
    pub api_flags:          u32,
    pub vulkan_major:       u32,
    pub vulkan_minor:       u32,
    pub pcie_gen:           u32,
    pub pcie_lanes:         u32,
    pub supports_graphics:  bool,
    pub supports_compute:   bool,
    pub supports_display:   bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[repr(C)]
pub struct AggregateCapability {
    pub gpu_count:                  u32,
    pub total_vram_bytes:           u64,
    pub total_compute_units:        u32,
    pub total_vram_bandwidth_gbps:  f32,
    pub min_vulkan_major:           u32,
    pub min_vulkan_minor:           u32,
    pub api_union:                  u32,
    pub api_intersection:           u32,
    pub tier:                       CapabilityTier,
}
```

#### FFI Functions

```rust
pub type MvgalCapHandle = *mut AggregateCapability;

/// Compute an aggregate capability profile from an array of per-GPU descriptors.
/// Returns a handle that must be freed with mvgal_cap_free.
pub unsafe extern "C" fn mvgal_cap_compute(
    gpus: *const GpuCapability,
    count: u32,
) -> MvgalCapHandle

/// Free an aggregate capability handle.
pub unsafe extern "C" fn mvgal_cap_free(handle: MvgalCapHandle)

/// Return the total VRAM in bytes.
pub unsafe extern "C" fn mvgal_cap_total_vram(handle: MvgalCapHandle) -> u64

/// Return the capability tier. Returns -1 on null.
pub unsafe extern "C" fn mvgal_cap_tier(handle: MvgalCapHandle) -> i32

/// Serialize to JSON into buf. Returns bytes written or -1 on error.
/// buf must point to at least buf_len bytes.
pub unsafe extern "C" fn mvgal_cap_to_json(
    handle: MvgalCapHandle,
    buf: *mut c_char,
    buf_len: usize,
) -> i32
```

#### Usage from C

```c
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Define the C-compatible types
typedef enum {
    GPU_VENDOR_UNKNOWN = 0,
    GPU_VENDOR_AMD = 1,
    GPU_VENDOR_NVIDIA = 2,
    GPU_VENDOR_INTEL = 3,
    GPU_VENDOR_MOORE_THREADS = 4
} GpuVendor;

typedef enum {
    CAPABILITY_TIER_FULL = 0,
    CAPABILITY_TIER_COMPUTE_ONLY = 1,
    CAPABILITY_TIER_MIXED = 2
} CapabilityTier;

typedef struct {
    GpuVendor vendor;
    uint32_t device_id;
    uint64_t vram_bytes;
    float vram_bandwidth_gbps;
    uint32_t compute_units;
    uint32_t api_flags;
    uint32_t vulkan_major;
    uint32_t vulkan_minor;
    uint32_t pcie_gen;
    uint32_t pcie_lanes;
    bool supports_graphics;
    bool supports_compute;
    bool supports_display;
} GpuCapability;

// Example usage
int main() {
    GpuCapability gpus[2];
    
    // Initialize GPU 0 (AMD)
    gpus[0].vendor = GPU_VENDOR_AMD;
    gpus[0].vram_bytes = 8ULL * 1024 * 1024 * 1024;  // 8GB
    gpus[0].api_flags = (1 << 0) | (1 << 1);  // Vulkan | OpenCL
    gpus[0].supports_graphics = true;
    gpus[0].supports_compute = true;
    
    // Initialize GPU 1 (NVIDIA)
    gpus[1].vendor = GPU_VENDOR_NVIDIA;
    gpus[1].vram_bytes = 12ULL * 1024 * 1024 * 1024;  // 12GB
    gpus[1].api_flags = (1 << 0) | (1 << 1) | (1 << 2);  // Vulkan | OpenCL | CUDA
    gpus[1].supports_graphics = true;
    gpus[1].supports_compute = true;
    
    // Compute aggregate capability
    void* agg = mvgal_cap_compute(gpus, 2);
    
    if (agg != NULL) {
        // Get total VRAM
        uint64_t total_vram = mvgal_cap_total_vram(agg);
        // total_vram == 20GB
        
        // Get tier
        int tier = mvgal_cap_tier(agg);
        // tier == CAPABILITY_TIER_FULL (0)
        
        // Serialize to JSON
        char json_buf[1024];
        int json_len = mvgal_cap_to_json(agg, json_buf, sizeof(json_buf));
        if (json_len > 0) {
            printf("JSON: %.*s\n", json_len, json_buf);
        }
        
        // Free the aggregate
        mvgal_cap_free(agg);
    }
    
    return 0;
}
```

#### Unit Tests

```rust
#[cfg(test)]
mod tests {
    use super::*;

    fn make_gpu(vendor: GpuVendor, vram: u64, apis: u32, graphics: bool, compute: bool) -> GpuCapability {
        GpuCapability {
            vendor, device_id: 0x1234, vram_bytes: vram,
            vram_bandwidth_gbps: 500.0, compute_units: 64,
            api_flags: apis, vulkan_major: 1, vulkan_minor: 3,
            pcie_gen: 4, pcie_lanes: 16,
            supports_graphics: graphics, supports_compute: compute,
            supports_display: graphics,
        }
    }

    #[test]
    fn test_aggregate_full_tier() {
        let gpus = vec![
            make_gpu(GpuVendor::Amd,    8 << 30, api_flags::VULKAN | api_flags::OPENCL, true, true),
            make_gpu(GpuVendor::Nvidia, 8 << 30, api_flags::VULKAN | api_flags::OPENCL | api_flags::CUDA, true, true),
        ];
        let agg = AggregateCapability::from_gpus(&gpus);
        assert_eq!(agg.gpu_count, 2);
        assert_eq!(agg.total_vram_bytes, 16u64 << 30);
        assert_eq!(agg.tier, CapabilityTier::Full);
        assert_eq!(agg.api_intersection, api_flags::VULKAN | api_flags::OPENCL);
        assert!(agg.api_union & api_flags::CUDA != 0);
    }

    #[test]
    fn test_aggregate_compute_only_tier() {
        let gpus = vec![
            make_gpu(GpuVendor::Nvidia, 16 << 30, api_flags::CUDA | api_flags::OPENCL, false, true),
            make_gpu(GpuVendor::Intel,   8 << 30, api_flags::OPENCL | api_flags::SYCL, false, true),
        ];
        let agg = AggregateCapability::from_gpus(&gpus);
        assert_eq!(agg.tier, CapabilityTier::ComputeOnly);
    }

    #[test]
    fn test_empty_gpus() {
        let agg = AggregateCapability::from_gpus(&[]);
        assert_eq!(agg.gpu_count, 0);
        assert_eq!(agg.total_vram_bytes, 0);
    }

    #[test]
    fn test_json_serialization() {
        let gpus = vec![make_gpu(GpuVendor::Amd, 8 << 30, api_flags::VULKAN, true, true)];
        let agg = AggregateCapability::from_gpus(&gpus);
        let json = agg.to_json();
        assert!(json.contains("gpu_count"));
        assert!(json.contains("total_vram_bytes"));
    }
}
```

---

## 🔧 Development Guidelines

### Code Style

Follow Rust's standard style conventions:

- **Naming**: Use `snake_case` for variables, functions, and modules
- **Types**: Use `PascalCase` for types, traits, and enums
- **Constants**: Use `SCREAMING_SNAKE_CASE` for constants
- **Line Length**: Limit to 100 characters (Clippy default)
- **Documentation**: Use `///` for doc comments on all public items
- **Error Handling**: Prefer `Result` and `Option` over panics

### Testing

All public functions should have unit tests:

```bash
# Run all tests
cargo test

# Run tests for a specific crate
cargo test -p fence_manager

# Run tests with coverage (requires cargo-tarpaulin)
cargo tarpaulin

# Run tests with verbose output
cargo test -- --nocapture
```

### FFI Best Practices

1. **Always use `#[no_mangle]`** for exported functions
2. **Use `extern "C"`** for C-compatible ABI
3. **Use primitive types** in FFI signatures (u8, u16, u32, u64, i8, i16, i32, i64, f32, f64, *const T, *mut T)
4. **Document safety requirements** with `/// # Safety` comments
5. **Use `repr(C)`** on enums and structs that cross FFI boundaries
6. **Validate inputs** in FFI functions (check for null pointers, valid enums, etc.)
7. **Return error codes** for failure cases (typically 0 or -1 for simple functions)

### Documentation

Document all public items with Rustdoc:

```rust
/// Allocates a new fence for the given GPU index.
///
/// # Arguments
/// * `gpu_index` - The index of the GPU to associate with this fence
///
/// # Returns
/// A non-zero handle on success, 0 on failure.
///
/// # Safety
/// Thread-safe. The returned handle must be freed with `mvgal_fence_destroy`.
#[no_mangle]
pub extern "C" fn mvgal_fence_create(gpu_index: u32) -> MvgalFenceHandle {
    // implementation
}
```

---

## 🛠️ Build System Integration

### CMake Integration

The Rust components are integrated into the main CMake build system:

```cmake
# In CMakeLists.txt
find_package(Rust REQUIRED)

# Build Rust components
add_custom_target(rust_components ALL
    COMMAND cargo build --release --workspace
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/safe
    COMMENT "Building Rust safety components"
)

# Link Rust libraries with C targets
target_link_libraries(mvgal_core
    PRIVATE
    ${CMAKE_SOURCE_DIR}/safe/target/release/libfence_manager.a
    ${CMAKE_SOURCE_DIR}/safe/target/release/libmemory_safety.a
    ${CMAKE_SOURCE_DIR}/safe/target/release/libcapability_model.a
)
```

### Bindgen Integration

For automatic generation of C header files from Rust:

```bash
# Install bindgen
cargo install bindgen

# Generate headers
bindgen --lang c safe/fence_manager/src/lib.rs --output include/mvgal/mvgal_fence_manager.h
bindgen --lang c safe/memory_safety/src/lib.rs --output include/mvgal/mvgal_memory_safety.h
bindgen --lang c safe/capability_model/src/lib.rs --output include/mvgal/mvgal_capability_model.h
```

---

## 🔄 Workflow

### Developing a New Rust Component

1. **Create the crate:**
   ```bash
   cd safe
   cargo new --lib new_component
   ```

2. **Add to workspace:**
   Edit `Cargo.toml` and add the new crate to the `members` array.

3. **Implement FFI:**
   - Create your Rust types and functions
   - Mark public FFI functions with `#[no_mangle]` and `extern "C"`
   - Use `repr(C)` on structs and enums
   - Document with Rustdoc comments

4. **Write tests:**
   Add unit tests in a `#[cfg(test)]` module.

5. **Build and test:**
   ```bash
   cargo build --release
   cargo test
   ```

6. **Integrate with C:**
   - Create header files for C consumption
   - Link the Rust library with your C targets
   - Write C code to call the Rust functions

7. **Update documentation:**
   Add documentation to this file (`RUST_DEVELOPMENT.md`).

---

## 📚 References

### Rust Resources

- [The Rust Programming Language](https://doc.rust-lang.org/book/) - Official Rust book
- [Rust by Example](https://doc.rust-lang.org/rust-by-example/) - Practical examples
- [Rust Standard Library](https://doc.rust-lang.org/std/) - API documentation
- [Rust FFI Guide](https://doc.rust-lang.org/nomicon/ffi.html) - Nomicon FFI chapter
- [Rust Embedded](https://rust-embedded.org/) - Embedded and low-level Rust

### MVGAL Resources

- [README.md](../README.md) - Main project documentation
- [ARCHITECTURE_RESEARCH.md](ARCHITECTURE_RESEARCH.md) - Architecture analysis
- [STATUS.md](STATUS.md) - Current project status
- [PROGRESS.md](PROGRESS.md) - Development progress

---

## 🎯 Future Work

### Potential Rust Components

The following components could benefit from Rust implementations:

| Component | Description | Priority | Status |
|-----------|-------------|----------|--------|
| Vulkan Layer | Safe Vulkan API wrapper | High | ⚠️ Not started |
| Memory Manager | Full memory management in Rust | Medium | ⚠️ Not started |
| Scheduler | Workload scheduler in Rust | Medium | ⚠️ Not started |
| IPC Layer | Safe IPC between daemon and clients | Medium | ⚠️ Not started |
| Configuration | Configuration parsing in Rust | Low | ⚠️ Not started |

### Migration Strategy

1. **Start with new components**: Implement new features in Rust first
2. **Replace critical components**: Gradually replace C components with Rust
3. **Maintain FFI**: Keep C FFI interfaces for backward compatibility
4. **Document everything**: Ensure all Rust code is well-documented

---

## 🤝 Contributing

Contributions to the Rust components are welcome! Please:

1. Read this document (`RUST_DEVELOPMENT.md`)
2. Follow the code style guidelines above
3. Write tests for all new functionality
4. Document your code with Rustdoc comments
5. Submit pull requests to the main repository

---

*© 2026 MVGAL Project. Last updated: May 01, 2026. Version 0.2.1 "Health Monitor".*
*Rust components: fence_manager, memory_safety, capability_model - all 100% complete.*
