# MVGAL Build and Test Guide

## Quick Start

```bash
# Clone or extract MVGAL
cd mvgal

# Build everything
make all

# Run benchmarks
make benchmarks
./benchmarks/synthetic/mvgal_synthetic_bench -q
./benchmarks/real_world/mvgal_realworld_bench -q
./benchmarks/stress/mvgal_stress_bench -q -d 1000

# Build and test CLI tools
make tools
./tools/mvgal --version
./tools/mvgal status
./tools/mvgal list-gpus

# Build GUI tools (requires GTK3)
make gui
./gui/mvgal-gui &
./gui/mvgal-tray &

# Build DBus service (requires libdbus-1-dev)
make dbus
./pkg/dbus/mvgal-dbus-service &

# Build kernel module
make kernel

# Create tarball
make tarball

# Install to system
sudo make install

# Uninstall
sudo make uninstall

# Clean up
make clean       # Remove build artifacts
make distclean   # Remove all build artifacts
```

## Detailed Build Instructions

### Prerequisites

**Required Packages:**
```bash
# Debian/Ubuntu
sudo apt-get install gcc make linux-headers-$(uname -r) pkg-config

# Fedora/RHEL/CentOS
sudo dnf install gcc make kernel-headers kernel-devel pkgconfig

# Arch Linux
sudo pacman -S gcc make linux-headers pkgconf
```

**Optional Dependencies:**
```bash
# For GUI tools
sudo apt-get install libgtk-3-dev libglib2.0-dev      # Debian/Ubuntu
sudo dnf install gtk3-devel glib2-devel           # Fedora/RHEL
sudo pacman -S gtk3                                # Arch

# For DBus service
sudo apt-get install libdbus-1-dev                 # Debian/Ubuntu
sudo dnf install dbus-devel                        # Fedora/RHEL
sudo pacman -S dbus                               # Arch

# All dependencies (Debian/Ubuntu)
sudo apt-get install gcc make linux-headers-$(uname -r) pkg-config \
    libgtk-3-dev libglib2.0-dev libdbus-1-dev
```

### Building Individual Components

#### 1. Kernel Module
```bash
cd src/kernel
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

#### 2. Benchmarks
```bash
cd benchmarks
make clean
make -j4

# Run synthetic benchmarks
./synthetic/mvgal_synthetic_bench -q
./synthetic/mvgal_synthetic_bench -q -n  # No verification
./synthetic/mvgal_synthetic_bench -q -i 100  # 100 iterations

# Run real-world benchmarks
./real_world/mvgal_realworld_bench -q
./real_world/mvgal_realworld_bench -q -n

# Run stress benchmarks
./stress/mvgal_stress_bench -q -n -d 1000  # Run for 1 second
```

#### 3. CLI Tools
```bash
cd tools
make clean
make -j4

# Test CLI tools
./mvgal --version
./mvgal --help
./mvgal status
./mvgal list-gpus
./mvgal show-config
./mvgal set-strategy afr
./mvgal enable-gpu 0
./mvgal disable-gpu 1
./mvgal set-priority 0 10
./mvgal reset-stats
./mvgal reload
```

#### 4. GUI Tools
```bash
cd gui
make clean
make -j4

# Run GUI (requires X11)
./mvgal-gui &
./mvgal-tray &
```

#### 5. DBus Service
```bash
cd pkg/dbus
gcc -o mvgal-dbus-service mvgal-dbus-service.c \
    -I../.. -I../../src/userspace/core \
    -L../../src/userspace/core -lmvgal_core -lpthread \
    $(pkg-config --cflags --libs dbus-1) -O2 -Wall

# Test DBus service
# In one terminal:
./mvgal-dbus-service &

# In another terminal:
# Use dbus-send or gdbus to test
```

### Testing

#### Benchmark Tests
```bash
# Run all benchmarks with quiet mode (no output except results)
cd benchmarks && make run

# Check results
cat results/*.txt

# Manual run
./synthetic/mvgal_synthetic_bench -q -n
./real_world/mvgal_realworld_bench -q -n
./stress/mvgal_stress_bench -q -n -d 1000
```

#### CLI Tool Tests
```bash
cd tools

# Version check
./mvgal --version

# Help text
./mvgal --help
./mvgal-config --help

# List GPUs
./mvgal list-gpus
./mvgal-config list-gpus

# Show status
./mvgal status

# Change strategy
./mvgal set-strategy round_robin
./mvgal set-strategy afr
./mvgal set-strategy sfr

# GPU management
./mvgal list-gpus
./mvgal enable-gpu 0
./mvgal disable-gpu 1
./mvgal set-priority 0 5

# Configuration
./mvgal show-config
./mvgal reset-stats
```

### Kernel Module Testing

#### Load Module
```bash
# As root or with sudo
sudo insmod src/kernel/mvgal.ko

# Check if loaded
lsmod | grep mvgal

# Check device node
dmesg | tail -20
ls -la /dev/mvgal0

# Check module info
modinfo src/kernel/mvgal.ko
```

#### Unload Module
```bash
# As root or with sudo
sudo rmmod mvgal

# Check if unloaded
lsmod | grep mvgal || echo "Module unloaded"
```

#### Using Scripts
```bash
# Load
sudo ./config/load-module.sh

# Unload
sudo ./config/unload-module.sh
```

### Packaging

#### Create Tarball
```bash
make tarball
ls -lh dist/
```

#### Extract Tarball
```bash
tar -xzf dist/mvgal-0.1.0.tar.gz
cd mvgal
```

#### Debian Package
```bash
# Install build dependencies
sudo apt-get install debhelper dpkg-dev

# Build package
cd mvgal
cp -r pkg/debian .
dch -i  # Update changelog if needed
dpkg-buildpackage -us -uc

# Install
sudo dpkg -i ../mvgal_0.1.0-1_amd64.deb
```

#### RPM Package
```bash
# Install build dependencies
sudo dnf install rpm-build rpmdevtools

# Prepare build environment
rpmdev-setuptree
cp -r mvgal ~/rpmbuild/SOURCES/
cd ~/rpmbuild/SOURCES/mvgal

# Build package
rpmbuild -bb pkg/rpm/mvgal.spec

# Install
sudo rpm -ivh ~/rpmbuild/RPMS/x86_64/mvgal-0.1.0-1.x86_64.rpm
```

#### Arch Linux Package
```bash
# Build and install
cd mvgal/pkg/arch
makepkg -si

# Or just build
makepkg -p PKGBUILD
```

#### Flatpak
```bash
# Install flatpak and builder
sudo apt-get install flatpak flatpak-builder
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

# Build
flatpak-builder --user --install build-dir pkg/flatpak/org.mvgal.MVGAL.json

# Run
flatpak run org.mvgal.MVGAL
```

#### Snap
```bash
# Install snapd
sudo apt-get install snapd

# Build
snapcraft --use-lxd

# Install
sudo snap install mvgal_0.1.0_amd64.snap --dangerous --devmode

# Run
snap run mvgal
```

## Configuration

### Main Configuration
The main configuration file is `config/mvgal.conf`. It uses an INI-style format:

```ini
[core]
enabled = true
debug_level = info
gpu_count = 2
default_strategy = round_robin
enable_memory_migration = true
enable_dmabuf = true
enable_kernel_names = true
stats_interval = 1
enable_stats = true

[cuda]
enabled = true
intercept_driver = true
intercept_runtime = true
enable_launch_intercept = true
track_memory = true

[direct3d]
enabled = true

[webgpu]
enabled = true

[opencl]
enabled = true
```

### Per-GPU Configuration
```ini
[gpu_0]
type = auto
priority = 0
memory_limit_mb = 0
enabled = true

[gpu_1]
type = auto
priority = 1
memory_limit_mb = 0
enabled = true
```

### Using Environment Variables
```bash
# Enable/disable MVGAL
export MVGAL_ENABLED=1

# Set strategy
export MVGAL_STRATEGY=round_robin

# Set debug level
export MVGAL_DEBUG=info

# Enable specific API interception
export MVGAL_CUDA_ENABLED=1
export MVGAL_D3D_ENABLED=1
export MVGAL_WEBGPU_ENABLED=1
```

## DBus Interface

### Service
- **Bus Name:** org.mvgal.MVGAL
- **Object Path:** /org/mvgal/MVGAL
- **Interface:** org.mvgal.MVGAL

### Methods
```bash
# List GPUs
dbus-send --system --dest=org.mvgal.MVGAL --type=method_call \
    --print-reply /org/mvgal/MVGAL org.mvgal.MVGAL.ListGPUs

# Get GPU info
dbus-send --system --dest=org.mvgal.MVGAL --type=method_call \
    --print-reply /org/mvgal/MVGAL org.mvgal.MVGAL.GetGPUInfo int32:0

# Get stats
dbus-send --system --dest=org.mvgal.MVGAL --type=method_call \
    --print-reply /org/mvgal/MVGAL org.mvgal.MVGAL.GetStats
```

### Using gdbus
```bash
gdbus call --system --dest org.mvgal.MVGAL --object-path /org/mvgal/MVGAL \
    --method org.mvgal.MVGAL.ListGPUs
```

## Troubleshooting

### Kernel Module Issues

**Problem:** `insmod: ERROR: could not insert module: Invalid arguments`

**Solution:** 
```bash
# Check kernel version
uname -r

# Rebuild module for current kernel
cd src/kernel
make clean
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

**Problem:** `insmod: ERROR: could not insert module: Device or resource busy`

**Solution:**
```bash
# Unload existing module first
sudo rmmod mvgal

# Check what's using the device
lsof /dev/mvgal0

# Kill processes using the device
sudo kill -9 $(lsof -t /dev/mvgal0) 2>/dev/null || true

# Try again
sudo insmod mvgal.ko
```

### Compilation Issues

**Problem:** Missing headers

**Solution:** Install required development packages:
```bash
sudo apt-get install linux-headers-$(uname -r)
```

**Problem:** GTK compilation fails

**Solution:** Install GTK3 development packages:
```bash
sudo apt-get install libgtk-3-dev
```

### LD_PRELOAD Issues

**Problem:** `LD_PRELOAD not working`

**Solution:**
```bash
# Make sure library is in LD_LIBRARY_PATH
LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
ldconfig

# Verify library exists
ls -la /usr/local/lib/libmvgal_core.so

# Test with explicit LD_PRELOAD
LD_PRELOAD=/usr/local/lib/libmvgal_core.so your_application
```

## Performance Tuning

### Benchmark Results Interpretation

Each benchmark outputs:
- **Min/Max/Avg time:** Time in milliseconds
- **Std dev:** Standard deviation (lower = more consistent)
- **Operations:** Number of operations performed
- **Throughput:** Operations per second (higher = better)
- **Status:** PASS or FAIL

### GPU Strategy Selection

| Strategy | Description | Best For |
|----------|-------------|----------|
| round_robin | Distribute workloads evenly | General use |
| afr | Alternate Frame Rendering | Multi-frame workloads |
| sfr | Split Frame Rendering | Single large workloads |
| single | Use single GPU | Single-GPU systems |
| hybrid | Primary + fallback | Mixed workloads |
| custom | Custom script | Special requirements |

### Memory Settings

```ini
# Enable cross-GPU memory migration
enable_memory_migration = true

# Enable DMA-BUF sharing
enable_dmabuf = true

# Set per-GPU memory limits (MB)
[gpu_0]
memory_limit_mb = 8192

[gpu_1]
memory_limit_mb = 8192
```

## Advanced Usage

### Debugging

**Enable debug output:**
```bash
export MVGAL_DEBUG=debug
./your_application

# Or in config file
[core]
debug_level = debug
```

**Dump intercepted calls:**
```ini
[debug]
dump_calls = true
dump_file = /tmp/mvgal_dump.log
```

### Custom Strategy

Create a Lua script for custom workload distribution:

```lua
-- config/custom_strategy.lua
function distribute(workload)
    -- workload.gpu_index: current GPU index
    -- workload.workload_type: type of workload
    -- workload.priority: workload priority
    
    -- Return the GPU index to use
    if workload.priority > 10 then
        return 0  -- Use GPU 0 for high priority
    else
        return 1  -- Use GPU 1 for normal priority
    end
end
```

### Kernel Name Resolution

The CUDA wrapper attempts to resolve kernel names from function pointers. To improve accuracy:

```ini
[core]
enable_kernel_names = true

[cuda]
enable_launch_intercept = true
```

This requires symbol table access and may need additional configuration depending on your environment.

## Contributing

### Code Style
- Use C99 standard
- Use designated initializers for structs
- Use `__attribute__((unused))` for unused variables/parameters
- Keep lines under 100 characters when possible
- Use doxygen-style comments for functions

### Testing
1. Run all benchmarks
2. Test with multiple GPUs
3. Test different strategies
4. Check memory pressure handling

### Bug Reports
Include:
- MVGAL version
- Kernel version (`uname -a`)
- GPU hardware and drivers
- Steps to reproduce
- Relevant log output

## License

MVGAL is licensed under GPL-3.0-or-later. See COPYING or LICENSE file for details.
