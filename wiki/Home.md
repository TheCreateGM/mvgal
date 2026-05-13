---
title: Home
---

# Welcome to the MVGAL Wiki

**MVGAL** (Multi-Vendor GPU Aggregation Layer) combines GPUs from different vendors into one logical device — transparently, without modifying your applications.

## Quick Links

- [Architecture](Architecture)
- [Building from Source](Building)
- [Configuration](Configuration)
- [Scheduling Strategies](Scheduling-Strategies)
- [Memory Management](Memory-Management)
- [Rust Safety Subsystems](Rust-Safety)
- [CLI Tools](CLI-Tools)
- [Steam/Proton Integration](Steam-Proton)
- [FAQ](FAQ)
- [Troubleshooting](Troubleshooting)

## Supported Platforms

| Platform | Support |
|----------|---------|
| Fedora 40+ | ✅ Full |
| RHEL/AlmaLinux/Rocky 9+ | ✅ Full |
| CentOS Stream 9+ | ✅ Full |
| openSUSE Tumbleweed | ✅ Full |
| Debian/Ubuntu | ✅ Full |
| Arch Linux | ✅ Full |
| Amazon Linux 2023 | ✅ Full |

## Supported Hardware

| Vendor | Driver | Status |
|--------|--------|--------|
| AMD (RDNA 1/2/3, GCN) | `amdgpu` | ✅ |
| NVIDIA (Turing, Ampere, Ada, Pascal) | `nvidia-open` / proprietary | ✅ |
| Intel (Gen 9–12, Xe/Arc) | `i915` / `xe` | ✅ |
| Moore Threads (MTT S60, S80, S2000) | `mtgpu-drv` | ✅ |

## Quick Start

```bash
# Install from COPR (Fedora)
sudo dnf copr enable axogm/mvgal
sudo dnf install mvgal

# Start daemon
pkexec systemctl start mvgald

# Verify
mvgal-info
mvgal-status
```

## Project Structure

```
mvgal/
├── kernel/          # Linux kernel module
├── runtime/         # C++20 daemon (mvgald)
├── safe/            # Rust safety-critical crates
├── src/             # C userspace library
├── include/         # Public C headers
├── tools/           # CLI tools
├── steam/           # Steam/Proton layer
├── ui/              # Qt dashboard + REST API
├── docs/            # Documentation
└── config/          # Default config
```

## License

- Kernel module: **GPL-2.0-only**
- Userspace: **MIT**
- Rust crates: **MIT OR Apache-2.0**
