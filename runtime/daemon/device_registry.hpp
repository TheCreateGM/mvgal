/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Device Registry Header - GPU detection and management
 * 
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_DEVICE_REGISTRY_HPP
#define MVGAL_RUNTIME_DEVICE_REGISTRY_HPP

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace mvgal {

/* Forward declaration */
class Daemon;

/**
 * GPU capabilities
 */
struct GpuCapabilities {
    uint64_t vramSize;           /* Total VRAM in bytes */
    uint64_t vramFree;           /* Free VRAM in bytes */
    uint32_t vramBandwidth;      /* Memory bandwidth in MB/s */
    uint32_t computeUnits;       /* Number of compute units */
    uint32_t apiFlags;           /* Bitmask of supported APIs */
    uint32_t pcieGen;           /* PCIe generation */
    uint32_t pcieLanes;         /* PCIe lane count */
    int32_t numaNode;           /* NUMA node (-1 if unknown) */
    uint32_t maxClockMhz;       /* Maximum clock in MHz */
    uint32_t currentClockMhz;    /* Current clock in MHz */
    bool isDisplayConnected;     /* GPU drives a display */
};

/**
 * GPU state
 */
struct GpuState {
    uint32_t utilization;         /* Current utilization percentage */
    uint32_t memoryUsed;          /* Used memory percentage */
    int32_t temperature;        /* Temperature in Celsius */
    int32_t powerDrawWatts;      /* Current power draw in watts */
    bool available;             /* GPU is available for workloads */
    bool enabled;              /* GPU is enabled in MVGAL pool */
    enum PowerState { ACTIVE, SUSTAINED, IDLE, PARK } powerState;
    uint64_t lastActivityTime;  /* Last time GPU was used (monotonic ns) */
};

/**
 * API support flags
 */
namespace ApiFlags {
    constexpr uint32_t VULKAN = (1 << 0);
    constexpr uint32_t OPENGL = (1 << 1);
    constexpr uint32_t OPENCL = (1 << 2);
    constexpr uint32_t CUDA   = (1 << 3);
    constexpr uint32_t SYCL   = (1 << 4);
}

/**
 * Vendor IDs
 */
namespace VendorId {
    constexpr uint32_t UNKNOWN = 0;
    constexpr uint32_t AMD = 1;
    constexpr uint32_t NVIDIA = 2;
    constexpr uint32_t INTEL = 3;
    constexpr uint32_t MOORE_THREADS = 4;
}

/**
 * GPU device representation
 */
class GpuDevice {
public:
    GpuDevice(uint32_t index, uint16_t pciVendorId, uint16_t pciDeviceId);
    ~GpuDevice();

    uint32_t index() const { return m_index; }
    uint16_t pciVendorId() const { return m_pciVendorId; }
    uint16_t pciDeviceId() const { return m_pciDeviceId; }
    uint32_t vendor() const { return m_vendor; }
    const std::string& name() const { return m_name; }
    const std::string& pciPath() const { return m_pciPath; }
    const std::string& drmPath() const { return m_drmPath; }
    const GpuCapabilities& capabilities() const { return m_capabilities; }
    GpuState& state() { return m_state; }
    const GpuState& state() const { return m_state; }

    bool isEnabled() const { return m_state.enabled; }
    bool isAvailable() const { return m_state.available; }
    uint32_t utilization() const { return m_state.utilization; }

    /* Update capabilities from sysfs/DRM */
    bool updateCapabilities();
    
    /* Update state from sysfs/DRM */
    bool updateState();

    /* Enable/disable GPU */
    void setEnabled(bool enabled);
    
    /* Query from DRM */
    bool queryFromDrm();
    
    /* Query from sysfs */
    bool queryFromSysfs();
    
    /* Set DRM path */
    void setDrmPath(const std::string& path) { m_drmPath = path; }

private:
    uint32_t m_index;
    uint16_t m_pciVendorId;
    uint16_t m_pciDeviceId;
    uint32_t m_vendor;
    std::string m_name;
    std::string m_pciPath;      /* /sys/bus/pci/devices/0000:01:00.0 */
    std::string m_drmPath;      /* /dev/dri/card0 */
    GpuCapabilities m_capabilities;
    GpuState m_state;
};

/**
 * Device Registry - Manages all GPU devices detected by MVGAL
 */
class DeviceRegistry {
public:
    explicit DeviceRegistry(Daemon* daemon);
    ~DeviceRegistry();

    bool init();
    void fini();

    /* Enumerate GPUs using DRM */
    bool enumerateDrmDevices();
    
    /* Enumerate GPUs using PCI */
    bool enumeratePciDevices();
    
    /* Re-enumerate (for hotplug support) */
    void reenum() { enumerateDrmDevices(); enumeratePciDevices(); }

    /* Get GPU by index */
    GpuDevice* getGpu(uint32_t index);
    const GpuDevice* getGpu(uint32_t index) const;
    
    /* Get GPU by vendor */
    GpuDevice* getGpuByVendor(uint32_t vendor);
    
    /* Get display-connected GPU */
    GpuDevice* getDisplayGpu();
    
    /* GPU count */
    uint32_t gpuCount() const { return static_cast<uint32_t>(m_gpus.size()); }
    
    /* Enable/disable GPU */
    void enableGpu(uint32_t index, bool enabled);
    
    /* Set GPU priority (for scheduling) */
    void setGpuPriority(uint32_t index, int priority);
    
    /* Process device events (hotplug, etc.) */
    void processEvents();

    /* Statistics */
    uint64_t totalVRAM() const;
    uint64_t freeVRAM() const;
    uint32_t aggregateComputeUnits() const;
    uint32_t capabilityTier() const;

public:
    /* Helper to detect vendor from PCI IDs */
    static uint32_t detectVendor(uint16_t vendorId, uint16_t deviceId);
    
    /* Helper to build PCI path */
    static std::string buildPciPath(uint16_t vendorId, uint16_t deviceId);

private:
    Daemon* m_daemon;
    
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<GpuDevice>> m_gpus;
    
    /* Algorithm for capability tier */
    uint32_t computeCapabilityTier() const;
    
    /* Helper to find DRM devices */
    std::vector<std::string> findDrmDevices() const;
    
    /* Helper to find PCI devices */
    std::vector<std::pair<uint16_t, uint16_t>> findPciDevices() const;
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_DEVICE_REGISTRY_HPP
