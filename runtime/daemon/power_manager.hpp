/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * Power Manager Header - Idle detection, power gating, DVFS control
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_POWER_MANAGER_HPP
#define MVGAL_RUNTIME_POWER_MANAGER_HPP

#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>

namespace mvgal {

class Daemon;
class DeviceRegistry;


/**
 * Power state enum
 */
enum class PowerState {
    ACTIVE = 0,       /* GPU is fully active */
    SUSTAINED = 1,    /* GPU in sustained performance mode */
    IDLE = 2,         /* GPU is idle */
    PARK = 3,         /* GPU is parked (completely off) */
};

/**
 * Thermal state enum
 */
enum class ThermalState {
    NORMAL = 0,       /* Temperature is normal */
    WARN = 1,         /* Temperature is elevated */
    HOT = 2,          /* Temperature is high */
    THROTTLING = 3,   /* Temperature is at throttle point */
    CRITICAL = 4,     /* Temperature is at shutdown */
};

/**
 * Power statistics for a GPU
 */
struct GpuPowerStats {
    uint64_t powerDrawMilliwatts;  /* Current power draw in milliwatts */
    uint64_t energyConsumedMillis; /* Energy consumed in milliwatt-hours */
    uint64_t activeTimeUs;          /* Time spent in active state in microseconds */
    uint64_t idleTimeUs;            /* Time spent in idle state in microseconds */
    uint64_t transitions;          /* Number of power state transitions */
    int32_t currentTemperature;     /* Current temperature in Celsius */
    int32_t maxTemperature;        /* Maximum temperature seen */
};

/**
 * GPU power profile
 */
struct GpuPowerProfile {
    PowerState state;
    uint32_t minClockMhz;
    uint32_t maxClockMhz;
    uint32_t targetClockMhz;
    uint32_t voltageMv;
    uint32_t powerLimitWatts;
    bool dynamicBoost;    /* Allow boost above target clock */
};

/**
 * Power management configuration
 */
struct PowerConfig {
    /* Idle detection */
    uint32_t idleTimeoutMs;       /* Time without workload before going idle */
    uint32_t sustainedTimeoutMs;   /* Time in idle before going to sustained */
    uint32_t parkTimeoutMs;        /* Time in sustained before parking */
    
    /* Thermal management */
    uint32_t thermalThreshold;    /* Temperature threshold for throttling (C) */
    uint32_t criticalThreshold;   /* Temperature threshold for shutdown (C) */
    uint32_t thermalPollIntervalMs; /* How often to check temperature */
    
    /* Power limits */
    uint32_t powerLimitWatts;     /* Global power limit */
    bool enablePowerCapping;      /* Enable power capping */
    
    /* DVFS */
    bool enableDvfs;              /* Enable dynamic frequency scaling */
    uint32_t dvfsPollIntervalMs;  /* How often to adjust frequencies */
    
    /* Flags */
    bool enableAggressivePowerDown; /* Aggressively power down idle GPUs */
};

/**
 * Power Manager - Handles idle detection, power gating, and DVFS
 */
class PowerManager {
public:
    PowerManager(Daemon* daemon, DeviceRegistry* deviceRegistry);
    ~PowerManager();

    bool init();
    void fini();

    /* Process power management events */
    void processEvents();

    /* Set power state for a GPU */
    bool setPowerState(uint32_t gpuIndex, PowerState state);
    
    /* Get current power state for a GPU */
    PowerState getPowerState(uint32_t gpuIndex) const;

    /* Get power stats for a GPU */
    GpuPowerStats getPowerStats(uint32_t gpuIndex) const;

    /* Set thermal threshold */
    void setThermalThreshold(uint32_t gpuIndex, uint32_t thresholdCelsius);
    
    /* Get thermal state */
    ThermalState getThermalState(uint32_t gpuIndex) const;

    /* Set power configuration */
    void setConfig(const PowerConfig& config);
    
    /* Get current configuration */
    const PowerConfig& config() const { return m_config; }

    /* Notify that a GPU has been used */
    void notifyActivity(uint32_t gpuIndex);
    
    /* Enable/disable power management for a GPU */
    void setEnabled(uint32_t gpuIndex, bool enabled);

    /* Update thermal data from sysfs */
    void updateThermalData();

    /* Apply DVFS settings */
    void applyDvfs();

private:
    Daemon* m_daemon;
    DeviceRegistry* m_deviceRegistry;
    mutable std::mutex m_mutex;
    
    /* Power configuration */
    PowerConfig m_config;
    
    /* Per-GPU state */
    struct GpuPowerInfo {
        PowerState state;
        PowerState targetState;
        GpuPowerStats stats;
        std::chrono::nanoseconds lastActivityTime;
        std::chrono::nanoseconds lastStateChange;
        ThermalState thermalState;
        int32_t lastTemperature;
        bool enabled;
        bool inThermalThrottle;
    };
    std::vector<GpuPowerInfo> m_gpuPowerInfo;
    
    /* Timers */
    std::chrono::nanoseconds m_lastThermalUpdate;
    std::chrono::nanoseconds m_lastDvfsUpdate;
    
    /* DVFS state */
    struct DvfsState {
        uint32_t lastUtilization;
        uint32_t targetClockMhz;
        uint32_t currentClockMhz;
    };
    std::vector<DvfsState> m_dvfsState;

    /* Helper functions */
    void checkIdleTimeouts();
    void checkThermalState();
    void transitionGpuPower(uint32_t gpuIndex, PowerState newState);
    bool readPowerDraw(uint32_t gpuIndex, uint32_t* milliwatts);
    bool readTemperature(uint32_t gpuIndex, int32_t* temperatureCelsius);
    bool writeFrequency(uint32_t gpuIndex, uint32_t mhz);
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_POWER_MANAGER_HPP
