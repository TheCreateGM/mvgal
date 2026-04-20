#!/bin/bash
# MVGAL Kernel Module Unloader
# Unloads the MVGAL kernel module

MODULE_NAME="mvgal"

# Check if running as root
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: Must be root to unload kernel modules"
    exit 1
fi

# Check if module is loaded
if ! lsmod | grep -q "^${MODULE_NAME} "; then
    echo "Module ${MODULE_NAME} is not currently loaded"
    exit 0
fi

echo "Unloading MVGAL kernel module..."

# First, close any open file descriptors to the device
# This helps prevent "Device or resource busy" errors
if [ -e "/dev/mvgal0" ]; then
    echo "Closing open file descriptors for /dev/mvgal0..."
    lsof /dev/mvgal0 2>/dev/null | awk 'NR>1 {print $2}' | xargs -r kill -9 2>/dev/null
    sleep 1
fi

# Try to unload the module
if rmmod "${MODULE_NAME}" 2>/dev/null; then
    echo "Module ${MODULE_NAME} unloaded successfully"

    # Verify
    if lsmod | grep -q "^${MODULE_NAME} "; then
        echo "Error: Module still appears to be loaded"
        lsmod | grep "^${MODULE_NAME} "
        exit 1
    else
        echo "Verification: Module is unloaded"
    fi

    exit 0
else
    # Try force unload
    echo "Normal unload failed, trying force unload..."
    rmmod -f "${MODULE_NAME}" 2>/dev/null

    if [ $? -eq 0 ]; then
        echo "Module ${MODULE_NAME} force unloaded successfully"
        exit 0
    else
        echo "Error: Failed to unload module"
        echo "This may be due to the module being in use."
        echo ""
        echo "Attempting to identify what's using the module..."

        # Check for processes using the module
        lsof /dev/mvgal* 2>/dev/null

        # Check kernel module usage
        lsmod | grep mvgal

        # Check dmesg
        dmesg | tail -20

        exit 1
    fi
fi
