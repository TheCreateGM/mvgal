/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * Power Manager Implementation - Idle detection, power gating, DVFS
 * SPDX-License-Identifier: MIT
 */

#include "power_manager.hpp"
#include "device_registry.hpp"
#include "daemon.hpp"
#include <fstream>
#include <sstream>
#include <thread>
#include <cmath>

namespace mvgal {

PowerManager::PowerManager(Daemon* daemon, DeviceRegistry* deviceRegistry)
    : m_daemon(daemon),
      m_deviceRegistry(deviceRegistry)
{
    /* Initialize default configuration */
    m_config.idleTimeoutMs = 5000;       /* 5 seconds */
    m_config.sustainedTimeoutMs = 10000;  /* 10 seconds */
    m_config.parkTimeoutMs = 30000;       /* 30 seconds */
    m_config.thermalThreshold = 85;       /* 85 C */
    m_config.criticalThreshold = 95;     /* 95 C */
    m_config.thermalPollIntervalMs = 1000; /* 1 second */
    m_config.powerLimitWatts = 0;         /* No limit */
    m_config.enablePowerCapping = false;
    m_config.enableDvfs = true;
    m_config.dvfsPollIntervalMs = 2000;   /* 2 seconds */
    m_config.enableAggressivePowerDown = false;
}

PowerManager::~PowerManager()
{
}

bool PowerManager::init()
{
    uint32_t gpuCount = m_deviceRegistry->gpuCount();
    
    m_gpuPowerInfo.resize(gpuCount);
    m_dvfsState.resize(gpuCount);
    
    for (uint32_t i = 0; i < gpuCount; i++) {
        auto ts = std::chrono::high_resolution_clock::now().time_since_epoch();
        
        m_gpuPowerInfo[i] = {
            PowerState::ACTIVE,      /* state */
            PowerState::ACTIVE,      /* targetState */
            {0, 0, 0, 0, 0, 0, 0},   /* stats */
            ts,                      /* lastActivityTime */
            ts,                      /* lastStateChange */
            ThermalState::NORMAL,    /* thermalState */
            0,                       /* lastTemperature */
            true,                    /* enabled */
            false                     /* inThermalThrottle */
        };
        
        m_dvfsState[i] = {
            0,                       /* lastUtilization */
            0,                       /* targetClockMhz */
            0                        /* currentClockMhz */
        };
    }
    
    m_lastThermalUpdate = std::chrono::high_resolution_clock::now().time_since_epoch();
    m_lastDvfsUpdate = m_lastThermalUpdate;
    
    return true;
}

void PowerManager::fini()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gpuPowerInfo.clear();
    m_dvfsState.clear();
}

void PowerManager::processEvents()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    /* Check idle timeouts */
    checkIdleTimeouts();
    
    /* Check thermal state */
    auto thermalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastThermalUpdate).count();
    if (thermalElapsed >= static_cast<int64_t>(m_config.thermalPollIntervalMs)) {
        updateThermalData();
        checkThermalState();
        m_lastThermalUpdate = now;
    }
    
    /* Apply DVFS */
    if (m_config.enableDvfs) {
        auto dvfsElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastDvfsUpdate).count();
        if (dvfsElapsed >= static_cast<int64_t>(m_config.dvfsPollIntervalMs)) {
            applyDvfs();
            m_lastDvfsUpdate = now;
        }
    }
}

bool PowerManager::setPowerState(uint32_t gpuIndex, PowerState state)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_gpuPowerInfo.size()) {
        return false;
    }
    
    transitionGpuPower(gpuIndex, state);
    return true;
}

PowerState PowerManager::getPowerState(uint32_t gpuIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_gpuPowerInfo.size()) {
        return PowerState::ACTIVE;
    }
    
    return m_gpuPowerInfo[gpuIndex].state;
}

GpuPowerStats PowerManager::getPowerStats(uint32_t gpuIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_gpuPowerInfo.size()) {
        return {0, 0, 0, 0, 0, 0, 0};
    }
    
    return m_gpuPowerInfo[gpuIndex].stats;
}

void PowerManager::setThermalThreshold(uint32_t /*gpuIndex*/, uint32_t thresholdCelsius)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    /* Not per-GPU yet */
    m_config.thermalThreshold = thresholdCelsius;
}

ThermalState PowerManager::getThermalState(uint32_t gpuIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_gpuPowerInfo.size()) {
        return ThermalState::NORMAL;
    }
    
    return m_gpuPowerInfo[gpuIndex].thermalState;
}

void PowerManager::setConfig(const PowerConfig& config)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void PowerManager::notifyActivity(uint32_t gpuIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_gpuPowerInfo.size()) {
        return;
    }
    
    auto& info = m_gpuPowerInfo[gpuIndex];
    info.lastActivityTime = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    /* If GPU was in a low-power state, wake it up */
    if (info.state != PowerState::ACTIVE && !info.inThermalThrottle) {
        transitionGpuPower(gpuIndex, PowerState::ACTIVE);
    }
    
    /* Update stats */
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        now - info.lastStateChange).count();
    
    if (info.state == PowerState::ACTIVE) {
        info.stats.activeTimeUs += static_cast<uint64_t>(duration);
    } else {
        info.stats.idleTimeUs += static_cast<uint64_t>(duration);
    }
    
    info.lastStateChange = now;
}

void PowerManager::setEnabled(uint32_t gpuIndex, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex < m_gpuPowerInfo.size()) {
        m_gpuPowerInfo[gpuIndex].enabled = enabled;
    }
}

void PowerManager::updateThermalData()
{
    for (uint32_t i = 0; i < m_gpuPowerInfo.size(); i++) {
        int32_t temp;
        if (readTemperature(i, &temp)) {
            m_gpuPowerInfo[i].lastTemperature = temp;
        }
        
        uint32_t milliwatts;
        if (readPowerDraw(i, &milliwatts)) {
            m_gpuPowerInfo[i].stats.powerDrawMilliwatts = milliwatts;
        }
        
        // Update max temperature
        if (temp > m_gpuPowerInfo[i].stats.maxTemperature) {
            m_gpuPowerInfo[i].stats.maxTemperature = temp;
        }
    }
}

void PowerManager::applyDvfs()
{
    if (!m_config.enableDvfs) {
        return;
    }
    
    for (uint32_t i = 0; i < m_dvfsState.size(); i++) {
        const auto* gpu = m_deviceRegistry->getGpu(i);
        if (!gpu) {
            continue;
        }
        
        uint32_t utilization = gpu->utilization();
        m_dvfsState[i].lastUtilization = utilization;
        
        /* Simple DVFS: scale frequency with utilization */
        /* Get max clock from capabilities (not yet available) */
        uint32_t max_clock = 2000; /* Assume 2 GHz max */
        uint32_t min_clock = 500;  /* Assume 500 MHz min */
        
        /* Target clock = min + (max - min) * utilization/100 */
        uint32_t target_clock = min_clock + 
            static_cast<uint32_t>((max_clock - min_clock) * (utilization / 100.0));
        
        m_dvfsState[i].targetClockMhz = target_clock;
        
        /* Apply to GPU if different */
        if (m_dvfsState[i].currentClockMhz != target_clock) {
            if (writeFrequency(i, target_clock)) {
                m_dvfsState[i].currentClockMhz = target_clock;
            }
        }
    }
}

void PowerManager::checkIdleTimeouts()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    for (uint32_t i = 0; i < m_gpuPowerInfo.size(); i++) {
        auto& info = m_gpuPowerInfo[i];
        
        if (!info.enabled || info.inThermalThrottle) {
            continue;
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - info.lastActivityTime).count();
        
        PowerState newState = info.state;
        
        switch (info.state) {
        case PowerState::ACTIVE:
            if (elapsed >= static_cast<int64_t>(m_config.idleTimeoutMs)) {
                newState = PowerState::SUSTAINED;
            }
            break;
        case PowerState::SUSTAINED:
            if (elapsed >= static_cast<int64_t>(m_config.idleTimeoutMs + m_config.sustainedTimeoutMs)) {
                newState = PowerState::IDLE;
            } else if (elapsed < static_cast<int64_t>(m_config.idleTimeoutMs)) {
                newState = PowerState::ACTIVE;
            }
            break;
        case PowerState::IDLE:
            if (elapsed >= static_cast<int64_t>(m_config.idleTimeoutMs + 
                                                   m_config.sustainedTimeoutMs + 
                                                   m_config.parkTimeoutMs)) {
                if (m_config.enableAggressivePowerDown) {
                    newState = PowerState::PARK;
                }
            } else if (elapsed < static_cast<int64_t>(m_config.idleTimeoutMs + m_config.sustainedTimeoutMs)) {
                newState = PowerState::ACTIVE;
            }
            break;
        case PowerState::PARK:
            if (elapsed < static_cast<int64_t>(m_config.idleTimeoutMs + 
                                                   m_config.sustainedTimeoutMs + 
                                                   m_config.parkTimeoutMs)) {
                newState = PowerState::ACTIVE;
            }
            break;
        }
        
        if (newState != info.state) {
            transitionGpuPower(i, newState);
        }
    }
}

void PowerManager::checkThermalState()
{
    for (uint32_t i = 0; i < m_gpuPowerInfo.size(); i++) {
        auto& info = m_gpuPowerInfo[i];
        int32_t temp = info.lastTemperature;
        
        /* Update thermal state */
        if (temp >= static_cast<int32_t>(m_config.criticalThreshold)) {
            info.thermalState = ThermalState::CRITICAL;
            info.inThermalThrottle = true;
        } else if (temp >= static_cast<int32_t>(m_config.thermalThreshold)) {
            info.thermalState = ThermalState::THROTTLING;
            info.inThermalThrottle = true;
        } else if (temp >= static_cast<int32_t>(m_config.thermalThreshold - 5)) {
            info.thermalState = ThermalState::HOT;
            info.inThermalThrottle = false;
        } else if (temp >= static_cast<int32_t>(m_config.thermalThreshold - 15)) {
            info.thermalState = ThermalState::WARN;
            info.inThermalThrottle = false;
        } else {
            info.thermalState = ThermalState::NORMAL;
            info.inThermalThrottle = false;
        }
        
        /* If in thermal throttle, force to active state to allow throttling */
        if (info.inThermalThrottle && info.state != PowerState::ACTIVE) {
            transitionGpuPower(i, PowerState::ACTIVE);
        }
    }
}

void PowerManager::transitionGpuPower(uint32_t gpuIndex, PowerState newState)
{
    auto& info = m_gpuPowerInfo[gpuIndex];
    
    if (info.state == newState) {
        return;
    }
    
    info.state = newState;
    info.targetState = newState;
    info.lastStateChange = std::chrono::high_resolution_clock::now().time_since_epoch();
    info.stats.transitions++;
    
    /* In a full implementation, we would:
     * 1. Call vendor-specific power state function
     * 2. For NVIDIA: nvidia-smi or NVML
     * 3. For AMD: sysfs power_dpm_force_performance_level
     * 4. For Intel: RPS sysfs
     */
    
    /* Dimple debug log */
}

bool PowerManager::readPowerDraw(uint32_t gpuIndex, uint32_t* milliwatts)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* Try to read power from sysfs
     * Example path: /sys/class/drm/cardX/device/hwmon/<hwmonN>/power1_average
     * or /sys/bus/pci/devices/.../power/...
     *
     * For now, we read from the amdgpu/nvidia power interface if available.
     */
    
    /* Simplified: return a default value for now */
    *milliwatts = 100 * 1000; /* 100W in mW */
    return true;
}

bool PowerManager::readTemperature(uint32_t gpuIndex, int32_t* temperatureCelsius)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* Try to read temperature from sysfs
     * Example path: /sys/class/drm/cardX/device/hwmon/<hwmonN>/temp1_input
     * The file contains temperature in millidegrees Celsius.
     */
    
    /* For now, use the GPU's spot field if available */
    // *temperatureCelsius = gpu->state().temperature;
    
    /* Simplified: return a default temperature */
    *temperatureCelsius = 50; /* 50 C */
    return true;
}

bool PowerManager::writeFrequency(uint32_t gpuIndex, uint32_t /*mhz*/)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* In a full implementation:
     * 1. For AMD: modify /sys/class/drm/cardX/device/pp_od_clk_voltage
     * 2. For NVIDIA: Use nvidia-smi or NVML
     * 3. For Intel: Use /sys/class/drm/cardX/gt/gt0/rps_boost_freq_mhz
     */
    
    return true; /* Assume success for now */
}

} // namespace mvgal
