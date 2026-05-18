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
 * Fan curve point (temperature -> fan speed)
 */
struct FanCurvePoint {
    int32_t temperatureCelsius;   /* Temperature point */
    uint8_t fanSpeedPercent;      /* Fan speed at this temperature (0-100) */
};

/**
 * Power curve point (GPU utilization % -> power limit % of TDP)
 */
struct PowerCurvePoint {
    uint8_t utilizationPercent; /* 0-100 */
    uint8_t powerLimitPercent;  /* 0-100 of per-GPU TDP */
};

#define MVGAL_MAX_POWER_CURVE_POINTS 8

struct GpuPowerCurve {
    PowerCurvePoint points[MVGAL_MAX_POWER_CURVE_POINTS];
    uint32_t numPoints;
};

/**
 * Fan curve definition
 */
#define MVGAL_MAX_FAN_CURVE_POINTS 10

struct GpuFanCurve {
    FanCurvePoint points[MVGAL_MAX_FAN_CURVE_POINTS];
    uint32_t numPoints;
};

/**
 * Per-GPU fan control state
 */
struct GpuFanControl {
    bool enabled;
    bool automatic;               /* Auto fan curve vs manual override */
    uint8_t manualSpeedPercent;   /* Manual fan speed if not automatic */
    uint8_t currentSpeedPercent;  /* Current fan speed */
    uint32_t currentRpm;          /* Current fan RPM (0 if not available) */
};

/**
 * PSU headroom information
 */
struct PsuHeadroom {
    uint32_t totalCapacityWatts;  /* Total PSU capacity */
    uint32_t currentDrawWatts;    /* Current total draw */
    uint32_t headroomWatts;       /* Available headroom */
    float safetyMarginPercent;    /* Safety margin (e.g., 20.0 = 20%) */
};

/**
 * PSU configuration
 */
struct PsuConfig {
    uint32_t totalCapacityWatts;  /* Total PSU capacity in watts */
    uint32_t safetyMarginPercent; /* Safety margin percentage (default 20) */
    bool enableHeadroomTracking;  /* Enable PSU headroom tracking */
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

    /* Fan control */
    void setFanCurve(uint32_t gpuIndex, const GpuFanCurve& curve);
    GpuFanCurve getFanCurve(uint32_t gpuIndex) const;
    void setFanSpeed(uint32_t gpuIndex, uint8_t speedPercent);
    void setFanAutomatic(uint32_t gpuIndex, bool automatic);
    void applyFanControl();

    /* PSU headroom */
    void setPsuConfig(const PsuConfig& config);
    PsuHeadroom getPsuHeadroom() const;
    const PsuConfig& psuConfig() const { return m_psuConfig; }

    /* Per-GPU power curve (utilization -> power cap) */
    void setPowerCurve(uint32_t gpuIndex, const GpuPowerCurve& curve);
    GpuPowerCurve getPowerCurve(uint32_t gpuIndex) const;
    void applyPowerCurves();

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
        GpuFanControl fanControl;
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

    /* Fan curves */
    std::vector<GpuFanCurve> m_fanCurves;

    /* Power curves */
    std::vector<GpuPowerCurve> m_powerCurves;

    /* PSU configuration */
    PsuConfig m_psuConfig;

    /* Helper functions */
    void checkIdleTimeouts();
    void checkThermalState();
    void transitionGpuPower(uint32_t gpuIndex, PowerState newState);
    bool readPowerDraw(uint32_t gpuIndex, uint32_t* milliwatts);
    bool readTemperature(uint32_t gpuIndex, int32_t* temperatureCelsius);
    bool writeFrequency(uint32_t gpuIndex, uint32_t mhz);

    /* Fan helpers */
    uint8_t interpolateFanSpeed(const GpuFanCurve& curve, int32_t temperature) const;
    bool readFanSpeed(uint32_t gpuIndex, uint32_t* rpm);
    bool writeFanSpeed(uint32_t gpuIndex, uint8_t speedPercent);

    /* PSU helpers */
    void updatePsuHeadroom();

    uint8_t interpolatePowerLimit(const GpuPowerCurve& curve, uint8_t utilization) const;
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_POWER_MANAGER_HPP
