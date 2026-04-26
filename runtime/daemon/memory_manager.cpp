/**
 * MVGAL implementation - Memory Manager
 * SPDX-License-Identifier: MIT
 */

#include "memory_manager.hpp"
#include "device_registry.hpp"
#include "daemon.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace mvgal {

MemoryManager::MemoryManager(Daemon* daemon)
    : m_daemon(daemon),
      m_nextAllocationId(1),
      m_strategy(AllocationStrategy::NUMA_AWARE)
{
}

MemoryManager::~MemoryManager()
{
}

bool MemoryManager::init()
{
    return true;
}

void MemoryManager::fini()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocations.clear();
}

uint64_t MemoryManager::allocate(size_t size, MemoryAllocationFlags flags,
                                uint32_t preferredGpu, AllocationStrategy strategy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    MemoryAllocation alloc;
    alloc.id = m_nextAllocationId++;
    alloc.size = size;
    alloc.flags = flags;
    alloc.preferredGpu = preferredGpu;
    alloc.uva = 0; /* TODO: Allocate UVA */
    alloc.accessCount = 0;
    alloc.lastAccessTime = 0;
    alloc.isReplicated = false;
    
    /* Determine allocation GPU based on strategy */
    uint32_t targetGpu = 0; /* Default to first GPU */
    
    switch (strategy) {
    case AllocationStrategy::FIRST_AVAILABLE:
        targetGpu = 0;
        break;
    case AllocationStrategy::MOST_VRAM:
        targetGpu = findGpuWithMostVRAM();
        break;
    case AllocationStrategy::LEAST_UTILIZED:
        targetGpu = findLeastUtilizedGpu();
        break;
    case AllocationStrategy::NUMA_AWARE:
        /* If a preferred GPU was provided, honor it; otherwise use the NUMA-aware helper.
           The helper is called with a placeholder NUMA node (0) until a real NUMA detection
           mechanism is implemented. */
        if (preferredGpu != static_cast<uint32_t>(-1)) {
            targetGpu = preferredGpu;
        } else {
            targetGpu = getGpuForNumaNode(0);
        }
        break;
    case AllocationStrategy::EXPLICIT:
        targetGpu = preferredGpu;
        break;
    }
    
    /* Allocate on target GPU */
    alloc.gpuAddresses.resize(m_daemon->deviceRegistry().gpuCount());
    alloc.populated.resize(m_daemon->deviceRegistry().gpuCount(), false);
    alloc.dmaBufFds.resize(m_daemon->deviceRegistry().gpuCount(), -1);
    
    if (allocateOnGpu(alloc, targetGpu)) {
        alloc.populated[targetGpu] = true;
        alloc.preferredGpu = targetGpu;
    } else {
        /* Fallback to system RAM */
        if (!allocateSystemRam(size, alloc)) {
            m_nextAllocationId--;
            return 0; /* Allocation failed */
        }
    }
    
    m_allocations[alloc.id] = alloc;
    return alloc.id;
}

bool MemoryManager::free(uint64_t allocationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) {
        return false;
    }
    
    auto& alloc = it->second;
    
    /* Free on all GPUs where it's populated */
    for (size_t i = 0; i < alloc.populated.size(); i++) {
        if (alloc.populated[i]) {
            freeOnGpu(alloc, static_cast<uint32_t>(i));
        }
    }
    
    /* Close DMA-BUF FDs */
    for (int fd : alloc.dmaBufFds) {
        if (fd >= 0) {
            close(fd);
        }
    }
    
    /* Free UVA */
    // TODO: Free unified virtual address
    
    m_allocations.erase(it);
    return true;
}

const MemoryAllocation* MemoryManager::getAllocation(uint64_t id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(id);
    if (it == m_allocations.end()) {
        return nullptr;
    }
    return &it->second;
}

MemoryAllocation* MemoryManager::getAllocation(uint64_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(id);
    if (it == m_allocations.end()) {
        return nullptr;
    }
    return &it->second;
}

bool MemoryManager::mapToGpu(uint64_t allocationId, uint32_t gpuIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) {
        return false;
    }
    
    auto& alloc = it->second;
    if (gpuIndex >= alloc.populated.size()) {
        return false;
    }
    
    if (alloc.populated[gpuIndex]) {
        return true; /* Already mapped */
    }
    
    /* Import via DMA-BUF if available */
    if (canUseDmaBuf(gpuIndex)) {
        return importViaDmaBuf(alloc, gpuIndex);
    }
    
    /* Fallback: CPU copy via staging buffer */
    return copyViaStagingBuffer(alloc, gpuIndex);
}

bool MemoryManager::unmapFromGpu(uint64_t allocationId, uint32_t gpuIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) {
        return false;
    }
    
    auto& alloc = it->second;
    if (gpuIndex >= alloc.populated.size()) {
        return false;
    }
    
    if (!alloc.populated[gpuIndex]) {
        return true; /* Already unmapped */
    }
    
    /* Free resources on this GPU */
    freeOnGpu(alloc, gpuIndex);
    alloc.populated[gpuIndex] = false;
    
    return true;
}

bool MemoryManager::replicate(uint64_t allocationId, const std::vector<uint32_t>& targetGpus)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) {
        return false;
    }
    
    auto& alloc = it->second;
    
    for (uint32_t gpuIndex : targetGpus) {
        if (gpuIndex >= alloc.gpuAddresses.size()) {
            continue;
        }
        if (!alloc.populated[gpuIndex]) {
            if (!mapToGpu(allocationId, gpuIndex)) {
                return false;
            }
        }
    }
    
    alloc.isReplicated = true;
    alloc.replicationTargets = targetGpus;
    
    return true;
}

bool MemoryManager::migrate(uint64_t allocationId, uint32_t targetGpu)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) {
        return false;
    }
    
    auto& alloc = it->second;
    
    /* Find current GPU */
    uint32_t currentGpu = alloc.preferredGpu;
    if (currentGpu >= alloc.populated.size() || !alloc.populated[currentGpu]) {
        return false;
    }
    
    /* Allocate on target GPU */
    if (!allocateOnGpu(alloc, targetGpu)) {
        return false;
    }
    
    /* Free on old GPU */
    freeOnGpu(alloc, currentGpu);
    alloc.populated[currentGpu] = false;
    
    /* Update preferred GPU */
    alloc.preferredGpu = targetGpu;
    alloc.populated[targetGpu] = true;
    
    return true;
}

uint64_t MemoryManager::importDmaBuf(int dmabufFd, size_t size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    MemoryAllocation alloc;
    alloc.id = m_nextAllocationId++;
    alloc.size = size;
    alloc.flags = MemoryAllocationFlags::CPU_VISIBLE | MemoryAllocationFlags::COHERENT;
    alloc.preferredGpu = static_cast<uint32_t>(-1);
    alloc.uva = 0;
    
    alloc.gpuAddresses.resize(m_daemon->deviceRegistry().gpuCount());
    alloc.populated.resize(m_daemon->deviceRegistry().gpuCount(), false);
    alloc.dmaBufFds.resize(m_daemon->deviceRegistry().gpuCount(), -1);
    
    /* Import into all GPUs */
    for (uint32_t i = 0; i < m_daemon->deviceRegistry().gpuCount(); i++) {
        alloc.dmaBufFds[i] = dup(dmabufFd);
        if (importViaDmaBuf(alloc, i)) {
            alloc.populated[i] = true;
        } else {
            if (alloc.dmaBufFds[i] >= 0) {
                close(alloc.dmaBufFds[i]);
                alloc.dmaBufFds[i] = -1;
            }
        }
    }
    
    m_allocations[alloc.id] = alloc;
    return alloc.id;
}

int MemoryManager::exportAsDmaBuf(uint64_t allocationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) {
        return -1;
    }
    
    auto& alloc = it->second;
    
    /* Find first GPU where allocation is populated */
    for (size_t i = 0; i < alloc.populated.size(); i++) {
        if (alloc.populated[i]) {
            /* Return an existing DMA-BUF fd if available for this allocation on the GPU. */
            int fd = alloc.dmaBufFds[i];
            if (fd >= 0) {
                return fd;
            }
        }
    }
    
    return -1;
}

uint64_t MemoryManager::totalMemory() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t total = 0;
    for (const auto& [id, alloc] : m_allocations) {
        total += alloc.size;
    }
    return total;
}

uint64_t MemoryManager::freeMemory() const
{
    /* Not tracking free memory yet */
    return 0;
}

uint64_t MemoryManager::usedMemory() const
{
    return totalMemory();
}

void MemoryManager::setStrategy(AllocationStrategy strategy)
{
    m_strategy = strategy;
}



/* Private helper functions */
bool MemoryManager::allocateOnGpu(MemoryAllocation& alloc, uint32_t gpuIndex)
{
    /* In full implementation:
     * 1. Use vendor-specific API to allocate on GPU
     * 2. Store GPU address in alloc.gpuAddresses[gpuIndex]
     * 3. Return true if successful
     */
    alloc.gpuAddresses[gpuIndex] = 0x1000 + alloc.id; /* Fake address for now */
    return true;
}

void MemoryManager::freeOnGpu(const MemoryAllocation& /*alloc*/, uint32_t /*gpuIndex*/)
{
    /* In full implementation: free the allocation on this GPU */
}

bool MemoryManager::allocateSystemRam(size_t size, MemoryAllocation& alloc)
{
    /* Allocate system RAM using mmap */
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return false;
    }
    
    alloc.gpuAddresses[0] = reinterpret_cast<uint64_t>(ptr);
    alloc.preferredGpu = 0;
    return true;
}

bool MemoryManager::canUseDmaBuf(uint32_t /*gpuIndex*/) const
{
    /* Check if GPU supports DMA-BUF import */
    return true; /* Assume supported for now */
}

bool MemoryManager::importViaDmaBuf(MemoryAllocation& /*alloc*/, uint32_t /*gpuIndex*/)
{
    /* Import DMA-BUF into GPU */
    /* In full implementation, use vendor-specific DMA-BUF import */
    return true; /* Assume success for now */
}

bool MemoryManager::copyViaStagingBuffer(const MemoryAllocation& /*alloc*/, uint32_t /*gpuIndex*/)
{
    /* Copy via CPU staging buffer */
    return true; /* Assume success for now */
}

uint32_t MemoryManager::findGpuWithMostVRAM() const
{
    if (m_daemon->deviceRegistry().gpuCount() == 0) {
        return 0;
    }
    
    uint64_t mostVram = 0;
    uint32_t bestGpu = 0;
    
    for (uint32_t i = 0; i < m_daemon->deviceRegistry().gpuCount(); i++) {
        const auto* gpu = m_daemon->deviceRegistry().getGpu(i);
        if (gpu && gpu->capabilities().vramSize > mostVram) {
            mostVram = gpu->capabilities().vramSize;
            bestGpu = i;
        }
    }
    
    return bestGpu;
}

uint32_t MemoryManager::findLeastUtilizedGpu() const
{
    if (m_daemon->deviceRegistry().gpuCount() == 0) {
        return 0;
    }
    
    uint32_t lowestUtil = ~0u;
    uint32_t bestGpu = 0;
    
    for (uint32_t i = 0; i < m_daemon->deviceRegistry().gpuCount(); i++) {
        const auto* gpu = m_daemon->deviceRegistry().getGpu(i);
        if (gpu && gpu->utilization() < lowestUtil) {
            lowestUtil = gpu->utilization();
            bestGpu = i;
        }
    }
    
    return bestGpu;
}

uint32_t MemoryManager::getGpuForNumaNode(int numaNode) const
{
    /* Use the preferred GPU if valid */
    if (numaNode < static_cast<int>(m_daemon->deviceRegistry().gpuCount())) {
        return static_cast<uint32_t>(numaNode);
    }
    
    /* Default to first GPU */
    return 0;
}


} // namespace mvgal
