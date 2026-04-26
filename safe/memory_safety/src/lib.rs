// SPDX-License-Identifier: MIT OR Apache-2.0
//! Safe wrappers for cross-GPU memory operations in MVGAL.

use std::collections::HashMap;
use std::sync::{Mutex};
use std::sync::atomic::{AtomicU64, Ordering};

pub type MvgalAllocHandle = u64;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
pub enum MemoryPlacement {
    SystemRam = 0,
    GpuVram   = 1,
    Mirrored  = 2,
}

struct Allocation {
    size_bytes:  u64,
    placement:   MemoryPlacement,
    #[allow(dead_code)]
    gpu_index:   u32,
    ref_count:   u32,
    #[allow(dead_code)]
    has_dmabuf:  bool,
    #[allow(dead_code)]
    dmabuf_fd:   i32,
}

struct AllocationRegistry {
    allocs:             HashMap<MvgalAllocHandle, Allocation>,
    next_id:            AtomicU64,
    total_bytes_system: u64,
    total_bytes_gpu:    u64,
}

impl AllocationRegistry {
    fn new() -> Self {
        Self {
            allocs: HashMap::new(),
            next_id: AtomicU64::new(1),
            total_bytes_system: 0,
            total_bytes_gpu: 0,
        }
    }

    fn allocate(&mut self, size_bytes: u64, placement: MemoryPlacement, gpu_index: u32) -> MvgalAllocHandle {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        match placement {
            MemoryPlacement::SystemRam => self.total_bytes_system += size_bytes,
            _ => self.total_bytes_gpu += size_bytes,
        }
        self.allocs.insert(id, Allocation {
            size_bytes, placement, gpu_index,
            ref_count: 1, has_dmabuf: false, dmabuf_fd: -1,
        });
        id
    }

    fn retain(&mut self, handle: MvgalAllocHandle) -> bool {
        if let Some(a) = self.allocs.get_mut(&handle) {
            a.ref_count = a.ref_count.saturating_add(1);
            return true;
        }
        false
    }

    fn release(&mut self, handle: MvgalAllocHandle) -> bool {
        if let Some(a) = self.allocs.get_mut(&handle) {
            if a.ref_count == 0 { return false; }
            a.ref_count -= 1;
            if a.ref_count == 0 {
                let (size, placement) = (a.size_bytes, a.placement);
                self.allocs.remove(&handle);
                match placement {
                    MemoryPlacement::SystemRam => {
                        self.total_bytes_system = self.total_bytes_system.saturating_sub(size);
                    }
                    _ => {
                        self.total_bytes_gpu = self.total_bytes_gpu.saturating_sub(size);
                    }
                }
            }
            return true;
        }
        false
    }

    fn set_dmabuf(&mut self, handle: MvgalAllocHandle, fd: i32) -> bool {
        if let Some(a) = self.allocs.get_mut(&handle) {
            a.has_dmabuf = fd >= 0;
            a.dmabuf_fd = fd;
            return true;
        }
        false
    }

    fn get_size(&self, handle: MvgalAllocHandle) -> u64 {
        self.allocs.get(&handle).map(|a| a.size_bytes).unwrap_or(0)
    }

    fn get_placement(&self, handle: MvgalAllocHandle) -> i32 {
        self.allocs.get(&handle).map(|a| a.placement as i32).unwrap_or(-1)
    }

    fn total_system_bytes(&self) -> u64 { self.total_bytes_system }
    fn total_gpu_bytes(&self) -> u64 { self.total_bytes_gpu }
}

static REGISTRY: Mutex<Option<AllocationRegistry>> = Mutex::new(None);

fn with_reg<F, R: Default>(f: F) -> R
where F: FnOnce(&mut AllocationRegistry) -> R {
    match REGISTRY.lock() {
        Ok(mut g) => { let r = g.get_or_insert_with(AllocationRegistry::new); f(r) }
        Err(_) => R::default(),
    }
}

fn with_reg_read<F, R: Default>(f: F) -> R
where F: FnOnce(&AllocationRegistry) -> R {
    match REGISTRY.lock() {
        Ok(mut g) => { let r = g.get_or_insert_with(AllocationRegistry::new); f(r) }
        Err(_) => R::default(),
    }
}

/// Track a new cross-GPU memory allocation.
///
/// # Safety
/// Thread-safe. The returned handle must be released with `mvgal_mem_release`.
#[no_mangle]
pub extern "C" fn mvgal_mem_track(size_bytes: u64, placement: u32, gpu_index: u32) -> MvgalAllocHandle {
    let p = match placement {
        0 => MemoryPlacement::SystemRam,
        1 => MemoryPlacement::GpuVram,
        2 => MemoryPlacement::Mirrored,
        _ => return 0,
    };
    with_reg(|r| r.allocate(size_bytes, p, gpu_index))
}

/// Increment the reference count.
///
/// # Safety
/// `handle` must be a valid handle returned by `mvgal_mem_track`.
#[no_mangle]
pub extern "C" fn mvgal_mem_retain(handle: MvgalAllocHandle) -> i32 {
    with_reg(|r| r.retain(handle) as i32)
}

/// Decrement the reference count. Frees the record when it reaches zero.
///
/// # Safety
/// `handle` must be a valid handle. After the last release the handle is invalid.
#[no_mangle]
pub extern "C" fn mvgal_mem_release(handle: MvgalAllocHandle) -> i32 {
    with_reg(|r| r.release(handle) as i32)
}

/// Associate a DMA-BUF fd with an allocation. Pass -1 to clear.
///
/// # Safety
/// `handle` must be valid. The caller retains ownership of `fd`.
#[no_mangle]
pub extern "C" fn mvgal_mem_set_dmabuf(handle: MvgalAllocHandle, fd: i32) -> i32 {
    with_reg(|r| r.set_dmabuf(handle, fd) as i32)
}

/// Query the size in bytes of a tracked allocation. Returns 0 if invalid.
///
/// # Safety
/// `handle` must be a valid handle returned by `mvgal_mem_track`.
#[no_mangle]
pub extern "C" fn mvgal_mem_size(handle: MvgalAllocHandle) -> u64 {
    with_reg_read(|r| r.get_size(handle))
}

/// Query the placement. Returns -1 if the handle is invalid.
///
/// # Safety
/// `handle` must be a valid handle returned by `mvgal_mem_track`.
#[no_mangle]
pub extern "C" fn mvgal_mem_placement(handle: MvgalAllocHandle) -> i32 {
    with_reg_read(|r| r.get_placement(handle))
}

/// Return total bytes tracked in system RAM.
#[no_mangle]
pub extern "C" fn mvgal_mem_total_system_bytes() -> u64 {
    with_reg_read(|r| r.total_system_bytes())
}

/// Return total bytes tracked in GPU VRAM.
#[no_mangle]
pub extern "C" fn mvgal_mem_total_gpu_bytes() -> u64 {
    with_reg_read(|r| r.total_gpu_bytes())
}

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
        assert_eq!(mvgal_mem_release(h), 1);
        assert_eq!(mvgal_mem_size(h), 0);
    }

    #[test]
    fn test_dmabuf_association() {
        let h = mvgal_mem_track(4096, 1, 1);
        assert_ne!(h, 0);
        assert_eq!(mvgal_mem_set_dmabuf(h, 5), 1);
        assert_eq!(mvgal_mem_set_dmabuf(h, -1), 1);
        mvgal_mem_release(h);
    }

    #[test]
    fn test_invalid_placement() {
        let h = mvgal_mem_track(4096, 99, 0);
        assert_eq!(h, 0);
    }
}
