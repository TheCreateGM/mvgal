#!/bin/bash
# MVGAL PolicyKit Execution Helper
# 
# This script provides a unified interface for all privileged MVGAL operations.
# It is called via pkexec to ensure secure privilege escalation.
#
# Usage: pkexec /usr/lib/mvgal/mvgal-pkexec-helper.sh <action> [args...]
#
# Actions:
#   load-module       - Load the MVGAL kernel module
#   unload-module     - Unload the MVGAL kernel module
#   enable-gpu N      - Enable GPU at index N
#   disable-gpu N     - Disable GPU at index N
#   set-power GPU STATE - Set GPU power state (auto, on, off)
#   rescan-gpus       - Rescan for GPU changes
#   reload-config     - Reload MVGAL configuration
#   install-vulkan-layer - Install Vulkan implicit layer
#   remove-vulkan-layer  - Remove Vulkan implicit layer
#   install-mtt-driver   - Run the Moore Threads DKMS installer
#
# Copyright (C) 2026 MVGAL Project
# SPDX-License-Identifier: MIT

set -e

ACTION="${1:-}"
LOG_FILE="/var/log/mvgal-pkexec.log"
MVGAL_CONFIG="/etc/mvgal/mvgal.conf"

# Logging function
log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') [$$] $*" >> "$LOG_FILE"
}

# Error handler
error_exit() {
    log "ERROR: $1"
    echo "Error: $1" >&2
    exit 1
}

# Check if running with elevated privileges
check_privileges() {
    if [ "$(id -u)" -ne 0 ]; then
        error_exit "This operation requires root privileges"
    fi
}

# Load kernel module
load_module() {
    log "Loading MVGAL kernel module"
    
    if lsmod | grep -q "^mvgal "; then
        log "Module already loaded"
        echo "MVGAL kernel module is already loaded"
        return 0
    fi
    
    # Try to load the module
    if modprobe mvgal 2>/dev/null; then
        log "Module loaded successfully"
        
        # Verify device node was created
        if [ -e "/dev/mvgal0" ]; then
            log "Device node /dev/mvgal0 created"
            chmod 666 /dev/mvgal0 2>/dev/null || true
            echo "MVGAL kernel module loaded successfully"
        else
            log "Warning: Device node not found, waiting for udev..."
            sleep 1
            if [ -e "/dev/mvgal0" ]; then
                chmod 666 /dev/mvgal0 2>/dev/null || true
                echo "MVGAL kernel module loaded successfully"
            else
                error_exit "Device node /dev/mvgal0 was not created"
            fi
        fi
    else
        # Try manual insmod as fallback
        MODULE_PATH="/lib/modules/$(uname -r)/kernel/drivers/gpu/mvgal.ko"
        if [ -f "$MODULE_PATH" ]; then
            if insmod "$MODULE_PATH" 2>/dev/null; then
                log "Module loaded via insmod"
                chmod 666 /dev/mvgal0 2>/dev/null || true
                echo "MVGAL kernel module loaded successfully"
            else
                error_exit "Failed to load kernel module. Check dmesg for details."
            fi
        else
            error_exit "Kernel module not found at $MODULE_PATH"
        fi
    fi
}

# Unload kernel module
unload_module() {
    log "Unloading MVGAL kernel module"
    
    if ! lsmod | grep -q "^mvgal "; then
        log "Module not loaded"
        echo "MVGAL kernel module is not loaded"
        return 0
    fi
    
    # Stop daemon first if running
    if systemctl is-active --quiet mvgald 2>/dev/null; then
        log "Stopping mvgald daemon"
        systemctl stop mvgald || true
    fi
    
    # Unload the module
    if modprobe -r mvgal 2>/dev/null; then
        log "Module unloaded successfully"
        echo "MVGAL kernel module unloaded successfully"
    else
        # Try rmmod as fallback
        if rmmod mvgal 2>/dev/null; then
            log "Module unloaded via rmmod"
            echo "MVGAL kernel module unloaded successfully"
        else
            error_exit "Failed to unload kernel module. Module may be in use."
        fi
    fi
}

# Enable GPU
enable_gpu() {
    local gpu_index="${1:-}"
    if [ -z "$gpu_index" ]; then
        error_exit "GPU index required"
    fi
    
    log "Enabling GPU $gpu_index"
    
    # Write to sysfs or use ioctl
    if [ -e "/sys/class/mvgal/mvgal0/gpu${gpu_index}/enabled" ]; then
        echo 1 > "/sys/class/mvgal/mvgal0/gpu${gpu_index}/enabled"
        log "GPU $gpu_index enabled via sysfs"
    fi
    
    # Update config
    if [ -f "$MVGAL_CONFIG" ]; then
        sed -i "s/^enabled.*=.*false/enabled = true/" "$MVGAL_CONFIG" 2>/dev/null || true
    fi
    
    echo "GPU $gpu_index enabled in MVGAL"
}

# Disable GPU
disable_gpu() {
    local gpu_index="${1:-}"
    if [ -z "$gpu_index" ]; then
        error_exit "GPU index required"
    fi
    
    log "Disabling GPU $gpu_index"
    
    # Write to sysfs
    if [ -e "/sys/class/mvgal/mvgal0/gpu${gpu_index}/enabled" ]; then
        echo 0 > "/sys/class/mvgal/mvgal0/gpu${gpu_index}/enabled"
        log "GPU $gpu_index disabled via sysfs"
    fi
    
    echo "GPU $gpu_index disabled in MVGAL"
}

# Set GPU power state
set_power_state() {
    local gpu_index="${1:-}"
    local state="${2:-}"
    
    if [ -z "$gpu_index" ] || [ -z "$state" ]; then
        error_exit "Usage: set-power <gpu_index> <state (auto|on|off)>"
    fi
    
    log "Setting GPU $gpu_index power state to $state"
    
    # Validate state
    case "$state" in
        auto|on|off)
            ;;
        *)
            error_exit "Invalid power state: $state (must be auto, on, or off)"
            ;;
    esac
    
    # Find the GPU PCI device
    local pci_path="/sys/class/mvgal/mvgal0/gpu${gpu_index}/pci_path"
    if [ -f "$pci_path" ]; then
        local pci_addr
        pci_addr=$(cat "$pci_path")
        local power_control="/sys/bus/pci/devices/${pci_addr}/power/control"
        
        if [ -f "$power_control" ]; then
            echo "$state" > "$power_control"
            log "Power state set via PCI power control"
        fi
    fi
    
    # Also set via sysfs
    if [ -e "/sys/class/mvgal/mvgal0/gpu${gpu_index}/power_state" ]; then
        echo "$state" > "/sys/class/mvgal/mvgal0/gpu${gpu_index}/power_state"
    fi
    
    echo "GPU $gpu_index power state set to $state"
}

# Rescan GPUs
rescan_gpus() {
    log "Rescanning for GPU changes"
    
    if [ -e "/sys/class/mvgal/mvgal0/rescan" ]; then
        echo 1 > "/sys/class/mvgal/mvgal0/rescan"
        log "Rescan triggered via sysfs"
        echo "GPU rescan initiated"
    else
        error_exit "Rescan interface not available"
    fi
}

# Reload configuration
reload_config() {
    log "Reloading MVGAL configuration"
    
    # Signal daemon if running
    if systemctl is-active --quiet mvgald 2>/dev/null; then
        systemctl kill -s HUP mvgald || true
        log "Sent SIGHUP to mvgald"
    fi
    
    echo "Configuration reload triggered"
}

# Install Vulkan layer
install_vulkan_layer() {
    log "Installing MVGAL Vulkan implicit layer"
    
    local layer_src="/usr/share/mvgal/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json"
    local layer_dst="/etc/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json"
    
    if [ -f "$layer_src" ]; then
        mkdir -p "$(dirname "$layer_dst")"
        cp "$layer_src" "$layer_dst"
        chmod 644 "$layer_dst"
        log "Vulkan layer installed to $layer_dst"
        echo "Vulkan implicit layer installed"
    else
        error_exit "Vulkan layer source not found: $layer_src"
    fi
}

# Remove Vulkan layer
remove_vulkan_layer() {
    log "Removing MVGAL Vulkan implicit layer"
    
    local layer_file="/etc/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json"
    
    if [ -f "$layer_file" ]; then
        rm -f "$layer_file"
        log "Vulkan layer removed"
        echo "Vulkan implicit layer removed"
    else
        log "Vulkan layer not installed"
        echo "Vulkan implicit layer was not installed"
    fi
}

# Install Moore Threads DKMS driver
install_mtt_driver() {
    log "Starting Moore Threads DKMS installer"

    local installer="/usr/share/mvgal/scripts/mtt-dkms-installer.sh"
    if [ ! -x "$installer" ]; then
        error_exit "MTT installer not found or not executable: $installer"
    fi

    "$installer" install
}

# Main entry point
main() {
    check_privileges
    
    log "Action requested: $ACTION"
    
    case "$ACTION" in
        load-module)
            load_module
            ;;
        unload-module)
            unload_module
            ;;
        enable-gpu)
            enable_gpu "$2"
            ;;
        disable-gpu)
            disable_gpu "$2"
            ;;
        set-power)
            set_power_state "$2" "$3"
            ;;
        rescan-gpus)
            rescan_gpus
            ;;
        reload-config)
            reload_config
            ;;
        install-vulkan-layer)
            install_vulkan_layer
            ;;
        remove-vulkan-layer)
            remove_vulkan_layer
            ;;
        install-mtt-driver)
            install_mtt_driver
            ;;
        *)
            echo "Usage: $0 <action> [args...]"
            echo ""
            echo "Actions:"
            echo "  load-module              Load the MVGAL kernel module"
            echo "  unload-module            Unload the MVGAL kernel module"
            echo "  enable-gpu N             Enable GPU at index N"
            echo "  disable-gpu N            Disable GPU at index N"
            echo "  set-power GPU STATE      Set GPU power state (auto|on|off)"
            echo "  rescan-gpus              Rescan for GPU changes"
            echo "  reload-config            Reload MVGAL configuration"
            echo "  install-vulkan-layer     Install Vulkan implicit layer"
            echo "  remove-vulkan-layer      Remove Vulkan implicit layer"
            echo "  install-mtt-driver       Install Moore Threads DKMS driver"
            exit 1
            ;;
    esac
}

main "$@"
