// SPDX-License-Identifier: MIT OR Apache-2.0
//! GPU capability normalization and comparison for MVGAL.

use serde::{Deserialize, Serialize};
use std::os::raw::c_char;

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
    Full        = 0,
    ComputeOnly = 1,
    Mixed       = 2,
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

impl AggregateCapability {
    pub fn from_gpus(gpus: &[GpuCapability]) -> Self {
        if gpus.is_empty() {
            return Self {
                gpu_count: 0, total_vram_bytes: 0, total_compute_units: 0,
                total_vram_bandwidth_gbps: 0.0, min_vulkan_major: 0,
                min_vulkan_minor: 0, api_union: 0, api_intersection: 0,
                tier: CapabilityTier::Mixed,
            };
        }

        let mut total_vram: u64 = 0;
        let mut total_cu: u32 = 0;
        let mut total_bw: f32 = 0.0;
        let mut min_vk_major = u32::MAX;
        let mut min_vk_minor = u32::MAX;
        let mut api_union: u32 = 0;
        let mut api_intersection: u32 = u32::MAX;
        let mut all_graphics = true;
        let mut all_compute = true;

        for gpu in gpus {
            total_vram = total_vram.saturating_add(gpu.vram_bytes);
            total_cu = total_cu.saturating_add(gpu.compute_units);
            total_bw += gpu.vram_bandwidth_gbps;
            if gpu.vulkan_major < min_vk_major
                || (gpu.vulkan_major == min_vk_major && gpu.vulkan_minor < min_vk_minor)
            {
                min_vk_major = gpu.vulkan_major;
                min_vk_minor = gpu.vulkan_minor;
            }
            api_union |= gpu.api_flags;
            api_intersection &= gpu.api_flags;
            if !gpu.supports_graphics { all_graphics = false; }
            if !gpu.supports_compute  { all_compute  = false; }
        }

        let tier = if all_graphics && all_compute {
            CapabilityTier::Full
        } else if all_compute && !all_graphics {
            CapabilityTier::ComputeOnly
        } else {
            CapabilityTier::Mixed
        };

        Self {
            gpu_count: gpus.len() as u32,
            total_vram_bytes: total_vram,
            total_compute_units: total_cu,
            total_vram_bandwidth_gbps: total_bw,
            min_vulkan_major: if min_vk_major == u32::MAX { 0 } else { min_vk_major },
            min_vulkan_minor: if min_vk_minor == u32::MAX { 0 } else { min_vk_minor },
            api_union,
            api_intersection,
            tier,
        }
    }

    pub fn to_json(&self) -> String {
        serde_json::to_string(self).unwrap_or_default()
    }
}

pub type MvgalCapHandle = *mut AggregateCapability;

/// Compute an aggregate capability profile from an array of per-GPU descriptors.
///
/// # Safety
/// `gpus` must point to `count` valid `GpuCapability` structs.
/// The returned pointer must be freed with `mvgal_cap_free`.
#[no_mangle]
pub unsafe extern "C" fn mvgal_cap_compute(
    gpus: *const GpuCapability,
    count: u32,
) -> MvgalCapHandle {
    let slice = if gpus.is_null() || count == 0 {
        &[]
    } else {
        // SAFETY: caller guarantees gpus points to count valid structs.
        unsafe { std::slice::from_raw_parts(gpus, count as usize) }
    };
    Box::into_raw(Box::new(AggregateCapability::from_gpus(slice)))
}

/// Free an aggregate capability handle.
///
/// # Safety
/// `handle` must be a valid pointer returned by `mvgal_cap_compute`.
#[no_mangle]
pub unsafe extern "C" fn mvgal_cap_free(handle: MvgalCapHandle) {
    if !handle.is_null() {
        // SAFETY: handle was created by Box::into_raw in mvgal_cap_compute.
        let _ = unsafe { Box::from_raw(handle) };
    }
}

/// Return the total VRAM in bytes.
///
/// # Safety
/// `handle` must be a valid non-null pointer returned by `mvgal_cap_compute`.
#[no_mangle]
pub unsafe extern "C" fn mvgal_cap_total_vram(handle: MvgalCapHandle) -> u64 {
    if handle.is_null() { return 0; }
    // SAFETY: handle is non-null and was created by mvgal_cap_compute.
    unsafe { (*handle).total_vram_bytes }
}

/// Return the capability tier. Returns -1 on null.
///
/// # Safety
/// `handle` must be a valid non-null pointer returned by `mvgal_cap_compute`.
#[no_mangle]
pub unsafe extern "C" fn mvgal_cap_tier(handle: MvgalCapHandle) -> i32 {
    if handle.is_null() { return -1; }
    // SAFETY: handle is non-null and was created by mvgal_cap_compute.
    unsafe { (*handle).tier as i32 }
}

/// Serialize to JSON into `buf`. Returns bytes written or -1 on error.
///
/// # Safety
/// `handle` must be valid. `buf` must point to at least `buf_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn mvgal_cap_to_json(
    handle: MvgalCapHandle,
    buf: *mut c_char,
    buf_len: usize,
) -> i32 {
    if handle.is_null() || buf.is_null() || buf_len == 0 { return -1; }
    // SAFETY: handle is non-null.
    let json = unsafe { (*handle).to_json() };
    let bytes = json.as_bytes();
    let copy_len = bytes.len().min(buf_len - 1);
    // SAFETY: buf points to buf_len bytes.
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, buf, copy_len);
        *buf.add(copy_len) = 0;
    }
    copy_len as i32
}

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
