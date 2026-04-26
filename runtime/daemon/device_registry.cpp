/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Device Registry Implementation - GPU detection and management
 *
 * SPDX-License-Identifier: MIT
 */

#include "device_registry.hpp"
#include "daemon.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace mvgal {

/* GPU Device implementation */
GpuDevice::GpuDevice(uint32_t index, uint16_t pciVendorId, uint16_t pciDeviceId)
    : m_index(index),
      m_pciVendorId(pciVendorId),
      m_pciDeviceId(pciDeviceId),
      m_vendor(VendorId::UNKNOWN)
{
    /* Detect vendor */
    m_vendor = DeviceRegistry::detectVendor(pciVendorId, pciDeviceId);
    
    /* Build name */
    char name[64];
    snprintf(name, sizeof(name), "GPU%02d: %04x:%04x", index, pciVendorId, pciDeviceId);
    m_name = name;
    
    /* Build PCI path */
    m_pciPath = DeviceRegistry::buildPciPath(pciVendorId, pciDeviceId);
    
    /* Initialize capabilities */
    m_capabilities = {};
    m_capabilities.vramSize = 0;
    m_capabilities.vramFree = 0;
    m_capabilities.apiFlags = 0;
    m_capabilities.numaNode = -1;
    
    /* Initialize state */
    m_state = {};
    m_state.utilization = 0;
    m_state.memoryUsed = 0;
    m_state.temperature = 0;
    m_state.powerDrawWatts = 0;
    m_state.available = true;
    m_state.enabled = true;
    m_state.powerState = GpuState::ACTIVE;
    m_state.lastActivityTime = 0;
}

GpuDevice::~GpuDevice()
{
}

bool GpuDevice::updateCapabilities()
{
    /* Try to query from DRM first */
    if (queryFromDrm()) {
        return true;
    }
    
    /* Fall back to sysfs */
    return queryFromSysfs();
}

bool GpuDevice::updateState()
{
    /* Query state from sysfs */
    return queryFromSysfs();
}

void GpuDevice::setEnabled(bool enabled)
{
    m_state.enabled = enabled;
}

bool GpuDevice::queryFromDrm()
{
    /* In the full implementation:
     * 1. Open /dev/dri/cardX
     * 2. Use DRM ioctls to query capabilities
     * 3. Use ioctl with DRM_IOCTL_* commands
     */
    return false; /* Not yet implemented */
}

bool GpuDevice::queryFromSysfs()
{
    /* Query from sysfs */
    bool updated = false;
    
    /* Try to read VRAM size */
    std::string memTotalPath = m_pciPath + "/resource0";
    std::ifstream memTotalFile(memTotalPath);
    if (memTotalFile) {
        /* Resource0 typically contains the VRAM range */
        /* Format: 0xXXXXX000-0xXXXXXXXX */
        std::string line;
        if (std::getline(memTotalFile, line)) {
            /* Not yet parsing, but we can get size */
            /* TODO: Parse the range */
        }
    }
    
    /* Read utilization */
    std::string gpuBusyPath = m_pciPath + "/gpu_busy_percent";
    std::ifstream busyFile(gpuBusyPath);
    if (busyFile) {
        int utilization;
        if (busyFile >> utilization) {
            m_state.utilization = static_cast<uint32_t>(utilization);
            updated = true;
        }
    }
    
    /* Read temperature */
    std::string tempPath = m_pciPath + "/hwmon/hwmon*/temp1_input";
    /* For now, use a simpler approach - find the hwmon directory */
    std::string hwmonPath = m_pciPath + "/hwmon";
    DIR* dir = opendir(hwmonPath.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_DIR && strncmp(entry->d_name, "hwmon", 5) == 0) {
                std::string tempFile = hwmonPath + "/" + entry->d_name + "/temp1_input";
                std::ifstream tempStream(tempFile);
                if (tempStream) {
                    int temp;
                    if (tempStream >> temp) {
                        m_state.temperature = temp / 1000; /* Convert from millidegrees */
                        updated = true;
                    }
                }
            }
        }
        closedir(dir);
    }
    
    return updated;
}

/* DeviceRegistry implementation */
DeviceRegistry::DeviceRegistry(Daemon* daemon)
    : m_daemon(daemon)
{
}

DeviceRegistry::~DeviceRegistry()
{
}

bool DeviceRegistry::init()
{
    /* Enumerate devices */
    enumerateDrmDevices();
    enumeratePciDevices();
    
    return true;
}

void DeviceRegistry::fini()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gpus.clear();
}

bool DeviceRegistry::enumerateDrmDevices()
{
    auto drmDevices = findDrmDevices();
    
    for (const auto& drmPath : drmDevices) {
        uint16_t vendorId = 0, deviceId = 0;
        
        /* Try to get PCI vendor/device IDs from DRM path */
        /* /dev/dri/cardX -> /sys/class/drm/cardX/device/vendor, device */
        std::string sysDrmPath = "/sys/class/drm/";
        size_t pos = drmPath.find("card");
        if (pos != std::string::npos) {
            std::string cardNum = drmPath.substr(pos + 4);
            sysDrmPath += "card" + cardNum + "/device";
            
            /* Read vendor */
            std::ifstream vendorFile(sysDrmPath + "/vendor");
            if (vendorFile) {
                std::string vendorHex;
                vendorFile >> vendorHex;
                vendorId = static_cast<uint16_t>(std::stoi(vendorHex, nullptr, 16));
            }
            
            /* Read device */
            std::ifstream deviceFile(sysDrmPath + "/device");
            if (deviceFile) {
                std::string deviceHex;
                deviceFile >> deviceHex;
                deviceId = static_cast<uint16_t>(std::stoi(deviceHex, nullptr, 16));
            }
            
            if (vendorId != 0) {
                uint32_t index = static_cast<uint32_t>(m_gpus.size());
                auto gpu = std::make_shared<GpuDevice>(index, vendorId, deviceId);
                gpu->setDrmPath(drmPath);
                m_gpus.push_back(gpu);
            }
        }
    }
    
    return !m_gpus.empty();
}

bool DeviceRegistry::enumeratePciDevices()
{
    auto pciDevices = findPciDevices();
    
    for (const auto& [vendorId, deviceId] : pciDevices) {
        /* Check if this device is already registered via DRM */
        bool found = false;
        for (const auto& gpu : m_gpus) {
            if (gpu->pciVendorId() == vendorId && gpu->pciDeviceId() == deviceId) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            uint32_t index = static_cast<uint32_t>(m_gpus.size());
            auto gpu = std::make_shared<GpuDevice>(index, vendorId, deviceId);
            m_gpus.push_back(gpu);
        }
    }
    
    return true;
}

GpuDevice* DeviceRegistry::getGpu(uint32_t index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < m_gpus.size()) {
        return m_gpus[index].get();
    }
    return nullptr;
}

const GpuDevice* DeviceRegistry::getGpu(uint32_t index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < m_gpus.size()) {
        return m_gpus[index].get();
    }
    return nullptr;
}

GpuDevice* DeviceRegistry::getGpuByVendor(uint32_t vendor)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& gpu : m_gpus) {
        if (gpu->vendor() == vendor) {
            return gpu.get();
        }
    }
    return nullptr;
}

GpuDevice* DeviceRegistry::getDisplayGpu()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& gpu : m_gpus) {
        if (gpu->capabilities().isDisplayConnected) {
            return gpu.get();
        }
    }
    return nullptr;
}

void DeviceRegistry::enableGpu(uint32_t index, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < m_gpus.size()) {
        m_gpus[index]->setEnabled(enabled);
    }
}

void DeviceRegistry::setGpuPriority(uint32_t /*index*/, int /*priority*/)
{
    /* Not yet implemented */
}

void DeviceRegistry::processEvents()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    /* Update state for all GPUs */
    for (auto& gpu : m_gpus) {
        gpu->updateState();
    }
}

uint64_t DeviceRegistry::totalVRAM() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t total = 0;
    for (const auto& gpu : m_gpus) {
        if (gpu->isEnabled() && gpu->isAvailable()) {
            total += gpu->capabilities().vramSize;
        }
    }
    return total;
}

uint64_t DeviceRegistry::freeVRAM() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t free = 0;
    for (const auto& gpu : m_gpus) {
        if (gpu->isEnabled() && gpu->isAvailable()) {
            free += gpu->capabilities().vramFree;
        }
    }
    return free;
}

uint32_t DeviceRegistry::aggregateComputeUnits() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t total = 0;
    for (const auto& gpu : m_gpus) {
        total += gpu->capabilities().computeUnits;
    }
    return total;
}

uint32_t DeviceRegistry::capabilityTier() const
{
    return computeCapabilityTier();
}

uint32_t DeviceRegistry::computeCapabilityTier() const
{
    /* Determine tier based on capabilities */
    uint32_t commonFlags = ~0u;
    uint32_t unionFlags = 0;
    bool hasGraphics = false;
    
    for (const auto& gpu : m_gpus) {
        commonFlags &= gpu->capabilities().apiFlags;
        unionFlags |= gpu->capabilities().apiFlags;
        
        if (gpu->capabilities().apiFlags & ApiFlags::VULKAN) {
            hasGraphics = true;
        }
    }
    
    if (commonFlags == unionFlags) {
        return 0; /* TIER_FULL */
    } else if (!hasGraphics) {
        return 1; /* TIER_COMPUTE_ONLY */
    } else {
        return 2; /* TIER_MIXED */
    }
}

std::vector<std::string> DeviceRegistry::findDrmDevices() const
{
    std::vector<std::string> devices;
    
    DIR* drmDir = opendir("/dev/dri");
    if (drmDir) {
        struct dirent* entry;
        while ((entry = readdir(drmDir)) != nullptr) {
            if (entry->d_type == DT_CHR || entry->d_type == DT_UNKNOWN) {
                if (strncmp(entry->d_name, "card", 4) == 0) {
                    devices.push_back("/dev/dri/" + std::string(entry->d_name));
                }
            }
        }
        closedir(drmDir);
        
        /* Sort by card number */
        std::sort(devices.begin(), devices.end());
    }
    
    return devices;
}

std::vector<std::pair<uint16_t, uint16_t>> DeviceRegistry::findPciDevices() const
{
    std::vector<std::pair<uint16_t, uint16_t>> devices;
    
    DIR* pciDir = opendir("/sys/bus/pci/devices");
    if (pciDir) {
        struct dirent* entry;
        while ((entry = readdir(pciDir)) != nullptr) {
            if (entry->d_type == DT_DIR && strchr(entry->d_name, ':')) {
                std::string deviceLink = "/sys/bus/pci/devices/" + std::string(entry->d_name) + "/device";
                
                FILE* f = fopen(deviceLink.c_str(), "r");
                if (f) {
                    uint16_t vendorId, deviceId;
                    if (fscanf(f, "%04hx:%04hx", &vendorId, &deviceId) == 2) {
                        /* Check if it's a display device */
                        std::string classPath = "/sys/bus/pci/devices/" + std::string(entry->d_name) + "/class";
                        std::ifstream classFile(classPath);
                        if (classFile) {
                            uint32_t classCode;
                            std::string classHex;
                            classFile >> classHex;
                            classCode = static_cast<uint32_t>(std::stoi(classHex, nullptr, 16));
                            
                            /* Class 0x0300 = VGA display controller */
                            if ((classCode >> 16) == 0x0300) {
                                devices.emplace_back(vendorId, deviceId);
                            }
                        }
                    }
                    fclose(f);
                }
            }
        }
        closedir(pciDir);
    }
    
    return devices;
}

uint32_t DeviceRegistry::detectVendor(uint16_t vendorId, uint16_t /*deviceId*/)
{
    switch (vendorId) {
    case 0x1002: return VendorId::AMD;
    case 0x10DE: return VendorId::NVIDIA;
    case 0x8086: return VendorId::INTEL;
    case 0x1A82: return VendorId::MOORE_THREADS;
    default: return VendorId::UNKNOWN;
    }
}

std::string DeviceRegistry::buildPciPath(uint16_t vendorId, uint16_t deviceId)
{
    DIR* pciDir = opendir("/sys/bus/pci/devices");
    if (pciDir) {
        struct dirent* entry;
        while ((entry = readdir(pciDir)) != nullptr) {
            if (entry->d_type == DT_DIR && strchr(entry->d_name, ':')) {
                std::string deviceLink = "/sys/bus/pci/devices/" + std::string(entry->d_name) + "/device";
                
                FILE* f = fopen(deviceLink.c_str(), "r");
                if (f) {
                    uint16_t v, d;
                    if (fscanf(f, "%04hx:%04hx", &v, &d) == 2) {
                        if (v == vendorId && d == deviceId) {
                            closedir(pciDir);
                            return "/sys/bus/pci/devices/" + std::string(entry->d_name);
                        }
                    }
                    fclose(f);
                }
            }
        }
        closedir(pciDir);
    }
    
    return "/sys/bus/pci/devices/" + std::to_string(vendorId) + ":" + std::to_string(deviceId);
}

} // namespace mvgal
