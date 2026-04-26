/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * Metrics Collector Header - GPU utilization, bandwidth, latency telemetry
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_METRICS_COLLECTOR_HPP
#define MVGAL_RUNTIME_METRICS_COLLECTOR_HPP

#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>

namespace mvgal {

class DeviceRegistry;
class Daemon;


/**
 * Metric types
 */
enum class MetricType {
    GPU_UTILIZATION,     /* GPU core utilization percentage */
    MEMORY_UTILIZATION,  /* Memory utilization percentage */
    MEMORY_BANDWIDTH,    /* Memory bandwidth in MB/s */
    SUBMIT_LATENCY,      /* Workload submission latency in us */
    EXECUTION_TIME,      /* Workload execution time in us */
    WAIT_TIME,           /* Time spent waiting for dependencies */
    TEMPERATURE,         /* Temperature in Celsius */
    POWER_DRAW,          /* Power draw in milliwatts */
    CLOCK_SPEED,        /* Clock speed in MHz */
    QUEUE_DEPTH,         /* Number of queued workloads */
    FENCE_LATENCY,       /* Time between fence submission and signal */
};

/**
 * Metric sample
 */
struct MetricSample {
    MetricType type;
    uint32_t gpuIndex;    /* Which GPU this sample is from */
    uint64_t timestampNs; /* Timestamp in nanoseconds since epoch */
    uint64_t value;       /* The metric value */
    uint64_t min;         /* Minimum value in this collection interval */
    uint64_t max;         /* Maximum value in this collection interval */
    uint64_t average;     /* Average value in this collection interval */
};

/**
 * Metrics for a single GPU
 */
struct GpuMetrics {
    /* Curent values */
    uint32_t gpuUtilization;      /* 0-100% */
    uint32_t memoryUtilization;   /* 0-100% */
    uint64_t memoryBandwidthRead; /* Bytes/s */
    uint64_t memoryBandwidthWrite;/* Bytes/s */
    int32_t temperature;          /* Celsius */
    uint32_t powerDraw;           /* milliwatts */
    uint32_t clockSpeed;          /* MHz */
    uint32_t queueDepth;          /* Number of queued workloads */
    
    /* Historical */
    uint64_t totalSubmitLatencyUs;
    uint64_t totalExecutionTimeUs;
    uint64_t totalWaitTimeUs;
    uint64_t totalWorkloads;
    uint64_t minSubmitLatencyUs;
    uint64_t maxSubmitLatencyUs;
    
    /* Timestamp of last update */
    std::chrono::nanoseconds lastUpdate;
};

/**
 * Telemetry subscription
 */
struct TelemetrySubscription {
    uint32_t clientId;
    std::vector<MetricType> metricTypes;
    uint32_t updateIntervalMs;    /* How often to send updates */
    std::chrono::nanoseconds lastSent;
};

/**
 * Metrics Collector - Collects and reports GPU telemetry
 */
class MetricsCollector {
public:
    MetricsCollector(Daemon* daemon, DeviceRegistry* deviceRegistry);
    ~MetricsCollector();

    bool init();
    void fini();

    /* Process metrics collection */
    void processEvents();

    /* Collect metrics for a specific GPU */
    void collectGpuMetrics(uint32_t gpuIndex);
    
    /* Collect metrics for all GPUs */
    void collectAllMetrics();

    /* Get metrics for a GPU */
    GpuMetrics getGpuMetrics(uint32_t gpuIndex) const;
    
    /* Get aggregate metrics */
    void getAggregateMetrics(uint64_t* totalUtilization, uint64_t* totalBandwidth) const;

    /* Sample a specific metric */
    void sampleMetric(MetricType type, uint32_t gpuIndex, uint64_t value);
    
    /* Subscribe to telemetry */
    uint32_t subscribeTelemetry(uint32_t clientId, const std::vector<MetricType>& metricTypes, uint32_t updateIntervalMs);
    
    /* Unsubscribe from telemetry */
    void unsubscribeTelemetry(uint32_t subscriptionId);
    
    /* Check for pending telemetry updates */
    void checkTelemetryUpdates();
    
    /* Set metrics collection interval */
    void setCollectionInterval(uint32_t intervalMs);
    
    /* Get metrics as JSON */
    std::string getMetricsJson() const;

private:
    Daemon* m_daemon;
    DeviceRegistry* m_deviceRegistry;
    mutable std::mutex m_mutex;
    
    /* Metrics collection configuration */
    uint32_t m_collectionIntervalMs;
    
    /* Per-GPU metrics */
    std::vector<GpuMetrics> m_gpuMetrics;
    
    /* Historical metrics */
    std::vector<std::vector<MetricSample>> m_samplesByGpu;
    
    /* Telemetry subscriptions */
    uint32_t m_nextSubscriptionId;
    std::vector<TelemetrySubscription> m_subscriptions;
    
    /* Statistics */
    std::vector<uint64_t> m_totalWorkloads;
    std::vector<uint64_t> m_totalExecutionTimeUs;
    
    /* Last collection time */
    std::chrono::nanoseconds m_lastCollection;
    
    /* Helper functions */
    void updateGpuMetrics(uint32_t gpuIndex, const GpuMetrics& metrics);
    bool readGpuUtilization(uint32_t gpuIndex, uint32_t* utilization);
    bool readMemoryUtilization(uint32_t gpuIndex, uint32_t* utilization);
    bool readMemoryBandwidth(uint32_t gpuIndex, uint64_t* readBps, uint64_t* writeBps);
    bool readTemperature(uint32_t gpuIndex, int32_t* temperature);
    bool readClockSpeed(uint32_t gpuIndex, uint32_t* clockMhz);
    bool readPowerDraw(uint32_t gpuIndex, uint32_t* milliwatts);
    bool readQueueDepth(uint32_t gpuIndex, uint32_t* depth);
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_METRICS_COLLECTOR_HPP
