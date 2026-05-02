# MVGAL — Multi-Vendor GPU Aggregation Layer for Linux

<div align="center">

<img src="assets/icons/mvgal.svg" alt="MVGAL Logo" width="128" height="128" />

![Version](https://img.shields.io/badge/version-0.2.1-76B900?style=for-the-badge)
![Status](https://img.shields.io/badge/status-95%25_complete-4CAF50?style=for-the-badge)
![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=for-the-badge)
![Language](https://img.shields.io/badge/C17-A8B9CC?style=for-the-badge&logo=c&logoColor=white)
![Language](https://img.shields.io/badge/C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Language](https://img.shields.io/badge/Rust-DEA584?style=for-the-badge&logo=rust&logoColor=white)
![Language](https://img.shields.io/badge/Go-00ADD8?style=for-the-badge&logo=go&logoColor=white)

[![Copr build status](https://copr.fedorainfracloud.org/coprs/axogm/mvgal/package/mvgal/status_image/last_build.png)](https://copr.fedorainfracloud.org/coprs/axogm/mvgal/package/mvgal/)

**Combine multiple GPUs from different vendors into one logical device — transparently, without modifying your applications.**

[Quick Start](#quick-start) · [Architecture](#architecture) · [Build](#building) · [Tools](#cli-tools) · [Docs](docs/) · [COPR](https://copr.fedorainfracloud.org/coprs/axogm/mvgal/)

</div>

---

## What is MVGAL?

Most Linux systems with multiple GPUs (e.g. an AMD RX 7900 + NVIDIA RTX 4080) treat each card as a completely separate device. Applications can only use one at a time, leaving the other idle.

MVGAL solves this by aggregating all available GPUs — regardless of vendor — into a single logical device. Any application, game, or compute workload can use it without modification.

```
┌─────────────────────────────────────────────────────────────┐
│          Your Application / Game / AI Workload              │
└──────────────┬──────────────┬──────────────┬────────────────┘
               │ Vulkan        │ OpenCL        │ CUDA
               ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────┐
│              MVGAL API Interception Layer                    │
│   VK_LAYER_MVGAL  │  libmvgal_opencl.so  │  libmvgal_cuda  │
└──────────────────────────┬──────────────────────────────────┘
                           │ Unix socket (/run/mvgal/mvgal.sock)
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    mvgald  (daemon)                          │
│  Scheduler │ MemoryMgr │ PowerMgr │ MetricsCollector │ IPC  │
└──────┬──────────────┬──────────────┬──────────────┬─────────┘
       │              │              │              │
       ▼              ▼              ▼              ▼
  amdgpu.ko     nvidia.ko       i915/xe.ko    mtgpu-drv.ko
  (AMD GPU)    (NVIDIA GPU)    (Intel GPU)   (MTT GPU)
```

---

## Features

- **Heterogeneous multi-GPU** — AMD, NVIDIA, Intel, and Moore Threads GPUs in any combination
- **Transparent interception** — Vulkan layer, OpenCL ICD, CUDA shim; no application changes needed
- **7 scheduling strategies** — Round-robin, AFR, SFR, Task-based, Compute offload, Hybrid, Single-GPU
- **Unified memory manager** — DMA-BUF zero-copy, PCIe P2P, host-RAM staging fallback
- **GPU health monitoring** — Temperature, utilization, VRAM pressure with configurable thresholds and callbacks
- **Steam/Proton integration** — Frame pacing, AFR for games, DXVK and VKD3D-Proton compatible
- **Power management** — Idle detection, GPU parking, dynamic frequency scaling
- **Memory-safe subsystems** — Fence manager, memory tracker, and capability model written in Rust
- **Qt dashboard + REST API** — Real-time monitoring, scheduler control, log viewer

---

## Supported Hardware

| Vendor | Architectures | Driver |
|--------|--------------|--------|
| **AMD** | RDNA 1/2/3, GCN, APU (Vega/RDNA) | `amdgpu` |
| **NVIDIA** | Turing (RTX 20xx), Ampere (RTX 30xx), Ada (RTX 40xx), Pascal | `nvidia-open` / proprietary |
| **Intel** | Gen 9–12 (iGPU), Xe / Arc (discrete) | `i915` / `xe` |
| **Moore Threads** | MTT S60, S80, S2000 | `mtgpu-drv` |

---

## Install from COPR (Fedora / RHEL / CentOS Stream)

MVGAL is published on Fedora COPR at [copr.fedorainfracloud.org/coprs/axogm/mvgal](https://copr.fedorainfracloud.org/coprs/axogm/mvgal/).

```bash
# Enable the COPR repository
sudo dnf copr enable axogm/mvgal

# Install MVGAL
sudo dnf install mvgal
```

Supported targets: Fedora 40, 41, 42, 43, 44, Rawhide · RHEL/AlmaLinux/Rocky 9 & 10 · CentOS Stream 9 & 10 · openSUSE Tumbleweed · Amazon Linux 2023

---

## Quick Start

### 1. Install dependencies

```bash
# Ubuntu / Debian
sudo apt install cmake ninja-build libdrm-dev libpci-dev libudev-dev \
                 libvulkan-dev vulkan-tools pkg-config gcc g++ rustup golang

# Fedora / RHEL
sudo dnf install cmake ninja-build libdrm-devel pciutils-devel systemd-devel \
                 vulkan-devel gcc-c++ rust cargo golang

# Arch Linux
sudo pacman -S cmake ninja libdrm pciutils systemd vulkan-devel gcc rust go
```

Or use the automated script (uses `pkexec` for privileged steps):
```bash
bash scripts/install_dependencies.sh
```

### 2. Build

```bash
mkdir -p build_output && cd build_output
cmake .. -DCMAKE_BUILD_TYPE=Release -DMVGAL_BUILD_RUNTIME=ON -DMVGAL_BUILD_TOOLS=ON
make -j$(nproc)
```

### 3. Install

```bash
# Generic installer — uses pkexec for all privileged operations
bash build/install.sh
```

### 4. Start the daemon

```bash
pkexec systemctl start mvgald
pkexec systemctl enable mvgald   # start on boot
```

### 5. Verify

```bash
mvgal-info          # list detected GPUs
mvgal-status        # real-time utilization
mvgal-compat --system   # check readiness
```

---

## Architecture

MVGAL is a six-layer stack:

| Layer | Component | Language |
|-------|-----------|----------|
| 6 — Tooling | `mvgal-info`, `mvgal-status`, Qt dashboard, REST API | C, Go |
| 5 — API Interception | Vulkan layer, OpenCL ICD, CUDA shim, OpenGL preload | C |
| 4 — Execution Engine | Frame sessions, migration plans, Steam profiles | C |
| 3 — Runtime Daemon | Scheduler, memory manager, power manager, IPC | C++20, Rust |
| 2 — Safety Subsystems | Fence manager, memory safety, capability model | Rust |
| 1 — Kernel Module | DRM meta-driver, `/dev/mvgal0`, vendor ops | C (GPL-2.0) |

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design.

---

## CLI Tools

| Tool | Description |
|------|-------------|
| `mvgal-info` | Print all detected GPUs, VRAM, temperature, utilization, logical device config |
| `mvgal-status` | Real-time GPU utilization/VRAM bars; `--watch` for continuous refresh |
| `mvgal-bench` | Memory bandwidth, compute FLOPS, scheduling latency, sync overhead |
| `mvgal-compat` | System readiness check + per-app compatibility database |
| `mvgal-config` | Configure scheduler mode, idle thresholds, GPU enable/disable |
| `mvgal` | Main CLI: start/stop daemon, set strategy, show stats |

```bash
# Examples
mvgal-info --json
mvgal-status --watch --interval 500
mvgal-bench all
mvgal-compat "Cyberpunk 2077"
mvgal-config set-strategy afr
```

---

## Scheduling Strategies

| Strategy | Flag | Best For |
|----------|------|----------|
| Round-robin | `round_robin` | Even distribution, general compute |
| Alternate Frame Rendering | `afr` | Gaming — odd/even frames on different GPUs |
| Split Frame Rendering | `sfr` | Gaming — horizontal/vertical tile split |
| Task-based | `task` | Mixed graphics + compute workloads |
| Compute offload | `compute_offload` | AI/HPC — route compute to best GPU |
| Hybrid adaptive | `hybrid` | Automatic selection based on workload metrics |
| Single GPU | `single` | Fallback / compatibility mode |

---

## Steam / Proton Integration

Add to Steam launch options:
```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr %command%
```

Or use MVGAL as a Steam compatibility tool (select in Properties → Compatibility).

| Variable | Values | Description |
|----------|--------|-------------|
| `ENABLE_MVGAL` | `0` / `1` | Enable MVGAL for this launch |
| `MVGAL_STRATEGY` | `afr`, `sfr`, `hybrid`, `single` | Scheduling strategy |
| `MVGAL_FRAME_PACING` | `0` / `1` | Enable vsync-aligned frame pacing |
| `MVGAL_GPU_MASK` | hex bitmask | Which GPUs to use (e.g. `0x3` = GPU 0+1) |
| `MVGAL_VULKAN_DEBUG` | `0` / `1` | Enable Vulkan layer debug logging |

See [steam/README.md](steam/README.md) and [docs/STEAM.md](docs/STEAM.md) for details.

---

## Memory Management

MVGAL uses a three-tier transfer strategy:

```
1. DMA-BUF zero-copy   (preferred — kernel-supported, all vendors)
2. PCIe P2P transfer   (fallback — requires same root complex, kernel 5.10+)
3. Host-RAM staging    (last resort — always works, highest latency)
```

Memory allocation flags: `HOST_VALID`, `GPU_VALID`, `SHARED`, `DMA_BUF`, `P2P`, `REPLICATED`, `PERSISTENT`, `LAZY_ALLOCATE`.

---

## Rust Safety Components

Three Rust crates provide memory-safe implementations of critical subsystems:

| Crate | Purpose | C FFI Functions |
|-------|---------|-----------------|
| `fence_manager` | Cross-device fence lifecycle | `mvgal_fence_create/submit/signal/state/reset/destroy` |
| `memory_safety` | Allocation tracking + ref counting | `mvgal_mem_track/retain/release/set_dmabuf/size/placement` |
| `capability_model` | GPU capability normalization + JSON | `mvgal_cap_compute/free/total_vram/tier/to_json` |

```bash
cargo build --release          # build all crates
cargo test                     # run all unit tests (10 tests)
```

---

## Monitoring Dashboard

```bash
# Start REST API backend
cd ui && go build -o mvgal-rest-server ./mvgal_rest_server.go
./mvgal-rest-server --listen :7474

# Start Qt dashboard
cd ui/build && cmake .. && make
./mvgal-dashboard
```

REST API endpoints: `GET /api/v1/gpus`, `GET /api/v1/stats`, `GET /api/v1/scheduler`, `PUT /api/v1/scheduler`, `GET /api/v1/logs`.

---

## Building

### CMake (primary)

```bash
cmake -B build_output \
  -DCMAKE_BUILD_TYPE=Release \
  -DMVGAL_BUILD_RUNTIME=ON \
  -DMVGAL_BUILD_TOOLS=ON \
  -DMVGAL_BUILD_API=ON \
  -DMVGAL_ENABLE_RUST=ON
cmake --build build_output --parallel $(nproc)
```

### Meson (alternative)

```bash
meson setup builddir -Dwith_vulkan=true -Dwith_opencl=true -Dwith_daemon=true
ninja -C builddir
```

### Zig

```bash
zig build -Dbuild-runtime=true -Dbuild-tools=true
```

### Kernel module only

```bash
cd kernel && make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
pkexec insmod mvgal.ko
```

---

## Packaging

```bash
# Debian / Ubuntu
cd packaging && bash build_deb.sh

# RPM (Fedora / RHEL / openSUSE)
rpmbuild -bb packaging/rpm/mvgal.spec

# Arch Linux
cd packaging/arch && makepkg -si

# Generic tarball installer
bash build/install.sh
```

> **Pre-built RPMs** are available via Fedora COPR — no need to build from source on Fedora/RHEL:
> ```bash
> sudo dnf copr enable axogm/mvgal && sudo dnf install mvgal
> ```

---

## Configuration

Default config: `/etc/mvgal/mvgal.conf`

```ini
[core]
enabled = true
debug_level = info
default_strategy = round_robin
enable_dmabuf = true

[gpu_0]
priority = 0
enabled = true

[gpu_1]
priority = 1
enabled = true

[afr]
enable_sync = true
sync_timeout_ms = 16

[cuda]
enabled = true
intercept_driver = true
intercept_runtime = true
```

Full reference: [docs/API.md](docs/API.md)

---

## Project Structure

```
mvgal/
├── kernel/          # Linux kernel module (GPL-2.0)
│   └── vendors/     # Per-vendor driver integration (AMD, NVIDIA, Intel, MTT)
├── runtime/         # C++20 daemon (mvgald)
│   └── daemon/      # Scheduler, device registry, IPC, power, metrics
├── safe/            # Rust safety-critical crates
│   ├── fence_manager/
│   ├── memory_safety/
│   └── capability_model/
├── src/userspace/   # C userspace library
│   ├── api/         # Public API implementation
│   ├── daemon/      # C daemon (legacy/alternative)
│   ├── execution/   # Frame session engine
│   ├── memory/      # DMA-BUF, P2P, allocator, sync
│   ├── scheduler/   # 7 distribution strategies
│   └── intercept/   # Vulkan, OpenCL, CUDA, D3D, Metal, WebGPU
├── include/mvgal/   # Public C headers
├── tools/           # CLI tools (mvgal-info, status, bench, compat, config)
├── steam/           # Steam/Proton compatibility layer + frame pacer
├── opengl/          # OpenGL LD_PRELOAD shim
├── ui/              # Qt dashboard + Go REST API
├── professional/    # Blender, Unreal Engine, AI framework guides
├── packaging/       # .deb, .rpm, PKGBUILD
├── build/           # Generic installer + CMake toolchains
├── docs/            # Full documentation
├── bindings/        # Language bindings (Java, C#, D, Nim, V, Crystal, Haxe)
└── config/          # Default config, udev rules, systemd service
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Full system architecture |
| [docs/API.md](docs/API.md) | Complete public API reference |
| [docs/BUILD.md](docs/BUILD.md) | Detailed build instructions |
| [docs/QUICKSTART.md](docs/QUICKSTART.md) | 5-minute getting started guide |
| [docs/DRIVER_INTEGRATION.md](docs/DRIVER_INTEGRATION.md) | Per-vendor driver integration |
| [docs/MEMORY.md](docs/MEMORY.md) | Memory management deep-dive |
| [docs/GAMING.md](docs/GAMING.md) | Gaming and Steam/Proton guide |
| [docs/STEAM.md](docs/STEAM.md) | Steam compatibility tool setup |
| [docs/RUST_DEVELOPMENT.md](docs/RUST_DEVELOPMENT.md) | Rust crate development guide |
| [docs/STATUS.md](docs/STATUS.md) | Current implementation status |
| [professional/](professional/) | Blender, Unreal Engine, AI, video encoding |

---

## Status

**Version 0.2.1 "Health Monitor" — ~95% complete**

| Component | Status |
|-----------|--------|
| Kernel module (`mvgal.ko`) | ✅ Complete — loads on kernel 6.19 |
| Runtime daemon (`mvgald`) | ✅ Complete — C++20, all subsystems |
| Vulkan layer (`VK_LAYER_MVGAL`) | ✅ Complete — dispatch-chain layer |
| OpenCL ICD | ✅ Complete — LD_PRELOAD wrapper |
| CUDA shim | ✅ Complete — 40+ functions intercepted |
| Memory manager | ✅ Complete — DMA-BUF, P2P, UVM |
| Scheduler | ✅ Complete — 7 strategies |
| Rust safety crates | ✅ Complete — 10/10 tests pass |
| Execution engine | ✅ Complete — frame sessions, migration plans |
| GPU health monitoring | ✅ Complete — 8 API functions |
| Steam/Proton layer | ✅ Complete — frame pacer, AFR |
| CLI tools | ✅ Complete — info, status, bench, compat, config |
| Qt dashboard + REST API | ✅ Complete |
| OpenGL preload shim | ✅ Complete |
| Packaging (deb/rpm/arch) | ✅ Complete |
| CI workflows | ✅ Manual-only (`workflow_dispatch`) |

---

## License

- Kernel module: **GPL-2.0-only**
- Userspace components: **MIT**
- Rust crates: **MIT OR Apache-2.0**

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). All privileged operations in scripts must use `pkexec`, never `sudo`.

## Security

See [SECURITY.md](SECURITY.md) for the vulnerability disclosure policy.
