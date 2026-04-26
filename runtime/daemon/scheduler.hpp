/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Scheduler Header - Workload distribution and scheduling
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_SCHEDULER_HPP
#define MVGAL_RUNTIME_SCHEDULER_HPP

#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <string>

namespace mvgal {

/* Forward declarations */
class Daemon;
class DeviceRegistry;
class MemoryManager;
class Workload;

/* Include Daemon header for full definition */


/**
 * Scheduling modes
 */
enum class SchedulingMode {
    STATIC_PARTITIONING,      /* Workload divided by static weights */
    DYNAMIC_LOAD_BALANCING,   /* Route to GPU with most capacity */
    APPLICATION_PROFILE,      /* Pre-configured profiles for known apps */
};

/**
 * Workload types
 */
enum class WorkloadType {
    GRAPHICS,                  /* Graphics rendering */
    COMPUTE,                   /* Compute shaders/kernels */
    TRANSFER,                  /* Memory transfers */
};

/**
 * Scheduling strategy for specific applications
 */
struct ApplicationProfile {
    std::string executableName;
    std::string vulkanAppName;
    SchedulingMode mode;
    std::vector<int> gpuWeights;     /* Weight for each GPU (static partitioning) */
    int priorityBoost;             /* Priority boost for this app */
    bool preferSingleGpu;          /* Prefer single GPU for this app */
    uint32_t gpuMask;             /* Specific GPUs to use (0 = all) */
};

/**
 * Workload object
 */
class Workload {
public:
    Workload(uint32_t id, WorkloadType type, uint32_t priority);
    ~Workload();

    uint32_t id() const { return m_id; }
    WorkloadType type() const { return m_type; }
    uint32_t priority() const { return m_priority; }
    uint32_t gpuMask() const { return m_gpuMask; }
    
    void setGpuMask(uint32_t mask) { m_gpuMask = mask; }
    void setAssignedGpu(uint32_t gpuIndex) { m_assignedGpu = gpuIndex; }
    uint32_t assignedGpu() const { return m_assignedGpu; }
    
    enum State { PENDING, SCHEDULED, RUNNING, COMPLETE, ERROR };
    State state() const { return m_state; }
    void setState(State state) { m_state = state; }

private:
    uint32_t m_id;
    WorkloadType m_type;
    uint32_t m_priority;           /* 0 = lowest, 15 = highest */
    uint32_t m_gpuMask;            /* Bitmask of allowed GPUs */
    uint32_t m_assignedGpu;        /* Index of assigned GPU */
    
    State m_state;
    
    /* Timing */
    std::chrono::nanoseconds m_submitTime;
    std::chrono::nanoseconds m_startTime;
    std::chrono::nanoseconds m_endTime;
    
    /* Memory resources for this workload */
    // std::vector<std::shared_ptr<MemoryAllocation>> m_memoryAllocations;
};

/**
 * Priority queue for workloads
 */
class PriorityQueue {
public:
    void push(std::shared_ptr<Workload> workload);
    std::shared_ptr<Workload> pop();
    std::shared_ptr<Workload> tryPop();
    bool empty() const;
    size_t size() const;

    /* Wake up any waiters */
    void wakeUp();
    
    /* Wait for a workload to be available */
    void waitForWorkload();

private:
    mutable std::mutex m_mutex;
    std::vector<std::queue<std::shared_ptr<Workload>>> m_queueByPriority;
    std::condition_variable m_cv;
    static const int NUM_PRIORITY_LEVELS = 16;
};

/**
 * Scheduler class
 * 
 * Implements multi-level scheduling with three modes:
 * 1. Static Partitioning: Workload divided by static weights
 * 2. Dynamic Load Balancing: Route to GPU with most capacity
 * 3. Application Profile Mode: Pre-configured profiles for known apps
 */
class Scheduler {
public:
    explicit Scheduler(Daemon* daemon);
    ~Scheduler();

    bool init();
    void fini();

    /* Submit a workload for scheduling */
    uint32_t submit(WorkloadType type, uint32_t priority = 8, uint32_t gpuMask = ~0u);
    
    /* Wait for a workload to complete */
    bool wait(uint32_t workloadId, uint32_t timeoutMs = 0);
    
    /* Get workload by ID */
    std::shared_ptr<Workload> getWorkload(uint32_t id);
    
    /* Process pending workloads */
    void processPending();

    /* Set scheduling mode */
    void setMode(SchedulingMode mode);
    SchedulingMode mode() const { return m_mode; }

    /* Set static partitioning weights */
    void setStaticWeights(const std::vector<int>& weights);

    /* Add/remove application profiles */
    void addApplicationProfile(const ApplicationProfile& profile);
    void removeApplicationProfile(const std::string& executableName);
    const ApplicationProfile* getApplicationProfile(const std::string& executableName) const;

    /* Statistics */
    uint64_t totalWorkloads() const { return m_totalWorkloads; }
    uint64_t completedWorkloads() const { return m_completedWorkloads; }
    uint64_t schedulingTimeUs() const { return m_schedulingTimeUs; }

private:
    friend class Daemon;
    
    Daemon* m_daemon;
    SchedulingMode m_mode;
    
    /* Priority queues */
    PriorityQueue m_priorityQueue;
    
    /* Workload map */
    mutable std::mutex m_workloadMutex;
    std::unordered_map<uint32_t, std::shared_ptr<Workload>> m_workloads;
    uint32_t m_nextWorkloadId;
    
    /* Static partitioning weights */
    std::vector<int> m_staticWeights;
    
    /* Application profiles */
    mutable std::mutex m_profileMutex;
    std::unordered_map<std::string, ApplicationProfile> m_applicationProfiles;
    
    /* Statistics */
    std::atomic<uint64_t> m_totalWorkloads{0};
    std::atomic<uint64_t> m_completedWorkloads{0};
    std::atomic<uint64_t> m_schedulingTimeUs{0};

    /* Work stealing */
    void checkWorkStealing();
    bool stealWork(uint32_t fromGpu, uint32_t toGpu);

    /* Scheduling algorithms */
    uint32_t scheduleStatic(std::shared_ptr<Workload> workload);
    uint32_t scheduleDynamic(std::shared_ptr<Workload> workload);
    uint32_t scheduleByProfile(std::shared_ptr<Workload> workload);

    /* Dispatch to GPU */
    bool dispatchToGpu(std::shared_ptr<Workload> workload, uint32_t gpuIndex);
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_SCHEDULER_HPP
