/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Scheduler Implementation - Workload distribution and scheduling
 *
 * SPDX-License-Identifier: MIT
 */

#include "scheduler.hpp"
#include "device_registry.hpp"
#include "daemon.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace mvgal {

/* Workload implementation */
Workload::Workload(uint32_t id, WorkloadType type, uint32_t priority)
    : m_id(id),
      m_type(type),
      m_priority(priority),
      m_gpuMask(~0u),
      m_assignedGpu(static_cast<uint32_t>(-1)),
      m_state(PENDING),
      m_submitTime(std::chrono::high_resolution_clock::now().time_since_epoch())
{
}

Workload::~Workload()
{
}

/* PriorityQueue implementation */
void PriorityQueue::push(std::shared_ptr<Workload> workload)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int priority = static_cast<int>(workload->priority());
    if (priority > NUM_PRIORITY_LEVELS - 1) {
        priority = NUM_PRIORITY_LEVELS - 1;
    }
    
    m_queueByPriority[static_cast<size_t>(priority)].push(workload);
    m_cv.notify_one();
}

std::shared_ptr<Workload> PriorityQueue::pop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    
    for (int i = NUM_PRIORITY_LEVELS - 1; i >= 0; i--) {
        if (!m_queueByPriority[static_cast<size_t>(i)].empty()) {
            auto workload = m_queueByPriority[static_cast<size_t>(i)].front();
            m_queueByPriority[static_cast<size_t>(i)].pop();
            return workload;
        }
    }
    
    return nullptr; /* No workloads available */
}

std::shared_ptr<Workload> PriorityQueue::tryPop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (int i = NUM_PRIORITY_LEVELS - 1; i >= 0; i--) {
        if (!m_queueByPriority[static_cast<size_t>(i)].empty()) {
            return m_queueByPriority[static_cast<size_t>(i)].front();
        }
    }
    
    return nullptr; /* No workloads available */
}

bool PriorityQueue::empty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (const auto& queue : m_queueByPriority) {
        if (!queue.empty()) {
            return false;
        }
    }
    
    return true;
}

size_t PriorityQueue::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t total = 0;
    for (const auto& queue : m_queueByPriority) {
        total += queue.size();
    }
    
    return total;
}

void PriorityQueue::wakeUp()
{
    m_cv.notify_all();
}

void PriorityQueue::waitForWorkload()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return !empty(); });
}

/* Scheduler implementation */
Scheduler::Scheduler(Daemon* daemon)
    : m_daemon(daemon),
      m_mode(SchedulingMode::DYNAMIC_LOAD_BALANCING),
      m_nextWorkloadId(1)
{
    m_staticWeights.resize(8, 1); /* Default: equal weights for all GPUs */
}

Scheduler::~Scheduler()
{
}

bool Scheduler::init()
{
    return true;
}

void Scheduler::fini()
{
    std::lock_guard<std::mutex> lock(m_workloadMutex);
    m_workloads.clear();
}

uint32_t Scheduler::submit(WorkloadType type, uint32_t priority, uint32_t gpuMask)
{
    std::lock_guard<std::mutex> lock(m_workloadMutex);
    
    uint32_t workloadId = m_nextWorkloadId++;
    auto workload = std::make_shared<Workload>(workloadId, type, priority);
    workload->setGpuMask(gpuMask);
    
    m_workloads[workloadId] = workload;
    m_priorityQueue.push(workload);
    m_totalWorkloads++;
    
    return workloadId;
}

bool Scheduler::wait(uint32_t workloadId, uint32_t timeoutMs)
{
    std::shared_ptr<Workload> workload;
    {
        std::lock_guard<std::mutex> lock(m_workloadMutex);
        auto it = m_workloads.find(workloadId);
        if (it == m_workloads.end()) {
            return false; /* Workload not found */
        }
        workload = it->second;
    }

    auto start = std::chrono::high_resolution_clock::now();
    
    while (true) {
        auto state = workload->state();
        if (state == Workload::COMPLETE || state == Workload::ERROR) {
            return (state == Workload::COMPLETE);
        }
        
        if (timeoutMs > 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= timeoutMs) {
                return false; /* Timeout */
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

std::shared_ptr<Workload> Scheduler::getWorkload(uint32_t id)
{
    std::lock_guard<std::mutex> lock(m_workloadMutex);
    auto it = m_workloads.find(id);
    if (it == m_workloads.end()) {
        return nullptr;
    }
    return it->second;
}

void Scheduler::processPending()
{
    /* Process all pending workloads */
    while (true) {
        auto workload = m_priorityQueue.tryPop();
        if (!workload) {
            break;
        }
        
        /* Check if workload is still pending (not already scheduled) */
        if (workload->state() != Workload::PENDING) {
            continue;
        }
        
        /* Schedule the workload */
        uint32_t gpuIndex = 0;
        auto start = std::chrono::high_resolution_clock::now();
        
        switch (m_mode) {
        case SchedulingMode::STATIC_PARTITIONING:
            gpuIndex = scheduleStatic(workload);
            break;
        case SchedulingMode::DYNAMIC_LOAD_BALANCING:
            gpuIndex = scheduleDynamic(workload);
            break;
        case SchedulingMode::APPLICATION_PROFILE:
            gpuIndex = scheduleByProfile(workload);
            break;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        m_schedulingTimeUs += static_cast<uint64_t>(elapsed.count());
        
        /* Dispatch to GPU */
        dispatchToGpu(workload, gpuIndex);
    }
    
    /* Check for work stealing */
    checkWorkStealing();
}

void Scheduler::setMode(SchedulingMode mode)
{
    m_mode = mode;
}

void Scheduler::setStaticWeights(const std::vector<int>& weights)
{
    m_staticWeights = weights;
}

void Scheduler::addApplicationProfile(const ApplicationProfile& profile)
{
    std::lock_guard<std::mutex> lock(m_profileMutex);
    m_applicationProfiles[profile.executableName] = profile;
}

void Scheduler::removeApplicationProfile(const std::string& executableName)
{
    std::lock_guard<std::mutex> lock(m_profileMutex);
    m_applicationProfiles.erase(executableName);
}

const ApplicationProfile* Scheduler::getApplicationProfile(const std::string& executableName) const
{
    std::lock_guard<std::mutex> lock(m_profileMutex);
    auto it = m_applicationProfiles.find(executableName);
    if (it == m_applicationProfiles.end()) {
        return nullptr;
    }
    return &it->second;
}

/* Work stealing */
void Scheduler::checkWorkStealing()
{
    /* For now, work stealing is not implemented */
    /* In a full implementation:
     * 1. Check GPU queue depths
     * 2. If one GPU has empty queue and another has > threshold
     * 3. Steal workloads from busy GPU and migrate to idle GPU
     */
}

bool Scheduler::stealWork(uint32_t /*fromGpu*/, uint32_t /*toGpu*/)
{
    /* Not yet implemented */
    return false;
}

/* Scheduling algorithms */
uint32_t Scheduler::scheduleStatic(std::shared_ptr<Workload> /*workload*/)
{
    /* Static partitioning: use round-robin based on weights */
    uint32_t totalWeight = 0;
    for (int weight : m_staticWeights) {
        totalWeight += static_cast<uint32_t>(weight);
    }
    
    if (totalWeight == 0) {
        return 0; /* Fallback to first GPU */
    }
    
    /* Find next GPU based on workload ID */
    static std::atomic<uint32_t> nextGpuIndex{0};
    uint32_t index = nextGpuIndex.fetch_add(1) % static_cast<uint32_t>(m_staticWeights.size());
    
    return index;
}

uint32_t Scheduler::scheduleDynamic(std::shared_ptr<Workload> /*workload*/)
{
    /* Dynamic load balancing: choose GPU with lowest utilization */
    if (!m_daemon) {
        return 0;
    }
    
    const auto& deviceRegistry = m_daemon->deviceRegistry();
    uint32_t bestGpu = 0;
    uint32_t bestUtilization = ~0u;
    
    for (uint32_t i = 0; i < deviceRegistry.gpuCount(); i++) {
        uint32_t utilization = deviceRegistry.getGpu(i)->utilization();
        if (utilization < bestUtilization) {
            bestUtilization = utilization;
            bestGpu = i;
        }
    }
    
    return bestGpu;
}

uint32_t Scheduler::scheduleByProfile(std::shared_ptr<Workload> workload)
{
    /* Application profile mode: use pre-configured profile if available */
    /* For now, fall back to dynamic scheduling */
    return scheduleDynamic(workload);
}

/* Dispatch to GPU */
bool Scheduler::dispatchToGpu(std::shared_ptr<Workload> workload, uint32_t gpuIndex)
{
    if (!m_daemon) {
        return false;
    }
    
    workload->setState(Workload::SCHEDULED);
    workload->setAssignedGpu(gpuIndex);
    
    auto& deviceRegistry = m_daemon->deviceRegistry();
    auto gpu = deviceRegistry.getGpu(gpuIndex);
    
    if (!gpu || !gpu->isEnabled()) {
        workload->setState(Workload::ERROR);
        m_completedWorkloads++;
        return false;
    }
    
    /* Submit workload to GPU via IPC or direct submission */
    /* For now, mark as running immediately */
    workload->setState(Workload::RUNNING);
    
    /* In a full implementation, we would:
     * 1. Submit command buffer to the GPU via vendor-specific API
     * 2. Create fence for synchronization
     * 3. Track completion
     */
    
    workload->setState(Workload::COMPLETE);
    m_completedWorkloads++;
    
    return true;
}

} // namespace mvgal
