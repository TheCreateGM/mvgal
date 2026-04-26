/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * Memory Manager Header - Unified virtual memory management
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_MEMORY_MANAGER_HPP
#define MVGAL_RUNTIME_MEMORY_MANAGER_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

namespace mvgal {

class Daemon;

/**
 * Memory allocation flags
 */
enum class MemoryAllocationFlags : uint32_t {
    NONE = 0,
    READ = (1 << 0),
    WRITE = (1 << 1),
    EXECUTE = (1 << 2),
    COHERENT = (1 << 3),
    CPU_VISIBLE = (1 << 4),
    NON_CACHED = (1 << 5),
    P2P_CAPABLE = (1 << 6),
};

inline MemoryAllocationFlags operator|(MemoryAllocationFlags a, MemoryAllocationFlags b) {
    return static_cast<MemoryAllocationFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/**
 * Memory type
 */
enum class MemoryType {
    VRAM,        /* GPU video memory */
    HOST,        /* System memory */
    SHARED,      /* Shared memory (CPU-visible GPU) */
    COHERENT,    /* CPU-coherent GPU memory */
};

/**
 * Memory allocation strategy
 */
enum class AllocationStrategy {
    FIRST_AVAILABLE,    /* Use first available GPU */
    MOST_VRAM,          /* Use GPU with most free VRAM */
    LEAST_UTILIZED,    /* Use least utilized GPU */
    NUMA_AWARE,        /* Use GPU on same NUMA node */
    EXPLICIT,          /* Use explicitly specified GPU */
};

/**
 * DMA-BUF information
 */
struct DmaBufInfo {
    int fd;                    /* File descriptor */
    uint64_t size;             /* Size in bytes */
    uint64_t offset;           /* Offset in the buffer */
    std::string name;         /* Debug name */
};

/**
 * Memory allocation
 */
struct MemoryAllocation {
    uint64_t id;               /* Unique allocation ID */
    uint64_t uva;              /* Unified virtual address */
    size_t size;               /* Size in bytes */
    MemoryType type;           /* Memory type */
    MemoryAllocationFlags flags;
    
    /* Physical mappings */
    std::vector<uint64_t> gpuAddresses;  /* GPU address on each GPU */
    std::vector<int> dmaBufFds;          /* DMA-BUF file descriptors */
    std::vector<bool> populated;         /* Is populated on each GPU */
    
    /* Statistics */
    uint64_t accessCount;
    uint64_t lastAccessTime;
    uint32_t preferredGpu;    /* GPU that prefers this allocation */
    
    /* Replication */
    bool isReplicated;
    std::vector<uint32_t> replicationTargets; /* GPUs to replicate to */
};

/**
 * Unified Virtual Memory Manager
 */
class MemoryManager {
public:
    explicit MemoryManager(Daemon* daemon);
    ~MemoryManager();

    bool init();
    void fini();

    /* Allocate memory */
    uint64_t allocate(size_t size, MemoryAllocationFlags flags = MemoryAllocationFlags::NONE,
                     uint32_t preferredGpu = static_cast<uint32_t>(-1),
                     AllocationStrategy strategy = AllocationStrategy::NUMA_AWARE);

    /* Free memory */
    bool free(uint64_t allocationId);

    /* Get allocation */
    const MemoryAllocation* getAllocation(uint64_t id) const;
    MemoryAllocation* getAllocation(uint64_t id);

    /* Map into GPU address space */
    bool mapToGpu(uint64_t allocationId, uint32_t gpuIndex);
    
    /* Unmap from GPU address space */
    bool unmapFromGpu(uint64_t allocationId, uint32_t gpuIndex);

    /* Replicate to other GPUs */
    bool replicate(uint64_t allocationId, const std::vector<uint32_t>& targetGpus);
    
    /* Migrate to another GPU */
    bool migrate(uint64_t allocationId, uint32_t targetGpu);

    /* Import DMA-BUF */
    uint64_t importDmaBuf(int dmabufFd, size_t size);

    /* Export as DMA-BUF */
    int exportAsDmaBuf(uint64_t allocationId);

    /* Query properties */
    uint64_t totalMemory() const;
    uint64_t freeMemory() const;
    uint64_t usedMemory() const;

    /* Set allocation strategy */
    void setStrategy(AllocationStrategy strategy);
    
    /* NUMA-aware allocation */
    uint32_t getGpuForNumaNode(int numaNode) const;

private:
    Daemon* m_daemon;
    mutable std::mutex m_mutex;
    uint64_t m_nextAllocationId;
    std::unordered_map<uint64_t, MemoryAllocation> m_allocations;
    AllocationStrategy m_strategy;
    
    /* Interval tree for unified virtual address space */
    // struct IntervalTree m_uvaTree;
    
    /* NUMA-aware allocation */
    bool allocateNumaAware(size_t size, MemoryAllocationFlags flags, uint32_t preferredGpu);
    
    /* GPU selection helpers */
    uint32_t findGpuWithMostVRAM() const;
    uint32_t findLeastUtilizedGpu() const;
    
    /* Per-GPU operations */
    bool allocateOnGpu(MemoryAllocation& alloc, uint32_t gpuIndex);
    void freeOnGpu(const MemoryAllocation& alloc, uint32_t gpuIndex);
    
    /* DMA support */
    bool canUseDmaBuf(uint32_t gpuIndex) const;
    bool isP2PSupported(uint32_t fromGpu, uint32_t toGpu) const;
    bool importViaDmaBuf(MemoryAllocation& alloc, uint32_t gpuIndex);
    bool copyViaStagingBuffer(const MemoryAllocation& alloc, uint32_t gpuIndex);
    
    /* Peer-to-peer DMA support */
    
    /* Fallback to system RAM */
    bool allocateSystemRam(size_t size, MemoryAllocation& alloc);
    
    /* DMA-BUF operations */
    std::shared_ptr<DmaBufInfo> createDmaBuf(size_t size);
    std::shared_ptr<DmaBufInfo> importDmaBufInternal(int fd);
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_MEMORY_MANAGER_HPP
