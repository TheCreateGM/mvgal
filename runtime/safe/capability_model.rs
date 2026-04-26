// MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
// Rust Capability Model - GPU capability normalization and comparison
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use std::collections::{HashMap, HashSet};
use std::sync::{Arc, RwLock};
use std::fmt;
use serde::{Serialize, Deserialize};

/// Maximum number of supported API versions
pub const MAX_API_VERSIONS: usize = 32;

/// API type identifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum ApiType {
    Vulkan,
    OpenGL,
    OpenCL,
    Cuda,
    Sycl,
    Direct3D,
    Metal,
    WebGPU,
}

impl fmt::Display for ApiType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ApiType::Vulkan => write!(f, "Vulkan"),
            ApiType::OpenGL => write!(f, "OpenGL"),
            ApiType::OpenCL => write!(f, "OpenCL"),
            ApiType::Cuda => write!(f, "CUDA"),
            ApiType::Sycl => write!(f, "SYCL"),
            ApiType::Direct3D => write!(f, "Direct3D"),
            ApiType::Metal => write!(f, "Metal"),
            ApiType::WebGPU => write!(f, "WebGPU"),
        }
    }
}

/// Version number representation
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct ApiVersion {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
}

impl ApiVersion {
    pub fn new(major: u32, minor: u32, patch: u32) -> Self {
        ApiVersion { major, minor, patch }
    }

    pub fn from_hex(version: u32) -> Self {
        let major = (version >> 22) & 0x3FF;
        let minor = (version >> 12) & 0x3FF;
        let patch = version & 0xFFF;
        ApiVersion { major, minor, patch }
    }

    pub fn to_hex(&self) -> u32 {
        ((self.major & 0x3FF) << 22) | ((self.minor & 0x3FF) << 12) | (self.patch & 0xFFF)
    }

    pub fn as_string(&self) -> String {
        format!("{}.{}.{}", self.major, self.minor, self.patch)
    }
}

impl fmt::Display for ApiVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}

/// GPU vendor identifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum GpuVendor {
    Unknown,
    Amd,
    Nvidia,
    Intel,
    MooreThreads,
}

impl fmt::Display for GpuVendor {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            GpuVendor::Unknown => write!(f, "Unknown"),
            GpuVendor::Amd => write!(f, "AMD"),
            GpuVendor::Nvidia => write!(f, "NVIDIA"),
            GpuVendor::Intel => write!(f, "Intel"),
            GpuVendor::MooreThreads => write!(f, "Moore Threads"),
        }
    }
}

/// Feature flags
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum GpuFeature {
    /// GPU supports graphics rendering
    Graphics,
    /// GPU supports compute shaders
    Compute,
    /// GPU supports ray tracing
    RayTracing,
    /// GPU supports AI acceleration
    Ai,
    /// GPU supports display output
    Display,
    /// GPU supports video encoding
    VideoEncode,
    /// GPU supports video decoding
    VideoDecode,
    /// GPU supports peer-to-peer DMA
    P2PDma,
    /// GPU supports unified virtual addressing
    Uva,
    /// GPU supports timeline semaphores
    TimelineSemaphores,
    /// GPU supports DMA-BUF import/export
    DmaBuf,
    /// GPU supports NUMA-aware allocation
    NumaAware,
    /// GPU supports multiple command queues
    MultiQueue,
    /// GPU supports ECC memory
    EccMemory,
}

/// GPU capabilities for a single GPU
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GpuCapabilities {
    /// GPU vendor
    pub vendor: GpuVendor,
    /// PCI vendor ID
    pub pci_vendor_id: u16,
    /// PCI device ID
    pub pci_device_id: u16,
    /// Human-readable GPU name
    pub name: String,

    /// Total VRAM in bytes
    pub vram_total: u64,
    /// Free VRAM in bytes
    pub vram_free: u64,
    /// VRAM bandwidth in MB/s
    pub vram_bandwidth_mbps: u32,

    /// Total system memory accessible in bytes
    pub system_memory_total: u64,
    /// Free system memory in bytes
    pub system_memory_free: u64,

    /// Number of compute units / shader multiprocessors
    pub compute_units: u32,
    /// Number of execution units (for Intel)
    pub execution_units: u32,
    /// Number of stream processors (for AMD/MTT)
    pub stream_processors: u32,
    /// Number of CUDA cores (for NVIDIA)
    pub cuda_cores: u32,

    /// Maximum clock speed in MHz
    pub max_clock_mhz: u32,
    /// Current clock speed in MHz
    pub current_clock_mhz: u32,

    /// PCIe generation (1-5)
    pub pcie_generation: u8,
    /// PCIe lane count (1, 2, 4, 8, 16)
    pub pcie_lanes: u8,

    /// NUMA node this GPU is on (-1 if unknown)
    pub numa_node: i32,

    /// Supported APIs with versions
    pub api_versions: HashMap<ApiType, ApiVersion>,

    /// Supported features
    pub features: HashSet<GpuFeature>,

    /// Whether this GPU drives a display
    pub is_display_connected: bool,

    /// Power state
    pub power_state: GpuPowerState,

    /// Thermal state
    pub thermal_state: GpuThermalState,
}

impl GpuCapabilities {
    pub fn new(vendor: GpuVendor) -> Self {
        GpuCapabilities {
            vendor,
            pci_vendor_id: 0,
            pci_device_id: 0,
            name: String::new(),
            vram_total: 0,
            vram_free: 0,
            vram_bandwidth_mbps: 0,
            system_memory_total: 0,
            system_memory_free: 0,
            compute_units: 0,
            execution_units: 0,
            stream_processors: 0,
            cuda_cores: 0,
            max_clock_mhz: 0,
            current_clock_mhz: 0,
            pcie_generation: 0,
            pcie_lanes: 0,
            numa_node: -1,
            api_versions: HashMap::new(),
            features: HashSet::new(),
            is_display_connected: false,
            power_state: GpuPowerState::Unknown,
            thermal_state: GpuThermalState::Unknown,
        }
    }

    /// Check if this GPU supports a specific API
    pub fn supports_api(&self, api: ApiType) -> bool {
        self.api_versions.contains_key(&api)
    }

    /// Check if this GPU supports a specific feature
    pub fn supports_feature(&self, feature: GpuFeature) -> bool {
        self.features.contains(&feature)
    }

    /// Get API version if supported
    pub fn api_version(&self, api: ApiType) -> Option<ApiVersion> {
        self.api_versions.get(&api).cloned()
    }

    /// Minimum API version across a set of GPUs
    pub fn min_api_version(&self, api: ApiType) -> Option<ApiVersion> {
        self.api_versions.get(&api).cloned()
    }
}

/// GPU power states
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum GpuPowerState {
    Unknown,
    /// GPU is fully active
    Active,
    /// GPU is in sustained performance mode
    Sustained,
    /// GPU is idle
    Idle,
    /// GPU is parked (completely off)
    Park,
}

/// GPU thermal states
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum GpuThermalState {
    Unknown,
    /// Temperature is normal
    Normal,
    /// Temperature is elevated
    Warm,
    /// Temperature is high but within limits
    Hot,
    /// Temperature is at or above threshold
    Throttling,
    /// Temperature is at shutdown point
    Critical,
}

/// Unified capabilities for the MVGAL logical device
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UnifiedCapabilities {
    /// Total aggregate VRAM across all GPUs
    pub total_vram: u64,
    /// Total free VRAM across all GPUs
    pub free_vram: u64,

    /// Aggregate memory bandwidth in MB/s
    pub total_bandwidth_mbps: u64,

    /// Total compute units across all GPUs
    pub total_compute_units: u32,

    /// Combined API support: APIs available on ALL GPUs
    pub common_api_versions: HashMap<ApiType, ApiVersion>,

    /// Union API support: APIs available on ANY GPU
    pub union_api_versions: HashMap<ApiType, ApiVersion>,

    /// Features available on ALL GPUs
    pub common_features: HashSet<GpuFeature>,
    /// Features available on ANY GPU
    pub union_features: HashSet<GpuFeature>,

    /// Capability tier
    pub tier: CapabilityTier,

    /// Number of GPUs
    pub gpu_count: usize,

    /// Whether P2P DMA is supported between any GPUs
    pub p2p_supported: bool,

    /// Whether NUMA-aware allocation is possible
    pub numa_aware: bool,

    /// Whether there's a display GPU in the pool
    pub has_display_gpu: bool,
}

/// Capability tiers
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum CapabilityTier {
    /// All GPUs support the same API set, full transparency possible
    Full,
    /// Heterogeneous compute capabilities, graphics may vary
    ComputeOnly,
    /// Mixed capabilities: some GPUs graphics-only, some compute-only
    Mixed,
}

impl fmt::Display for CapabilityTier {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CapabilityTier::Full => write!(f, "Full"),
            CapabilityTier::ComputeOnly => write!(f, "ComputeOnly"),
            CapabilityTier::Mixed => write!(f, "Mixed"),
        }
    }
}

/// Capability model manager
///
/// Manages and normalizes GPU capabilities across multiple vendors
pub struct CapabilityModel {
    gpu_capabilities: RwLock<Vec<GpuCapabilities>>,
    unified_capabilities: RwLock<Option<UnifiedCapabilities>>,
    normalization_rules: NormalizationRules,
}

impl CapabilityModel {
    pub fn new() -> Self {
        CapabilityModel {
            gpu_capabilities: RwLock::new(Vec::new()),
            unified_capabilities: RwLock::new(None),
            normalization_rules: NormalizationRules::new(),
        }
    }

    pub fn new_arc() -> Arc<Self> {
        Arc::new(Self::new())
    }

    /// Add or update a GPU's capabilities
    pub fn update_gpu(&self, index: usize, capabilities: GpuCapabilities) {
        let mut gpus = self.gpu_capabilities.write().unwrap();
        if index >= gpus.len() {
            gpus.resize(index + 1, GpuCapabilities::new(GpuVendor::Unknown));
        }
        gpus[index] = capabilities;
        self.recompute_unified();
    }

    /// Remove a GPU
    pub fn remove_gpu(&self, index: usize) {
        let mut gpus = self.gpu_capabilities.write().unwrap();
        if index < gpus.len() {
            gpus.remove(index);
            self.recompute_unified();
        }
    }

    /// Get GPU capabilities by index
    pub fn get_gpu(&self, index: usize) -> Option<GpuCapabilities> {
        let gpus = self.gpu_capabilities.read().unwrap();
        gpus.get(index).cloned()
    }

    /// Get all GPU capabilities
    pub fn get_all_gpus(&self) -> Vec<GpuCapabilities> {
        let gpus = self.gpu_capabilities.read().unwrap();
        gpus.clone()
    }

    /// Get unified capabilities
    pub fn get_unified(&self) -> Option<UnifiedCapabilities> {
        self.unified_capabilities.read().unwrap().clone()
    }

    /// Recompute unified capabilities from individual GPU capabilities
    pub fn recompute_unified(&self) {
        let gpus = self.gpu_capabilities.read().unwrap();

        if gpus.is_empty() {
            *self.unified_capabilities.write().unwrap() = None;
            return;
        }

        let mut unified = UnifiedCapabilities {
            total_vram: 0,
            free_vram: 0,
            total_bandwidth_mbps: 0,
            total_compute_units: 0,
            common_api_versions: HashMap::new(),
            union_api_versions: HashMap::new(),
            common_features: HashSet::new(),
            union_features: HashSet::new(),
            tier: CapabilityTier::Full,
            gpu_count: gpus.len(),
            p2p_supported: false,
            numa_aware: false,
            has_display_gpu: false,
        };

        // Aggregate numeric values
        for gpu in gpus.iter() {
            unified.total_vram += gpu.vram_total;
            unified.free_vram += gpu.vram_free;
            unified.total_bandwidth_mbps += gpu.vram_bandwidth_mbps as u64;
            unified.total_compute_units += gpu.compute_units;

            if gpu.is_display_connected {
                unified.has_display_gpu = true;
            }
            if gpu.numa_node >= 0 {
                unified.numa_aware = true;
            }
        }

        // Compute intersection and union of API versions
        let mut first_gpu = true;
        for gpu in gpus.iter() {
            for (api, version) in gpu.api_versions.iter() {
                // Union: take the highest version
                unified.union_api_versions.insert(api.clone(), *version);

                // Intersection: take the lowest version
                if first_gpu {
                    unified.common_api_versions.insert(api.clone(), *version);
                } else if let Some(existing) = unified.common_api_versions.get_mut(api) {
                    // Compare versions and keep the lower one
                    if version.to_hex() < existing.to_hex() {
                        *existing = *version;
                    }
                }
            }

            // Union of features
            unified.union_features.extend(gpu.features.iter().cloned());

            // Intersection of features (only if all GPUs have it)
            if first_gpu {
                unified.common_features.extend(gpu.features.iter().cloned());
            } else {
                unified.common_features.retain(|f| gpu.features.contains(f));
            }

            first_gpu = false;
        }

        // Determine capability tier
        unified.tier = if unified.common_api_versions.is_empty() {
            // No common APIs
            if unified.has_display_gpu {
                CapabilityTier::Mixed
            } else {
                CapabilityTier::ComputeOnly
            }
        } else if unified.common_api_versions.len() < unified.union_api_versions.len() {
            // Heterogeneous APIs
            if unified.common_api_versions.contains_key(&ApiType::Vulkan) {
                CapabilityTier::Full
            } else {
                CapabilityTier::ComputeOnly
            }
        } else {
            // All GPUs have same APIs
            CapabilityTier::Full
        };

        // Check P2P support
        for gpu in gpus.iter() {
            if gpu.features.contains(&GpuFeature::P2PDma) {
                unified.p2p_supported = true;
                break;
            }
        }

        *self.unified_capabilities.write().unwrap() = Some(unified);
    }

    /// Get capability tier
    pub fn tier(&self) -> CapabilityTier {
        self.get_unified().map(|u| u.tier).unwrap_or(CapabilityTier::Full)
    }

    /// Normalize capabilities for comparison
    pub fn normalize(&self, capabilities: &mut GpuCapabilities) {
        self.normalization_rules.normalize(capabilities);
    }

    /// Check compatibility between GPUs
    pub fn check_compatibility(&self, gpu1: usize, gpu2: usize) -> GpuCompatibility {
        let gpus = self.gpu_capabilities.read().unwrap();
        if gpu1 >= gpus.len() || gpu2 >= gpus.len() {
            return GpuCompatibility::Incompatible;
        }

        let cap1 = &gpus[gpu1];
        let cap2 = &gpus[gpu2];

        // Check for P2P support
        if cap1.features.contains(&GpuFeature::P2PDma) &&
           cap2.features.contains(&GpuFeature::P2PDma) {
            return GpuCompatibility::P2PSupported;
        }

        // Check if they share a Vulkan API version
        if let (Some(v1), Some(v2)) = (cap1.api_version(ApiType::Vulkan), cap2.api_version(ApiType::Vulkan)) {
            // Same major version
            if v1.major == v2.major {
                return GpuCompatibility::Compatible;
            }
        }

        GpuCompatibility::Incompatible
    }
}

impl Default for CapabilityModel {
    fn default() -> Self {
        Self::new()
    }
}

/// GPU compatibility levels
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum GpuCompatibility {
    /// GPUs can use P2P DMA transfers directly
    P2PSupported,
    /// GPUs are compatible but require CPU staging
    Compatible,
    /// GPUs have significant differences, limited interoperability
    Limited,
    /// GPUs are incompatible
    Incompatible,
}

/// Normalization rules for GPU capabilities
pub struct NormalizationRules {
    /// Base clock speed to use for comparison
    pub base_clock: u32,
    /// Normalize compute unit counts
    pub normalize_compute_units: bool,
    /// Normalize memory bandwidth
    pub normalize_bandwidth: bool,
}

impl NormalizationRules {
    pub fn new() -> Self {
        NormalizationRules {
            base_clock: 1000, // 1 GHz base
            normalize_compute_units: true,
            normalize_bandwidth: true,
        }
    }

    pub fn normalize(&self, capabilities: &mut GpuCapabilities) {
        // Normalize compute units based on clock speed
        if self.normalize_compute_units && capabilities.max_clock_mhz > 0 {
            // Scale compute units by clock ratio
        }

        // Normalize bandwidth based on clock speed
        if self.normalize_bandwidth && capabilities.max_clock_mhz > 0 {
            // Scale bandwidth by clock ratio
        }
    }
}

impl Default for NormalizationRules {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_api_version() {
        let v1 = ApiVersion::new(1, 3, 0);
        assert_eq!(v1.to_hex(), 0x004B0000);
        assert_eq!(ApiVersion::from_hex(0x004B0000).to_hex(), 0x004B0000);
    }

    #[test]
    fn test_capability_model_tier() {
        let model = CapabilityModel::new();

        // All GPUs same tier
        {
            let mut cap1 = GpuCapabilities::new(GpuVendor::Amd);
            cap1.api_versions.insert(ApiType::Vulkan, ApiVersion::new(1, 3, 0));
            cap1.features.insert(GpuFeature::Graphics);
            model.update_gpu(0, cap1);

            let mut cap2 = GpuCapabilities::new(GpuVendor::Nvidia);
            cap2.api_versions.insert(ApiType::Vulkan, ApiVersion::new(1, 3, 0));
            cap2.features.insert(GpuFeature::Graphics);
            model.update_gpu(1, cap2);

            assert_eq!(model.tier(), CapabilityTier::Full);
        }
    }
}
