// SPDX-License-Identifier: MIT OR Apache-2.0
//! Cross-device fence lifecycle management for MVGAL.
//!
//! This crate provides memory-safe wrappers for cross-GPU fence operations,
//! exposed via a C FFI interface for use by the C++ runtime daemon.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;
use std::collections::HashMap;

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

struct Fence {
    #[allow(dead_code)]
    id:           MvgalFenceHandle,
    state:        FenceState,
    #[allow(dead_code)]
    gpu_index:    u32,
    #[allow(dead_code)]
    created_ns:   u64,
    #[allow(dead_code)]
    signalled_ns: u64,
}

struct FenceRegistry {
    fences:  HashMap<MvgalFenceHandle, Fence>,
    next_id: AtomicU64,
}

impl FenceRegistry {
    fn new() -> Self {
        Self { fences: HashMap::new(), next_id: AtomicU64::new(1) }
    }

    fn allocate(&mut self, gpu_index: u32) -> MvgalFenceHandle {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        self.fences.insert(id, Fence {
            id,
            state: FenceState::Pending,
            gpu_index,
            created_ns: monotonic_ns(),
            signalled_ns: 0,
        });
        id
    }

    fn submit(&mut self, handle: MvgalFenceHandle) -> bool {
        if let Some(f) = self.fences.get_mut(&handle) {
            if f.state == FenceState::Pending {
                f.state = FenceState::Submitted;
                return true;
            }
        }
        false
    }

    fn signal(&mut self, handle: MvgalFenceHandle) -> bool {
        if let Some(f) = self.fences.get_mut(&handle) {
            if f.state == FenceState::Submitted || f.state == FenceState::Pending {
                f.state = FenceState::Signalled;
                f.signalled_ns = monotonic_ns();
                return true;
            }
        }
        false
    }

    fn state(&self, handle: MvgalFenceHandle) -> Option<FenceState> {
        self.fences.get(&handle).map(|f| f.state)
    }

    fn reset(&mut self, handle: MvgalFenceHandle) -> bool {
        if let Some(f) = self.fences.get_mut(&handle) {
            if f.state == FenceState::Signalled {
                f.state = FenceState::Reset;
                f.signalled_ns = 0;
                return true;
            }
        }
        false
    }

    fn destroy(&mut self, handle: MvgalFenceHandle) -> bool {
        self.fences.remove(&handle).is_some()
    }
}

fn monotonic_ns() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0)
}

static REGISTRY: Mutex<Option<FenceRegistry>> = Mutex::new(None);

fn with_registry<F, R>(f: F) -> R
where
    F: FnOnce(&mut FenceRegistry) -> R,
    R: Default,
{
    match REGISTRY.lock() {
        Ok(mut guard) => {
            let reg = guard.get_or_insert_with(FenceRegistry::new);
            f(reg)
        }
        Err(_) => R::default(),
    }
}

fn with_registry_read<F, R>(f: F) -> R
where
    F: FnOnce(&FenceRegistry) -> R,
    R: Default,
{
    match REGISTRY.lock() {
        Ok(mut guard) => {
            let reg = guard.get_or_insert_with(FenceRegistry::new);
            f(reg)
        }
        Err(_) => R::default(),
    }
}

// ============================================================================
// C FFI interface
// ============================================================================

/// Allocate a new fence for the given GPU index.
///
/// Returns a non-zero handle on success, 0 on failure.
///
/// # Safety
/// Thread-safe. The returned handle must be freed with `mvgal_fence_destroy`.
#[no_mangle]
pub extern "C" fn mvgal_fence_create(gpu_index: u32) -> MvgalFenceHandle {
    with_registry(|r| r.allocate(gpu_index))
}

/// Submit a fence (Pending → Submitted).
///
/// Returns 1 on success, 0 otherwise.
///
/// # Safety
/// `handle` must be a valid handle returned by `mvgal_fence_create`.
#[no_mangle]
pub extern "C" fn mvgal_fence_submit(handle: MvgalFenceHandle) -> i32 {
    with_registry(|r| r.submit(handle) as i32)
}

/// Signal a fence (Submitted/Pending → Signalled).
///
/// Returns 1 on success, 0 otherwise.
///
/// # Safety
/// `handle` must be a valid handle returned by `mvgal_fence_create`.
#[no_mangle]
pub extern "C" fn mvgal_fence_signal(handle: MvgalFenceHandle) -> i32 {
    with_registry(|r| r.signal(handle) as i32)
}

/// Query the state of a fence.
///
/// Returns the `FenceState` value, or -1 if the handle is invalid.
///
/// # Safety
/// `handle` must be a valid handle returned by `mvgal_fence_create`.
#[no_mangle]
pub extern "C" fn mvgal_fence_state(handle: MvgalFenceHandle) -> i32 {
    with_registry_read(|r| match r.state(handle) {
        Some(s) => s as i32,
        None => -1_i32,
    })
}

/// Reset a signalled fence.
///
/// Returns 1 on success, 0 otherwise.
///
/// # Safety
/// `handle` must be a valid handle returned by `mvgal_fence_create`.
#[no_mangle]
pub extern "C" fn mvgal_fence_reset(handle: MvgalFenceHandle) -> i32 {
    with_registry(|r| r.reset(handle) as i32)
}

/// Destroy a fence and release its resources.
///
/// Returns 1 on success, 0 if the handle was not found.
///
/// # Safety
/// `handle` must be a valid handle. After this call the handle is invalid.
#[no_mangle]
pub extern "C" fn mvgal_fence_destroy(handle: MvgalFenceHandle) -> i32 {
    with_registry(|r| r.destroy(handle) as i32)
}

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
