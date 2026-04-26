# MVGAL Unified Memory Manager

**Version:** 0.2.0 | **Last Updated:** April 2026

---

## Overview

The MVGAL unified memory manager presents a single virtual address space to
applications while managing physical VRAM across all GPUs. It handles
allocation placement, replication, migration, and DMA-BUF-based cross-GPU
transfers.

---

## Data Structures

### `mvgal_buffer_t` (Public Handle)

An opaque handle returned to callers. Internally maps to:

```c
typedef struct mvgal_buffer_internal {
    uint64_t            handle;         /* Unique MVGAL allocation ID */
    uint64_t            size_bytes;     /* Allocation size */
    uint64_t            unified_va;     /* Virtual address in unified space */
    mvgal_placement_t   placement;      /* Current physical placement */
    uint32_t            primary_gpu;    /* Primary GPU index */
    uint32_t            mirror_mask;    /* Bitmask of GPUs holding replicas */
    int                 dmabuf_fd;      /* DMA-BUF fd (-1 if none) */
    uint32_t            ref_count;      /* Reference count */
    uint64_t            last_access_ns; /* Last access timestamp */
    uint32_t            access_gpu_mask;/* GPUs that accessed this buffer */
    bool                read_only;      /* Whether buffer is read-only */
    bool                pinned;         /* Whether buffer is pinned (no migration) */
    pthread_mutex_t     lock;           /* Per-buffer lock */
} mvgal_buffer_internal_t;
```

### `mvgal_placement_t`

```c
typedef enum {
    MVGAL_PLACEMENT_SYSTEM_RAM = 0,  /* Host system RAM */
    MVGAL_PLACEMENT_GPU_VRAM   = 1,  /* Single GPU VRAM */
    MVGAL_PLACEMENT_MIRRORED   = 2,  /* Replicated across multiple GPUs */
    MVGAL_PLACEMENT_OVERFLOW   = 3,  /* System RAM overflow (VRAM exhausted) */
} mvgal_placement_t;
```

### Unified Virtual Address Space

The unified VA space is managed via an interval tree (`memory_internal.h`).
Each allocation occupies a unique range in the unified VA space. Physical GPU
addresses are mapped behind the unified VA handle.

```
Unified VA Space (64-bit)
┌──────────────────────────────────────────────────────────────┐
│ 0x0000_0000_0000 │ [alloc 0: 8MB, GPU 0 VRAM]               │
│ 0x0000_0080_0000 │ [alloc 1: 4MB, GPU 1 VRAM]               │
│ 0x0000_00C0_0000 │ [alloc 2: 16MB, mirrored GPU 0+1]        │
│ 0x0000_01C0_0000 │ [alloc 3: 2MB, system RAM overflow]       │
│ ...              │                                            │
└──────────────────────────────────────────────────────────────┘
```

---

## Allocation Algorithm

```
mvgal_memory_allocate(size, flags, hint_gpu)
         │
         ├─ size < 64 MB?
         │       │
         │       └─ YES ──► Select GPU with most free VRAM
         │
         ├─ flags & MVGAL_MEM_RENDER_TARGET?
         │       │
         │       └─ YES ──► Select GPU that will write first
         │                  (from recent workload history)
         │
         ├─ hint_gpu valid?
         │       │
         │       └─ YES ──► Use hint_gpu if it has sufficient free VRAM
         │
         ├─ Large allocation (≥ 64 MB)?
         │       │
         │       └─ YES ──► Select GPU most likely to use it
         │                  (from access pattern history)
         │
         ├─ All GPU VRAM exhausted?
         │       │
         │       └─ YES ──► Overflow to system RAM
         │                  Use MAP_HUGETLB if size ≥ 2 MB
         │
         └─ Allocate on selected GPU
            Register in unified VA space
            Return mvgal_buffer_t handle
```

---

## Memory Replication Policy

Read-only allocations (textures, vertex buffers, constant buffers) are
automatically replicated to all GPUs that access them.

### Replication Decision

```
On GPU access to buffer B by GPU N:
    if B.read_only AND B.primary_gpu != N:
        if N not in B.mirror_mask:
            cost = transfer_cost(B.primary_gpu, N, B.size_bytes)
            benefit = estimated_future_accesses(B, N) * local_access_speedup
            if benefit > cost AND vram_pressure(N) < MIRROR_VRAM_THRESHOLD:
                replicate(B, N)
                B.mirror_mask |= (1 << N)
```

### Hysteresis

To avoid thrashing between replicated and non-replicated states, MVGAL
applies hysteresis:

- **Replicate threshold:** `benefit > cost * 1.5`
- **Evict threshold:** `benefit < cost * 0.5`

The hysteresis window is configurable:

```ini
[memory]
mirror_replicate_threshold = 1.5
mirror_evict_threshold = 0.5
mirror_vram_pressure_limit = 0.85  # 85% VRAM used
```

---

## Memory Migration

When a GPU that holds exclusive ownership of a buffer is idle and another GPU
needs that buffer, MVGAL migrates it.

### Migration Path Selection

```
migrate(buffer B, from GPU A, to GPU B):
    │
    ├─ P2P viable between A and B?
    │       │
    │       └─ YES ──► dma_buf_map_attachment (zero-copy)
    │
    ├─ DMA-BUF export supported on A?
    │       │
    │       └─ YES ──► Export DMA-BUF from A
    │                  mmap in user space
    │                  DMA to staging buffer
    │                  Import to B
    │
    └─ Fallback ──► memcpy via system RAM
                    (slowest path, always available)
```

### Migration Triggers

Migration is triggered when:
1. A GPU requests a buffer it does not own and the owning GPU is idle.
2. VRAM pressure on the owning GPU exceeds the eviction threshold.
3. The scheduler assigns a workload to a GPU that needs a buffer on another GPU.

---

## NUMA-Aware Allocation

For host-side staging buffers, MVGAL queries the NUMA node of each GPU:

```bash
cat /sys/bus/pci/devices/0000:01:00.0/numa_node
```

Staging buffers are allocated from the NUMA node closest to the target GPU
using `mbind()` with `MPOL_BIND`.

---

## System RAM Overflow

When VRAM across all GPUs is exhausted:

1. MVGAL allocates from system RAM using `mmap(MAP_ANONYMOUS | MAP_PRIVATE)`.
2. For allocations ≥ 2 MB, `MAP_HUGETLB` is attempted first.
3. The allocation is tracked with `MVGAL_PLACEMENT_OVERFLOW`.
4. The prefetch engine schedules migration to VRAM when space becomes available.

---

## DMA-BUF Bridge

The DMA-BUF bridge (`src/userspace/memory/dmabuf.c`) implements:

1. **Allocation:** Via `/dev/dma_heap/system` (kernel DMA heap).
   Falls back to `memfd_create` or `tmpfile` for compatibility.
2. **Export:** `ioctl(DMABUF_HEAP_IOCTL_ALLOC)` → returns fd.
3. **Import:** `dma_buf_attach` + `dma_buf_map_attachment`.
4. **P2P check:** `p2pdma_distance()` equivalent via sysfs topology.
5. **Staging copy:** `mmap` source fd → `write` to destination fd.

---

## Prefetch Engine

The prefetch engine (`src/userspace/memory/`) predicts future buffer accesses
based on:

- Recent access history (last N frames).
- Workload type (graphics vs compute).
- Scheduler assignment (which GPU will run next).

When a buffer is predicted to be needed on GPU N within the next 2 frames,
the prefetch engine initiates migration in the background.

---

## Configuration

```ini
# /etc/mvgal/mvgal.conf

[memory]
# Threshold for small vs large allocation routing (bytes)
small_alloc_threshold = 67108864  # 64 MB

# VRAM pressure threshold for overflow to system RAM (0.0–1.0)
vram_overflow_threshold = 0.95

# Enable huge pages for system RAM overflow
use_hugepages = true

# Mirror replication thresholds
mirror_replicate_threshold = 1.5
mirror_evict_threshold = 0.5
mirror_vram_pressure_limit = 0.85

# Prefetch lookahead (frames)
prefetch_lookahead_frames = 2
```

---

## Rust Safety Layer

The `safe/memory_safety` Rust crate tracks all cross-GPU allocations with
reference counting, preventing double-frees and use-after-free bugs:

```rust
// Track a new allocation
let handle = mvgal_mem_track(size_bytes, PLACEMENT_GPU_VRAM, gpu_index);

// Increment reference count (e.g., when sharing with another subsystem)
mvgal_mem_retain(handle);

// Decrement reference count (frees tracking record when it reaches zero)
mvgal_mem_release(handle);

// Associate a DMA-BUF fd
mvgal_mem_set_dmabuf(handle, dmabuf_fd);
```

All `unsafe` blocks in the Rust crate are at FFI boundaries only and are
annotated with safety comments explaining why they are sound.
