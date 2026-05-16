# MVGAL Installation Guide

**Version:** 0.2.2 | **Date:** May 2026

---

## Quick Install

### Fedora / RHEL / CentOS

```bash
# Enable COPR repository
sudo dnf copr enable mvgal/mvgal

# Install
sudo dnf install mvgal mvgal-dkms

# Start daemon
sudo systemctl enable --now mvgald
```

### Debian / Ubuntu

```bash
# Add PPA
sudo add-apt-repository ppa:mvgal/stable
sudo apt update

# Install
sudo apt install mvgal mvgal-dkms

# Start daemon
sudo systemctl enable --now mvgald
```

### Arch Linux

```bash
# Install from AUR
yay -S mvgal-dkms mvgal

# Start daemon
sudo systemctl enable --now mvgald
```

---

## Prerequisites

### Hardware

- **Minimum**: 2 GPUs from any supported vendor
- **Recommended**: GPUs on same PCIe root complex for P2P support
- **Supported vendors**: AMD (RDNA 1/2/3), NVIDIA (Turing/Ampere/Hopper/Ada), Intel (Xe/Arc), Moore Threads (S2000/S3000/S4000)

### Software

| Component | Version | Required |
|-----------|---------|----------|
| Linux kernel | 6.1+ | Yes |
| GCC | 11+ | Yes (C11) |
| CMake | 3.16+ | Yes |
| Rust | 1.75+ | Yes (safety crates) |
| Go | 1.21+ | Optional (REST server, exporter) |
| Qt5/Qt6 | 5.15+ / 6.2+ | Optional (dashboard) |
| Vulkan SDK | 1.3+ | Optional (Vulkan layer) |
| CUDA Toolkit | 12.0+ | Optional (CUDA wrapper) |
| ROCm | 5.0+ | Optional (AMD compute) |
| oneAPI | 2024.0+ | Optional (Intel compute) |

### Vendor Drivers

Install vendor drivers **before** installing MVGAL:

```bash
# AMD (open-source, included in kernel)
# No additional installation needed

# NVIDIA (proprietary)
sudo dnf install akmod-nvidia  # Fedora
sudo apt install nvidia-driver  # Debian/Ubuntu

# Intel (open-source, included in kernel)
# No additional installation needed

# Moore Threads
# Install mtgpu-drv from vendor
```

---

## Build from Source

### CMake (Recommended)

```bash
git clone https://github.com/axogm/mvgal.git
cd mvgal
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### Meson

```bash
meson setup build --buildtype=release
ninja -C build
sudo ninja -C build install
```

### Zig

```bash
zig build -Doptimize=ReleaseSafe
sudo zig build install
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `-DMVGAL_BUILD_TESTS=ON` | ON | Build test suite |
| `-DMVGAL_BUILD_BENCHMARKS=ON` | ON | Build benchmarks |
| `-DMVGAL_BUILD_VULKAN_LAYER=ON` | ON | Build Vulkan layer |
| `-DMVGAL_BUILD_CUDA_WRAPPER=ON` | ON | Build CUDA wrapper |
| `-DMVGAL_BUILD_OPENCL_ICD=ON` | ON | Build OpenCL ICD |
| `-DMVGAL_BUILD_D3D_WRAPPER=ON` | ON | Build D3D wrapper |
| `-DMVGAL_BUILD_METAL_WRAPPER=ON` | ON | Build Metal wrapper |
| `-DMVGAL_BUILD_WEBGPU_WRAPPER=ON` | ON | Build WebGPU wrapper |
| `-DMVGAL_BUILD_UI=ON` | ON | Build Qt dashboard |
| `-DMVGAL_BUILD_REST_SERVER=ON` | ON | Build Go REST server |
| `-DMVGAL_BUILD_PROMETHEUS_EXPORTER=ON` | ON | Build Prometheus exporter |
| `-DMVGAL_BUILD_STEAM_LAYER=ON` | ON | Build Steam compatibility layer |
| `-DMVGAL_BUILD_OPENGL_LAYER=ON` | ON | Build OpenGL preload shim |
| `-DMVGAL_BUILD_BINDINGS=ON` | ON | Build language bindings |

### Kernel Module

```bash
# Build
cd kernel
make

# Install
sudo make install

# Load
sudo modprobe mvgal

# Verify
ls -l /dev/mvgal*
ls /sys/class/mvgal/
```

### DKMS (Auto-rebuild on kernel update)

```bash
sudo dkms add ./kernel
sudo dkms build mvgal/0.2.2
sudo dkms install mvgal/0.2.2
```

### Rust Crates

```bash
cd safe
cargo build --release
cargo test
```

---

## Post-Installation

### 1. Verify Installation

```bash
# Check kernel module
lsmod | grep mvgal

# Check device
ls -l /dev/mvgal*

# Check sysfs
ls /sys/class/mvgal/

# Check daemon
systemctl status mvgald

# Run info tool
mvgal-info
```

### 2. Configure

```bash
# Edit configuration
sudo nano /etc/mvgal/mvgal.conf

# Set scheduling strategy
mvgal-config strategy set auto

# Enable GPUs
mvgal-config gpu enable 0
mvgal-config gpu enable 1
```

### 3. Enable Vulkan Layer

```bash
# System-wide
sudo cp src/userspace/vulkan_layer/MVGAL_VkLayer_mvgal.json \
  /etc/vulkan/implicit_layer.d/

# Or per-user
mkdir -p ~/.local/share/vulkan/implicit_layer.d/
cp src/userspace/vulkan_layer/MVGAL_VkLayer_mvgal.json \
  ~/.local/share/vulkan/implicit_layer.d/
```

### 4. Enable CUDA Wrapper

```bash
# Add to /etc/ld.so.preload
echo "/usr/lib/mvgal/libmvgal_cuda.so" | sudo tee -a /etc/ld.so.preload

# Or per-application
LD_PRELOAD=/usr/lib/mvgal/libmvgal_cuda.so your_app
```

### 5. Enable OpenCL ICD

```bash
# Register ICD
echo "/usr/lib/mvgal/libmvgal_opencl.so" | sudo tee /etc/OpenCL/vendors/mvgal.icd
```

### 6. Enable OpenGL Layer

```bash
# Per-application
LD_PRELOAD=/usr/lib/mvgal/libmvgal_gl.so your_app
```

### 7. Enable Steam Layer

```bash
# Copy to Steam compatibility tools
cp -r steam ~/.steam/root/compatibilitytools.d/mvgal

# Or system-wide
sudo cp -r steam /usr/share/steam/compatibilitytools.d/mvgal
```

---

## Configuration Reference

### `/etc/mvgal/mvgal.conf`

```ini
[daemon]
log_level = info
log_file = /var/log/mvgal/mvgald.log
pid_file = /var/run/mvgal/mvgald.pid
ipc_socket = /var/run/mvgal/mvgald.sock

[scheduler]
strategy = auto
frame_timeout_ms = 16
migration_threshold = 3
work_stealing = true

[memory]
allocation_policy = best_fit
transfer_policy = dma_buf
staging_buffer_size_mb = 256

[power]
idle_timeout_ms = 5000
sustained_timeout_ms = 30000
park_timeout_ms = 60000
dvfs_enabled = true
thermal_threshold_c = 85

[metrics]
poll_interval_ms = 1000
telemetry_enabled = true
prometheus_enabled = true
prometheus_port = 9100

[rest]
enabled = true
port = 7474
bind = 127.0.0.1
```

---

## Troubleshooting

### Kernel Module Not Loading

```bash
# Check dmesg
dmesg | grep mvgal

# Check for conflicts
lsmod | grep -E "nvidia|amdgpu|i915"

# Force load
sudo modprobe mvgal
```

### Daemon Not Starting

```bash
# Check logs
journalctl -u mvgald -f

# Check socket
ls -l /var/run/mvgal/mvgald.sock

# Run manually
sudo mvgald --foreground --log-level debug
```

### Vulkan Layer Not Working

```bash
# Check layer discovery
vulkaninfo | grep -i mvgal

# Check layer JSON
cat /etc/vulkan/implicit_layer.d/MVGAL_VkLayer_mvgal.json

# Enable validation
export VK_LAYER_MVGAL_DEBUG=1
```

### CUDA Wrapper Not Intercepting

```bash
# Check preload
cat /etc/ld.so.preload

# Check library
ldd /usr/lib/mvgal/libmvgal_cuda.so

# Test with verbose
LD_PRELOAD=/usr/lib/mvgal/libmvgal_cuda.so LD_DEBUG=libs your_app
```

### No GPUs Detected

```bash
# Check PCI devices
lspci | grep -i vga

# Check sysfs
ls /sys/class/mvgal/mvgal0/

# Rescan
echo 1 | sudo tee /sys/class/mvgal/mvgal0/rescan
```

---

## Uninstall

```bash
# Stop daemon
sudo systemctl stop mvgald
sudo systemctl disable mvgald

# Remove kernel module
sudo rmmod mvgal
sudo dkms remove mvgal/0.2.2 --all

# Remove packages
sudo dnf remove mvgal mvgal-dkms  # Fedora
sudo apt remove mvgal mvgal-dkms  # Debian/Ubuntu
sudo pacman -R mvgal mvgal-dkms   # Arch

# Remove configuration
sudo rm -rf /etc/mvgal/
sudo rm -rf /var/log/mvgal/
sudo rm -rf /var/run/mvgal/

# Remove Vulkan layer
sudo rm /etc/vulkan/implicit_layer.d/MVGAL_VkLayer_mvgal.json

# Remove CUDA wrapper
sudo sed -i '/mvgal_cuda/d' /etc/ld.so.preload

# Remove OpenCL ICD
sudo rm /etc/OpenCL/vendors/mvgal.icd

# Remove Steam layer
rm -rf ~/.steam/root/compatibilitytools.d/mvgal
sudo rm -rf /usr/share/steam/compatibilitytools.d/mvgal
```
