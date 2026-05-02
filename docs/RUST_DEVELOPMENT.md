# MVGAL Rust Development Guide

**Version:** 0.2.1 | **Rust Edition:** 2021 | **MSRV:** 1.75

---

## Overview

MVGAL uses Rust for safety-critical subsystems where memory safety bugs (use-after-free, data races, buffer overflows) would be catastrophic. The Rust crates are compiled as static libraries and linked into the C/C++ daemon via FFI.

---

## Workspace Structure

```
Cargo.toml                    # Workspace root
safe/
├── fence_manager/
│   ├── Cargo.toml
│   └── src/lib.rs            # ~248 LOC
├── memory_safety/
│   ├── Cargo.toml
│   └── src/lib.rs            # ~230 LOC
└── capability_model/
    ├── Cargo.toml
    └── src/lib.rs            # ~260 LOC
runtime/safe/
├── Cargo.toml
└── lib.rs                    # Runtime entry point
```

---

## Building

```bash
# Build all crates (release)
cargo build --release

# Build individual crates
cargo build --release -p fence_manager
cargo build --release -p memory_safety
cargo build --release -p capability_model

# Run all tests
cargo test

# Run tests for one crate
cargo test -p fence_manager

# Check without building
cargo check --all

# Lint
cargo clippy --all-targets --all-features -- -D warnings

# Format check
cargo fmt --all -- --check

# Format (apply)
cargo fmt --all
```

---

## Crate: `fence_manager`

**Purpose:** Cross-device fence lifecycle management with a formal state machine.

**State machine:**
```
Pending → Submitted → Signalled → Reset → Pending
```

**Key types:**
```rust
pub enum FenceState {
    Pending,
    Submitted,
    Signalled,
    Reset,
}

pub struct Fence {
    gpu_index: u32,
    state: FenceState,
    created_ns: u64,    // monotonic nanoseconds
    signalled_ns: u64,
}
```

**Thread safety:** All operations are protected by a `Mutex<HashMap<u64, Fence>>`. Handles are `u64` opaque IDs.

**C FFI:**
```c
uint64_t mvgal_fence_create(uint32_t gpu_index);
void     mvgal_fence_submit(uint64_t handle);
void     mvgal_fence_signal(uint64_t handle);
uint32_t mvgal_fence_state(uint64_t handle);
         // 0=Pending 1=Submitted 2=Signalled 3=Reset
void     mvgal_fence_reset(uint64_t handle);
void     mvgal_fence_destroy(uint64_t handle);
```

**Tests (3):**
- `test_fence_lifecycle` — create → submit → signal → reset → destroy
- `test_invalid_handle` — operations on invalid handle return gracefully
- `test_multiple_fences` — concurrent fences on different GPUs

---

## Crate: `memory_safety`

**Purpose:** Safe wrappers for cross-GPU memory allocation tracking with reference counting.

**Key types:**
```rust
pub enum MemoryPlacement {
    SystemRam = 0,
    GpuVram   = 1,
    Mirrored  = 2,
}

pub struct MemoryAllocation {
    size:      u64,
    placement: MemoryPlacement,
    refcount:  u32,
    dmabuf_fd: Option<i32>,
}
```

**Thread safety:** `Mutex<HashMap<u64, MemoryAllocation>>` + `AtomicU64` for total byte counters.

**C FFI:**
```c
uint64_t mvgal_mem_track(uint64_t size, uint32_t placement);
void     mvgal_mem_retain(uint64_t handle);
void     mvgal_mem_release(uint64_t handle);  // frees at refcount=0
void     mvgal_mem_set_dmabuf(uint64_t handle, int32_t fd);
uint64_t mvgal_mem_size(uint64_t handle);
uint32_t mvgal_mem_placement(uint64_t handle);
uint64_t mvgal_mem_total_system_bytes(void);
uint64_t mvgal_mem_total_gpu_bytes(void);
```

**Tests (3):**
- `test_alloc_lifecycle` — track → retain → release → release (free)
- `test_dmabuf_association` — set and retrieve DMA-BUF fd
- `test_invalid_placement` — invalid placement value handled gracefully

---

## Crate: `capability_model`

**Purpose:** GPU capability normalization, aggregate profile computation, and JSON serialization.

**Key types:**
```rust
pub enum GpuVendor {
    Amd, Nvidia, Intel, MooreThreads,
}

pub enum CapabilityTier {
    Full,        // all GPUs support same API set
    ComputeOnly, // heterogeneous compute, not all support graphics
    Mixed,       // some graphics-only, some compute-only
}

pub struct GpuCapability {
    vendor:     GpuVendor,
    vram_bytes: u64,
    api_flags:  u32,  // bitmask: Vulkan=1, OpenCL=2, CUDA=4, etc.
}

pub struct AggregateCapability {
    gpus:           Vec<GpuCapability>,
    total_vram:     u64,
    api_union:      u32,  // union of all GPU API flags
    api_intersection: u32, // intersection (common denominator)
    tier:           CapabilityTier,
}
```

**Dependencies:** `serde`, `serde_json` (for JSON serialization)

**C FFI:**
```c
uint64_t    mvgal_cap_compute(const GpuCapability *caps, uint32_t count);
void        mvgal_cap_free(uint64_t handle);
uint64_t    mvgal_cap_total_vram(uint64_t handle);
uint32_t    mvgal_cap_tier(uint64_t handle);
            // 0=Full 1=ComputeOnly 2=Mixed
const char *mvgal_cap_to_json(uint64_t handle);
            // caller must not free; valid until next call
```

**Tests (4):**
- `test_aggregate_full_tier` — all GPUs same vendor → Full tier
- `test_compute_only_tier` — mixed vendors, compute-only → ComputeOnly tier
- `test_empty_gpus` — zero GPUs handled gracefully
- `test_json_serialization` — JSON output is valid and contains expected fields

---

## FFI Safety Guidelines

All `unsafe` blocks in MVGAL Rust code are at FFI boundaries only. Each is annotated with a safety comment:

```rust
/// # Safety
/// `caps` must be a valid pointer to `count` initialized GpuCapability structs.
/// `count` must not exceed the actual array length.
#[no_mangle]
pub unsafe extern "C" fn mvgal_cap_compute(
    caps: *const GpuCapability,
    count: u32,
) -> u64 {
    // SAFETY: caller guarantees caps is valid for count elements
    let slice = unsafe { std::slice::from_raw_parts(caps, count as usize) };
    // ...
}
```

Rules:
1. Never dereference a null pointer — check before use
2. Never store raw pointers across FFI boundary — convert to owned types immediately
3. All public FFI functions must be `#[no_mangle] extern "C"`
4. Use `Option<NonNull<T>>` for nullable pointer parameters
5. Return `u64` handles (opaque IDs) instead of raw pointers

---

## Adding a New Crate

1. Create the crate:
```bash
cargo new --lib safe/my_component
```

2. Add to workspace `Cargo.toml`:
```toml
[workspace]
members = [
    "safe/fence_manager",
    "safe/memory_safety",
    "safe/capability_model",
    "safe/my_component",   # add here
    "runtime/safe",
]
```

3. Set crate type in `safe/my_component/Cargo.toml`:
```toml
[lib]
crate-type = ["staticlib", "rlib"]
```

4. Implement with C FFI:
```rust
#[no_mangle]
pub extern "C" fn my_component_init() -> u32 {
    0 // MVGAL_SUCCESS
}
```

5. Add tests:
```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_init() {
        assert_eq!(my_component_init(), 0);
    }
}
```

6. Link in CMake (`runtime/CMakeLists.txt`):
```cmake
target_link_libraries(mvgald PRIVATE
    "${CARGO_TARGET_DIR}/release/libmy_component.a"
)
```

---

## Workspace Dependencies

```toml
[workspace.dependencies]
tokio     = { version = "1", features = ["full"] }
serde     = { version = "1", features = ["derive"] }
serde_json = "1"
cbindgen  = "0.26"
```

Use workspace dependencies in crates:
```toml
[dependencies]
serde = { workspace = true }
```

---

## Generating C Headers with cbindgen

```bash
# Install cbindgen
cargo install cbindgen

# Generate header for a crate
cbindgen --config safe/fence_manager/cbindgen.toml \
         --crate fence_manager \
         --output include/mvgal/mvgal_fence_ffi.h
```

---

## CI Checks

The CI workflow runs these Rust checks:
```bash
cargo fmt --all -- --check    # formatting
cargo clippy --all-targets --all-features -- -D warnings  # lints
cargo test                    # all unit tests
```
