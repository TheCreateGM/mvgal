# Contributing to MVGAL

Thank you for your interest in contributing to the Multi-Vendor GPU Aggregation Layer for Linux (MVGAL).

## Project Overview

MVGAL enables heterogeneous GPUs (AMD, NVIDIA, Intel, Moore Threads) to function as a single logical compute and rendering device. The project is written in C11 and targets Linux systems.

## Development Status

- **Version:** 0.2.0 "Health Monitor"
- **Completion:** ~92%
- **License:** GPLv3
- **Language:** C11

## Prerequisites

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| Linux Kernel | 5.4+ | 6.0+ |
| GCC/Clang | 11+ | 13+ |
| CMake | 3.16+ | 3.20+ |
| libdrm | 2.4.100+ | latest |
| libpci | latest | latest |
| Vulkan SDK | 1.3+ | latest |

### Ubuntu/Debian
```bash
sudo apt update
sudo apt install -y git build-essential cmake pkg-config \
    libdrm-dev libpci-dev libudev-dev \
    vulkan-tools libvulkan-dev libopencl-dev
```

### Fedora/RHEL
```bash
sudo dnf install -y git gcc gcc-c++ cmake make pkgconfig \
    libdrm-devel libpci-devel systemd-devel \
    vulkan-devel opencl-headers ocl-icd-devel
```

### Arch Linux
```bash
sudo pacman -S git gcc make cmake pkgconf \
    libdrm libpci systemd ccache \
    vulkan-devel opencl-headers ocl-icd
```

## Building

```bash
# Clone the repository
git clone https://github.com/TheCreateGM/mvgal.git
cd mvgal

# Create build directory
mkdir -p build && cd build

# Configure (with Vulkan and tests)
cmake -DWITH_VULKAN=OFF -DWITH_TESTS=ON ..

# Build
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

## Project Structure

```
mvgal/
├── include/mvgal/         # Public API headers
├── src/userspace/
│   ├── api/              # Core API implementations
│   ├── daemon/           # Daemon (gpu_manager, config, ipc, main)
│   ├── memory/           # Memory layer (memory, dmabuf, allocator, sync)
│   ├── scheduler/        # Workload scheduler and strategies
│   └── intercept/        # API interception layers
├── tests/
│   ├── unit/            # Unit tests
│   └── integration/      # Integration tests
└── docs/                # Documentation
```

## Coding Standards

- **Language:** C11
- **Compiler Flags:** `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
- **No Warnings:** Code must compile with `-Wall -Wextra -Werror`
- **Thread Safety:** Use mutexes or atomics for shared state
- **Error Handling:** All public APIs return proper error codes

## Current Missing Components

| Component | Priority | Status |
|-----------|----------|--------|
| Vulkan Layer | High | 5% (blocker: Vulkan SDK headers) |
| CUDA Wrapper | Medium | 0% |
| Kernel Module | Medium | 0% |

See [docs/MISSING.md](docs/MISSING.md) for detailed component status.

## How to Contribute

### 1. Pick an Issue
- Check [GitHub Issues](https://github.com/TheCreateGM/mvgal/issues)
- Look for issues tagged `help wanted` or `good first issue`

### 2. Fork and Clone
```bash
git clone https://github.com/TheCreateGM/mvgal.git
cd mvgal
```

### 3. Create a Branch
```bash
git checkout -b feature/your-feature-name
# or
git checkout -b fix/issue-description
```

### 4. Make Changes
- Follow coding standards
- Test your changes
- Ensure no new warnings

### 5. Commit
```bash
git add -A
git commit -m "Add: description of changes"
```

### 6. Push
```bash
git push origin feature/your-feature-name
```

### 7. Create Pull Request
Open a PR on GitHub with:
- Clear title describing the change
- Description of what/why/how
- Reference any related issues

## Areas Needing Contribution

1. **Vulkan Layer** - Complete vk_instance.c, vk_device.c, vk_queue.c, vk_command.c
2. **CUDA Wrapper** - LD_PRELOAD wrapper for CUDA API
3. **Kernel Module** - Optional kernel module for advanced features
4. **Tests** - Additional unit and integration tests

## Communication

- **Issues:** [GitHub Issues](https://github.com/TheCreateGM/mvgal/issues)
- **Discussions:** [GitHub Discussions](https://github.com/TheCreateGM/mvgal/discussions)
- **Email:** creategm10@proton.me

## Related Documentation

- [README.md](README.md) - Project overview
- [docs/PROGRESS.md](docs/PROGRESS.md) - Development progress
- [docs/MISSING.md](docs/MISSING.md) - Missing components
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) - Community guidelines

---

*© 2026 MVGAL Project. Version 0.2.0 "Health Monitor".*