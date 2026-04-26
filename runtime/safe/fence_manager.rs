// MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
// Rust Fence Manager - Cross-device fence lifecycle management
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use std::collections::HashMap;
use std::sync::{Arc, Mutex, Condvar, atomic::{AtomicU64, Ordering}};
use std::time::{Duration, Instant};
use std::cell::Cell;
use crate::{MvgalResult, MvgalError};

/// State of a fence
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FenceState {
    /// Fence has not been signaled yet
    Unsignaled = 0,
    /// Fence has been signaled
    Signaled = 1,
}

/// Cross-device fence that works across GPUs from different vendors
///
/// This implements a unified fence abstraction that can synchronize
/// operations across AMD, NVIDIA, Intel, and Moore Threads GPUs
/// using timeline semaphores as the common primitive.
#[derive(Debug)]
pub struct CrossDeviceFence {
    id: u64,

    /// State of the fence
    state: Mutex<FenceState>,

    /// Condition variable for waiting
    condvar: Condvar,

    /// Timeline value on which this fence was created
    timeline_value: u64,

    /// Optional pointer to external synchronization primitive
    /// (e.g., VkFence, sync_file*, dma_fence*)
    external_handle: Option<*mut std::ffi::c_void>,

    /// Timestamp when fence was signaled
    signal_time: Cell<Option<Instant>>,

    /// Whether this fence is part of a device timeline
    is_timeline: bool,
}

impl Clone for CrossDeviceFence {
    fn clone(&self) -> Self {
        CrossDeviceFence {
            id: self.id,
            state: Mutex::new(*self.state.lock().unwrap()),
            condvar: Condvar::new(),
            timeline_value: self.timeline_value,
            external_handle: self.external_handle,
            signal_time: Cell::new(self.signal_time.get()),
            is_timeline: self.is_timeline,
        }
    }
}

impl CrossDeviceFence {
    /// Create a new unsignaled fence
    pub fn new() -> Self {
        CrossDeviceFence {
            id: 0,
            state: Mutex::new(FenceState::Unsignaled),
            condvar: Condvar::new(),
            timeline_value: 0,
            external_handle: None,
            signal_time: Cell::new(None),
            is_timeline: false,
        }
    }

    /// Create a fence with a specific ID
    pub fn with_id(id: u64) -> Self {
        let mut fence = Self::new();
        fence.id = id;
        fence
    }

    /// Get the fence ID
    pub fn id(&self) -> u64 {
        self.id
    }

    /// Signal the fence
    pub fn signal(&self) {
        let mut state = self.state.lock().unwrap();
        if *state == FenceState::Unsignaled {
            *state = FenceState::Signaled;
            self.signal_time.set(Some(Instant::now()));
            self.condvar.notify_all();
        }
    }

    /// Wait for the fence to be signaled
    pub fn wait(&self, timeout: Option<Duration>) -> MvgalResult<FenceState> {
        let state = self.state.lock().unwrap();

        if *state == FenceState::Signaled {
            return Ok(FenceState::Signaled);
        }

        match timeout {
            Some(to) => {
                let (state, timed_out) = self.condvar.wait_timeout(state, to).unwrap();
                if timed_out.timed_out() {
                    return Ok(FenceState::Unsignaled);
                }
                let _ = timed_out;
                if *state == FenceState::Signaled {
                    Ok(FenceState::Signaled)
                } else {
                    Ok(FenceState::Unsignaled)
                }
            }
            None => {
                let mut state = state;
                loop {
                    state = self.condvar.wait(state).unwrap();
                    if *state == FenceState::Signaled {
                        return Ok(FenceState::Signaled);
                    }
                }
            }
        }
    }

    /// Check if fence is signaled without waiting
    pub fn is_signaled(&self) -> bool {
        let state = self.state.lock().unwrap();
        *state == FenceState::Signaled
    }

    /// Reset the fence to unsignaled state
    pub fn reset(&self) {
        let mut state = self.state.lock().unwrap();
        *state = FenceState::Unsignaled;
        self.signal_time.set(None);
    }

    /// Get the signal time if available
    pub fn signal_time(&self) -> Option<Instant> {
        self.signal_time.get()
    }

    /// Set timeline value
    pub fn set_timeline_value(&mut self, value: u64) {
        self.timeline_value = value;
        self.is_timeline = true;
    }

    /// Get timeline value
    pub fn timeline_value(&self) -> u64 {
        self.timeline_value
    }

    /// Check if this is a timeline fence
    pub fn is_timeline(&self) -> bool {
        self.is_timeline
    }
}

/// Timeline for a GPU
///
/// Each GPU can have its own timeline(s) for synchronization
/// Timeline semaphores allow waiting on a specific value
pub struct GpuTimeline {
    /// GPU index this timeline belongs to
    gpu_index: u32,

    /// Current value of the timeline
    current_value: Mutex<u64>,

    /// Next value to be assigned
    next_value: AtomicU64,

    /// All fences on this timeline
    fences: Mutex<Vec<Arc<CrossDeviceFence>>>,

    /// Maximum number of fences to keep
    max_fences: usize,
}

impl GpuTimeline {
    pub fn new(gpu_index: u32) -> Self {
        GpuTimeline {
            gpu_index,
            current_value: Mutex::new(0),
            next_value: AtomicU64::new(1),
            fences: Mutex::new(Vec::new()),
            max_fences: 1024,
        }
    }

    /// Signal the timeline to a specific value
    pub fn signal(&self, value: u64) {
        let mut current = self.current_value.lock().unwrap();
        *current = value;
        self.next_value.fetch_add(1, Ordering::SeqCst);
    }

    /// Get current timeline value
    pub fn current_value(&self) -> u64 {
        *self.current_value.lock().unwrap()
    }

    /// Get next value and increment
    pub fn next_value(&self) -> u64 {
        self.next_value.fetch_add(1, Ordering::SeqCst)
    }

    /// Create a fence on this timeline
    pub fn create_fence(&self) -> Arc<CrossDeviceFence> {
        let value = self.next_value();
        let fence = CrossDeviceFence::with_id(value);

        {
            let mut fences = self.fences.lock().unwrap();
            if fences.len() >= self.max_fences {
                fences.remove(0);
            }
            fences.push(Arc::new(fence.clone()));
        }

        Arc::new(fence)
    }
}

/// Timelines for all GPUs
pub struct TimelineManager {
    timelines: Mutex<Vec<Arc<GpuTimeline>>>,
}

impl TimelineManager {
    pub fn new(num_gpus: usize) -> Self {
        let mut timelines = Vec::with_capacity(num_gpus);
        for i in 0..num_gpus {
            timelines.push(Arc::new(GpuTimeline::new(i as u32)));
        }
        TimelineManager {
            timelines: Mutex::new(timelines),
        }
    }

    /// Get timeline for a GPU
    pub fn get_timeline(&self, gpu_index: u32) -> Option<Arc<GpuTimeline>> {
        let timelines = self.timelines.lock().unwrap();
        if gpu_index as usize >= timelines.len() {
            return None;
        }
        Some(timelines[gpu_index as usize].clone())
    }
}

/// Main fence manager
///
/// Manages all fences and timelines across all GPUs
pub struct FenceManager {
    /// Timeline manager
    timeline_manager: TimelineManager,

    /// Map fence IDs to fences
    fence_map: Mutex<HashMap<u64, Arc<CrossDeviceFence>>>,

    /// Next fence ID
    next_fence_id: AtomicU64,
}

impl FenceManager {
    /// Create a new fence manager
    pub fn new(num_gpus: usize) -> Self {
        FenceManager {
            timeline_manager: TimelineManager::new(num_gpus),
            fence_map: Mutex::new(HashMap::new()),
            next_fence_id: AtomicU64::new(1),
        }
    }

    /// Create a new fence manager wrapped in Arc
    pub fn new_arc(num_gpus: usize) -> Arc<Self> {
        Arc::new(Self::new(num_gpus))
    }

    /// Create a new cross-device fence
    pub fn create_fence(&self) -> Arc<CrossDeviceFence> {
        let id = self.next_fence_id.fetch_add(1, Ordering::SeqCst);
        let fence = CrossDeviceFence::with_id(id);

        {
            let mut map = self.fence_map.lock().unwrap();
            map.insert(id, Arc::new(fence.clone()));
        }

        Arc::new(fence)
    }

    /// Create a timeline fence for a specific GPU
    pub fn create_timeline_fence(&self, gpu_index: u32) -> Option<Arc<CrossDeviceFence>> {
        let timeline = self.timeline_manager.get_timeline(gpu_index)?;
        Some(timeline.create_fence())
    }

    /// Signal a fence by ID
    pub fn signal_fence(&self, fence_id: u64) -> bool {
        let map = self.fence_map.lock().unwrap();
        if let Some(fence) = map.get(&fence_id) {
            fence.signal();
            true
        } else {
            false
        }
    }

    /// Wait for a fence by ID with timeout
    pub fn wait_fence(&self, fence_id: u64, timeout_ms: u32) -> MvgalResult<bool> {
        let map = self.fence_map.lock().unwrap();
        if let Some(fence) = map.get(&fence_id) {
            let timeout = if timeout_ms > 0 {
                Some(Duration::from_millis(timeout_ms as u64))
            } else {
                None
            };
            let state = fence.wait(timeout)?;
            Ok(state == FenceState::Signaled)
        } else {
            Err(MvgalError::NotFound)
        }
    }

    /// Check if fence is signaled
    pub fn test_fence(&self, fence_id: u64) -> bool {
        let map = self.fence_map.lock().unwrap();
        map.get(&fence_id).map_or(false, |f| f.is_signaled())
    }

    /// Get a fence by ID
    pub fn get_fence(&self, fence_id: u64) -> Option<Arc<CrossDeviceFence>> {
        let map = self.fence_map.lock().unwrap();
        map.get(&fence_id).cloned()
    }

    /// Remove a fence by ID
    pub fn destroy_fence(&self, fence_id: u64) -> bool {
        let mut map = self.fence_map.lock().unwrap();
        map.remove(&fence_id).is_some()
    }

    /// Get timeline manager
    pub fn timelines(&self) -> &TimelineManager {
        &self.timeline_manager
    }
}

impl Default for FenceManager {
    fn default() -> Self {
        Self::new(8)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fence_creation() {
        let manager = FenceManager::new(2);
        let fence = manager.create_fence();
        assert_eq!(fence.id(), 1);
        assert!(!fence.is_signaled());
    }

    #[test]
    fn test_fence_signaling() {
        let manager = FenceManager::new(2);
        let fence = manager.create_fence();

        assert!(!fence.is_signaled());
        fence.signal();
        assert!(fence.is_signaled());
    }

    #[test]
    fn test_fence_wait() {
        let manager = FenceManager::new(2);
        let fence = manager.create_fence();

        fence.signal();
        let result = fence.wait(None).unwrap();
        assert_eq!(result, FenceState::Signaled);
    }
}
