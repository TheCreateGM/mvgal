/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * Metrics Collector Implementation - GPU utilization, bandwidth, latency telemetry
 * SPDX-License-Identifier: MIT
 */

#include "metrics_collector.hpp"
#include "device_registry.hpp"
#include "daemon.hpp"
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>

namespace mvgal {

MetricsCollector::MetricsCollector(Daemon* daemon, DeviceRegistry* deviceRegistry)
    : m_daemon(daemon),
      m_deviceRegistry(deviceRegistry),
      m_collectionIntervalMs(1000), /* 1 second default */
      m_nextSubscriptionId(1)
{
}

MetricsCollector::~MetricsCollector()
{
}

bool MetricsCollector::init()
{
    uint32_t gpuCount = m_deviceRegistry->gpuCount();
    
    m_gpuMetrics.resize(gpuCount);
    m_samplesByGpu.resize(gpuCount);
    m_totalWorkloads.resize(gpuCount, 0);
    m_totalExecutionTimeUs.resize(gpuCount, 0);
    
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    for (uint32_t i = 0; i < gpuCount; i++) {
        m_gpuMetrics[i] = {
            0, 0,            /* gpuUtilization, memoryUtilization */
            0, 0,           /* memoryBandwidthRead, memoryBandwidthWrite */
            0, 0, 0, 0,    /* temperature, powerDraw, clockSpeed, queueDepth */
            0, 0, 0,       /* totalSubmitLatencyUs, totalExecutionTimeUs, totalWaitTimeUs */
            0,             /* totalWorkloads */
            UINT64_MAX, 0,/* minSubmitLatencyUs, maxSubmitLatencyUs */
            now            /* lastUpdate */
        };
    }
    
    m_lastCollection = now;
    
    return true;
}

void MetricsCollector::fini()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gpuMetrics.clear();
    m_samplesByGpu.clear();
    m_subscriptions.clear();
}

void MetricsCollector::processEvents()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastCollection).count();
    
    if (elapsed >= static_cast<int64_t>(m_collectionIntervalMs)) {
        collectAllMetrics();
        m_lastCollection = now;
    }
    
    checkTelemetryUpdates();
}

void MetricsCollector::collectGpuMetrics(uint32_t gpuIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_gpuMetrics.size()) {
        return;
    }
    
    GpuMetrics metrics;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    /* Collect all metrics */
    readGpuUtilization(gpuIndex, &metrics.gpuUtilization);
    readMemoryUtilization(gpuIndex, &metrics.memoryUtilization);
    readMemoryBandwidth(gpuIndex, &metrics.memoryBandwidthRead, &metrics.memoryBandwidthWrite);
    readTemperature(gpuIndex, &metrics.temperature);
    readClockSpeed(gpuIndex, &metrics.clockSpeed);
    readPowerDraw(gpuIndex, &metrics.powerDraw);
    readQueueDepth(gpuIndex, &metrics.queueDepth);
    
    metrics.lastUpdate = now;
    metrics.totalWorkloads = m_totalWorkloads[gpuIndex];
    metrics.totalExecutionTimeUs = m_totalExecutionTimeUs[gpuIndex];
    
    m_gpuMetrics[gpuIndex] = metrics;
}

void MetricsCollector::collectAllMetrics()
{
    uint32_t gpuCount = static_cast<uint32_t>(m_gpuMetrics.size());
    
    for (uint32_t i = 0; i < gpuCount; i++) {
        collectGpuMetrics(i);
    }
}

GpuMetrics MetricsCollector::getGpuMetrics(uint32_t gpuIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_gpuMetrics.size()) {
        return {};
    }
    
    return m_gpuMetrics[gpuIndex];
}

void MetricsCollector::getAggregateMetrics(uint64_t* totalUtilization, uint64_t* totalBandwidth) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint64_t sumUtil = 0;
    uint64_t sumBandwidth = 0;
    
    for (const auto& metrics : m_gpuMetrics) {
        sumUtil += metrics.gpuUtilization;
        sumBandwidth += metrics.memoryBandwidthRead + metrics.memoryBandwidthWrite;
    }
    
    *totalUtilization = sumUtil / m_gpuMetrics.size();
    *totalBandwidth = sumBandwidth;
}

void MetricsCollector::sampleMetric(MetricType type, uint32_t gpuIndex, uint64_t value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (gpuIndex >= m_samplesByGpu.size()) {
        return;
    }
    
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    MetricSample sample;
    sample.type = type;
    sample.gpuIndex = gpuIndex;
    sample.timestampNs = static_cast<uint64_t>(now.count());
    sample.value = value;
    sample.min = value;
    sample.max = value;
    sample.average = value;
    
    m_samplesByGpu[gpuIndex].push_back(sample);
    
    /* Keep only recent samples (e.g., last 1000) */
    if (m_samplesByGpu[gpuIndex].size() > 1000) {
        m_samplesByGpu[gpuIndex].erase(m_samplesByGpu[gpuIndex].begin());
    }
}

uint32_t MetricsCollector::subscribeTelemetry(uint32_t clientId, const std::vector<MetricType>& metricTypes, uint32_t updateIntervalMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uint32_t id = m_nextSubscriptionId++;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    TelemetrySubscription sub;
    sub.clientId = clientId;
    sub.metricTypes = metricTypes;
    sub.updateIntervalMs = updateIntervalMs;
    sub.lastSent = now;
    
    m_subscriptions.push_back(sub);
    
    return id;
}

void MetricsCollector::unsubscribeTelemetry(uint32_t /*subscriptionId*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        /* Need to track subscription IDs */
    }
}

void MetricsCollector::checkTelemetryUpdates()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    
    for (auto& sub : m_subscriptions) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - sub.lastSent).count();
        
        if (elapsed >= static_cast<int64_t>(sub.updateIntervalMs)) {
            /* Send telemetry update to client */
            /* In full implementation, use IPC server */
            sub.lastSent = now;
        }
    }
}

void MetricsCollector::setCollectionInterval(uint32_t intervalMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_collectionIntervalMs = intervalMs;
}

std::string MetricsCollector::getMetricsJson() const
{
    /* Return metrics as JSON string */
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"gpu_count\": " << m_gpuMetrics.size() << ",\n";
    oss << "  \"timestamp\": " << m_lastCollection.count() << ",\n";
    oss << "  \"gpus\": [\n";
    
    for (size_t i = 0; i < m_gpuMetrics.size(); i++) {
        const auto& metrics = m_gpuMetrics[i];
        oss << "    {\n";
        oss << "      \"index\": " << i << ",\n";
        oss << "      \"gpu_utilization\": " << metrics.gpuUtilization << ",\n";
        oss << "      \"memory_utilization\": " << metrics.memoryUtilization << ",\n";
        oss << "      \"temperature\": " << metrics.temperature << ",\n";
        oss << "      \"clock_speed\": " << metrics.clockSpeed << ",\n";
        oss << "      \"power_draw\": " << metrics.powerDraw << "\n";
        oss << "    }";
        if (i < m_gpuMetrics.size() - 1) {
            oss << ",";
        }
        oss << "\n";
    }
    
    oss << "  ]\n";
    oss << "}\n";
    
    return oss.str();
}

bool MetricsCollector::readGpuUtilization(uint32_t gpuIndex, uint32_t* utilization)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* Use the GPU's cached utilization value */
    *utilization = gpu->utilization();
    return true;
}

bool MetricsCollector::readMemoryUtilization(uint32_t gpuIndex, uint32_t* utilization)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* Read from sysfs: /sys/class/drm/cardX/device/mem_batch_info */
    /* or use gpu->capabilities().vramFree / vramTotal */
    
    *utilization = 50; /* Default 50% for now */
    return true;
}

bool MetricsCollector::readMemoryBandwidth(uint32_t /*gpuIndex*/, uint64_t* readBps, uint64_t* writeBps)
{
    /* Read memory bandwidth from GPU counters */
    /* For now, return default values */
    *readBps = 1024 * 1024 * 1024; /* 1 GB/s */
    *writeBps = 1024 * 1024 * 1024; /* 1 GB/s */
    return true;
}

bool MetricsCollector::readTemperature(uint32_t gpuIndex, int32_t* temperature)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* Use the GPU's cached temperature value */
    *temperature = gpu->state().temperature;
    return true;
}

bool MetricsCollector::readClockSpeed(uint32_t gpuIndex, uint32_t* clockMhz)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* Read from sysfs */
    *clockMhz = gpu->capabilities().currentClockMhz;
    return true;
}

bool MetricsCollector::readPowerDraw(uint32_t gpuIndex, uint32_t* milliwatts)
{
    const auto* gpu = m_deviceRegistry->getGpu(gpuIndex);
    if (!gpu) {
        return false;
    }
    
    /* Read from sysfs */
    *milliwatts = static_cast<uint32_t>(gpu->state().powerDrawWatts * 1000);
    return true;
}

bool MetricsCollector::readQueueDepth(uint32_t /*gpuIndex*/, uint32_t* depth)
{
    /* Read from scheduler */
    *depth = 0; /* Not yet implemented */
    return true;
}

} // namespace mvgal
