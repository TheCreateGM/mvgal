#!/bin/bash
# MVGAL Auto-Install Dependencies Script
# This script automatically detects the distribution and installs all required dependencies
# All privileged operations use pkexec (never sudo).

set -e

echo "=========================================="
echo "MVGAL Dependencies Auto-Installer"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "INFO: Will use pkexec for privileged package installation."
    echo ""
    NEED_PRIV=true
else
    NEED_PRIV=false
fi

# Detect distribution
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
    VERSION=$VERSION_ID
elif type lsb_release >/dev/null 2>&1; then
    DISTRO=$(lsb_release -si | tr '[:upper:]' '[:lower:]')
    VERSION=$(lsb_release -sr)
elif [ -f /etc/lsb-release ]; then
    . /etc/lsb-release
    DISTRO=$DISTRIB_ID
    VERSION=$DISTRIB_RELEASE
else
    DISTRO=$(uname -s)
    VERSION=$(uname -r)
fi

echo "Detected: $DISTRO $VERSION"
echo ""

# Function to run command with pkexec if needed
run_cmd() {
    if [ "$NEED_PRIV" = true ] && [ "$EUID" -ne 0 ]; then
        pkexec bash -c "$*"
    else
        bash -c "$*"
    fi
}

# Function to get package name for current distro
get_pkg() {
    local pkg_type=$1

    case $DISTRO in
        ubuntu|debian|pop|linuxmint)
            case $pkg_type in
                gcc) echo "g++" ;;
                make) echo "make" ;;
                cmake) echo "cmake" ;;
                pkgconfig) echo "pkg-config" ;;
                git) echo "git" ;;
                ccache) echo "ccache" ;;
                libdrm) echo "libdrm-dev" ;;
                libpci) echo "libpci-dev" ;;
                libudev) echo "libudev-dev" ;;
                libsystemd) echo "libsystemd-dev" ;;
                vulkan) echo "libvulkan-dev vulkan-tools" ;;
                vulkan-validation) echo "vulkan-validationlayers-dev" ;;
                opencl) echo "opencl-headers ocl-icd-dev libopencl-dev" ;;
                clang) echo "clang" ;;
                llvm) echo "llvm" ;;
                *) echo "$pkg_type" ;;
            esac
            ;;
        fedora|rhel|centos|rocky|almalinux)
            case $pkg_type in
                gcc) echo "gcc-c++" ;;
                make) echo "make" ;;
                cmake) echo "cmake" ;;
                pkgconfig) echo "pkgconfig" ;;
                git) echo "git" ;;
                ccache) echo "ccache" ;;
                libdrm) echo "libdrm-devel" ;;
                libpci) echo "pciutils-devel" ;;
                libudev) echo "systemd-devel" ;;
                libsystemd) echo "systemd-devel" ;;
                vulkan) echo "vulkan-devel vulkan-tools" ;;
                vulkan-validation) echo "vulkan-validation-layers" ;;
                opencl) echo "opencl-headers ocl-icd-devel" ;;
                clang) echo "clang" ;;
                llvm) echo "llvm" ;;
                *) echo "$pkg_type" ;;
            esac
            ;;
        arch|archlinux|manjaro|endeavour)
            case $pkg_type in
                gcc) echo "" ;;
                make) echo "make" ;;
                cmake) echo "cmake" ;;
                pkgconfig) echo "pkgconf" ;;
                git) echo "git" ;;
                ccache) echo "ccache" ;;
                libdrm) echo "libdrm" ;;
                libpci) echo "libpci" ;;
                libudev) echo "systemd" ;;
                libsystemd) echo "systemd" ;;
                vulkan) echo "vulkan-devel vulkan-tools" ;;
                vulkan-validation) echo "vulkan-validation-layers glslang spirv-tools" ;;
                opencl) echo "opencl-headers ocl-icd opencl-mesa" ;;
                clang) echo "clang" ;;
                llvm) echo "llvm" ;;
                *) echo "$pkg_type" ;;
            esac
            ;;
        opensuse|suse)
            case $pkg_type in
                gcc) echo "gcc-c++" ;;
                make) echo "make" ;;
                cmake) echo "cmake" ;;
                pkgconfig) echo "pkg-config" ;;
                git) echo "git" ;;
                ccache) echo "ccache" ;;
                libdrm) echo "libdrm-devel" ;;
                libpci) echo "libpci-devel" ;;
                libudev) echo "systemd-devel" ;;
                libsystemd) echo "systemd-devel" ;;
                vulkan) echo "libvulkan-devel Vulkan-tools" ;;
                vulkan-validation) echo "vulkan-validation-layers" ;;
                opencl) echo "opencl-headers ocl-icd-devel" ;;
                clang) echo "clang" ;;
                llvm) echo "llvm" ;;
                *) echo "$pkg_type" ;;
            esac
            ;;
        alpine)
            case $pkg_type in
                gcc) echo "g++" ;;
                make) echo "make" ;;
                cmake) echo "cmake" ;;
                pkgconfig) echo "pkgconf" ;;
                git) echo "git" ;;
                ccache) echo "ccache" ;;
                libdrm) echo "libdrm-dev" ;;
                libpci) echo "pciutils-dev" ;;
                libudev) echo "libudev-dev" ;;
                libsystemd) echo "systemd-dev" ;;
                vulkan) echo "vulkan-dev vulkan-tools" ;;
                vulkan-validation) echo "" ;;
                opencl) echo "opencl-dev" ;;
                clang) echo "clang" ;;
                llvm) echo "llvm" ;;
                *) echo "$pkg_type" ;;
            esac
            ;;
        *)
            echo "$pkg_type"
            ;;
    esac
}

# Function to install packages
install_packages() {
    local pkg_type=$1
    local pkg_name=$(get_pkg "$pkg_type")

    if [ -z "$pkg_name" ]; then
        echo "  Skipping $pkg_type (not available on $DISTRO)"
        return 0
    fi

    echo -n "  Installing $pkg_type ($pkg_name)... "

    case $DISTRO in
        ubuntu|debian|pop|linuxmint)
            run_cmd "apt-get update -qq > /dev/null 2>&1"
            run_cmd "apt-get install -y $pkg_name > /dev/null 2>&1"
            ;;
        fedora|rhel|centos|rocky|almalinux)
            local cmd="dnf"
            if [ "$DISTRO" = "rhel" ] || [ "$DISTRO" = "centos" ] || [ "$DISTRO" = "rocky" ] || [ "$DISTRO" = "almalinux" ]; then
                cmd="yum"
            fi
            run_cmd "$cmd install -y $pkg_name > /dev/null 2>&1"
            ;;
        arch|archlinux|manjaro|endeavour)
            run_cmd "pacman -S --noconfirm --needed $pkg_name > /dev/null 2>&1"
            ;;
        opensuse|suse)
            run_cmd "zypper install -y $pkg_name > /dev/null 2>&1"
            ;;
        alpine)
            run_cmd "apk add --no-cache $pkg_name > /dev/null 2>&1"
            ;;
        *)
            echo "Unsupported distribution: $DISTRO"
            return 1
            ;;
    esac

    echo "OK"
}

# Function to check if package is installed
check_installed() {
    local pkg_name=$1

    if [ -z "$pkg_name" ]; then
        return 1
    fi

    case $DISTRO in
        ubuntu|debian|pop|linuxmint)
            dpkg -l "$pkg_name" > /dev/null 2>&1
            ;;
        fedora|rhel|centos|rocky|almalinux)
            rpm -q "${pkg_name%% *}" > /dev/null 2>&1 || dnf list installed "${pkg_name%% *}" > /dev/null 2>&1
            ;;
        arch|archlinux|manjaro|endeavour)
            pacman -Q "$pkg_name" > /dev/null 2>&1
            ;;
        opensuse|suse)
            rpm -q "$pkg_name" > /dev/null 2>&1
            ;;
        alpine)
            apk info -e "$pkg_name" > /dev/null 2>&1
            ;;
        *)
            command -v "$pkg_name" > /dev/null 2>&1 || return 1
            ;;
    esac
}

echo "Installing MVGAL dependencies..."
echo ""

# Install build tools
echo "[1/4] Build Tools"
for pkg in gcc make cmake pkgconfig git ccache; do
    pkg_name=$(get_pkg "$pkg")
    if [ -n "$pkg_name" ] && ! check_installed "$pkg_name"; then
        install_packages "$pkg"
    else
        echo "  $pkg: already installed"
    fi
done

# Install core development libraries
echo ""
echo "[2/4] Core Libraries"
for pkg in libdrm libpci libudev libsystemd; do
    pkg_name=$(get_pkg "$pkg")
    if [ -n "$pkg_name" ]; then
        # Check each package in the list
        pkg_installed=true
        for single_pkg in $pkg_name; do
            if ! check_installed "$single_pkg"; then
                pkg_installed=false
                break
            fi
        done
        if [ "$pkg_installed" = false ]; then
            install_packages "$pkg"
        else
            echo "  $pkg: already installed"
        fi
    else
        echo "  $pkg: skipped (not available)"
    fi
done

# Install Vulkan (optional)
echo ""
echo "[3/4] Vulkan SDK"
VULKAN_PKG=$(get_pkg vulkan)
if [ -n "$VULKAN_PKG" ]; then
    vulkan_installed=true
    for single_pkg in $VULKAN_PKG; do
        if ! check_installed "$single_pkg"; then
            vulkan_installed=false
            break
        fi
    done
    if [ "$vulkan_installed" = false ]; then
        install_packages vulkan
        install_packages vulkan-validation
    else
        echo "  Vulkan: already installed"
    fi
else
    echo "  Vulkan: not available on $DISTRO"
fi

# Install OpenCL (optional)
echo ""
echo "[4/4] OpenCL"
OPENCL_PKG=$(get_pkg opencl)
if [ -n "$OPENCL_PKG" ]; then
    opencl_installed=true
    for single_pkg in $OPENCL_PKG; do
        if ! check_installed "$single_pkg"; then
            opencl_installed=false
            break
        fi
    done
    if [ "$opencl_installed" = false ]; then
        install_packages opencl
    else
        echo "  OpenCL: already installed"
    fi
else
    echo "  OpenCL: not available on $DISTRO"
fi

echo ""
echo "=========================================="
echo "Dependencies installation complete!"
echo "=========================================="
echo ""

# Verify installations
echo "Verifying installations..."

verify_cmd() {
    if command -v $1 >/dev/null 2>&1; then
        echo "  [OK] $1"
    else
        echo "  [MISSING] $1"
    fi
}

check_header() {
    for path in /usr/include /usr/local/include /opt/vulkan/include; do
        if [ -f "$path/$1" ]; then
            echo "  [OK] $1"
            return 0
        fi
    done
    echo "  [MISSING] $1"
    return 1
}

echo ""
echo "Build tools:"
verify_cmd gcc
verify_cmd g++
verify_cmd make
verify_cmd cmake

echo ""
echo "Core libraries:"
pkg-config --exists libdrm && echo "  [OK] libdrm" || echo "  [MISSING] libdrm"
(pkg-config --exists libpci 2>/dev/null || pkg-config --exists pci 2>/dev/null || pkg-config --exists pciaccess 2>/dev/null) && echo "  [OK] libpci" || echo "  [MISSING] libpci"
pkg-config --exists libsystemd && echo "  [OK] libsystemd" || echo "  [MISSING] libsystemd"

echo ""
echo "Header files:"
check_header "drm/drm.h"
check_header "pciaccess.h"
check_header "vulkan/vulkan.h"
check_header "CL/cl.h"

echo ""
echo "Done!"
echo ""
echo "Next steps:"
echo "  1. cd mvgal"
echo "  2. mkdir -p build && cd build"
echo "  3. cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_VULKAN=ON -DWITH_OPENCL=ON"
echo "  4. make -j\$(nproc)"
echo ""
