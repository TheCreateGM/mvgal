# MVGAL Build Guide

**Version:** 0.2.1

---

## Prerequisites

### Required

| Package | Ubuntu/Debian | Fedora/RHEL | Arch |
|---------|--------------|-------------|------|
| CMake ≥ 3.16 | `cmake` | `cmake` | `cmake` |
| Ninja | `ninja-build` | `ninja-build` | `ninja` |
| GCC ≥ 11 or Clang ≥ 13 | `gcc g++` | `gcc-c++` | `gcc` |
| libdrm | `libdrm-dev` | `libdrm-devel` | `libdrm` |
| libpci / pciaccess | `libpci-dev` | `pciutils-devel` | `pciutils` |
| libudev | `libudev-dev` | `systemd-devel` | `systemd` |
| pkg-config | `pkg-config` | `pkgconfig` | `pkgconf` |

### Optional

| Package | Purpose | Ubuntu/Debian |
|---------|---------|--------------|
| Vulkan SDK | Vulkan layer build | `libvulkan-dev vulkan-tools` |
| OpenCL headers | OpenCL layer build | `opencl-headers ocl-icd-dev` |
| Rust ≥ 1.75 | Safety crates | `rustup` |
| Go ≥ 1.21 | REST API server | `golang` |
| Qt5 or Qt6 | Dashboard | `qtbase5-dev` or `qt6-base-dev` |
| Linux kernel headers | Kernel module | `linux-headers-$(uname -r)` |

### Automated install

```bash
bash scripts/install_dependencies.sh
```

All privileged steps use `pkexec`.

---

## CMake Build (Primary)

### Quick build

```bash
mkdir -p build_output && cd build_output
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Full build with all options

```bash
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DMVGAL_BUILD_KERNEL=ON \
  -DMVGAL_BUILD_RUNTIME=ON \
  -DMVGAL_BUILD_API=ON \
  -DMVGAL_BUILD_TOOLS=ON \
  -DMVGAL_ENABLE_RUST=ON \
  -DMVGAL_BUILD_TESTS=ON \
  -G Ninja
ninja -j$(nproc)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `MVGAL_BUILD_KERNEL` | ON | Build kernel module source |
| `MVGAL_BUILD_RUNTIME` | ON | Build C++20 runtime daemon |
| `MVGAL_BUILD_API` | OFF | Build API layers (Vulkan, OpenCL, CUDA) |
| `MVGAL_BUILD_GAMING` | OFF | Build gaming integration |
| `MVGAL_BUILD_TOOLS` | ON | Build CLI tools |
| `MVGAL_ENABLE_RUST` | ON | Build Rust safety crates |
| `MVGAL_BUILD_TESTS` | ON | Build test suite |
| `MVGAL_ENABLE_SANITIZERS` | OFF | Enable ASan + UBSan (Debug only) |
| `MVGAL_ENABLE_COVERAGE` | OFF | Enable gcov coverage |
| `MVGAL_USE_CCACHE` | ON | Use ccache if available |

### Build targets

```bash
cmake --build build_output --target mvgald          # daemon only
cmake --build build_output --target mvgal-info      # single tool
cmake --build build_output --target VK_LAYER_MVGAL  # Vulkan layer
cmake --build build_output --target mvgal_opencl    # OpenCL layer
```

---

## Meson Build (Alternative)

```bash
meson setup builddir \
  -Dwith_vulkan=true \
  -Dwith_opencl=true \
  -Dwith_daemon=true \
  -Dwith_tests=true \
  -Dbuildtype=release
ninja -C builddir
ninja -C builddir test
```

### Meson options (`meson_options.txt`)

| Option | Default | Description |
|--------|---------|-------------|
| `with_vulkan` | false | Build Vulkan layer |
| `with_opencl` | false | Build OpenCL layer |
| `with_cuda` | false | Build CUDA shim |
| `with_daemon` | true | Build daemon |
| `with_tests` | false | Build tests |
| `with_benchmarks` | false | Build benchmarks |
| `with_kernel_module` | false | Build kernel module |

---

## Zig Build

```bash
zig build                                    # build all defaults
zig build -Dbuild-runtime=true              # daemon + frame pacer
zig build -Dbuild-tools=true                # CLI tools
zig build -Dbuild-tests=true test           # run tests
```

---

## Rust Components

```bash
# Build all crates
cargo build --release

# Build individual crates
cargo build --release -p fence_manager
cargo build --release -p memory_safety
cargo build --release -p capability_model

# Run tests
cargo test
cargo test --release

# Check without building
cargo check --all
```

---

## Kernel Module

The kernel module requires kernel headers and must be built with kbuild:

```bash
cd kernel
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load (requires pkexec)
pkexec insmod mvgal.ko
pkexec insmod mvgal.ko enable_debug=1   # with debug logging

# Verify
dmesg | grep MVGAL
ls /dev/mvgal0

# Unload
pkexec rmmod mvgal
```

The kernel module is tested on Linux 6.19. It uses `class_create` compatibility shims for kernels 6.4+.

---

## Qt Dashboard

```bash
mkdir -p ui/build && cd ui/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./mvgal-dashboard
```

Requires Qt5 or Qt6 with Widgets and Network modules.

## Go REST Server

```bash
cd ui
go build -o mvgal-rest-server ./mvgal_rest_server.go
./mvgal-rest-server --listen :7474
```

---

## Running Tests

```bash
# C tests (via CTest)
cd build_output
ctest --output-on-failure --timeout 60

# Rust tests
cargo test

# Standalone tool tests
./tools/mvgal-info
./tools/mvgal-bench all
./tools/mvgal-compat --system
```

---

## Installation

### Generic installer (recommended)

```bash
bash build/install.sh [--prefix /usr] [--no-kernel] [--no-daemon]
```

All privileged steps use `pkexec`:
- Kernel module → `/lib/modules/$(uname -r)/extra/mvgal.ko`
- udev rules → `/etc/udev/rules.d/99-mvgal.rules`
- Vulkan layer → `/usr/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json`
- OpenCL ICD → `/etc/OpenCL/vendors/mvgal.icd`
- Systemd service → `/etc/systemd/system/mvgald.service`
- Config → `/etc/mvgal/mvgal.conf`

### CMake install

```bash
cd build_output
pkexec make install   # or: pkexec cmake --install .
```

---

## Cross-Compilation (ARM64)

```bash
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=build/cmake/toolchains/aarch64-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DMVGAL_BUILD_KERNEL=OFF   # kernel module requires native build
make -j$(nproc)
```

Requires `aarch64-linux-gnu-gcc` cross-compiler:
```bash
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

---

## Packaging

### Debian / Ubuntu

```bash
cd packaging && bash build_deb.sh
# Output: packaging/build/mvgal_0.2.1_amd64.deb
pkexec dpkg -i packaging/build/mvgal_0.2.1_amd64.deb
```

### RPM (Fedora / RHEL / openSUSE)

```bash
rpmbuild -bb packaging/rpm/mvgal.spec
# Output: ~/rpmbuild/RPMS/x86_64/mvgal-0.2.1-1.x86_64.rpm
pkexec rpm -ivh ~/rpmbuild/RPMS/x86_64/mvgal-0.2.1-1.x86_64.rpm
```

### Arch Linux

```bash
cd packaging/arch
makepkg -si
```

---

## CI / GitHub Actions

Both workflows are **manual-only** (`workflow_dispatch`). To run:

1. Go to **Actions** tab on GitHub
2. Select **CI** or **Build on Fedora COPR**
3. Click **Run workflow**

The CI workflow runs:
- Build matrix: Ubuntu 22.04 + 24.04, GCC + Clang
- Unit tests via CTest
- Vulkan layer smoke test (lavapipe)
- clang-tidy static analysis
- clang-format check
- Rust clippy + rustfmt
- Packaging check
- shellcheck on all `.sh` files

---

## Troubleshooting

### `libdrm not found`
```bash
sudo apt install libdrm-dev   # Ubuntu
sudo dnf install libdrm-devel  # Fedora
```

### `vulkan/vulkan.h not found`
```bash
sudo apt install libvulkan-dev
cmake .. -DMVGAL_BUILD_API=ON
```

### Kernel module fails to load: `-EBUSY`
The module uses `alloc_chrdev_region` to avoid conflicts. If `/dev/mvgal0` already exists from a previous load:
```bash
pkexec rmmod mvgal
pkexec insmod kernel/mvgal.ko
```

### Rust build fails: `MSRV`
MVGAL requires Rust 1.75+:
```bash
rustup update stable
rustup default stable
```
