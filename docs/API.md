# MVGAL IPC Protocol Reference

**Version:** 0.2.0 | **Last Updated:** May 2026

---

## Overview

The MVGAL daemon (`mvgald`) exposes a Unix domain socket at
`/run/mvgal/mvgal.sock`. Clients connect, authenticate via `SCM_CREDENTIALS`,
and exchange binary messages.

---

## Connection and Authentication

```c
// Connect
int fd = socket(AF_UNIX, SOCK_STREAM, 0);
struct sockaddr_un addr = { .sun_family = AF_UNIX };
strncpy(addr.sun_path, "/run/mvgal/mvgal.sock", sizeof(addr.sun_path) - 1);
connect(fd, (struct sockaddr *)&addr, sizeof(addr));

// Authenticate via SCM_CREDENTIALS
struct ucred cred = { .pid = getpid(), .uid = getuid(), .gid = getgid() };
struct msghdr msg = { ... };
// (see ipc.c for full implementation)
sendmsg(fd, &msg, 0);
```

**Access control:**
- Members of the `video` group: may submit workloads and query status.
- Members of the `mvgal-admin` group or root: may change scheduling policy
  and power settings.

---

## Message Format

All messages use a fixed header followed by a variable-length payload.

```c
// Defined in include/mvgal/mvgal_ipc.h
typedef struct {
    uint32_t magic;     /* MVGAL_IPC_MAGIC = 0x4D564741 ('MVGA') */
    uint16_t version;   /* Protocol version (currently 1) */
    uint16_t type;      /* Message type (mvgal_ipc_msg_type_t) */
    uint32_t length;    /* Payload length in bytes */
    uint32_t seq;       /* Sequence number (for request/response matching) */
} mvgal_ipc_header_t;
```

---

## Message Types

### `MVGAL_IPC_MSG_QUERY_DEVICES` (0x0001)

Query all detected GPU devices.

**Request payload:** None (length = 0)

**Response payload:**
```json
{
  "gpu_count": 2,
  "gpus": [
    {
      "id": 0,
      "name": "AMD Radeon RX 7900 XTX",
      "vendor": "AMD",
      "vram_total_bytes": 25769803776,
      "vram_free_bytes": 20000000000,
      "drm_node": "/dev/dri/card0",
      "drm_render_node": "/dev/dri/renderD128",
      "temperature_celsius": 45.0,
      "utilization_percent": 12.5,
      "enabled": true
    },
    {
      "id": 1,
      "name": "NVIDIA GeForce RTX 4090",
      "vendor": "NVIDIA",
      "vram_total_bytes": 25769803776,
      "vram_free_bytes": 22000000000,
      "drm_node": "/dev/dri/card1",
      "drm_render_node": "/dev/dri/renderD129",
      "temperature_celsius": 38.0,
      "utilization_percent": 5.0,
      "enabled": true
    }
  ]
}
```

---

### `MVGAL_IPC_MSG_SUBMIT_WORKLOAD` (0x0002)

Submit a workload for scheduling.

**Request payload:**
```json
{
  "workload_type": "graphics",
  "priority": 50,
  "gpu_mask": 3,
  "estimated_bytes": 4194304,
  "api": "vulkan",
  "strategy_hint": "afr"
}
```

**Response payload:**
```json
{
  "workload_id": 12345,
  "assigned_gpu": 0,
  "status": "queued"
}
```

**`workload_type` values:** `"graphics"`, `"compute"`, `"transfer"`, `"mixed"`  
**`priority` range:** 0 (lowest) – 100 (highest)  
**`gpu_mask`:** Bitmask of eligible GPUs (0 = any)

---

### `MVGAL_IPC_MSG_ALLOC_MEMORY` (0x0003)

Allocate memory via the unified memory manager.

**Request payload:**
```json
{
  "size_bytes": 8388608,
  "placement": "gpu_vram",
  "hint_gpu": 0,
  "flags": ["read_only"]
}
```

**Response payload:**
```json
{
  "handle": 67890,
  "unified_va": "0x0000000000800000",
  "actual_gpu": 0,
  "actual_placement": "gpu_vram",
  "dmabuf_fd": 7
}
```

**`placement` values:** `"gpu_vram"`, `"system_ram"`, `"any"`  
**`flags` values:** `"read_only"`, `"pinned"`, `"hugepages"`

---

### `MVGAL_IPC_MSG_FREE_MEMORY` (0x0004)

Free a previously allocated buffer.

**Request payload:**
```json
{
  "handle": 67890
}
```

**Response payload:**
```json
{
  "status": "ok"
}
```

---

### `MVGAL_IPC_MSG_QUERY_CAPABILITIES` (0x0005)

Query the aggregate capability profile.

**Request payload:** None

**Response payload:**
```json
{
  "gpu_count": 2,
  "total_vram_bytes": 51539607552,
  "total_compute_units": 128,
  "min_vulkan_version": "1.3",
  "api_union": ["vulkan", "opencl", "cuda"],
  "api_intersection": ["vulkan", "opencl"],
  "tier": "full",
  "pcie_topology": {
    "same_root_complex": [0, 1],
    "p2p_viable": [[0, 1]]
  }
}
```

---

### `MVGAL_IPC_MSG_SET_STRATEGY` (0x0006)

Change the scheduling strategy. Requires `mvgal-admin` group or root.

**Request payload:**
```json
{
  "strategy": "afr"
}
```

**`strategy` values:** `"single"`, `"round_robin"`, `"afr"`, `"sfr"`,
`"task"`, `"compute_offload"`, `"hybrid"`

**Response payload:**
```json
{
  "status": "ok",
  "previous_strategy": "hybrid"
}
```

---

### `MVGAL_IPC_MSG_SUBSCRIBE_TELEMETRY` (0x0007)

Subscribe to telemetry events. The daemon will push `MVGAL_IPC_MSG_TELEMETRY`
messages at the requested rate.

**Request payload:**
```json
{
  "interval_ms": 500,
  "events": ["gpu_utilization", "vram_usage", "temperature", "power", "frame_stats"]
}
```

**Response payload:**
```json
{
  "status": "ok",
  "subscription_id": 42
}
```

---

### `MVGAL_IPC_MSG_TELEMETRY` (0x0008)

Pushed by the daemon to subscribed clients.

**Payload:**
```json
{
  "subscription_id": 42,
  "timestamp_ns": 1714000000000000000,
  "gpus": [
    {
      "id": 0,
      "utilization_percent": 87.5,
      "vram_used_bytes": 18000000000,
      "temperature_celsius": 72.0,
      "power_watts": 280.0,
      "clock_mhz": 2500
    },
    {
      "id": 1,
      "utilization_percent": 91.2,
      "vram_used_bytes": 20000000000,
      "temperature_celsius": 68.0,
      "power_watts": 320.0,
      "clock_mhz": 2800
    }
  ],
  "frame_stats": {
    "frames_rendered": 12450,
    "avg_frame_time_ms": 8.3,
    "min_frame_time_ms": 7.1,
    "max_frame_time_ms": 12.4
  }
}
```

---

### `MVGAL_IPC_MSG_ATTACH_PROFILE` (0x0009)

Attach a scheduling profile to a process.

**Request payload:**
```json
{
  "pid": 12345,
  "profile_name": "cyberpunk2077"
}
```

**Response payload:**
```json
{
  "status": "ok"
}
```

---

### `MVGAL_IPC_MSG_SHUTDOWN` (0x000A)

Request graceful daemon shutdown. Requires root.

**Request payload:** None

**Response payload:**
```json
{
  "status": "shutting_down"
}
```

---

## Error Responses

All message types may return an error response:

```json
{
  "error": true,
  "code": 13,
  "message": "Permission denied: requires mvgal-admin group"
}
```

**Error codes:**

| Code | Meaning |
|------|---------|
| 1 | Invalid message format |
| 2 | Unknown message type |
| 3 | Invalid payload |
| 4 | GPU not found |
| 5 | Allocation failed (out of memory) |
| 6 | Invalid handle |
| 7 | Strategy not supported |
| 8 | Profile not found |
| 9 | Daemon not initialized |
| 10 | Operation not supported |
| 11 | Timeout |
| 12 | Internal error |
| 13 | Permission denied |

---

## Protocol Versioning

The protocol version is negotiated at connection time. The client sends its
supported version in the first message header. The daemon responds with the
negotiated version. Old clients continue to work with newer daemon versions
as long as the negotiated version is supported.

**Current version:** 1  
**Minimum supported version:** 1

---

## C API (libmvgal.so)

The `libmvgal.so` shared library wraps the IPC protocol:

```c
#include <mvgal/mvgal.h>

// Initialize
mvgal_error_t err = mvgal_init(0);

// Query GPUs
uint32_t count;
mvgal_gpu_get_count(&count);

mvgal_gpu_descriptor_t desc;
mvgal_gpu_get_descriptor(0, &desc);

// Set strategy
mvgal_set_strategy(ctx, MVGAL_STRATEGY_AFR);

// Shutdown
mvgal_shutdown();
```

See `include/mvgal/mvgal.h` for the complete public API reference.
