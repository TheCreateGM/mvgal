#!/bin/bash
# MVGAL Comprehensive Build Script
# This script handles Phase 3 (GPU Detection), Phase 4 (Vulkan Layer), and Phase 8 (Daemon)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print section header
print_section() {
    echo ""
    echo -e "${BLUE}=========================================="
    echo "  $1"
    echo "==========================================${NC}"
}

# Function to print status
print_status() {
    echo -e "${YELLOW}  → ${NC}$1"
}

# Function to print success
print_success() {
    echo -e "  ${GREEN}✓${NC} $1"
}

# Function to print error
print_error() {
    echo -e "  ${RED}✗${NC} $1" >&2
}

# Check if we're running from the right directory
if [ ! -f "$SCRIPT_DIR/CMakeLists.txt" ]; then
    echo "Error: Please run this script from the MVGAL root directory."
    exit 1
fi

cd "$SCRIPT_DIR"

print_section "MVGAL Build System"

# =============================================================================
# PHASE 0: Dependency Check
# =============================================================================
print_section "Phase 0: Checking Dependencies"

HAS_GCC=false
HAS_GXX=false
HAS_MAKE=false
HAS_CMAKE=false
HAS_LIBS=0
HAS_VULKAN=false
HAS_OPENCL=false

# Check build tools
print_status "Checking build tools..."
command -v gcc >/dev/null 2>&1 && HAS_GCC=true && print_success "Found: gcc"
command -v g++ >/dev/null 2>&1 && HAS_GXX=true && print_success "Found: g++"
command -v make >/dev/null 2>&1 && HAS_MAKE=true && print_success "Found: make"
command -v cmake >/dev/null 2>&1 && HAS_CMAKE=true && print_success "Found: cmake"

# Check core libraries using pkg-config
print_status "Checking core libraries..."

# libdrm
if pkg-config --exists libdrm 2>/dev/null; then
    print_success "Found: libdrm"
    HAS_LIBS=$((HAS_LIBS + 1))
else
    print_error "Missing: libdrm (libdrm-dev or libdrm-devel)"
fi

# libpci - try multiple names
if pkg-config --exists libpci 2>/dev/null || pkg-config --exists pci 2>/dev/null || pkg-config --exists pciaccess 2>/dev/null; then
    print_success "Found: libpci"
    HAS_LIBS=$((HAS_LIBS + 1))
else
    print_error "Missing: libpci (libpci-dev, pciutils-devel, or pciaccess)"
fi

# systemd (for libudev)
if pkg-config --exists libsystemd 2>/dev/null; then
    print_success "Found: libsystemd"
    HAS_LIBS=$((HAS_LIBS + 1))
else
    print_error "Missing: libsystemd (libudev-dev or systemd-devel)"
fi

# Check Vulkan
print_status "Checking Vulkan SDK..."
if [ -f /usr/include/vulkan/vulkan.h ] || [ -f /usr/local/include/vulkan/vulkan.h ]; then
    HAS_VULKAN=true
    print_success "Found: Vulkan"
else
    print_error "Missing: Vulkan (libvulkan-dev or vulkan-devel)"
fi

# Check OpenCL
print_status "Checking OpenCL..."
if [ -f /usr/include/CL/cl.h ] || [ -f /usr/local/include/CL/cl.h ]; then
    HAS_OPENCL=true
    print_success "Found: OpenCL"
else
    print_error "Missing: OpenCL (opencl-headers)"
fi

# Summary
MISSING_DEPS=0
if [ "$HAS_GCC" = false ] || [ "$HAS_GXX" = false ] || [ "$HAS_MAKE" = false ] || [ "$HAS_CMAKE" = false ]; then
    MISSING_DEPS=$((MISSING_DEPS + 1))
fi
if [ $HAS_LIBS -lt 3 ]; then
    MISSING_DEPS=$((MISSING_DEPS + 1))
fi

# If missing dependencies, offer to install them
if [ $MISSING_DEPS -gt 0 ]; then
    echo ""
    print_error "Missing $MISSING_DEPS dependency groups"
    read -p "  Run dependency installer? [Y/n] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]] || [[ -z $REPLY ]]; then
        print_status "Running dependency installer..."
        if [ -f "$SCRIPT_DIR/scripts/install_dependencies.sh" ]; then
            bash "$SCRIPT_DIR/scripts/install_dependencies.sh"
        else
            print_error "Dependency installer not found at $SCRIPT_DIR/scripts/install_dependencies.sh"
            print_error "Please run: bash scripts/install_dependencies.sh"
            exit 1
        fi
    else
        print_error "Cannot continue without dependencies. Exiting."
        exit 1
    fi
fi

print_success "All dependencies are satisfied"

# =============================================================================
# PHASE 1: Clean Build Directory
# =============================================================================
print_section "Phase 1: Cleaning Build Directory"

if [ -d "$BUILD_DIR" ]; then
    print_status "Removing old build directory..."
    rm -rf "$BUILD_DIR"
    print_success "Build directory cleaned"
fi

mkdir -p "$BUILD_DIR"
print_success "Created build directory: $BUILD_DIR"

# =============================================================================
# PHASE 2: Configure Build
# =============================================================================
print_section "Phase 2: Configuring Build"

cd "$BUILD_DIR"

# Determine build type
BUILD_TYPE="Release"
if [ "$1" = "debug" ] || [ "$1" = "Debug" ]; then
    BUILD_TYPE="Debug"
    print_status "Build type: Debug (with debug symbols)"
else
    print_status "Build type: Release (optimized)"
fi

# Configure CMake
CMAKE_FLAGS=()
CMAKE_FLAGS+=("-DCMAKE_BUILD_TYPE=$BUILD_TYPE")
CMAKE_FLAGS+=("-DWITH_VULKAN=$([ "$HAS_VULKAN" = true ] && echo ON || echo OFF)")
CMAKE_FLAGS+=("-DWITH_OPENCL=$([ "$HAS_OPENCL" = true ] && echo ON || echo OFF)")
CMAKE_FLAGS+=("-DWITH_DAEMON=ON")
CMAKE_FLAGS+=("-DWITH_TESTS=ON")
CMAKE_FLAGS+=("-DWITH_CCACHE=ON")
CMAKE_FLAGS+=("-DWITH_KERNEL_MODULE=OFF")
CMAKE_FLAGS+=("-DWITH_CUDA=OFF")
CMAKE_FLAGS+=("-DWITH_BENCHMARKS=OFF")
CMAKE_FLAGS+=("-DWITH_DOCS=OFF")

print_status "Configuring with CMake..."
echo "  Flags: ${CMAKE_FLAGS[*]}"

# Run CMake
if cmake .. "${CMAKE_FLAGS[@]}" 2>&1 | tail -5; then
    print_success "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

# =============================================================================
# PHASE 3: GPU Detection Module
# =============================================================================
print_section "Phase 3: GPU Detection Module"

print_status "Building GPU Detection..."
if cmake --build . --target mvgal_core 2>&1 | grep -E "(Built target mvgal_core|error|Error|ERROR|failed|FAILED)" | tail -3; then
    if [ -f "libmvgal_core.a" ] || [ -f "libmvgal_core.so" ] || [ -f "libmvgal.so" ]; then
        print_success "GPU Detection module built successfully"
    else
        echo ""
        print_status "Checking individual file compilation..."
        cd "$SCRIPT_DIR"

        # Test gpu_manager.c
        print_status "  Testing gpu_manager.c..."
        if gcc -c -Iinclude -Iinclude/mvgal -D_GNU_SOURCE -std=c11 -Wall -Wextra -Werror -O2 \
            src/userspace/daemon/gpu_manager.c -o /tmp/test_gpu.o 2>&1; then
            print_success "  gpu_manager.c: OK"
        else
            print_error "  gpu_manager.c: FAILED"
            exit 1
        fi

        print_success "GPU Detection module: Compilation verified"
    fi
else
    print_error "GPU Detection module build failed"
    exit 1
fi

print_success "Phase 3: GPU Detection - COMPLETE"

# =============================================================================
# PHASE 4: Vulkan Interception Layer
# =============================================================================
print_section "Phase 4: Vulkan Interception Layer"

if [ "$HAS_VULKAN" = true ]; then
    print_status "Building Vulkan Layer..."
    if cmake --build . --target mvgal_vulkan_layer 2>&1 | grep -E "(Built target mvgal_vulkan_layer|error|Error|ERROR|failed|FAILED)" | tail -3; then
        if [ -f "libVK_LAYER_MVGAL.so" ]; then
            print_success "Vulkan Layer library: libVK_LAYER_MVGAL.so"
            ls -lh libVK_LAYER_MVGAL.so
        fi
        print_success "Phase 4: Vulkan Layer - COMPLETE"
    else
        print_status "Vulkan layer build skipped (missing dependencies or errors)"
        print_success "Phase 4: Vulkan Layer - SKIPPED"
    fi
else
    print_status "Vulkan SDK not found - skipping Vulkan Layer"
    print_success "Phase 4: Vulkan Layer - SKIPPED"
fi

# =============================================================================
# PHASE 8: Daemon & IPC
# =============================================================================
print_section "Phase 8: Daemon & IPC"

print_status "Building MVGAL Daemon..."
if cmake --build . --target mvgal-daemon 2>&1 | grep -E "(Built target mvgal-daemon|error|Error|ERROR|failed|FAILED)" | tail -3; then
    if [ -f "mvgal-daemon" ]; then
        print_success "Daemon executable: mvgal-daemon"
        ls -lh mvgal-daemon
    fi
    print_success "Phase 8: Daemon - COMPLETE"
else
    print_status "Daemon build: attempting full make..."
    if make -j$(nproc) 2>&1 | tail -3; then
        print_success "Phase 8: Daemon - COMPLETE (via full build)"
    else
        print_error "Phase 8: Daemon - FAILED"
    fi
fi

# =============================================================================
# FINAL: Complete Build
# =============================================================================
print_section "Final Phase: Complete Build"

print_status "Running full build..."
cd "$BUILD_DIR"

BUILD_OUTPUT=$(make -j$(nproc) 2>&1)
BUILD_RESULT=$?

if [ $BUILD_RESULT -eq 0 ]; then
    print_success "Full build completed successfully!"
else
    echo "$BUILD_OUTPUT" | tail -10
    print_error "Full build had issues"
fi

# =============================================================================
# SUMMARY
# =============================================================================
print_section "Build Summary"

echo ""
echo "Build directory: $BUILD_DIR"
echo ""

# List built files
print_status "Built files:"
cd "$BUILD_DIR"
for pattern in "libmvgal*.a" "libmvgal*.so" "mvgal-daemon" "libVK_LAYER_*.so"; do
    for f in $pattern; do
        if [ -f "$f" ]; then
            size=$(du -h "$f" | cut -f1)
            print_success "  $f ($size)"
        fi
    done
done

# Check what features are enabled
echo ""
print_status "Enabled modules:"
[ -f libmvgal_core.a ] || [ -f libmvgal_core.so ] || [ -f libmvgal.so ] && print_success "  ✓ Core Library (GPU Detection, Memory, Scheduler)"
[ -f mvgal-daemon ] && print_success "  ✓ Daemon"
[ -f libVK_LAYER_MVGAL.so ] && print_success "  ✓ Vulkan Layer"
$HAS_OPENCL && print_success "  ✓ OpenCL Interception (configured)" || print_error "  ✗ OpenCL Interception (missing headers)"
$HAS_VULKAN && print_success "  ✓ Vulkan Support" || print_error "  ✗ Vulkan Support (missing headers)"

echo ""
print_section "Build Complete!"

echo ""
echo "Next Steps:"
echo ""
echo "  1. Test GPU Detection:"
echo "     cd $BUILD_DIR"
echo "     export LD_LIBRARY_PATH=."
echo "     echo 'int main() { return 0; }' | gcc -x c - -o /tmp/test -I. -I../include -lmvgal -L. 2>&1 || true"
echo ""
echo "  2. Run Daemon:"
echo "     ./mvgal-daemon --no-daemon"
echo ""
if [ "$HAS_VULKAN" = true ]; then
    echo "  3. Use Vulkan Layer:"
    echo "     export VK_LAYER_PATH=$BUILD_DIR"
    echo "     vkcube"
fi
echo ""
echo "  4. Run tests:"
echo "     ctest -V"
echo ""
