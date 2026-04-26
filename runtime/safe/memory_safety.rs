// MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
// Rust Memory Safety - Safe wrappers for cross-GPU memory operations
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use std::sync::{Arc, Mutex, RwLock};
use std::collections::HashMap;
use std::ops::Deref;
use crate::{MvgalResult, MvgalError, MvgalSize};

/// Maximum trackable allocations per GPU
pub const MAX_TRACKED_ALLOCATIONS: usize = 4096;

/// Alignment for GPU memory allocations
pub const GPU_MEMORY_ALIGNMENT: usize = 256;

/// Memory region on a GPU
#[derive(Debug, Clone)]
pub struct GpuMemoryRegion {
    /// Starting address of the region
    pub base: MvgalSize,
    /// Size of the region in bytes
    pub size: MvgalSize,
    /// Memory type (VRAM, system, etc.)
    pub memory_type: MemoryType,
    /// Where this region is mapped
    pub gpuaffinity: Vec<bool>, // Which GPUs can access this region
    /// Reference count
    pub refcount: usize,
    /// Whether this region is mappable
    pub mappable: bool,
}

impl GpuMemoryRegion {
    pub fn new(base: MvgalSize, size: MvgalSize, memory_type: MemoryType, num_gpus: usize) -> Self {
        GpuMemoryRegion {
            base,
            size,
            memory_type,
            gpuaffinity: vec![true; num_gpus], // Default: all GPUs can access
            refcount: 1,
            mappable: false,
        }
    }

    pub fn is_accessible_by(&self, gpu_index: usize) -> bool {
        self.gpuaffinity.get(gpu_index).cloned().unwrap_or(false)
    }
}

/// Memory type for regions
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MemoryType {
    /// Device-local memory on a specific GPU
    GpuDeviceLocal,
    /// Host-visible memory on a GPU
    GpuHostVisible,
    /// System memory
    SystemRam,
    /// Unified memory accessible by all GPUs
    UnifiedMemory,
    /// Shared memory via DMA-BUF
    DmaBuf,
}

/// Safe wrapper for GPU memory addresses
///
/// This ensures that addresses are properly aligned and validated
/// before use.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct GpuMemoryAddress(MvgalSize);

impl GpuMemoryAddress {
    /// Create a new GPU memory address with validation
    pub fn new(address: MvgalSize) -> MvgalResult<Self> {
        if address == 0 {
            return Err(MvgalError::InvalidArgument);
        }
        if !is_aligned(address, GPU_MEMORY_ALIGNMENT as MvgalSize) {
            return Err(MvgalError::InvalidArgument);
        }
        Ok(GpuMemoryAddress(address))
    }

    /// Create a null address (special case)
    pub fn null() -> Self {
        GpuMemoryAddress(0)
    }

    pub fn get(&self) -> MvgalSize {
        self.0
    }

    pub fn is_null(&self) -> bool {
        self.0 == 0
    }
}

impl Deref for GpuMemoryAddress {
    type Target = MvgalSize;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// Memory allocation tracker for a GPU
pub struct GpuMemoryTracker {
    /// All active allocations
    allocations: RwLock<HashMap<GpuMemoryAddress, Arc<GpuMemoryRegion>>>,
    /// Next available address hint (for sequential allocation)
    next_address: Mutex<MvgalSize>,
    /// Total committed memory
    total_committed: Mutex<MvgalSize>,
    /// Peak memory usage
    peak_usage: Mutex<MvgalSize>,
    /// GPU index
    gpu_index: u32,
}

impl GpuMemoryTracker {
    pub fn new(gpu_index: u32) -> Self {
        GpuMemoryTracker {
            allocations: RwLock::new(HashMap::new()),
            next_address: Mutex::new(0),
            total_committed: Mutex::new(0),
            peak_usage: Mutex::new(0),
            gpu_index,
        }
    }

    /// Allocate a memory region
    pub fn allocate(&self, size: MvgalSize, memory_type: MemoryType, num_gpus: usize) -> MvgalResult<Arc<GpuMemoryRegion>> {
        // Align size
        let aligned_size = align_up(size, GPU_MEMORY_ALIGNMENT as MvgalSize);

        // Get next address (simplified allocation)
        let mut next_addr = self.next_address.lock().unwrap();
        let base = *next_addr;
        *next_addr += aligned_size;

        let mut total = self.total_committed.lock().unwrap();
        *total += aligned_size;

        let mut peak = self.peak_usage.lock().unwrap();
        if aligned_size + base > *peak {
            *peak = aligned_size + base;
        }

        let region = Arc::new(GpuMemoryRegion::new(base, aligned_size, memory_type, num_gpus));

        {
            let mut allocs = self.allocations.write().unwrap();
            allocs.insert(GpuMemoryAddress::new(base)?, region.clone());
        }

        Ok(region)
    }

    /// Free a memory region
    pub fn free(&self, address: GpuMemoryAddress) -> MvgalResult<()> {
        let mut allocs = self.allocations.write().unwrap();
        if let Some(region) = allocs.remove(&address) {
            let mut total = self.total_committed.lock().unwrap();
            *total -= region.size;
            Ok(())
        } else {
            Err(MvgalError::NotFound)
        }
    }

    /// Get a memory region by address
    pub fn get_region(&self, address: GpuMemoryAddress) -> Option<Arc<GpuMemoryRegion>> {
        let allocs = self.allocations.read().unwrap();
        allocs.get(&address).cloned()
    }

    /// Check if an address is valid
    pub fn is_valid_address(&self, address: GpuMemoryAddress) -> bool {
        self.get_region(address).is_some()
    }

    /// Get statistics
    pub fn get_stats(&self) -> (MvgalSize, MvgalSize, usize) {
        let total = *self.total_committed.lock().unwrap();
        let peak = *self.peak_usage.lock().unwrap();
        let count = self.allocations.read().unwrap().len();
        (total, peak, count)
    }
}

/// Memory barrier for ensuring ordering
pub struct MemoryBarrier;

impl MemoryBarrier {
    pub fn new() -> Self {
        MemoryBarrier
    }

    /// Insert a full memory barrier
    pub fn insert(&self) {
        std::sync::atomic::fence(std::sync::atomic::Ordering::SeqCst);
    }

    /// Insert a GPU memory barrier via command buffer
    /// This would insert a barrier command into the GPU command stream
    pub fn insert_gpu_barrier(&self, gpu_index: u32) {
        // TODO: Implement GPU-specific barrier insertion
        // This would involve inserting a pipeline barrier or similar
        // command into the GPU's command queue
    }
}

/// Safe wrapper for DMA-BUF file descriptors
pub struct SafeDmaBufFd(i32);

impl SafeDmaBufFd {
    pub fn new(fd: i32) -> Option<Self> {
        if fd >= 0 {
            Some(SafeDmaBufFd(fd))
        } else {
            None
        }
    }

    pub fn get(&self) -> i32 {
        self.0
    }

    pub fn into_raw(self) -> i32 {
        let fd = self.0;
        std::mem::forget(self);
        fd
    }
}

impl Drop for SafeDmaBufFd {
    fn drop(&mut self) {
        // Close the file descriptor
        let _ = unsafe { libc::close(self.0) };
    }
}

impl Deref for SafeDmaBufFd {
    type Target = i32;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// Helper function to check alignment
#[inline]
pub fn is_aligned(address: MvgalSize, alignment: MvgalSize) -> bool {
    (address % alignment) == 0
}

/// Helper function to align up
#[inline]
pub fn align_up(value: MvgalSize, alignment: MvgalSize) -> MvgalSize {
    ((value + alignment - 1) / alignment) * alignment
}

/// Cross-GPU memory copy operation
///
/// This represents a memory copy operation between two GPUs
/// that may use P2P DMA, system RAM staging, or direct GPU copy
pub struct CrossGpuMemoryCopy {
    /// Source GPU index
    pub src_gpu: u32,
    /// Source address
    pub src_address: GpuMemoryAddress,
    /// Destination GPU index
    pub dst_gpu: u32,
    /// Destination address
    pub dst_address: GpuMemoryAddress,
    /// Size in bytes
    pub size: MvgalSize,
    /// Copy method to use
    pub method: MemoryCopyMethod,
}

impl CrossGpuMemoryCopy {
    pub fn new(src_gpu: u32, src_addr: MvgalSize, dst_gpu: u32, dst_addr: MvgalSize, size: MvgalSize) -> MvgalResult<Self> {
        Ok(CrossGpuMemoryCopy {
            src_gpu,
            src_address: GpuMemoryAddress::new(src_addr)?,
            dst_gpu,
            dst_address: GpuMemoryAddress::new(dst_addr)?,
            size,
            method: MemoryCopyMethod::Auto,
        })
    }

    /// Determine the best copy method based on GPU capabilities
    pub fn determine_method(&self, can_p2p: bool, use_host_staging: bool) -> MemoryCopyMethod {
        if can_p2p {
            MemoryCopyMethod::P2PDma
        } else if use_host_staging {
            MemoryCopyMethod::HostStaging
        } else {
            MemoryCopyMethod::DirectGpuCopy
        }
    }
}

/// Memory copy methods
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MemoryCopyMethod {
    /// Automatic selection
    Auto,
    /// Peer-to-peer DMA transfer
    P2PDma,
    /// Via host staging buffer
    HostStaging,
    /// Direct GPU-to-GPU copy
    DirectGpuCopy,
    /// Useunified virtual addressing
    Uva,
}

/// Validate memory access
pub fn validate_memory_access(
    gpu_index: u32,
    address: GpuMemoryAddress,
    size: MvgalSize,
    access_type: MemoryAccessType,
    tracker: &GpuMemoryTracker,
) -> MvgalResult<()> {
    // Check if address is valid
    if !tracker.is_valid_address(address) {
        return Err(MvgalError::InvalidArgument);
    }

    // Get the region
    let region = tracker.get_region(address).ok_or(MvgalError::NotFound)?;

    // Check if this GPU can access the region
    if !region.is_accessible_by(gpu_index as usize) {
        return Err(MvgalError::AccessDenied);
    }

    // Check bounds
    if address.get() + size > region.base + region.size {
        return Err(MvgalError::InvalidArgument);
    }

    // TODO: Check access permissions (read/write/execute)

    Ok(())
}

/// Memory access types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MemoryAccessType {
    Read,
    Write,
    ReadWrite,
    Execute,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_address_alignment() {
        assert!(is_aligned(0x1000, 0x1000));
        assert!(!is_aligned(0x1001, 0x1000));
    }

    #[test]
    fn test_align_up() {
        assert_eq!(align_up(0x1001, 0x1000), 0x2000);
        assert_eq!(align_up(0x1000, 0x1000), 0x1000);
    }

    #[test]
    fn test_gpu_memory_address() {
        assert!(GpuMemoryAddress::new(0x1000).is_ok());
        assert!(GpuMemoryAddress::new(0).is_ok()); // Null is valid as a special case
        assert!(GpuMemoryAddress::new(0x123).is_err()); // Not aligned

        let addr = GpuMemoryAddress::new(0x1000).unwrap();
        assert_eq!(addr.get(), 0x1000);
        assert!(!addr.is_null());

        assert!(GpuMemoryAddress::null().is_null());
    }
}
