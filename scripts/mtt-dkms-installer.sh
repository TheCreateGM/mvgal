#!/bin/bash
# MVGAL Moore Threads (MTT) DKMS Installer
#
# Handles installation of proprietary Moore Threads GPU drivers
# through DKMS (Dynamic Kernel Module Support)
#
# Features:
# - Automatic driver download from MTT repositories
# - DKMS module building and installation
# - Version management and updates
# - Dependency checking
# - pkexec integration for privilege escalation
#
# Copyright (C) 2026 MVGAL Project
# SPDX-License-Identifier: MIT

set -e

# Configuration
MTT_REPO_URL="https://github.com/dixyes/mtgpu-drv"
MTT_VERSION="2.1.0"
MTT_DRIVER_NAME="mtgpu"
DKMS_MODULE_NAME="mtgpu"
INSTALL_DIR="/usr/src"
TEMP_DIR="/tmp/mvgal-mtt-install"
LOG_FILE="/var/log/mvgal-mtt-install.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a "$LOG_FILE"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a "$LOG_FILE"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$LOG_FILE"
}

log_debug() {
    if [ "${DEBUG:-0}" -eq 1 ]; then
        echo "[DEBUG] $1" >> "$LOG_FILE"
    fi
}

# Error handler
error_exit() {
    log_error "$1"
    cleanup
    exit 1
}

# Cleanup function
cleanup() {
    if [ -d "$TEMP_DIR" ]; then
        log_debug "Cleaning up temporary directory: $TEMP_DIR"
        rm -rf "$TEMP_DIR"
    fi
}

# Check if running with elevated privileges
check_privileges() {
    if [ "$(id -u)" -ne 0 ]; then
        # Try pkexec
        if command -v pkexec >/dev/null 2>&1; then
            log_info "Requesting elevated privileges via pkexec..."
            exec pkexec "$0" "$@"
        else
            error_exit "This installer requires root privileges. Please run with sudo or install pkexec."
        fi
    fi
}

# Check system requirements
check_requirements() {
    log_info "Checking system requirements..."
    
    # Check for DKMS
    if ! command -v dkms >/dev/null 2>&1; then
        error_exit "DKMS is not installed. Please install dkms package."
    fi
    
    # Check for git
    if ! command -v git >/dev/null 2>&1; then
        log_warn "Git not found. Will attempt to use curl/wget for download."
    fi
    
    # Check for build tools
    if ! command -v make >/dev/null 2>&1; then
        error_exit "Make is not installed. Please install build-essential or equivalent."
    fi
    
    # Check kernel headers
    KERNEL_VERSION=$(uname -r)
    if [ ! -d "/lib/modules/${KERNEL_VERSION}/build" ]; then
        error_exit "Kernel headers for ${KERNEL_VERSION} not found. Please install kernel headers."
    fi
    
    # Check for PCI devices
    MTT_DEVICES=$(lspci -nn | grep -i "1ed5:" | wc -l)
    if [ "$MTT_DEVICES" -eq 0 ]; then
        log_warn "No Moore Threads GPU devices detected via lspci."
        log_warn "Installation will proceed, but driver may not be functional."
    else
        log_info "Detected $MTT_DEVICES Moore Threads GPU(s)"
    fi
    
    log_info "System requirements satisfied"
}

# Download MTT driver source
download_driver() {
    log_info "Downloading Moore Threads driver source..."
    
    mkdir -p "$TEMP_DIR"
    cd "$TEMP_DIR"
    
    # Try git clone first
    if command -v git >/dev/null 2>&1; then
        log_info "Cloning from $MTT_REPO_URL..."
        if git clone --depth 1 --branch "v${MTT_VERSION}" "$MTT_REPO_URL" "${MTT_DRIVER_NAME}-${MTT_VERSION}"; then
            log_info "Successfully cloned driver source"
            return 0
        fi
    fi
    
    # Fallback to curl/wget for tarball
    local tarball_url="${MTT_REPO_URL}/archive/refs/tags/v${MTT_VERSION}.tar.gz"
    log_info "Attempting to download tarball from ${tarball_url}..."
    
    if command -v curl >/dev/null 2>&1; then
        if curl -L -o "mtgpu-${MTT_VERSION}.tar.gz" "$tarball_url"; then
            tar -xzf "mtgpu-${MTT_VERSION}.tar.gz"
            mv "mtgpu-drv-${MTT_VERSION}" "${MTT_DRIVER_NAME}-${MTT_VERSION}"
            log_info "Successfully downloaded and extracted driver"
            return 0
        fi
    elif command -v wget >/dev/null 2>&1; then
        if wget -O "mtgpu-${MTT_VERSION}.tar.gz" "$tarball_url"; then
            tar -xzf "mtgpu-${MTT_VERSION}.tar.gz"
            mv "mtgpu-drv-${MTT_VERSION}" "${MTT_DRIVER_NAME}-${MTT_VERSION}"
            log_info "Successfully downloaded and extracted driver"
            return 0
        fi
    fi
    
    error_exit "Failed to download Moore Threads driver source"
}

# Prepare DKMS structure
prepare_dkms() {
    log_info "Preparing DKMS module structure..."
    
    local src_dir="${TEMP_DIR}/${MTT_DRIVER_NAME}-${MTT_VERSION}"
    local dkms_dir="${INSTALL_DIR}/${MTT_DRIVER_NAME}-${MTT_VERSION}"
    
    # Remove existing installation
    if [ -d "$dkms_dir" ]; then
        log_info "Removing existing DKMS source..."
        rm -rf "$dkms_dir"
    fi
    
    # Copy source to DKMS location
    cp -r "$src_dir" "$dkms_dir"
    
    # Create or update dkms.conf
    cat > "${dkms_dir}/dkms.conf" << 'EOF'
PACKAGE_NAME="mtgpu"
PACKAGE_VERSION="@VERSION@"
CLEAN="make clean"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build modules"
BUILT_MODULE_NAME[0]="mtgpu"
DEST_MODULE_LOCATION[0]="/kernel/drivers/gpu/mtt"
AUTOINSTALL="yes"
REMAKE_INITRD="yes"
EOF
    
    # Replace version placeholder
    sed -i "s/@VERSION@/${MTT_VERSION}/" "${dkms_dir}/dkms.conf"
    
    log_info "DKMS structure prepared"
}

# Build and install via DKMS
install_dkms() {
    log_info "Building and installing DKMS module..."
    
    local module_name="${MTT_DRIVER_NAME}"
    local module_version="${MTT_VERSION}"
    
    # Remove any existing DKMS installation
    if dkms status "$module_name" | grep -q "$module_version"; then
        log_info "Removing existing DKMS module..."
        dkms remove "$module_name/$module_version" --all 2>/dev/null || true
    fi
    
    # Add to DKMS
    log_info "Adding module to DKMS..."
    if ! dkms add -m "$module_name" -v "$module_version"; then
        error_exit "Failed to add module to DKMS"
    fi
    
    # Build module
    log_info "Building kernel module..."
    if ! dkms build -m "$module_name" -v "$module_version"; then
        error_exit "Failed to build kernel module. Check ${LOG_FILE} for details."
    fi
    
    # Install module
    log_info "Installing kernel module..."
    if ! dkms install -m "$module_name" -v "$module_version"; then
        error_exit "Failed to install kernel module"
    fi
    
    log_info "DKMS module installed successfully"
}

# Load the module
load_module() {
    log_info "Loading Moore Threads kernel module..."
    
    # Check if already loaded
    if lsmod | grep -q "^mtgpu "; then
        log_warn "mtgpu module is already loaded"
        return 0
    fi
    
    # Load the module
    if modprobe mtgpu; then
        log_info "Module loaded successfully"
        
        # Verify device nodes
        if ls /dev/mtgpu* 1>/dev/null 2>&1; then
            log_info "Device nodes created:"
            ls -la /dev/mtgpu* | while read line; do
                log_info "  $line"
            done
        fi
        
        if ls /dev/dri/card* 1>/dev/null 2>&1; then
            log_info "DRM device nodes:"
            ls -la /dev/dri/card* 2>/dev/null | while read line; do
                log_info "  $line"
            done
        fi
    else
        error_exit "Failed to load mtgpu module. Check dmesg for errors."
    fi
}

# Create udev rules
create_udev_rules() {
    log_info "Creating udev rules for Moore Threads GPU..."
    
    local rules_file="/etc/udev/rules.d/99-mvgal-mtt.rules"
    
    cat > "$rules_file" << 'EOF'
# MVGAL Moore Threads GPU udev rules
# PCI vendor ID for Moore Threads: 0x1ED5

# Set permissions for MTT device nodes
SUBSYSTEM=="misc", KERNEL=="mtgpu*", MODE="0666", GROUP="video"
SUBSYSTEM=="drm", ATTR{vendor}=="0x1ed5", MODE="0666", GROUP="video"

# Create symlinks for easier access
SUBSYSTEM=="misc", KERNEL=="mtgpu0", SYMLINK+="mtgpu/primary"
SUBSYSTEM=="misc", KERNEL=="mtgpu1", SYMLINK+="mtgpu/secondary"

# Power management rules
SUBSYSTEM=="pci", ATTR{vendor}=="0x1ed5", ATTR{power/control}="auto"

# Tag for systemd device management
SUBSYSTEM=="pci", ATTR{vendor}=="0x1ed5", TAG+="systemd"
EOF
    
    chmod 644 "$rules_file"
    
    # Reload udev rules
    udevadm control --reload-rules
    udevadm trigger
    
    log_info "udev rules created and loaded"
}

# Create modprobe configuration
create_modprobe_config() {
    log_info "Creating modprobe configuration..."
    
    local modprobe_file="/etc/modprobe.d/mvgal-mtt.conf"
    
    cat > "$modprobe_file" << 'EOF'
# MVGAL Moore Threads GPU module options
# Load mtgpu module at boot
options mtgpu enable_guc=1
options mtgpu enable_huc=1

# Enable runtime power management
options mtgpu enable_rc6=1

# Set maximum power saving
options mtgpu modeset=1
EOF
    
    chmod 644 "$modprobe_file"
    
    # Update initramfs
    if command -v update-initramfs >/dev/null 2>&1; then
        update-initramfs -u
    elif command -v dracut >/dev/null 2>&1; then
        dracut -f
    elif command -v mkinitcpio >/dev/null 2>&1; then
        mkinitcpio -P
    fi
    
    log_info "Modprobe configuration created"
}

# Update MVGAL configuration
update_mvgal_config() {
    log_info "Updating MVGAL configuration for Moore Threads support..."
    
    local config_file="/etc/mvgal/mvgal.conf"
    
    # Backup existing config
    if [ -f "$config_file" ]; then
        cp "$config_file" "${config_file}.backup.$(date +%Y%m%d%H%M%S)"
    fi
    
    # Add MTT section if not exists
    if ! grep -q "\[mtt\]" "$config_file" 2>/dev/null; then
        cat >> "$config_file" << 'EOF'

[mtt]
enabled = true
# Moore Threads MUSA platform settings
musa_path = /opt/musa
# Enable hardware video encoding
video_encode = true
video_decode = true
# Enable compute API
opencl = true
EOF
        log_info "Added MTT configuration section"
    fi
    
    log_info "MVGAL configuration updated"
}

# Verify installation
verify_installation() {
    log_info "Verifying installation..."
    
    # Check DKMS status
    if dkms status "$MTT_DRIVER_NAME" | grep -q "${MTT_VERSION}"; then
        log_info "DKMS module status: OK"
    else
        log_warn "DKMS module may not be properly installed"
    fi
    
    # Check loaded module
    if lsmod | grep -q "^mtgpu "; then
        log_info "Kernel module: LOADED"
        
        # Get module info
        local modinfo_output
        modinfo_output=$(modinfo mtgpu 2>/dev/null | grep -E "(version|author|description)" || true)
        if [ -n "$modinfo_output" ]; then
            log_info "Module information:"
            echo "$modinfo_output" | while read line; do
                log_info "  $line"
            done
        fi
    else
        log_warn "Kernel module is not loaded"
    fi
    
    # Check device nodes
    if ls /dev/mtgpu* 1>/dev/null 2>&1; then
        log_info "Device nodes: PRESENT"
    else
        log_warn "Device nodes not found"
    fi
    
    log_info "Installation verification complete"
}

# Print usage
print_usage() {
    cat << EOF
Usage: $0 [OPTIONS] COMMAND

MVGAL Moore Threads (MTT) DKMS Installer

Commands:
    install         Download, build and install the MTT driver
    uninstall       Remove the MTT driver
    update          Update to the latest version
    verify          Verify installation status
    load            Load the kernel module
    unload          Unload the kernel module

Options:
    -v VERSION      Specify driver version (default: ${MTT_VERSION})
    --debug         Enable debug logging
    -h, --help      Show this help message

Examples:
    $0 install                    # Install MTT driver
    $0 install -v 2.1.1          # Install specific version
    $0 update                   # Update to latest version
    $0 uninstall                # Remove driver

EOF
}

# Uninstall the driver
uninstall_driver() {
    log_info "Uninstalling Moore Threads driver..."
    
    # Unload module
    if lsmod | grep -q "^mtgpu "; then
        log_info "Unloading kernel module..."
        modprobe -r mtgpu || rmmod mtgpu || true
    fi
    
    # Remove from DKMS
    if dkms status "$MTT_DRIVER_NAME" | grep -q "${MTT_VERSION}"; then
        log_info "Removing from DKMS..."
        dkms remove "$MTT_DRIVER_NAME/${MTT_VERSION}" --all 2>/dev/null || true
    fi
    
    # Remove source directory
    local dkms_dir="${INSTALL_DIR}/${MTT_DRIVER_NAME}-${MTT_VERSION}"
    if [ -d "$dkms_dir" ]; then
        log_info "Removing source directory..."
        rm -rf "$dkms_dir"
    fi
    
    # Remove configuration files
    rm -f /etc/udev/rules.d/99-mvgal-mtt.rules
    rm -f /etc/modprobe.d/mvgal-mtt.conf
    
    log_info "Uninstall complete"
}

# Main function
main() {
    # Check arguments
    if [ $# -eq 0 ]; then
        print_usage
        exit 1
    fi
    
    local command="$1"
    shift
    
    # Parse options
    while [ $# -gt 0 ]; do
        case "$1" in
            -v)
                MTT_VERSION="$2"
                shift 2
                ;;
            --debug)
                DEBUG=1
                shift
                ;;
            -h|--help)
                print_usage
                exit 0
                ;;
            *)
                log_warn "Unknown option: $1"
                shift
                ;;
        esac
    done
    
    # Initialize log
    mkdir -p "$(dirname "$LOG_FILE")"
    echo "=== MVGAL MTT Installer $(date) ===" >> "$LOG_FILE"
    
    case "$command" in
        install)
            check_privileges
            check_requirements
            download_driver
            prepare_dkms
            install_dkms
            load_module
            create_udev_rules
            create_modprobe_config
            update_mvgal_config
            verify_installation
            cleanup
            log_info "Installation complete!"
            ;;
        
        uninstall)
            check_privileges
            uninstall_driver
            cleanup
            log_info "Uninstall complete!"
            ;;
        
        update)
            check_privileges
            log_info "Updating Moore Threads driver..."
            uninstall_driver
            download_driver
            prepare_dkms
            install_dkms
            load_module
            verify_installation
            cleanup
            log_info "Update complete!"
            ;;
        
        verify)
            verify_installation
            ;;
        
        load)
            check_privileges
            load_module
            ;;
        
        unload)
            check_privileges
            if lsmod | grep -q "^mtgpu "; then
                modprobe -r mtgpu || rmmod mtgpu
                log_info "Module unloaded"
            else
                log_warn "Module is not loaded"
            fi
            ;;
        
        *)
            log_error "Unknown command: $command"
            print_usage
            exit 1
            ;;
    esac
}

# Run main function
trap cleanup EXIT
main "$@"
