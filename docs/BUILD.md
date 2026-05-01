# MVGAL Build Guide

**Version:** 0.2.1 | **Last Updated:** May 2026

---

> ⚠️ Note: Build artifacts and Copr build results have been archived to `builds_archive/` at the project root. See `docs/BUILD_ARTIFACTS.md` for details.


## Quick Start (Ubuntu 22.04 / 24.04)

```bash
# 1. Install dependencies
sudo apt-get update
sudo apt-get install -y \
    cmake ninja-build pkg-config \
    gcc g++ clang clang-tidy clang-format \
    libdrm-dev libpci-dev libudev-dev \
    libvulkan-dev vulkan-validationlayers mesa-vulkan-drivers \
    libgtest-dev \
    git

# 2. Clone and build
git clone https://github.com/TheCreateGM/mvgal.git
cd mvgal
cmake -B build -G Ninja -DWITH_VULKAN=ON -DWITH_TESTS=ON
cmake --build build --parallel $(nproc)

# 3. Run tests
ctest --test-dir build --output-on-failure

# 4. Install
sudo cmake --install build
```

---

## Dependencies

### Required

| Package | Ubuntu/Debian | Fedora/RHEL | Arch Linux |
|---------|--------------|-------------|------------|
| CMake ≥ 3.20 | `cmake` | `cmake` | `cmake` |
| Ninja | `ninja-build` | `ninja-build` | `ninja` |
| GCC ≥ 12 | `gcc g++` | `gcc gcc-c++` | `gcc` |
| libdrm | `libdrm-dev` | `libdrm-devel` | `libdrm` |
| libpci | `libpci-dev` | `pciutils-devel` | `pciutils` |
| libudev | `libudev-dev` | `systemd-devel` | `systemd-libs` |
| pkg-config | `pkg-config` | `pkgconfig` | `pkgconf` |

### Optional

| Package | Purpose | CMake Flag |
|---------|---------|------------|
| `libvulkan-dev` | Vulkan layer | `-DWITH_VULKAN=ON` |
| `mesa-vulkan-drivers` | lavapipe for Vulkan tests | — |
| `opencl-headers ocl-icd-dev` | OpenCL layer | `-DWITH_OPENCL=ON` |
| CUDA Toolkit | CUDA shim | `-DWITH_CUDA=ON` |
| `libgtest-dev` | Unit tests | `-DWITH_TESTS=ON` |
| Rust toolchain (≥ 1.75) | Rust safety crates | — |
| `linux-headers-$(uname -r)` | Kernel module | `-DWITH_KERNEL_MODULE=ON` |

---

## CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `WITH_VULKAN` | `ON` | Build Vulkan interception layer |
| `WITH_OPENCL` | `ON` | Build OpenCL interception layer |
| `WITH_CUDA` | `OFF` | Build CUDA wrapper (requires CUDA SDK) |
| `WITH_DAEMON` | `ON` | Build mvgald daemon |
| `WITH_TESTS` | `ON` | Build test suite |
| `WITH_BENCHMARKS` | `OFF` | Build benchmark suite |
| `WITH_KERNEL_MODULE` | `OFF` | Build kernel module |
| `WITH_DEBUG` | `OFF` | Debug build with symbols |
| `WITH_ASAN` | `OFF` | AddressSanitizer |
| `WITH_UBSAN` | `OFF` | UndefinedBehaviorSanitizer |
| `WITH_TSAN` | `OFF` | ThreadSanitizer |

### Example Configurations

```bash
# Default (recommended for most users)
cmake -B build -G Ninja -DWITH_VULKAN=ON -DWITH_TESTS=ON

# Debug with sanitizers
cmake -B build -G Ninja -DWITH_DEBUG=ON -DWITH_ASAN=ON -DWITH_UBSAN=ON

# Minimal (no Vulkan, no tests)
cmake -B build -G Ninja -DWITH_VULKAN=OFF -DWITH_TESTS=OFF

# Full (all optional components)
cmake -B build -G Ninja \
    -DWITH_VULKAN=ON \
    -DWITH_OPENCL=ON \
    -DWITH_TESTS=ON \
    -DWITH_BENCHMARKS=ON
```

---

## Meson Build (Alternative)

```bash
# Install meson
pip install meson  # or: sudo apt install meson

# Configure
meson setup builddir -Dwith_vulkan=true -Dwith_tests=true

# Build
ninja -C builddir

# Test
ninja -C builddir test

# Install
sudo ninja -C builddir install
```

---

## Rust Crates

The Rust safety-critical crates in `safe/` are built separately:

```bash
# Install Rust (if not already installed)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

# Build all Rust crates
cargo build --release

# Run Rust tests
cargo test

# Check with clippy
cargo clippy --all-targets -- -D warnings

# Format check
cargo fmt --all -- --check
```

The compiled static libraries (`libmvgal_fence.a`, `libmvgal_memory_safety.a`,
`libmvgal_capability.a`) are linked into the daemon when CMake finds them.

---

## Kernel Module

```bash
# Install kernel headers
sudo apt-get install linux-headers-$(uname -r)  # Ubuntu/Debian
sudo dnf install kernel-devel                    # Fedora

# Build kernel module
cmake -B build -G Ninja -DWITH_KERNEL_MODULE=ON
cmake --build build --target mvgal_kernel

# Or build directly with make
make -C src/kernel

# Load module
sudo insmod build/src/kernel/mvgal.ko

# Verify
ls /dev/mvgal0
dmesg | grep MVGAL

# Unload
sudo rmmod mvgal
```

### DKMS Installation

```bash
# Install DKMS
sudo apt-get install dkms

# Register with DKMS
sudo cp -r . /usr/src/mvgal-0.2.0
sudo dkms add mvgal/0.2.0
sudo dkms build mvgal/0.2.0
sudo dkms install mvgal/0.2.0
```

---

## Running Tests

```bash
# All tests
ctest --test-dir build --output-on-failure

# Specific test
ctest --test-dir build -R test_gpu_detection --output-on-failure

# With verbose output
ctest --test-dir build -V

# Vulkan layer test (requires lavapipe)
sudo apt-get install mesa-vulkan-drivers
ctest --test-dir build -R test_vulkan_layer_submit --output-on-failure
```

---

## Cross-Compilation (ARM64)

```bash
# Install cross-compiler
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Configure for ARM64
cmake -B build-arm64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
    -DWITH_VULKAN=OFF \
    -DWITH_KERNEL_MODULE=OFF

cmake --build build-arm64
```

---

## Static Analysis

```bash
# clang-tidy
cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/userspace/intercept/vulkan/vk_layer.c

# clang-format check
find src -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror

# Apply clang-format
find src -name "*.c" -o -name "*.h" | xargs clang-format -i

# shellcheck
find . -name "*.sh" | xargs shellcheck
```

---

## Packaging

### Debian/Ubuntu

```bash
cd packaging
./build_deb.sh
# Output: mvgal_0.2.0_amd64.deb
sudo dpkg -i mvgal_0.2.0_amd64.deb
```

### Fedora/RHEL

```bash
rpmbuild -ba packaging/rpm/mvgal.spec
# Output in ~/rpmbuild/RPMS/
```

### Arch Linux

```bash
cd packaging/arch
makepkg -si
```

---

## Troubleshooting

**Build fails: `vulkan/vulkan.h` not found**
```bash
sudo apt-get install libvulkan-dev
# or disable Vulkan:
cmake -B build -DWITH_VULKAN=OFF
```

**Build fails: `libdrm` not found**
```bash
sudo apt-get install libdrm-dev
```

**Kernel module fails to load: `-EBUSY`**
```bash
# Check if another module holds the device number
ls /dev/mvgal*
sudo rmmod mvgal
sudo insmod build/src/kernel/mvgal.ko
```

**Tests fail: `lavapipe ICD not found`**
```bash
sudo apt-get install mesa-vulkan-drivers
# Verify:
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json vulkaninfo
```
