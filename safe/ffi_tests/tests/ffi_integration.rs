// SPDX-License-Identifier: MIT OR Apache-2.0
//! Cross-crate FFI integration tests.
//!
//! Tests that:
//! 1. All FFI functions work correctly across crates
//! 2. Panics are caught at FFI boundaries (no unwind into C)
//! 3. Invalid handles return proper error codes

use mvgal_capability as capability_model;
use mvgal_capability::{GpuCapability, GpuVendor, CapabilityTier, api_flags};
use mvgal_fence as fence_manager;
use mvgal_fence::FenceState;
use mvgal_memory_safety as memory_safety;
use mvgal_memory_safety::MemoryPlacement;

// ============================================================================
// fence_manager tests
// ============================================================================

#[test]
fn test_fence_lifecycle_cross_crate() {
    let h = fence_manager::mvgal_fence_create(0);
    assert_ne!(h, 0, "fence_create should return non-zero handle");

    assert_eq!(fence_manager::mvgal_fence_state(h), FenceState::Pending as i32);
    assert_eq!(fence_manager::mvgal_fence_submit(h), 1);
    assert_eq!(fence_manager::mvgal_fence_state(h), FenceState::Submitted as i32);
    assert_eq!(fence_manager::mvgal_fence_signal(h), 1);
    assert_eq!(fence_manager::mvgal_fence_state(h), FenceState::Signalled as i32);
    assert_eq!(fence_manager::mvgal_fence_reset(h), 1);
    assert_eq!(fence_manager::mvgal_fence_state(h), FenceState::Reset as i32);
    assert_eq!(fence_manager::mvgal_fence_destroy(h), 1);
    assert_eq!(fence_manager::mvgal_fence_state(h), -1, "destroyed fence should return -1");
}

#[test]
fn test_fence_invalid_handles() {
    // Invalid handles should return error codes, not panics
    let invalid = 0xDEADBEEF_u64;

    assert_eq!(fence_manager::mvgal_fence_state(invalid), -1);
    assert_eq!(fence_manager::mvgal_fence_submit(invalid), 0);
    assert_eq!(fence_manager::mvgal_fence_signal(invalid), 0);
    assert_eq!(fence_manager::mvgal_fence_reset(invalid), 0);
    assert_eq!(fence_manager::mvgal_fence_destroy(invalid), 0);
}

// ============================================================================
// memory_safety tests
// ============================================================================

#[test]
fn test_memory_lifecycle_cross_crate() {
    let h = memory_safety::mvgal_mem_track(1024 * 1024, 1, 0); // GPU VRAM
    assert_ne!(h, 0, "mem_track should return non-zero handle");

    assert_eq!(memory_safety::mvgal_mem_size(h), 1024 * 1024);
    assert_eq!(memory_safety::mvgal_mem_placement(h), MemoryPlacement::GpuVram as i32);
    assert_eq!(memory_safety::mvgal_mem_retain(h), 1);
    assert_eq!(memory_safety::mvgal_mem_release(h), 1);
    assert_eq!(memory_safety::mvgal_mem_release(h), 1);
    assert_eq!(memory_safety::mvgal_mem_size(h), 0, "released handle should return 0");
}

#[test]
fn test_memory_invalid_handles() {
    let invalid = 0xDEADBEEF_u64;

    assert_eq!(memory_safety::mvgal_mem_size(invalid), 0);
    assert_eq!(memory_safety::mvgal_mem_placement(invalid), -1);
    assert_eq!(memory_safety::mvgal_mem_retain(invalid), 0);
    assert_eq!(memory_safety::mvgal_mem_release(invalid), 0);
    assert_eq!(memory_safety::mvgal_mem_set_dmabuf(invalid, 5), 0);
}

#[test]
fn test_memory_totals() {
    // These should never panic
    let _system = memory_safety::mvgal_mem_total_system_bytes();
    let _gpu = memory_safety::mvgal_mem_total_gpu_bytes();
}

// ============================================================================
// capability_model tests
// ============================================================================

fn make_test_gpu(vendor: GpuVendor, vram: u64) -> GpuCapability {
    GpuCapability {
        vendor,
        device_id: 0x1234,
        vram_bytes: vram,
        vram_bandwidth_gbps: 500.0,
        compute_units: 64,
        api_flags: api_flags::VULKAN,
        vulkan_major: 1,
        vulkan_minor: 3,
        pcie_gen: 4,
        pcie_lanes: 16,
        supports_graphics: true,
        supports_compute: true,
        supports_display: true,
    }
}

#[test]
fn test_capability_lifecycle_cross_crate() {
    let gpus = vec![
        make_test_gpu(GpuVendor::Amd, 8 << 30),
        make_test_gpu(GpuVendor::Nvidia, 8 << 30),
    ];

    let handle = unsafe { capability_model::mvgal_cap_compute(gpus.as_ptr(), 2) };
    assert!(!handle.is_null(), "cap_compute should return non-null handle");

    // Test accessor functions
    let total_vram = unsafe { capability_model::mvgal_cap_total_vram(handle) };
    assert_eq!(total_vram, 16u64 << 30, "total VRAM should be 16GB");

    let tier = unsafe { capability_model::mvgal_cap_tier(handle) };
    assert_eq!(tier, CapabilityTier::Full as i32, "tier should be Full");

    // Test JSON serialization
    let mut buf = [0i8; 2048];
    let json_len = unsafe {
        capability_model::mvgal_cap_to_json(handle, buf.as_mut_ptr(), buf.len())
    };
    assert!(json_len > 0, "JSON serialization should succeed");

    // Free the handle
    unsafe { capability_model::mvgal_cap_free(handle) };
}

#[test]
fn test_capability_null_args() {
    // Null gpus pointer should give empty aggregate (not null, not panic)
    let handle = unsafe { capability_model::mvgal_cap_compute(std::ptr::null(), 0) };
    assert!(!handle.is_null(), "cap_compute with null/0 should return empty aggregate");

    let total_vram = unsafe { capability_model::mvgal_cap_total_vram(handle) };
    assert_eq!(total_vram, 0, "empty aggregate should have 0 VRAM");

    unsafe { capability_model::mvgal_cap_free(handle) };
}

#[test]
fn test_capability_invalid_handles() {
    // Null handle tests - these should return error values, not panic
    assert_eq!(unsafe { capability_model::mvgal_cap_total_vram(std::ptr::null_mut()) }, 0);
    assert_eq!(unsafe { capability_model::mvgal_cap_tier(std::ptr::null_mut()) }, -1);

    let mut buf = [0i8; 256];
    assert_eq!(unsafe {
        capability_model::mvgal_cap_to_json(std::ptr::null_mut(), buf.as_mut_ptr(), buf.len())
    }, -1);

    // Freeing null should be a no-op (not panic)
    unsafe { capability_model::mvgal_cap_free(std::ptr::null_mut()) };
}

// ============================================================================
// Cross-crate interaction tests
// ============================================================================

#[test]
fn test_all_crates_together() {
    // Create objects from all three crates in sequence
    // This verifies there are no symbol conflicts or shared-state issues

    // fence_manager
    let fence_h = fence_manager::mvgal_fence_create(0);
    assert_ne!(fence_h, 0);

    // memory_safety
    let mem_h = memory_safety::mvgal_mem_track(4096, 0, 0);
    assert_ne!(mem_h, 0);

    // capability_model
    let gpu = make_test_gpu(GpuVendor::Intel, 16 << 30);
    let cap_h = unsafe { capability_model::mvgal_cap_compute(&gpu, 1) };
    assert!(!cap_h.is_null());

    // Verify all still work
    assert_eq!(fence_manager::mvgal_fence_state(fence_h), FenceState::Pending as i32);
    assert_eq!(memory_safety::mvgal_mem_size(mem_h), 4096);
    assert_eq!(unsafe { capability_model::mvgal_cap_tier(cap_h) }, CapabilityTier::Full as i32);

    // Cleanup
    assert_eq!(fence_manager::mvgal_fence_destroy(fence_h), 1);
    assert_eq!(memory_safety::mvgal_mem_release(mem_h), 1);
    unsafe { capability_model::mvgal_cap_free(cap_h) };
}
