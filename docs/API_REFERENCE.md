# MVGAL API Reference

**Version:** 0.2.2 | **Date:** May 2026

---

## Table of Contents

1. [Core API](#1-core-api)
2. [Kernel IOCTL Interface](#2-kernel-ioctl-interface)
3. [Sysfs Interface](#3-sysfs-interface)
4. [IPC Interface](#4-ipc-interface)
5. [REST API](#5-rest-api)
6. [Vulkan Layer](#6-vulkan-layer)
7. [Error Codes](#7-error-codes)
8. [Data Types](#8-data-types)

---

## 1. Core API

### 1.1 Initialization

```c
// Initialize MVGAL library
mvgal_error_t mvgal_init(void);

// Shutdown MVGAL library
void mvgal_shutdown(void);

// Create a new context
mvgal_error_t mvgal_context_create(mvgal_context_t **ctx);

// Destroy a context
void mvgal_context_destroy(mvgal_context_t *ctx);
```

### 1.2 GPU Enumeration

```c
// Get number of detected GPUs
uint32_t mvgal_get_gpu_count(mvgal_context_t *ctx);

// Get GPU descriptor by index
mvgal_error_t mvgal_get_gpu_info(mvgal_context_t *ctx,
                                  uint32_t index,
                                  mvgal_gpu_descriptor_t *desc);

// Get logical device descriptor
mvgal_error_t mvgal_get_logical_device(mvgal_context_t *ctx,
                                        mvgal_logical_device_descriptor_t *desc);

// Enable a GPU
mvgal_error_t mvgal_gpu_enable(mvgal_context_t *ctx, uint32_t index);

// Disable a GPU
mvgal_error_t mvgal_gpu_disable(mvgal_context_t *ctx, uint32_t index);
```

### 1.3 Memory Management

```c
// Allocate memory on specific GPU
mvgal_error_t mvgal_memory_alloc(mvgal_context_t *ctx,
                                  uint32_t gpu_index,
                                  size_t size,
                                  mvgal_memory_handle_t *handle);

// Allocate cross-GPU memory
mvgal_error_t mvgal_memory_alloc_cross_gpu(mvgal_context_t *ctx,
                                            uint64_t gpu_mask,
                                            size_t size,
                                            mvgal_memory_handle_t *handle);

// Free memory
mvgal_error_t mvgal_memory_free(mvgal_context_t *ctx,
                                 mvgal_memory_handle_t handle);

// Map memory to host
mvgal_error_t mvgal_memory_map(mvgal_context_t *ctx,
                                mvgal_memory_handle_t handle,
                                void **ptr);

// Unmap memory
mvgal_error_t mvgal_memory_unmap(mvgal_context_t *ctx,
                                  mvgal_memory_handle_t handle);

// Export as DMA-BUF
mvgal_error_t mvgal_memory_export_dmabuf(mvgal_context_t *ctx,
                                          mvgal_memory_handle_t handle,
                                          int *fd);

// Import from DMA-BUF
mvgal_error_t mvgal_memory_import_dmabuf(mvgal_context_t *ctx,
                                          int fd,
                                          mvgal_memory_handle_t *handle);
```

### 1.4 Workload Submission

```c
// Submit workload to GPU
mvgal_error_t mvgal_workload_submit(mvgal_context_t *ctx,
                                     uint32_t gpu_index,
                                     const mvgal_workload_t *workload,
                                     mvgal_fence_t *fence);

// Submit workload to multiple GPUs
mvgal_error_t mvgal_workload_submit_multi(mvgal_context_t *ctx,
                                           const uint32_t *gpu_indices,
                                           uint32_t count,
                                           const mvgal_workload_t *workload,
                                           mvgal_fence_t *fence);

// Wait for fence
mvgal_error_t mvgal_fence_wait(mvgal_context_t *ctx,
                                mvgal_fence_t fence,
                                uint64_t timeout_ns);

// Query fence status
mvgal_error_t mvgal_fence_query(mvgal_context_t *ctx,
                                 mvgal_fence_t fence,
                                 mvgal_fence_status_t *status);
```

### 1.5 Synchronization

```c
// Create timeline semaphore
mvgal_error_t mvgal_semaphore_create(mvgal_context_t *ctx,
                                      uint64_t initial_value,
                                      mvgal_semaphore_t *sem);

// Signal semaphore
mvgal_error_t mvgal_semaphore_signal(mvgal_context_t *ctx,
                                      mvgal_semaphore_t sem,
                                      uint64_t value);

// Wait on semaphore
mvgal_error_t mvgal_semaphore_wait(mvgal_context_t *ctx,
                                    mvgal_semaphore_t sem,
                                    uint64_t value,
                                    uint64_t timeout_ns);

// Destroy semaphore
mvgal_error_t mvgal_semaphore_destroy(mvgal_context_t *ctx,
                                       mvgal_semaphore_t sem);
```

### 1.6 Statistics

```c
// Get device statistics
mvgal_error_t mvgal_get_stats(mvgal_context_t *ctx,
                               mvgal_uapi_stats_t *stats);

// Get GPU utilization
mvgal_error_t mvgal_get_gpu_utilization(mvgal_context_t *ctx,
                                         uint32_t index,
                                         uint32_t *util_percent);

// Get GPU temperature
mvgal_error_t mvgal_get_gpu_temperature(mvgal_context_t *ctx,
                                         uint32_t index,
                                         uint32_t *temp_celsius);

// Get GPU memory usage
mvgal_error_t mvgal_get_gpu_memory_usage(mvgal_context_t *ctx,
                                          uint32_t index,
                                          uint64_t *used_bytes,
                                          uint64_t *total_bytes);
```

### 1.7 Scheduling

```c
// Set scheduling strategy
mvgal_error_t mvgal_scheduler_set_strategy(mvgal_context_t *ctx,
                                            mvgal_strategy_t strategy);

// Get current strategy
mvgal_error_t mvgal_scheduler_get_strategy(mvgal_context_t *ctx,
                                            mvgal_strategy_t *strategy);

// Get strategy name
const char* mvgal_strategy_name(mvgal_strategy_t strategy);
```

---

## 2. Kernel IOCTL Interface

### 2.1 IOCTL Numbers

| IOCTL | Number | Direction | Description |
|-------|--------|-----------|-------------|
| `MVGAL_IOCTL_QUERY_VERSION` | 0 | R | Get kernel module version |
| `MVGAL_IOCTL_GET_GPU_COUNT` | 1 | R | Get number of detected GPUs |
| `MVGAL_IOCTL_GET_GPU_INFO` | 2 | RW | Get GPU information by index |
| `MVGAL_IOCTL_ENABLE_GPU` | 3 | W | Enable a GPU for aggregation |
| `MVGAL_IOCTL_DISABLE_GPU` | 4 | W | Disable a GPU |
| `MVGAL_IOCTL_GET_STATS` | 5 | R | Get device statistics |
| `MVGAL_IOCTL_GET_CAPS` | 6 | R | Get capability profile |
| `MVGAL_IOCTL_RESCAN` | 7 | W | Trigger PCI rescan |
| `MVGAL_IOCTL_EXPORT_DMABUF` | 8 | RW | Export memory as DMA-BUF (stub) |
| `MVGAL_IOCTL_IMPORT_DMABUF` | 9 | RW | Import memory from DMA-BUF (stub) |
| `MVGAL_IOCTL_ALLOC_CROSS_VENDOR` | 10 | RW | Allocate cross-vendor memory (stub) |
| `MVGAL_IOCTL_FREE_CROSS_VENDOR` | 11 | W | Free cross-vendor memory (stub) |

### 2.2 Data Structures

```c
// Version info
struct mvgal_uapi_version {
    __u32 major;
    __u32 minor;
    __u32 patch;
    char  codename[32];
};

// GPU information
struct mvgal_gpu_info {
    __u32  index;
    __u32  vendor_id;
    __u32  device_id;
    __u32  subsystem_vendor_id;
    __u32  subsystem_device_id;
    __u16  pci_domain;
    __u8   pci_bus;
    __u8   pci_slot;
    __u8   pci_function;
    __u8   revision_id;
    __u64  vram_size;
    __u64  vram_bandwidth;
    __u32  compute_units;
    __u32  numa_node;
    char   name[64];
    char   pci_path[32];
    __u64  api_flags;
};

// Device statistics
struct mvgal_uapi_stats {
    __u32  gpu_count;
    __u32  enabled_count;
    __u32  topology_generation;
    __u64  total_vram;
    __u64  used_vram;
    __u32  active_workloads;
    __u64  total_submissions;
    __u64  total_completions;
    __u32  scheduler_strategy;
    struct {
        __u32 utilization;
        __u32 temperature;
        __u64 vram_used;
        __u64 vram_total;
        char  power_state[8];
    } gpus[MVGAL_UAPI_MAX_GPUS];
};

// Capability profile
struct mvgal_capability_profile {
    __u64 common_features;
    __u64 aggregate_features;
    __u32 max_gpus;
    __u32 max_memory_size;
    __u32 max_workload_size;
    __u8  p2p_supported;
    __u8  dma_buf_supported;
    __u8  cross_vendor_supported;
};
```

### 2.3 Usage Example

```c
#include <fcntl.h>
#include <sys/ioctl.h>
#include "kernel/mvgal_uapi.h"

int fd = open("/dev/mvgal0", O_RDWR);
if (fd < 0) {
    perror("open");
    return -1;
}

// Get GPU count
u32 count;
if (ioctl(fd, MVGAL_IOCTL_GET_GPU_COUNT, &count) < 0) {
    perror("ioctl");
    return -1;
}

// Get GPU info
struct mvgal_gpu_info info = { .index = 0 };
if (ioctl(fd, MVGAL_IOCTL_GET_GPU_INFO, &info) < 0) {
    perror("ioctl");
    return -1;
}

close(fd);
```

---

## 3. Sysfs Interface

### 3.1 Device Attributes

| Path | Access | Description |
|------|--------|-------------|
| `/sys/class/mvgal/mvgal0/gpu_count` | R | Number of detected GPUs |
| `/sys/class/mvgal/mvgal0/topology_generation` | R | Topology version counter |
| `/sys/class/mvgal/mvgal0/rescan` | W | Trigger PCI rescan (write "1") |

### 3.2 Per-GPU Attributes

| Path | Access | Description |
|------|--------|-------------|
| `/sys/class/mvgal/mvgal0/gpuN/enabled` | RW | Enable/disable GPU (0/1) |
| `/sys/class/mvgal/mvgal0/gpuN/pci_path` | R | PCI path string |
| `/sys/class/mvgal/mvgal0/gpuN/power_state` | RW | Power state ("auto", "on", "off") |
| `/sys/class/mvgal/mvgal0/gpuN/vendor` | R | Vendor name |
| `/sys/class/mvgal/mvgal0/gpuN/name` | R | GPU name |

### 3.3 Usage Example

```bash
# Check GPU count
cat /sys/class/mvgal/mvgal0/gpu_count

# Enable GPU 1
echo 1 > /sys/class/mvgal/mvgal0/gpu1/enabled

# Set power state
echo "on" > /sys/class/mvgal/mvgal0/gpu0/power_state

# Trigger rescan
echo 1 > /sys/class/mvgal/mvgal0/rescan
```

---

## 4. IPC Interface

### 4.1 Connection

```
Socket: /var/run/mvgal/mvgald.sock
Protocol: Unix domain socket with SCM_CREDENTIALS authentication
```

### 4.2 Message Types

| ID | Type | Direction | Description |
|----|------|-----------|-------------|
| 1 | `GET_STATUS` | C->S | Request daemon status |
| 2 | `STATUS_RESPONSE` | S->C | Daemon status response |
| 3 | `GET_GPU_INFO` | C->S | Request GPU information |
| 4 | `GPU_INFO_RESPONSE` | S->C | GPU information response |
| 5 | `SET_STRATEGY` | C->S | Set scheduling strategy |
| 6 | `STRATEGY_RESPONSE` | S->C | Strategy change confirmation |
| 7 | `GET_METRICS` | C->S | Request current metrics |
| 8 | `METRICS_RESPONSE` | S->C | Metrics data response |
| 9 | `ENABLE_GPU` | C->S | Enable a GPU |
| 10 | `DISABLE_GPU` | C->S | Disable a GPU |
| 11 | `GPU_STATE_CHANGE` | S->C | GPU state change notification |
| 12 | `SUBMIT_WORKLOAD` | C->S | Submit workload |
| 13 | `WORKLOAD_COMPLETE` | S->C | Workload completion notification |
| 14 | `GET_CONFIG` | C->S | Request configuration |
| 15 | `CONFIG_RESPONSE` | S->C | Configuration response |
| 16 | `SET_CONFIG` | C->S | Update configuration |
| 17 | `CONFIG_CHANGED` | S->C | Configuration change notification |
| 18 | `GET_LOGS` | C->S | Request log entries |
| 19 | `LOG_RESPONSE` | S->C | Log entries response |
| 20 | `ERROR` | S->C | Error notification |
| 21 | `PING` | C->S | Keepalive ping |

### 4.3 Message Format

```c
struct mvgal_ipc_message {
    uint32_t type;
    uint32_t length;
    uint64_t timestamp;
    uint32_t sequence;
    char     payload[];
};
```

---

## 5. REST API

### 5.1 Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/status` | Daemon status |
| GET | `/api/v1/gpus` | GPU list |
| GET | `/api/v1/gpus/{index}` | GPU details |
| GET | `/api/v1/metrics` | Current metrics |
| POST | `/api/v1/strategy` | Set strategy |
| GET | `/api/v1/config` | Get configuration |

### 5.2 Response Format

```json
{
  "status": "ok",
  "data": { ... },
  "timestamp": "2026-05-16T12:00:00Z"
}
```

### 5.3 Error Format

```json
{
  "status": "error",
  "error": {
    "code": 404,
    "message": "GPU not found"
  },
  "timestamp": "2026-05-16T12:00:00Z"
}
```

---

## 6. Vulkan Layer

### 6.1 Intercepted Functions

| Function | Purpose |
|----------|---------|
| `vkCreateInstance` | Instance creation, layer initialization |
| `vkDestroyInstance` | Cleanup |
| `vkEnumeratePhysicalDevices` | Return aggregated device |
| `vkEnumerateDeviceLayerProperties` | Report layer properties |
| `vkEnumerateDeviceExtensionProperties` | Report aggregated extensions |
| `vkGetPhysicalDeviceProperties` | Return aggregated properties |
| `vkGetPhysicalDeviceProperties2` | Return aggregated properties (KHR) |
| `vkGetPhysicalDeviceFeatures` | Return intersected features |
| `vkGetPhysicalDeviceFeatures2` | Return intersected features (KHR) |
| `vkGetPhysicalDeviceMemoryProperties` | Return aggregated memory |
| `vkGetPhysicalDeviceMemoryProperties2` | Return aggregated memory (KHR) |
| `vkGetPhysicalDeviceQueueFamilyProperties` | Return aggregated queues |
| `vkGetPhysicalDeviceQueueFamilyProperties2` | Return aggregated queues (KHR) |
| `vkCreateDevice` | Create logical device |
| `vkDestroyDevice` | Cleanup |
| `vkQueueSubmit` | Distribute workloads |
| `vkQueuePresentKHR` | Frame pacing |

### 6.2 Supported Extensions

| Extension | Version | Description |
|-----------|---------|-------------|
| `VK_KHR_device_group` | 4 | Device group operations |
| `VK_KHR_device_group_creation` | 1 | Device group creation |
| `VK_EXT_device_group` | 1 | Device group (EXT) |
| `VK_KHR_external_memory` | 1 | External memory |
| `VK_KHR_external_semaphore` | 1 | External semaphores |
| `VK_KHR_external_fence` | 1 | External fences |
| `VK_KHR_timeline_semaphore` | 2 | Timeline semaphores |
| `VK_KHR_shader_clock` | 1 | Shader clock |
| `VK_KHR_shader_float16_int8` | 1 | Float16/Int8 |
| `VK_KHR_shader_float_controls` | 4 | Float controls |
| `VK_KHR_16bit_storage` | 1 | 16-bit storage |
| `VK_KHR_8bit_storage` | 1 | 8-bit storage |
| `VK_KHR_storage_buffer_storage_class` | 1 | Storage buffer class |
| `VK_KHR_variable_pointers` | 1 | Variable pointers |
| `VK_KHR_multiview` | 1 | Multiview |
| `VK_KHR_maintenance1` | 1 | Maintenance 1 |
| `VK_KHR_maintenance2` | 1 | Maintenance 2 |
| `VK_KHR_maintenance3` | 1 | Maintenance 3 |
| `VK_KHR_maintenance4` | 1 | Maintenance 4 |
| `VK_KHR_sampler_mirror_clamp_to_edge` | 1 | Sampler clamp |
| `VK_KHR_descriptor_update_template` | 1 | Descriptor templates |
| `VK_KHR_push_descriptor` | 2 | Push descriptors |
| `VK_KHR_bind_memory2` | 1 | Bind memory 2 |
| `VK_KHR_image_format_list` | 1 | Image format list |
| `VK_KHR_dedicated_allocation` | 3 | Dedicated allocation |

---

## 7. Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | `MVGAL_SUCCESS` | Operation succeeded |
| -1 | `MVGAL_ERROR_INVALID_HANDLE` | Invalid handle provided |
| -2 | `MVGAL_ERROR_OUT_OF_MEMORY` | Memory allocation failed |
| -3 | `MVGAL_ERROR_DEVICE_NOT_FOUND` | GPU not found |
| -4 | `MVGAL_ERROR_NOT_SUPPORTED` | Operation not supported |
| -5 | `MVGAL_ERROR_TIMEOUT` | Operation timed out |
| -6 | `MVGAL_ERROR_BUSY` | Device is busy |
| -7 | `MVGAL_ERROR_PERMISSION_DENIED` | Insufficient permissions |
| -8 | `MVGAL_ERROR_IO` | I/O error |
| -9 | `MVGAL_ERROR_UNKNOWN` | Unknown error |

---

## 8. Data Types

### 8.1 Vendor Enumeration

```c
typedef enum {
    MVGAL_VENDOR_UNKNOWN = 0,
    MVGAL_VENDOR_AMD = 1,
    MVGAL_VENDOR_NVIDIA = 2,
    MVGAL_VENDOR_INTEL = 3,
    MVGAL_VENDOR_MOORE_THREADS = 4,
} mvgal_vendor_t;
```

### 8.2 GPU Type

```c
typedef enum {
    MVGAL_GPU_TYPE_UNKNOWN = 0,
    MVGAL_GPU_TYPE_INTEGRATED = 1,
    MVGAL_GPU_TYPE_DISCRETE = 2,
    MVGAL_GPU_TYPE_EXTERNAL = 3,
} mvgal_gpu_type_t;
```

### 8.3 Scheduling Strategy

```c
typedef enum {
    MVGAL_STRATEGY_AUTO = 0,
    MVGAL_STRATEGY_AFR = 1,
    MVGAL_STRATEGY_SFR = 2,
    MVGAL_STRATEGY_TASK = 3,
    MVGAL_STRATEGY_COMPUTE_OFFLOAD = 4,
    MVGAL_STRATEGY_HYBRID = 5,
    MVGAL_STRATEGY_SINGLE_GPU = 6,
} mvgal_strategy_t;
```

### 8.4 Workload Type

```c
typedef enum {
    MVGAL_WORKLOAD_UNKNOWN = 0,
    MVGAL_WORKLOAD_GRAPHICS = 1,
    MVGAL_WORKLOAD_COMPUTE = 2,
    MVGAL_WORKLOAD_TRANSFER = 3,
    MVGAL_WORKLOAD_PRESENT = 4,
} mvgal_workload_type_t;
```

### 8.5 Fence Status

```c
typedef enum {
    MVGAL_FENCE_STATUS_PENDING = 0,
    MVGAL_FENCE_STATUS_SIGNALED = 1,
    MVGAL_FENCE_STATUS_ERROR = 2,
} mvgal_fence_status_t;
```
