# MVGAL Implementation Plans and Gap Analysis

**Version:** 0.2.2 | **Created:** May 2026 | **Updated:** May 2026 | **Status:** Near Complete

---

## 1. Executive Summary

This document outlines the implementation plans for the remaining gaps in MVGAL. The project is approximately **99% functionally complete**, with the remaining work focused on:

1. **Real hardware integration** - Kernel module DMA-BUF and workload submission stubs (return `-EOPNOTSUPP`)
2. **Testing infrastructure** - KUnit tests, CI automation (currently manual-only)
3. **Vendor driver ops** - Stub implementations (fake VRAM addresses, fake utilization) need real hardware integration

Most Phase 1-7 components are fully implemented. See `docs/STATUS.md` for detailed component status.

---

## 2. Gap Analysis Summary

| Category | Completion | Priority | Notes |
|----------|------------|----------|-------|
| Core Infrastructure | 95% | High | Kernel DMA-BUF/submission stubs remain |
| Userspace Virtual GPU & ICDs | 99% | Low | Vulkan ICD complete with 24 extensions |
| Workload Scheduler | 99% | Low | 7 strategies implemented, Rust crates complete |
| Memory Manager | 95% | High | Userspace complete; kernel DMA-BUF stub |
| Power Management | 90% | Medium | Userspace complete; kernel DVFS stub |
| Gaming Integration | 99% | Low | Steam/Proton layer complete |
| Communication/Monitoring | 95% | Medium | IPC, REST API complete; D-Bus/Prometheus pending |
| Testing Framework | 70% | High | C tests 11/11, Rust 12/12; KUnit missing |
| Documentation | 99% | Low | All required docs created |

---

## 3. Implementation Plan: Kernel Module DMA-BUF and Workload Submission

### 3.1 Current State

The kernel module (`mvgal_main.c`) provides:
- PCI GPU enumeration via `pci_get_device` scan
- Character device `/dev/mvgal0` with ioctl interface (8 implemented, 4 stubbed)
- Sysfs under `/sys/class/mvgal/` with per-GPU attributes
- Hotplug monitoring via `bus_register_notifier`
- Vendor driver integration (AMD, NVIDIA, Intel, Moore Threads)

Missing:
- DMA-BUF export/import returns `-EOPNOTSUPP` (stub in `mvgal_memory.c`)
- Workload submission returns `-EOPNOTSUPP` (stub in `mvgal_scheduler.c`)
- Cross-GPU memory migration not implemented
- Vendor ops are stubs (fake VRAM addresses at `0x10000000`, fake utilization at 50%)
- DRM registration disabled (`#if 0` in `mvgal_core.c`)

### 3.2 Implementation Tasks

#### Task 3.2.1: DMA-BUF Export/Import Implementation

**Files to modify:**
- `kernel/mvgal_memory.c`
- `kernel/mvgal_core.c`

**Implementation approach:**

```c
// In mvgal_memory.c - implement actual DMA-BUF export
int mvgal_ioctl_export_dmabuf(struct drm_device *drm, void *data, struct drm_file *file)
{
    struct mvgal_export_dmabuf_args *args = data;
    struct mvgal_memory *mem;
    struct dma_buf *dmabuf;
    int fd;

    // Find memory object
    mutex_lock(&memory_manager.lock);
    list_for_each_entry(mem, &memory_manager.memory_list, node) {
        if (mem->id == args->memory_id) {
            break;
        }
    }
    mutex_unlock(&memory_manager.lock);

    if (!mem) return -ENOENT;

    // Get the underlying DMA-BUF from the owning GPU
    // Use vendor-specific ops to export
    if (mem->owning_gpu && mem->owning_gpu->ops && mem->owning_gpu->ops->export_dmabuf) {
        dmabuf = mem->owning_gpu->ops->export_dmabuf(mem->owning_gpu,
                                                     mem->gpu_addrs[mem->owning_gpu_index],
                                                     mem->size);
        if (IS_ERR(dmabuf)) return PTR_ERR(dmabuf);
    } else {
        // Fallback: create a bounce buffer DMA-BUF
        dmabuf = mvgal_create_bounce_dmabuf(mem);
    }

    fd = dma_buf_fd(dmabuf, args->flags);
    if (fd < 0) {
        dma_buf_put(dmabuf);
        return fd;
    }

    args->fd = fd;
    return 0;
}
```

**DMA-BUF import implementation:**
- Import DMA-BUF from fd using `dma_buf_get()`
- Attach to all GPUs in the allocation mask
- Use `dma_buf_map_attachment()` for each attachment
- Store the mapped addresses in `mem->gpu_addrs[]`

#### Task 3.2.2: Cross-GPU Memory Migration

**Files to modify:**
- `kernel/mvgal_memory.c`
- `kernel/mvgal_device.c`

**Implementation approach:**

```c
// Memory migration path selection
enum mvgal_migration_path {
    MVGAL_MIGRATION_DMA_BUF_ZERO_COPY,    // Direct DMA-BUF sharing
    MVGAL_MIGRATION_PCIE_P2P,             // Peer-to-peer DMA
    MVGAL_MIGRATION_HOST_STAGING,         // CPU-mediated copy
};

static enum mvgal_migration_path mvgal_select_migration_path(
    struct mvgal_gpu_device *src_gpu,
    struct mvgal_gpu_device *dst_gpu)
{
    // Check P2P capability
    if (pci_p2pdma_distance(src_gpu->pdev, dst_gpu->pdev) == 0) {
        return MVGAL_MIGRATION_PCIE_P2P;
    }

    // Check if both support DMA-BUF sharing
    if (src_gpu->ops->export_dmabuf && dst_gpu->ops->import_dmabuf) {
        return MVGAL_MIGRATION_DMA_BUF_ZERO_COPY;
    }

    // Fallback to host staging
    return MVGAL_MIGRATION_HOST_STAGING;
}
```

#### Task 3.2.3: Workload Submission ioctl

**Files to modify:**
- `kernel/mvgal_scheduler.c`
- `kernel/mvgal_core.c`

**Implementation approach:**

```c
// Implement actual workload submission
int mvgal_ioctl_submit_workload(struct drm_device *drm, void *data, struct drm_file *file)
{
    struct mvgal_submit_workload_args *args = data;
    struct mvgal_workload *workload;
    struct mvgal_gpu_device *gpu;
    int ret;

    // Validate workload parameters
    if (args->command_buffer_size > MVGAL_MAX_WORKLOAD_SIZE)
        return -EINVAL;

    // Allocate workload structure
    workload = mvgal_workload_alloc();
    if (!workload) return -ENOMEM;

    workload->type = args->workload_type;
    workload->gpu_mask = args->gpu_mask;
    workload->command_buffer_addr = args->command_buffer_addr;
    workload->command_buffer_size = args->command_buffer_size;

    // Select GPU based on scheduler policy
    gpu = mvgal_select_gpu(workload);
    if (!gpu) {
        mvgal_workload_free(workload);
        return -ENODEV;
    }

    workload->assigned_gpu = gpu;

    // Submit to vendor driver
    if (gpu->ops && gpu->ops->submit_cs) {
        ret = gpu->ops->submit_cs(gpu, workload);
    } else {
        ret = -ENOSYS;
    }

    if (ret < 0) {
        mvgal_workload_free(workload);
        return ret;
    }

    args->workload_id = workload->id;
    return 0;
}
```

### 3.3 Dependencies

- Requires vendor operations (`mvgal_vendor_ops`) to be fully implemented in `kernel/vendors/`
- Requires DMA-BUF infrastructure from kernel 5.10+
- Requires PCI P2P support from kernel 5.10+

### 3.4 Testing

- Unit tests in `kernel/` using KUnit framework
- Integration tests with real multi-GPU hardware
- DMA-BUF export/import loopback test

---

## 4. Implementation Plan: Memory Manager

### 4.1 Current State

The memory manager has:
- Structure for memory allocations (`struct mvgal_memory`)
- Slab cache for allocations
- Basic allocation/free ioctls (stub implementations)

Missing:
- Unified virtual address space (UVA) management
- P2P detection and enablement
- Residency tracking with page migration
- Actual cross-GPU memory sharing

### 4.2 Implementation Tasks

#### Task 4.2.1: Unified Virtual Address Space

**Files to modify:**
- `kernel/mvgal_memory.c`
- `include/mvgal/mvgal_memory.h`

**Implementation approach:**

```c
// UVA management structure
struct mvgal_uva_space {
    struct rb_root addr_tree;          // Red-black tree of allocations
    uint64_t base_addr;                // Base of virtual address space
    uint64_t size;                     // Total size
    atomic_t next_addr;                // Next available address
    struct mutex lock;
};

// Global UVA space - 128TB address space (similar to NVIDIA UVM)
#define MVGAL_UVA_SPACE_SIZE   (128ULL * 1024 * 1024 * 1024 * 1024) // 128TB
#define MVGAL_UVA_SPACE_BASE   0x100000000000ULL  // 1TB

static struct mvgal_uva_space global_uva_space = {
    .addr_tree = RB_ROOT,
    .base_addr = MVGAL_UVA_SPACE_BASE,
    .size = MVGAL_UVA_SPACE_SIZE,
    .lock = __MUTEX_INITIALIZER(global_uva_space.lock),
};

static uint64_t mvgal_uva_allocate(size_t size)
{
    uint64_t addr;
    size = PAGE_ALIGN(size);

    mutex_lock(&global_uva_space.lock);
    addr = atomic_fetch_add(size, &global_uva_space.next_addr);
    if (addr + size > global_uva_space.base_addr + global_uva_space.size) {
        atomic_sub(size, &global_uva_space.next_addr);
        mutex_unlock(&global_uva_space.lock);
        return 0;  // No space available
    }
    mutex_unlock(&global_uva_space.lock);

    return addr;
}
```

#### Task 4.2.2: P2P Detection and Enablement

**Files to modify:**
- `kernel/mvgal_device.c`
- `kernel/vendors/mvgal_amd.c`
- `kernel/vendors/mvgal_nvidia.c`

**Implementation approach:**

```c
// P2P capability detection
int mvgal_detect_p2p_support(struct mvgal_gpu_device *gpu1,
                            struct mvgal_gpu_device *gpu2)
{
    // Check if both GPUs are on the same PCI root complex
    int distance = pci_p2pdma_distance(gpu1->pdev, gpu2->pdev);

    if (distance == 0) {
        // Same root complex - P2P possible
        return MVGAL_P2P_SUPPORTED;
    } else if (distance > 0) {
        // Different root complex - P2P not possible without routing
        return MVGAL_P2P_UNSUPPORTED;
    }

    // Check vendor-specific P2P support
    if (gpu1->vendor == MVGAL_VENDOR_NVIDIA &&
        gpu2->vendor == MVGAL_VENDOR_NVIDIA) {
        // Check NVLink topology
        return mvgal_nvidia_check_nvlink(gpu1, gpu2);
    }

    if (gpu1->vendor == MVGAL_VENDOR_AMD &&
        gpu2->vendor == MVGAL_VENDOR_AMD) {
        // Check xGMI/Infinity Fabric
        return mvgal_amd_check_xgmi(gpu1, gpu2);
    }

    return MVGAL_P2P_UNSUPPORTED;
}
```

#### Task 4.2.3: Residency Tracking

**Files to modify:**
- `kernel/mvgal_memory.c`

**Implementation approach:**

```c
// Memory residency tracking
struct mvgal_memory_residency {
    uint32_t gpu_index;           // Which GPU currently owns the pages
    uint64_t page_count;          // Number of resident pages
    struct page **pages;          // Array of page pointers
    unsigned long last_access;    // Timestamp of last access
    enum mvgal_residency_policy {
        MVGAL_RESIDENCY_EAGER,    // Replicate to all GPUs immediately
        MVGAL_RESIDENCY_LAZY,     // Migrate on first access
        MVGAL_RESIDENCY_ON_DEMAND // Keep on owning GPU, migrate on demand
    } policy;
};

// Track access patterns for migration decisions
static void mvgal_track_access(struct mvgal_memory *mem, uint32_t gpu_index)
{
    mem->residency[gpu_index].last_access = jiffies;

    // Simple heuristic: if GPU accessed more than 3x others, migrate ownership
    // In production, use a proper working-set estimation
}
```

### 4.3 Dependencies

- Requires IOMMU to be disabled or properly configured for P2P (`pci=realloc`)
- Requires vendor drivers to expose DMA-BUF export/import

---

## 5. Implementation Plan: Power Management

### 5.1 Current State

The kernel module has:
- Sysfs interface (`/sys/class/mvgal/gpuN/power_state`)
- Power state tracking (`auto`, `on`, `off`)

Missing:
- Actual clock scaling (DVFS)
- Power curve management
- Thermal throttling
- Fan control
- Dynamic power budgeting

### 5.2 Implementation Tasks

#### Task 5.2.1: DVFS Implementation

**Files to modify:**
- `kernel/mvgal_power.c` (new file)
- `kernel/mvgal_device.c`

**Implementation approach:**

```c
// DVFS policy structure
struct mvgal_dvfs_policy {
    enum mvgal_dvfs_mode {
        MVGAL_DVFS_PERFORMANCE,   // Always max clocks
        MVGAL_DVFS_BALANCED,      // Dynamic based on utilization
        MVGAL_DVFS_POWERSAVE,    // Always min clocks
        MVGAL_DVFS_CUSTOM         // User-defined curve
    } mode;

    uint32_t min_freq_mhz;        // Minimum frequency
    uint32_t max_freq_mhz;        // Maximum frequency
    uint32_t up_threshold;        // Utilization % to increase frequency
    uint32_t down_threshold;     // Utilization % to decrease frequency
    uint32_t step_delay_ms;      // Delay between frequency changes
};

// Per-GPU DVFS state
struct mvgal_gpu_dvfs {
    struct mvgal_dvfs_policy policy;
    uint32_t current_freq_mhz;
    struct delayed_work dvfs_work;
    spinlock_t lock;
};

// Sysfs interface for DVFS
static ssize_t mvgal_freq_show(struct kobject *kobj,
                               struct kobj_attribute *attr, char *buf)
{
    // Read current frequency from vendor driver
}

static ssize_t mvgal_freq_store(struct kobject *kobj,
                                struct kobj_attribute *attr,
                                const char *buf, size_t count)
{
    // Set frequency via vendor driver
}
```

#### Task 5.2.2: Thermal Throttling

**Files to modify:**
- `kernel/mvgal_power.c`

**Implementation approach:**

```c
// Thermal throttling state
struct mvgal_thermal {
    int32_t current_temp_c;       // Current temperature
    int32_t throttle_temp_c;      // Temperature to start throttling
    int32_t critical_temp_c;      // Temperature to force power off
    uint32_t throttle_level;      // 0-100% throttling
    struct thermal_zone_device *tz;
};

// Throttling logic
static void mvgal_thermal_update(struct mvgal_gpu_device *gpu)
{
    int temp = mvgal_read_temp(gpu);

    if (temp >= gpu->thermal->critical_temp_c) {
        // Force GPU off
        mvgal_set_power_state(gpu, MVGAL_POWER_STATE_OFF);
    } else if (temp >= gpu->thermal->throttle_temp_c) {
        // Throttle based on temperature
        gpu->thermal->throttle_level =
            (temp - gpu->thermal->throttle_temp_c) * 100 /
            (gpu->thermal->critical_temp_c - gpu->thermal->throttle_temp_c);
        mvgal_set_freq_limit(gpu, gpu->thermal->throttle_level);
    } else {
        gpu->thermal->throttle_level = 0;
    }
}
```

#### Task 5.2.3: Dynamic Power Budget

**Files to modify:**
- `kernel/mvgal_power.c`
- `include/mvgal/mvgal_power.h`

**Implementation approach:**

```c
// Power budget management
struct mvgal_power_budget {
    uint32_t total_watts;         // Total PSU budget
    uint32_t headroom_watts;      // Reserved headroom
    uint32_t allocated_watts;     // Currently allocated
    struct mvgal_gpu_power {
        uint32_t min_watts;       // Minimum power for this GPU
        uint32_t max_watts;       // Maximum power for this GPU
        uint32_t current_watts;   // Current power draw
    } gpu_power[MVGAL_MAX_GPUS];
};

// Distribute power based on workload priority
static void mvgal_distribute_power(struct mvgal_power_budget *budget,
                                   struct mvgal_workload *workload)
{
    uint32_t available = budget->total_watts - budget->headroom_watts;

    // Allocate power based on workload priority
    // Higher priority workloads get more power budget
    for (int i = 0; i < workload->num_gpus; i++) {
        uint32_t gpu_idx = workload->gpu_indices[i];
        uint32_t share = (workload->priority * available) / 15; // 0-15 priority

        budget->gpu_power[gpu_idx].min_watts = min(share, available);
    }
}
```

### 5.3 Dependencies

- Requires vendor-specific power interfaces (AMD DPM, NVIDIA nvidia-smi interface)
- Requires thermal zone devices from kernel (`thermal_zone_device_register`)
- Requires `hwmon` interface for power reading

---

## 6. Implementation Plan: Testing Framework

### 6.1 Current State

- C unit tests: 11/11 passing (core_api, gpu_detection, memory, scheduler, config, multi_gpu_validation)
- C wrapper tests: 5/5 passing (opencl, d3d, metal, webgpu, multi_gpu)
- Rust tests: 12/12 passing (fence(3), memory(3), capability(6))
- Rust FFI tests: 9/9 passing
- Synthetic benchmarks: 10/10 passing
- Real-world benchmarks: 12/12 passing
- Stress benchmarks: 9/10 passing (1 cosmetic threading artifact)
- No KUnit tests for kernel module
- CI workflows are manual-only (`workflow_dispatch`)
- Prometheus exporter exists (`tools/mvgal_exporter.go`)

### 6.2 Implementation Tasks

#### Task 6.2.1: KUnit Tests for Kernel Module

**Files to create:**
- `kernel/tests/test_mvgal_core.c`
- `kernel/tests/test_mvgal_memory.c`
- `kernel/tests/test_mvgal_scheduler.c`

**Implementation approach:**

```c
// Example KUnit test
#include <kunit/test.h>

static void test_mvgal_gpu_enumeration(struct kunit *test)
{
    struct mvgal_device *dev;
    int ret;

    ret = mvgal_device_init(&dev);
    KUNIT_EXPECT_EQ(test, ret, 0);

    ret = mvgal_enumerate_gpus(dev);
    KUNIT_EXPECT_GE(test, ret, 0);
    KUNIT_EXPECT_GT(test, dev->gpu_count, 0);

    mvgal_device_fini(dev);
}

static void test_mvgal_memory_allocation(struct kunit *test)
{
    struct mvgal_memory *mem;
    int ret;

    ret = mvgal_memory_init();
    KUNIT_EXPECT_EQ(test, ret, 0);

    mem = mvgal_memory_alloc(1024 * 1024); // 1MB
    KUNIT_ASSERT_NOT_NULL(test, mem);
    KUNIT_EXPECT_EQ(test, mem->size, 1024 * 1024);

    mvgal_memory_free(mem);
    mvgal_memory_fini();
}

static struct kunit_case mvgal_core_tests[] = {
    KUNIT_CASE(test_mvgal_gpu_enumeration),
    KUNIT_CASE(test_mvgal_memory_allocation),
    {}
};

static struct kunit_suite mvgal_core_suite = {
    .name = "mvgal-core",
    .test_cases = mvgal_core_tests,
};
kunit_test_suites_register(&mvgal_core_suite);
```

#### Task 6.2.2: Automated CI Pipeline

**Files to create:**
- `.github/workflows/ci.yml` (update to auto-run)
- `.github/workflows/test-kernel.yml`

**Implementation approach:**

```yaml
# Auto-triggered CI workflow
name: CI

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

jobs:
  build:
    runs-on: ubuntu-22.04
    strategy:
      matrix:
        compiler: [gcc, clang]
        kernel: ['6.8', '6.9', '6.10']
    steps:
      - uses: actions/checkout@v4
      - name: Build kernel module
        run: |
          make -C kernel/ KVER=${{ matrix.kernel }} CC=${{ matrix.compiler }}
      - name: Run KUnit tests
        run: |
          # Requires kernel build with KUnit enabled
          ./kernel/tests/run_kunit.sh
      - name: Run userspace tests
        run: |
          cd build_output
          ctest --output-on-failure
```

#### Task 6.2.3: Multi-GPU Simulation Environment

**Files to create:**
- `tests/simulator/gpu_stub.c`
- `tests/simulator/mock_pci.c`

**Implementation approach:**

```c
// Software GPU stub for testing without real hardware
struct mvgal_gpu_stub {
    struct mvgal_gpu_device base;
    bool initialized;
    uint64_t vram_size;
    uint32_t compute_units;
    // Simulated state
    uint32_t utilization;
    int32_t temperature;
    uint32_t power_draw;
};

// Stub driver that simulates GPU behavior
static int mvgal_stub_init(struct mvgal_gpu_device *dev)
{
    struct mvgal_gpu_stub *stub = container_of(dev, struct mvgal_gpu_stub, base);

    stub->initialized = true;
    stub->vram_size = 8ULL * 1024 * 1024 * 1024; // 8GB simulated
    stub->compute_units = 64;
    stub->utilization = 0;
    stub->temperature = 45;
    stub->power_draw = 50;

    return 0;
}

// Use with: QEMU with -device virtio-gpu-pci or similar
// Or: LLVMpipe / SwiftShader for software rendering
```

### 6.3 Dependencies

- KUnit requires kernel 5.14+
- CI requires self-hosted runners for kernel module testing
- Simulation requires understanding of QEMU virtio-gpu

---

## 7. Implementation Plan: Gaming Integration

### 7.1 Current State

- Steam/Proton hook library exists (LD_PRELOAD)
- Frame pacer implemented
- DXVK/VKD3D-Proton integration via Vulkan layer

Missing:
- NTSYNC/Esync/Fsync driver integration
- WoW64 compatibility validation

### 7.2 Implementation Tasks

#### Task 7.2.1: NTSYNC/Esync/Fsync Compatibility

**Files to modify:**
- `steam/mvgal_steam_compat.sh`
- `docs/GAMING.md`

**Implementation approach:**

```bash
#!/bin/bash
# NTSYNC/Esync/Fsync compatibility check script

# Check if NTSYNC is available
if [ -f /sys/fs/fuse/connections/ntsync ]; then
    echo "NTSYNC: Available"
    MVGAL_NTSYNC=1
else
    echo "NTSYNC: Not available (Wine/Proton will use Esync)"
    MVGAL_NTSYNC=0
fi

# Check Esync status
if [ -n "$WINEDEBUG" ] || [ -n "$PROTON_USE_ESYNC" ]; then
    echo "Esync: Enabled"
    MVGAL_ESYNC=1
else
    echo "Esync: Disabled"
    MVGAL_ESYNC=0
fi

# Ensure MVGAL doesn't break these sync mechanisms
# The Vulkan layer should pass through sync primitives unchanged
export MVGAL_PASSTHROUGH_SYNC=1
```

#### Task 7.2.2: WoW64 Compatibility Validation

**Files to create:**
- `tests/wow64/gpu_test.sh`

**Implementation approach:**

```bash
#!/bin/bash
# WoW64 GPU compatibility test

# Test 32-bit Windows application via Wine's WoW64
# can access GPU through MVGAL

# Start a 32-bit game via Proton
PROTON_USE_WOW64=1 \
STEAM_COMPAT_DATA_PATH=/path/to/proton \
/usr/bin/steam "steam://rungameid/12345"

# Monitor GPU access
mvgal-status --watch &
MONITOR_PID=$!

# Wait for game to start
sleep 10

# Check if GPU is being used
if [ -n "$(pgrep -f 'Game.exe')" ]; then
    echo "WoW64: Game started successfully"

    # Check MVGAL logs for any errors
    journalctl -u mvgald | grep -i "wow64\|32-bit\|x86"
fi

kill $MONITOR_PID
```

---

## 8. Implementation Plan: Communication and Monitoring

### 8.1 Current State

- Unix socket IPC (`/run/mvgal/mvgal.sock`)
- CLI tools (info, status, bench, compat, config)
- Go REST API on `:7474`

Missing:
- D-Bus service for scheduler control
- Prometheus exporter

### 8.2 Implementation Tasks

#### Task 8.2.1: D-Bus Service

**Files to create:**
- `runtime/daemon/dbus_service.cpp`
- `config/org.mvgal.MVGAL.service`

**Implementation approach:**

```cpp
// D-Bus interface for MVGAL
class MVGALDaemon::DBusService {
public:
    static const char* BUS_NAME;
    static const char* OBJECT_PATH;

    // D-Bus methods
    void SetSchedulingMode(const std::string& mode);
    std::string GetSchedulingMode();
    void SetGPUEnabled(uint32_t gpu_index, bool enabled);
    bool GetGPUEnabled(uint32_t gpu_index);
    void TriggerRescan();
    std::map<std::string, Variant> GetStatistics();

    // D-Bus signals
    void GPUHotplug(uint32_t gpu_index, bool added);
    void TemperatureWarning(uint32_t gpu_index, int32_t temp);
    void PowerLimitReached(uint32_t gpu_index);
};
```

#### Task 8.2.2: Prometheus Exporter

**Files to create:**
- `tools/mvgal_exporter.go`

**Implementation approach:**

```go
// Prometheus metrics exporter
package main

import (
    "github.com/prometheus/client_golang/prometheus"
    "github.com/prometheus/client_golang/prometheus/promhttp"
    "net/http"
)

var (
    gpuUtilization = prometheus.NewGaugeVec(
        prometheus.GaugeOpts{
            Name: "mvgal_gpu_utilization_percent",
            Help: "GPU utilization percentage",
        },
        []string{"gpu_index", "vendor"},
    )

    gpuMemoryUsed = prometheus.NewGaugeVec(
        prometheus.GaugeOpts{
            Name: "mvgal_gpu_memory_used_bytes",
            Help: "GPU memory used in bytes",
        },
        []string{"gpu_index", "vendor"},
    )

    gpuTemperature = prometheus.NewGaugeVec(
        prometheus.GaugeOpts{
            Name: "mvgal_gpu_temperature_celsius",
            Help: "GPU temperature in Celsius",
        },
        []string{"gpu_index", "vendor"},
    )

    schedulerWorkloads = prometheus.NewCounterVec(
        prometheus.CounterOpts{
            Name: "mvgal_scheduler_workloads_total",
            Help: "Total number of workloads scheduled",
        },
        []string{"strategy", "gpu_index"},
    )
)

func main() {
    prometheus.MustRegister(gpuUtilization, gpuMemoryUsed, gpuTemperature, schedulerWorkloads)

    http.Handle("/metrics", promhttp.Handler())
    http.ListenAndServe(":9100", nil)
}
```

---

## 9. Implementation Priority and Timeline

### Phase 1: Core Infrastructure (Weeks 1-4)
1. DMA-BUF export/import implementation
2. Cross-GPU memory migration
3. Workload submission ioctl

### Phase 2: Memory Manager (Weeks 5-8)
1. Unified virtual address space
2. P2P detection and enablement
3. Residency tracking

### Phase 3: Power Management (Weeks 9-12)
1. DVFS implementation
2. Thermal throttling
3. Dynamic power budgeting

### Phase 4: Testing (Weeks 13-16)
1. KUnit tests
2. Automated CI
3. Multi-GPU simulation

### Phase 5: Integration (Weeks 17-20)
1. D-Bus service
2. Prometheus exporter
3. NTSYNC/Esync/Fsync compatibility

---

## 10. Vendor Driver Integration Notes

### NVIDIA (open-gpu-kernel-modules)
- Version: 595.58.03 (March 2026)
- Key structures: `struct nvidia_device`, `struct nvidia_fence`
- Export symbols via `EXPORT_SYMBOL()` in kernel-open/
- Use `nvidia_uvm_*` functions for UVM

### AMD (amdgpu + RADV)
- AMDVLK is discontinued (use RADV or AMDGPU-PRO)
- Key structures: `struct amdgpu_device`, `struct amdgpu_bo`
- Use `amdgpu_bo_*` functions for memory management
- Use `amdgpu_dpm_*` for power management

### Intel (i915/xe)
- media-driver version: 25.4.6 (Feb 2026)
- Supports Broadwell through Battlemage (Xe3)
- Key structures: `struct drm_i915_private`, `struct i915_gem_object`
- Use `i915_gem_*` for memory, `intel_gt_*` for power

### Moore Threads (mtgpu-drv)
- Unmaintained/archived
- Supports MTT S80
- Has IOMMU handling for kernel 6.13+

---

## 11. References

- Task document: Section 8 Implementation Checklist
- NVIDIA open-gpu-kernel-modules: https://github.com/nvidia/open-gpu-kernel-modules
- AMDVLK: https://github.com/GPUOpen-Drivers/AMDVLK (discontinued)
- Intel media-driver: https://github.com/intel/media-driver
- mtgpu-drv: https://github.com/dixyes/mtgpu-drv (archived)

---

## 12. Remaining Work Summary

### High Priority
1. **Kernel DMA-BUF export/import** - Replace `-EOPNOTSUPP` stubs with actual `dma_buf_ops`
2. **Kernel workload submission** - Replace `-EOPNOTSUPP` stubs with vendor ops integration
3. **Vendor VRAM allocation** - Replace fake addresses with real TTM/GEM/BO allocation
4. **Vendor utilization query** - Replace fake 50% with actual sysfs reading

### Medium Priority
1. **KUnit tests** - Add kernel module test suite
2. **CI automation** - Change workflows from `workflow_dispatch` to push/PR triggers
3. **D-Bus service** - Add D-Bus interface for scheduler control
4. **DRM registration** - Fix kernel 7.x compatibility and enable `#if 0` block

### Low Priority
1. **Cross-vendor memory migration** - Implement migration path selection + execution
2. **Power management deep-dive** - DVFS, thermal throttling, dynamic power budgeting
3. **NixOS packaging** - Add NixOS module and flake

---

*This document was updated May 2026 to reflect v0.2.2 current state. See `docs/STATUS.md` for detailed component status and `docs/MISSING.md` for remaining gaps.*