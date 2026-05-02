# MVGAL Public API Reference

**Version:** 0.2.1 | **Header:** `#include <mvgal/mvgal.h>`

---

## Core API (`mvgal.h`)

### Initialization

```c
// Initialize MVGAL with default configuration
mvgal_error_t mvgal_init(uint32_t flags);

// Initialize with a custom config file path
mvgal_error_t mvgal_init_with_config(const char *config_path, uint32_t flags);

// Shut down and free all resources
void mvgal_shutdown(void);

// Check initialization state
bool mvgal_is_initialized(void);

// Version information
const char *mvgal_get_version(void);
void        mvgal_get_version_numbers(uint32_t *major, uint32_t *minor, uint32_t *patch);
```

### Context Management

```c
mvgal_error_t mvgal_context_create(mvgal_context_t *context);
void          mvgal_context_destroy(mvgal_context_t context);
mvgal_error_t mvgal_context_set_current(mvgal_context_t context);
```

### Execution Control

```c
mvgal_error_t mvgal_flush(mvgal_context_t context);
mvgal_error_t mvgal_finish(mvgal_context_t context);
mvgal_error_t mvgal_wait_idle(mvgal_context_t context);
mvgal_error_t mvgal_set_enabled(mvgal_context_t context, bool enabled);
bool          mvgal_is_enabled(mvgal_context_t context);
```

### Strategy Control

```c
mvgal_error_t                mvgal_set_strategy(mvgal_context_t ctx, mvgal_distribution_strategy_t strategy);
mvgal_distribution_strategy_t mvgal_get_strategy(mvgal_context_t ctx);
```

### Statistics

```c
mvgal_error_t mvgal_get_stats(mvgal_context_t context, mvgal_stats_t *stats);
mvgal_error_t mvgal_reset_stats(mvgal_context_t context);
```

### Custom Splitters

```c
mvgal_error_t mvgal_register_custom_splitter(mvgal_context_t ctx,
                                              const mvgal_workload_splitter_t *splitter);
mvgal_error_t mvgal_unregister_custom_splitter(mvgal_context_t ctx);
```

### Synchronization Primitives

```c
// Fences
mvgal_error_t mvgal_fence_create(mvgal_context_t ctx, mvgal_fence_t *fence);
void          mvgal_fence_destroy(mvgal_fence_t fence);
mvgal_error_t mvgal_fence_wait(mvgal_fence_t fence, uint64_t timeout_ns);
bool          mvgal_fence_check(mvgal_fence_t fence);
mvgal_error_t mvgal_fence_reset(mvgal_fence_t fence);

// Timeline semaphores
mvgal_error_t mvgal_semaphore_create(mvgal_context_t ctx, mvgal_semaphore_t *sem);
void          mvgal_semaphore_destroy(mvgal_semaphore_t sem);
mvgal_error_t mvgal_semaphore_signal(mvgal_semaphore_t sem, uint64_t value);
mvgal_error_t mvgal_semaphore_wait(mvgal_semaphore_t sem, uint64_t value, uint64_t timeout_ns);
uint64_t      mvgal_semaphore_get_value(mvgal_semaphore_t sem);
```

---

## GPU Management API (`mvgal_gpu.h`)

### Enumeration

```c
int32_t       mvgal_gpu_get_count(void);
mvgal_error_t mvgal_gpu_enumerate(mvgal_gpu_descriptor_t *descriptors, uint32_t *count);
mvgal_error_t mvgal_gpu_get_descriptor(int32_t index, mvgal_gpu_descriptor_t *desc);
```

### Discovery

```c
mvgal_gpu_t mvgal_gpu_find_by_pci(uint16_t vendor_id, uint16_t device_id);
mvgal_gpu_t mvgal_gpu_find_by_node(const char *drm_node);
mvgal_gpu_t mvgal_gpu_find_by_vendor(mvgal_vendor_t vendor);
mvgal_gpu_t mvgal_gpu_select_best(const mvgal_gpu_selection_criteria_t *criteria);
mvgal_gpu_t mvgal_gpu_get_primary(void);
```

### Control

```c
mvgal_error_t mvgal_gpu_enable(int32_t index, bool enabled);
bool          mvgal_gpu_is_enabled(int32_t index);
mvgal_error_t mvgal_gpu_enable_all(void);
mvgal_error_t mvgal_gpu_disable_all(void);
```

### Capabilities

```c
bool          mvgal_gpu_has_feature(mvgal_gpu_t gpu, mvgal_feature_flags_t feature);
bool          mvgal_gpu_has_api(mvgal_gpu_t gpu, mvgal_api_type_t api);
mvgal_error_t mvgal_gpu_get_memory_stats(mvgal_gpu_t gpu, uint64_t *total, uint64_t *free);
float         mvgal_gpu_get_utilization(mvgal_gpu_t gpu);
float         mvgal_gpu_get_temperature(mvgal_gpu_t gpu);
```

### Health Monitoring

```c
mvgal_error_t          mvgal_gpu_get_health_status(mvgal_gpu_t gpu, mvgal_gpu_health_status_t *status);
mvgal_gpu_health_level_t mvgal_gpu_get_health_level(mvgal_gpu_t gpu);
bool                   mvgal_gpu_all_healthy(void);
mvgal_error_t          mvgal_gpu_get_health_thresholds(mvgal_gpu_health_thresholds_t *thresholds);
mvgal_error_t          mvgal_gpu_set_health_thresholds(const mvgal_gpu_health_thresholds_t *thresholds);
mvgal_error_t          mvgal_gpu_register_health_callback(mvgal_gpu_health_callback_t cb, void *user_data);
void                   mvgal_gpu_unregister_health_callback(mvgal_gpu_health_callback_t cb);
mvgal_error_t          mvgal_gpu_enable_health_monitoring(bool enable);
```

**Default health thresholds:**

| Metric | Warning | Critical |
|--------|---------|----------|
| Temperature | 80 °C | 95 °C |
| Utilization | 80 % | 95 % |
| Memory usage | 85 % | 95 % |

### Logical Device

```c
mvgal_error_t mvgal_device_create(const mvgal_gpu_t *gpus, uint32_t count,
                                   mvgal_logical_device_t *device);
void          mvgal_device_destroy(mvgal_logical_device_t device);
mvgal_error_t mvgal_device_get_descriptor(mvgal_logical_device_t device,
                                           mvgal_logical_device_descriptor_t *desc);
```

---

## Memory Management API (`mvgal_memory.h`)

### Allocation

```c
mvgal_error_t mvgal_memory_allocate(mvgal_context_t ctx,
                                     const mvgal_memory_alloc_info_t *info,
                                     mvgal_buffer_t *buffer);
mvgal_error_t mvgal_memory_allocate_simple(mvgal_context_t ctx, size_t size,
                                            mvgal_memory_flags_t flags,
                                            mvgal_buffer_t *buffer);
void          mvgal_memory_free(mvgal_buffer_t buffer);
mvgal_error_t mvgal_memory_get_descriptor(mvgal_buffer_t buffer,
                                           mvgal_memory_descriptor_t *desc);
```

### CPU Access

```c
mvgal_error_t mvgal_memory_map(mvgal_buffer_t buffer, void **ptr);
void          mvgal_memory_unmap(mvgal_buffer_t buffer);
bool          mvgal_memory_is_mapped(mvgal_buffer_t buffer);
void         *mvgal_memory_get_pointer(mvgal_buffer_t buffer);
mvgal_error_t mvgal_memory_write(mvgal_buffer_t buffer, uint64_t offset,
                                  const void *data, size_t size);
mvgal_error_t mvgal_memory_read(mvgal_buffer_t buffer, uint64_t offset,
                                 void *data, size_t size);
```

### GPU Transfers

```c
mvgal_error_t mvgal_memory_copy(mvgal_context_t ctx,
                                 const mvgal_memory_copy_region_t *region);
mvgal_error_t mvgal_memory_copy_gpu(mvgal_context_t ctx,
                                     mvgal_buffer_t src, uint32_t src_gpu,
                                     mvgal_buffer_t dst, uint32_t dst_gpu,
                                     size_t size);
```

### DMA-BUF

```c
mvgal_error_t mvgal_memory_export_dmabuf(mvgal_buffer_t buffer, int *fd);
mvgal_error_t mvgal_memory_import_dmabuf(mvgal_context_t ctx, int fd,
                                          size_t size, mvgal_buffer_t *buffer);
```

### Synchronization

```c
mvgal_error_t mvgal_memory_flush(mvgal_buffer_t buffer);
mvgal_error_t mvgal_memory_invalidate(mvgal_buffer_t buffer);
mvgal_error_t mvgal_memory_sync(mvgal_buffer_t buffer, uint32_t gpu_mask);
```

### Advanced

```c
mvgal_error_t mvgal_memory_replicate(mvgal_buffer_t buffer, uint32_t gpu_mask);
uint64_t      mvgal_memory_get_gpu_address(mvgal_buffer_t buffer, uint32_t gpu_index);
bool          mvgal_memory_is_accessible(mvgal_buffer_t buffer, uint32_t gpu_index);
mvgal_error_t mvgal_memory_create_shared(mvgal_context_t ctx, size_t size,
                                          uint32_t gpu_mask, mvgal_buffer_t *buffer);
mvgal_error_t mvgal_memory_get_stats(mvgal_context_t ctx, mvgal_memory_stats_t *stats);
```

---

## Scheduler API (`mvgal_scheduler.h`)

### Workload Submission

```c
mvgal_error_t mvgal_workload_submit(mvgal_context_t ctx,
                                     const mvgal_workload_submit_info_t *info,
                                     mvgal_workload_t *workload);
mvgal_error_t mvgal_workload_submit_with_callback(mvgal_context_t ctx,
                                                   const mvgal_workload_submit_info_t *info,
                                                   mvgal_workload_callback_t callback,
                                                   void *user_data,
                                                   mvgal_workload_t *workload);
```

### Workload Control

```c
mvgal_error_t mvgal_workload_wait(mvgal_workload_t workload, uint64_t timeout_ns);
bool          mvgal_workload_is_completed(mvgal_workload_t workload);
mvgal_error_t mvgal_workload_get_result(mvgal_workload_t workload);
mvgal_error_t mvgal_workload_get_descriptor(mvgal_workload_t workload,
                                             mvgal_workload_descriptor_t *desc);
mvgal_error_t mvgal_workload_cancel(mvgal_workload_t workload);
void          mvgal_workload_destroy(mvgal_workload_t workload);
mvgal_error_t mvgal_workload_set_priority(mvgal_workload_t workload, uint32_t priority);
mvgal_error_t mvgal_workload_assign_gpus(mvgal_workload_t workload, uint32_t gpu_mask);
```

### Scheduler Configuration

```c
mvgal_error_t mvgal_scheduler_configure(const mvgal_scheduler_config_t *config);
mvgal_error_t mvgal_scheduler_get_config(mvgal_scheduler_config_t *config);
mvgal_error_t mvgal_scheduler_get_stats(mvgal_scheduler_stats_t *stats);
mvgal_error_t mvgal_scheduler_reset_stats(void);
mvgal_error_t mvgal_scheduler_set_strategy(mvgal_distribution_strategy_t strategy);
mvgal_distribution_strategy_t mvgal_scheduler_get_strategy(void);
mvgal_error_t mvgal_scheduler_set_gpu_priority(uint32_t gpu_index, int32_t priority);
int32_t       mvgal_scheduler_get_gpu_priority(uint32_t gpu_index);
mvgal_error_t mvgal_scheduler_pause(void);
mvgal_error_t mvgal_scheduler_resume(void);
bool          mvgal_scheduler_is_paused(void);
```

### Distribution Functions

```c
mvgal_error_t mvgal_distribute_afr(mvgal_workload_t workload, uint32_t frame_number,
                                    uint32_t *gpu_index);
mvgal_error_t mvgal_distribute_sfr(mvgal_workload_t workload, float split_ratio,
                                    bool horizontal, uint32_t *gpu_indices);
mvgal_error_t mvgal_distribute_task(mvgal_workload_t workload, uint32_t *gpu_index);
```

---

## Execution Engine API (`mvgal_execution.h`)

```c
// Begin a new frame session
mvgal_error_t mvgal_execution_begin_frame(const mvgal_execution_frame_begin_info_t *info,
                                           uint64_t *frame_id);

// Plan and submit a workload within a frame
mvgal_error_t mvgal_execution_submit(const mvgal_execution_submit_info_t *info,
                                      mvgal_execution_plan_t *plan);

// Present a completed frame
mvgal_error_t mvgal_execution_present(uint64_t frame_id);

// Get frame statistics
mvgal_error_t mvgal_execution_get_frame_stats(uint64_t frame_id,
                                               mvgal_execution_frame_stats_t *stats);

// Plan a cross-GPU memory migration
mvgal_error_t mvgal_execution_migrate_memory(const mvgal_execution_migration_info_t *info,
                                              mvgal_execution_migration_result_t *result);

// Generate a Steam/Proton scheduling profile
mvgal_error_t mvgal_execution_get_steam_profile(const mvgal_steam_profile_request_t *request,
                                                  mvgal_steam_profile_t *profile);
```

---

## IPC API (`mvgal_ipc.h`)

```c
// Server
mvgal_error_t mvgal_ipc_server_init(const char *socket_path);
void          mvgal_ipc_server_cleanup(void);
mvgal_error_t mvgal_ipc_server_start(void);
void          mvgal_ipc_server_stop(void);

// Client
mvgal_error_t mvgal_ipc_client_connect(const char *socket_path);
void          mvgal_ipc_client_disconnect(void);
mvgal_error_t mvgal_ipc_send(mvgal_ipc_message_type_t type,
                              const void *payload, size_t payload_size);
mvgal_error_t mvgal_ipc_receive(mvgal_ipc_message_type_t *type,
                                 void *payload, size_t *payload_size);
```

**IPC Message Types:**

| Type | Value | Description |
|------|-------|-------------|
| `MVGAL_IPC_MSG_PING` | 0 | Keepalive ping |
| `MVGAL_IPC_MSG_PONG` | 1 | Keepalive response |
| `MVGAL_IPC_MSG_GPU_ENUMERATE` | 2 | Request GPU list |
| `MVGAL_IPC_MSG_GPU_LIST` | 3 | GPU list response |
| `MVGAL_IPC_MSG_WORKLOAD_SUBMIT` | 4 | Submit workload |
| `MVGAL_IPC_MSG_WORKLOAD_RESULT` | 5 | Workload result |
| `MVGAL_IPC_MSG_MEMORY_ALLOCATE` | 6 | Allocate memory |
| `MVGAL_IPC_MSG_MEMORY_FREE` | 7 | Free memory |
| `MVGAL_IPC_MSG_CONFIG_GET` | 8 | Get configuration |
| `MVGAL_IPC_MSG_CONFIG_SET` | 9 | Set configuration |
| `MVGAL_IPC_MSG_ERROR` | 10 | Error response |

---

## Logging API (`mvgal_log.h`)

```c
void mvgal_log_init(mvgal_log_level_t level, mvgal_log_callback_t callback, void *user_data);
void mvgal_log_shutdown(void);
void mvgal_log_set_level(mvgal_log_level_t level);
mvgal_log_level_t mvgal_log_get_level(void);
void mvgal_log_register_callback(mvgal_log_callback_t callback, void *user_data);
void mvgal_log_unregister_callback(mvgal_log_callback_t callback);
void mvgal_log_error(const char *fmt, ...);
void mvgal_log_warn(const char *fmt, ...);
void mvgal_log_info(const char *fmt, ...);
void mvgal_log_debug(const char *fmt, ...);
void mvgal_log_trace(const char *fmt, ...);
bool mvgal_log_enabled(mvgal_log_level_t level);
void mvgal_log_enable_file(const char *path);
void mvgal_log_disable_file(void);
void mvgal_log_enable_syslog(const char *ident);
void mvgal_log_disable_syslog(void);
void mvgal_log_enable_colors(bool enable);
void mvgal_log_flush(void);
```

**Log levels:** `MVGAL_LOG_LEVEL_ERROR=0`, `WARN=1`, `INFO=2`, `DEBUG=3`, `TRACE=4`

**Convenience macros:** `MVGAL_LOG_ERROR(fmt, ...)`, `MVGAL_LOG_WARN`, `MVGAL_LOG_INFO`, `MVGAL_LOG_DEBUG`, `MVGAL_LOG_TRACE`

---

## Configuration API (`mvgal_config.h`)

```c
mvgal_error_t mvgal_config_init(void);
void          mvgal_config_shutdown(void);
mvgal_error_t mvgal_config_load(const char *path);
mvgal_error_t mvgal_config_load_string(const char *config_string);
mvgal_error_t mvgal_config_save(const char *path);
mvgal_error_t mvgal_config_get(mvgal_config_t *config);
mvgal_error_t mvgal_config_set(const mvgal_config_t *config);
mvgal_error_t mvgal_config_get_by_name(const char *name, mvgal_config_value_t *value);
mvgal_error_t mvgal_config_set_by_name(const char *name, const mvgal_config_value_t *value);
mvgal_error_t mvgal_config_reset(void);
mvgal_error_t mvgal_config_validate(const mvgal_config_t *config);
const char   *mvgal_config_get_default_path(void);
void          mvgal_config_print(void);
```

---

## Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `MVGAL_SUCCESS` | 0 | Operation succeeded |
| `MVGAL_ERROR_INVALID_ARGUMENT` | 1 | Invalid argument passed |
| `MVGAL_ERROR_OUT_OF_MEMORY` | 2 | Memory allocation failed |
| `MVGAL_ERROR_NOT_FOUND` | 3 | Resource not found |
| `MVGAL_ERROR_TIMEOUT` | 4 | Operation timed out |
| `MVGAL_ERROR_UNSUPPORTED` | 5 | Operation not supported |
| `MVGAL_ERROR_BUSY` | 6 | Resource is busy |
| `MVGAL_ERROR_DEVICE_LOST` | 7 | GPU device lost |
| `MVGAL_ERROR_CONTEXT_LOST` | 8 | Context lost |
| `MVGAL_ERROR_NOT_INITIALIZED` | 9 | MVGAL not initialized |
| `MVGAL_ERROR_ALREADY_INITIALIZED` | 10 | Already initialized |
| `MVGAL_ERROR_INCOMPATIBLE` | 11 | Incompatible GPUs |
| `MVGAL_ERROR_GPU_NOT_FOUND` | 12 | No GPU found |
| `MVGAL_ERROR_NOT_SUPPORTED` | 13 | Feature not supported |
| `MVGAL_ERROR_DRIVER` | 14 | Driver error |

---

## Type Reference

### `mvgal_vendor_t`

```c
MVGAL_VENDOR_UNKNOWN       = 0
MVGAL_VENDOR_AMD           = 0x1002
MVGAL_VENDOR_NVIDIA        = 0x10DE
MVGAL_VENDOR_INTEL         = 0x8086
MVGAL_VENDOR_MOORE_THREADS = 0x1ED5
MVGAL_VENDOR_QUALCOMM      = 0x5143
MVGAL_VENDOR_ARM           = 0x13B5
MVGAL_VENDOR_BROADCOM      = 0x14E4
```

### `mvgal_distribution_strategy_t`

```c
MVGAL_STRATEGY_ROUND_ROBIN     = 0
MVGAL_STRATEGY_AFR             = 1
MVGAL_STRATEGY_SFR             = 2
MVGAL_STRATEGY_AUTO            = 3
MVGAL_STRATEGY_COMPUTE_OFFLOAD = 4
MVGAL_STRATEGY_HYBRID          = 5
MVGAL_STRATEGY_SINGLE_GPU      = 6
MVGAL_STRATEGY_TASK            = 7
MVGAL_STRATEGY_CUSTOM          = 100
```

### `mvgal_api_type_t` (API flags)

```c
MVGAL_API_VULKAN   MVGAL_API_OPENGL   MVGAL_API_OPENCL
MVGAL_API_CUDA     MVGAL_API_D3D11    MVGAL_API_D3D12
MVGAL_API_METAL    MVGAL_API_WEBGPU   MVGAL_API_VA_API
```

### `mvgal_feature_flags_t`

```c
MVGAL_FEATURE_DMA_BUF         MVGAL_FEATURE_CROSS_VENDOR
MVGAL_FEATURE_UNIFIED_MEMORY  MVGAL_FEATURE_P2P_TRANSFER
MVGAL_FEATURE_GRAPHICS        MVGAL_FEATURE_COMPUTE
MVGAL_FEATURE_VIDEO_DECODE    MVGAL_FEATURE_VIDEO_ENCODE
MVGAL_FEATURE_AI_ACCEL        MVGAL_FEATURE_RAY_TRACING
```

---

## Rust FFI Reference

### `fence_manager`

```c
uint64_t mvgal_fence_create(uint32_t gpu_index);
void     mvgal_fence_submit(uint64_t handle);
void     mvgal_fence_signal(uint64_t handle);
uint32_t mvgal_fence_state(uint64_t handle);
void     mvgal_fence_reset(uint64_t handle);
void     mvgal_fence_destroy(uint64_t handle);
```

### `memory_safety`

```c
uint64_t mvgal_mem_track(uint64_t size, uint32_t placement);
void     mvgal_mem_retain(uint64_t handle);
void     mvgal_mem_release(uint64_t handle);
void     mvgal_mem_set_dmabuf(uint64_t handle, int32_t fd);
uint64_t mvgal_mem_size(uint64_t handle);
uint32_t mvgal_mem_placement(uint64_t handle);
uint64_t mvgal_mem_total_system_bytes(void);
uint64_t mvgal_mem_total_gpu_bytes(void);
```

### `capability_model`

```c
uint64_t    mvgal_cap_compute(const GpuCapability *caps, uint32_t count);
void        mvgal_cap_free(uint64_t handle);
uint64_t    mvgal_cap_total_vram(uint64_t handle);
uint32_t    mvgal_cap_tier(uint64_t handle);
const char *mvgal_cap_to_json(uint64_t handle);
```

---

## REST API (`ui/mvgal_rest_server.go`)

Base URL: `http://localhost:7474`

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/v1/gpus` | All GPUs with current metrics |
| GET | `/api/v1/gpus/{id}` | Single GPU by index |
| GET | `/api/v1/scheduler` | Current scheduler mode and GPU count |
| PUT | `/api/v1/scheduler` | Set scheduler mode |
| GET | `/api/v1/stats` | Aggregate stats (VRAM, utilization, daemon status) |
| GET | `/api/v1/logs` | Last 100 lines of daemon log |
| GET | `/` | Service info and endpoint list |

### Example: `GET /api/v1/gpus`

```json
[
  {
    "index": 0,
    "name": "AMD GPU [0000:03:00.0]",
    "vendor": "AMD",
    "pci_slot": "0000:03:00.0",
    "drm_node": "/dev/dri/card2",
    "utilization_pct": 12,
    "vram_total_bytes": 4278190080,
    "vram_used_bytes": 847249408,
    "temperature_c": 56,
    "power_w": 45.2,
    "clock_mhz": 1800,
    "enabled": true
  }
]
```
