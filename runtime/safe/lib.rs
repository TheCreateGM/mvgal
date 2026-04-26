// MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
// Rust Safety-Critical Subsystems
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#![allow(dead_code)]
#![allow(unused_variables)]

pub mod fence_manager;
pub mod memory_safety;
pub mod capability_model;

/// MVGAL version
pub const VERSION: &str = "0.2.0";

/// Result type for MVGAL operations
pub type MvgalResult<T> = Result<T, MvgalError>;

/// Error types for MVGAL
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MvgalError {
    /// No error
    Success = 0,
    /// Invalid argument
    InvalidArgument = 1,
    /// Out of memory
    OutOfMemory = 2,
    /// Resource not found
    NotFound = 3,
    /// Operation not supported
    NotSupported = 4,
    /// Device busy
    DeviceBusy = 5,
    /// Timeout
    Timeout = 6,
    /// Fence already signaled
    FenceAlreadySignaled = 7,
    /// Fence not signaled
    FenceNotSignaled = 8,
    /// Memory mapping failed
    MappingFailed = 9,
    /// Access denied
    AccessDenied = 10,
    /// Unknown error
    Unknown = 0xFFFFFFFF,
}

impl std::fmt::Display for MvgalError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "MVGAL error: {:?}", self)
    }
}

impl std::error::Error for MvgalError {}

impl From<MvgalError> for i32 {
    fn from(err: MvgalError) -> i32 {
        err as i32
    }
}

/// C-compatible error code type
pub type MvgalErrorCode = i32;

/// Ensure usize is 64-bit on all platforms
#[cfg(target_pointer_width = "64")]
pub type MvgalSize = u64;

#[cfg(target_pointer_width = "32")]
pub type MvgalSize = u32;

/// Maximum number of GPUs
pub const MAX_GPUS: usize = 8;

/// Maximum number of timelines per GPU
pub const MAX_TIMELINES: usize = 64;

/// Magic constant for validation
pub const MAGIC: u32 = 0x4D56474C; // 'MVGL'

/// Version of the Rust API
pub const API_VERSION: u32 = 1;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_conversion() {
        let err = MvgalError::InvalidArgument;
        let code: i32 = err.into();
        assert_eq!(code, 1);
    }

    #[test]
    fn test_magic_constant() {
        assert_eq!(MAGIC, 0x4D56474C);
        assert_eq!(format!("{:08X}", MAGIC), "4D56474C");
    }

    #[test]
    fn test_version() {
        assert_eq!(VERSION, "0.2.0");
        assert_eq!(API_VERSION, 1);
    }
}
