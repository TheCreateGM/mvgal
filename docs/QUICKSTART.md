# MVGAL Quick Start

Get MVGAL running in 5 minutes.

---

## 1. Check your GPUs

```bash
# See what GPUs the system sees
ls /sys/class/drm/card*/device/vendor 2>/dev/null | xargs -I{} sh -c 'echo {} && cat {}'
```

You need at least 2 GPUs. MVGAL supports AMD, NVIDIA, Intel, and Moore Threads in any combination.

---

## 2. Install dependencies

```bash
# Ubuntu / Debian
sudo apt install cmake ninja-build libdrm-dev libpci-dev libudev-dev \
                 libvulkan-dev pkg-config gcc g++

# Fedora / RHEL
sudo dnf install cmake ninja-build libdrm-devel pciutils-devel \
                 systemd-devel vulkan-devel gcc-c++

# Arch Linux
sudo pacman -S cmake ninja libdrm pciutils systemd vulkan-devel gcc
```

---

## 3. Build

```bash
git clone https://github.com/TheCreateGM/mvgal.git
cd mvgal

mkdir -p build_output && cd build_output
cmake .. -DCMAKE_BUILD_TYPE=Release -DMVGAL_BUILD_RUNTIME=ON -DMVGAL_BUILD_TOOLS=ON
make -j$(nproc)
```

---

## 4. Install

```bash
# From the repo root — uses pkexec for privileged steps
bash build/install.sh
```

This installs:
- `mvgald` daemon → `/usr/bin/mvgald`
- CLI tools → `/usr/bin/mvgal-{info,status,bench,compat,config}`
- Vulkan layer → `/usr/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json`
- OpenCL ICD → `/etc/OpenCL/vendors/mvgal.icd`
- Config → `/etc/mvgal/mvgal.conf`
- Systemd service → `/etc/systemd/system/mvgald.service`

---

## 5. Start the daemon

```bash
pkexec systemctl start mvgald
pkexec systemctl enable mvgald   # auto-start on boot
```

---

## 6. Verify

```bash
mvgal-info
```

Expected output:
```
========================================================================
  MVGAL — Multi-Vendor GPU Aggregation Layer  |  mvgal-info
========================================================================
  Kernel : Linux 6.19.10-300.fc44.x86_64
  mvgal.ko: not loaded
  /dev/mvgal0: absent

  Detected 2 GPU(s):

  GPU 0 — AMD GPU [1002:743f]
  ------------------------------------------------------------
    PCI slot   : 0000:03:00.0
    Vendor ID  : 0x1002  (AMD)
    VRAM       : 3.98 GiB total, 0.79 GiB used (20%)
    Temperature: 56 °C
    Utilization: 12 %

  GPU 1 — NVIDIA GPU [10de:2584]
  ------------------------------------------------------------
    PCI slot   : 0000:04:00.0
    Vendor ID  : 0x10DE  (NVIDIA)
    VRAM       : 8.00 GiB total

========================================================================
  Logical MVGAL Device
========================================================================
  Physical GPUs aggregated : 2
  Vendors present          : AMD NVIDIA
  Capability tier          : Mixed (heterogeneous)
  Vulkan layer registered  : yes
  Daemon socket            : /run/mvgal/mvgal.sock (present)
```

---

## 7. Use with applications

### Any Vulkan application

The Vulkan layer is implicit — it activates automatically for all Vulkan apps. No changes needed.

```bash
# Verify the layer is in the chain
vulkaninfo 2>/dev/null | grep MVGAL
```

### Steam games

Add to Steam launch options (Properties → Launch Options):
```
ENABLE_MVGAL=1 MVGAL_STRATEGY=afr %command%
```

### OpenCL applications

```bash
# Check MVGAL platform is visible
clinfo | grep -A3 MVGAL
```

### CUDA applications

```bash
LD_PRELOAD=/usr/lib/libmvgal_cuda.so your_cuda_app
```

### OpenGL applications (via Zink)

```bash
MESA_LOADER_DRIVER_OVERRIDE=zink ENABLE_MVGAL=1 glxgears
```

---

## 8. Monitor in real time

```bash
# One-shot status
mvgal-status

# Continuous refresh every 500ms
mvgal-status --watch --interval 500

# Run benchmarks
mvgal-bench all

# Check app compatibility
mvgal-compat "doom"
mvgal-compat --system
```

---

## 9. Change scheduling strategy

```bash
# Alternate Frame Rendering (best for gaming)
mvgal-config set-strategy afr

# Compute offload (best for AI/HPC)
mvgal-config set-strategy compute_offload

# Show current config
mvgal-config show-config
```

---

## 10. Load the kernel module (optional)

The kernel module enables deeper integration (DMA-BUF at kernel level, `/dev/mvgal0`). It is optional — MVGAL works without it via user-space interception.

```bash
cd kernel
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
pkexec insmod mvgal.ko
dmesg | grep MVGAL
```

---

## Troubleshooting

**No GPUs detected:**
```bash
# Check DRM devices exist
ls /sys/class/drm/card*/device/vendor
# Check GPU drivers are loaded
lsmod | grep -E 'amdgpu|nvidia|i915|xe|mtgpu'
```

**Daemon not starting:**
```bash
pkexec journalctl -u mvgald -n 50
# Or run in foreground
mvgald --no-daemon
```

**Vulkan layer not active:**
```bash
ls /usr/share/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json
# If missing, reinstall:
bash build/install.sh --no-kernel --no-daemon
```

**Permission denied on socket:**
```bash
# Add yourself to the video group
pkexec usermod -aG video $USER
# Log out and back in
```
