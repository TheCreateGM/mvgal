#!/bin/bash
# MVGAL Kernel Module Loader
# Loads the MVGAL kernel module

MODULE_NAME="mvgal"
MODULE_PATH="/lib/modules/$(uname -r)/kernel/drivers/gpu/${MODULE_NAME}.ko"

# Check if running as root
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: Must be root to load kernel modules"
    exit 1
fi

# Check if module already loaded
if lsmod | grep -q "^${MODULE_NAME} "; then
    echo "Module ${MODULE_NAME} is already loaded"
    exit 0
fi

# Try to find the module
if [ -f "${MODULE_PATH}" ]; then
    MODULE_FILE="${MODULE_PATH}"
elif [ -f "/usr/lib/modules/$(uname -r)/kernel/drivers/gpu/${MODULE_NAME}.ko" ]; then
    MODULE_FILE="/usr/lib/modules/$(uname -r)/kernel/drivers/gpu/${MODULE_NAME}.ko"
elif [ -f "/usr/local/lib/modules/$(uname -r)/kernel/drivers/gpu/${MODULE_NAME}.ko" ]; then
    MODULE_FILE="/usr/local/lib/modules/$(uname -r)/kernel/drivers/gpu/${MODULE_NAME}.ko"
else
    # Search in current directory and common paths
    MODULE_FILE=$(find /lib/modules -name "${MODULE_NAME}.ko" 2>/dev/null | head -1)
    if [ -z "$MODULE_FILE" ]; then
        MODULE_FILE=$(find . -name "${MODULE_NAME}.ko" -type f 2>/dev/null | head -1)
    fi
fi

if [ -z "$MODULE_FILE" ] || [ ! -f "$MODULE_FILE" ]; then
    echo "Error: Could not find ${MODULE_NAME}.ko"
    echo "Tried:"
    echo "  ${MODULE_PATH}"
    echo "  /usr/lib/modules/$(uname -r)/kernel/drivers/gpu/"
    echo "  /usr/local/lib/modules/$(uname -r)/kernel/drivers/gpu/"
    exit 1
fi

echo "Loading MVGAL kernel module from: ${MODULE_FILE}"

# Load the module
if insmod "${MODULE_FILE}"; then
    echo "Module ${MODULE_NAME} loaded successfully"

    # Verify
    if lsmod | grep -q "^${MODULE_NAME} "; then
        echo "Verification: Module is loaded"
        lsmod | grep "^${MODULE_NAME} "

        # Check for device nodes
        if [ -e "/dev/mvgal0" ]; then
            echo "Device node /dev/mvgal0 exists"
            ls -la /dev/mvgal0
        else
            echo "Warning: Device node /dev/mvgal0 not found"
        fi
    else
        echo "Error: Module load reported success but not found in lsmod"
        exit 1
    fi

    exit 0
else
    echo "Error: Failed to load module"
    dmesg | tail -20
    exit 1
fi
