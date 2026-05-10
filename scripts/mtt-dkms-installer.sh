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
MTT_VERSION="${MTT_VERSION:-2.1.0}"
MTT_DRIVER_NAME="mtgpu"
DKMS_MODULE_NAME="mtgpu"
INSTALL_DIR="/usr/src"
TEMP_DIR="/tmp/mvgal-mtt-install"
LOG_FILE="/var/log/mvgal-mtt-install.log"

# Loginwall/Authentication Configuration
MTT_AUTH_REQUIRED=0
MTT_AUTH_TOKEN=""
MTT_API_KEY=""
MTT_LICENSE_KEY=""
MTT_LOGINWALL_URL="https://developer.moorethreads.com/login"
MTT_DOWNLOAD_BASE="https://developer.moorethreads.com/api/v1/download"
MTT_CREDENTIALS_FILE="/etc/mvgal/mtt_credentials.conf"
MTT_SESSION_COOKIE=""

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

# ============================================================================
# Loginwall and Authentication Handling
# ============================================================================

# Check if MTT download requires authentication.
#
# Moore Threads may require:
# - API key authentication
# - login credentials (session-based)
# - license key validation
# - developer account access
check_auth_required() {
    log_info "Checking authentication requirements for Moore Threads driver..."
    
    # Try to access the download URL without authentication
    local test_url="${MTT_DOWNLOAD_BASE}/driver/${MTT_VERSION}/check"
    local http_code
    
    if command -v curl >/dev/null 2>&1; then
        http_code=$(curl -s -o /dev/null -w "%{http_code}" "$test_url" 2>/dev/null || echo "000")
    else
        # Assume auth required if curl not available (safer)
        http_code="401"
    fi
    
    case "$http_code" in
        200|204)
            MTT_AUTH_REQUIRED=0
            log_info "Public download available (HTTP $http_code)"
            ;;
        401|403)
            MTT_AUTH_REQUIRED=1
            log_warn "Authentication required (HTTP $http_code) - loginwall detected"
            ;;
        000)
            # Network error - assume auth might be required
            MTT_AUTH_REQUIRED=1
            log_warn "Cannot determine auth status - assuming loginwall"
            ;;
        *)
            MTT_AUTH_REQUIRED=1
            log_warn "Unexpected response (HTTP $http_code) - may require authentication"
            ;;
    esac
    
    return $MTT_AUTH_REQUIRED
}

# Load saved credentials if they exist
load_credentials() {
    if [ -f "$MTT_CREDENTIALS_FILE" ]; then
        log_info "Loading stored credentials..."
        # Source the credentials file (must be root-owned and mode 600)
        local file_perms
        file_perms=$(stat -c %a "$MTT_CREDENTIALS_FILE" 2>/dev/null || echo "000")
        if [ "$file_perms" != "600" ]; then
            log_warn "Credentials file has insecure permissions: $file_perms (expected 600)"
            chmod 600 "$MTT_CREDENTIALS_FILE"
        fi
        # shellcheck source=/dev/null
        source "$MTT_CREDENTIALS_FILE"
        return 0
    fi
    return 1
}

# Save credentials securely
save_credentials() {
    if [ -n "$MTT_API_KEY" ] || [ -n "$MTT_LICENSE_KEY" ]; then
        log_info "Saving credentials securely..."
        mkdir -p "$(dirname "$MTT_CREDENTIALS_FILE")"
        
        cat > "$MTT_CREDENTIALS_FILE" << EOF
# MVGAL Moore Threads Credentials
# Generated: $(date -Iseconds)
# Permissions: 600 (owner read/write only)
# This file should be owned by root

MTT_API_KEY="${MTT_API_KEY}"
MTT_LICENSE_KEY="${MTT_LICENSE_KEY}"
EOF
        
        chmod 600 "$MTT_CREDENTIALS_FILE"
        chown root:root "$MTT_CREDENTIALS_FILE"
        log_info "Credentials saved to $MTT_CREDENTIALS_FILE"
    fi
}

# Clear stored credentials
clear_credentials() {
    if [ -f "$MTT_CREDENTIALS_FILE" ]; then
        log_info "Clearing stored credentials..."
        rm -f "$MTT_CREDENTIALS_FILE"
    fi
}

# Interactive credential prompt (for terminal use)
prompt_for_credentials() {
    log_info "Moore Threads authentication required"
    log_info "Please visit $MTT_LOGINWALL_URL to register or log in"
    echo ""
    
    # Check if we can prompt interactively
    if [ -t 0 ]; then
        echo -n "Enter MTT API Key (or press Enter to skip): "
        read -r MTT_API_KEY
        echo ""
        
        echo -n "Enter MTT License Key (or press Enter to skip): "
        read -rs MTT_LICENSE_KEY
        echo ""
        
        if [ -n "$MTT_API_KEY" ] || [ -n "$MTT_LICENSE_KEY" ]; then
            echo -n "Save credentials for future use? [y/N]: "
            read -r save_choice
            if [[ "$save_choice" =~ ^[Yy]$ ]]; then
                save_credentials
            fi
        fi
    else
        log_error "Cannot prompt for credentials in non-interactive mode"
        log_error "Please set MTT_API_KEY and/or MTT_LICENSE_KEY environment variables"
        log_error "Or create $MTT_CREDENTIALS_FILE with appropriate values"
        return 1
    fi
    
    return 0
}

# Authenticate and obtain session token
authenticate_session() {
    log_info "Authenticating with Moore Threads developer portal..."
    
    local auth_data=""
    
    if [ -n "$MTT_API_KEY" ]; then
        auth_data="api_key=${MTT_API_KEY}"
    fi
    
    if [ -n "$MTT_LICENSE_KEY" ]; then
        if [ -n "$auth_data" ]; then
            auth_data="${auth_data}&license_key=${MTT_LICENSE_KEY}"
        else
            auth_data="license_key=${MTT_LICENSE_KEY}"
        fi
    fi
    
    if [ -z "$auth_data" ]; then
        log_error "No authentication credentials available"
        return 1
    fi
    
    # Attempt authentication
    local auth_url="${MTT_DOWNLOAD_BASE}/auth"
    local temp_response="${TEMP_DIR}/auth_response.json"
    
    mkdir -p "$TEMP_DIR"
    
    if command -v curl >/dev/null 2>&1; then
        if curl -s -X POST -d "$auth_data" -o "$temp_response" "$auth_url" 2>/dev/null; then
            # Parse session token from response (if JSON)
            if command -v jq >/dev/null 2>&1; then
                MTT_SESSION_COOKIE=$(jq -r '.session_token // .token // empty' "$temp_response" 2>/dev/null)
            else
                # Basic grep extraction fallback
                MTT_SESSION_COOKIE=$(grep -o '"session_token"[^,]*' "$temp_response" | cut -d'"' -f4 2>/dev/null)
            fi
            
            if [ -n "$MTT_SESSION_COOKIE" ]; then
                log_info "Authentication successful"
                return 0
            else
                log_error "Authentication failed - check your API key or license"
                return 1
            fi
        else
            log_warn "Authentication endpoint unavailable - proceeding with direct download attempt"
            # Don't fail - the download might still work with API key in header
            return 0
        fi
    else
        log_warn "curl not available - cannot perform authentication"
        return 1
    fi
}

# Download with authentication headers
download_with_auth() {
    local url="$1"
    local output="$2"
    
    local curl_opts="-L --fail --retry 3 --retry-delay 5"
    
    # Add authentication headers if available
    if [ -n "$MTT_API_KEY" ]; then
        curl_opts="${curl_opts} -H \"X-API-Key: ${MTT_API_KEY}\""
    fi
    
    if [ -n "$MTT_LICENSE_KEY" ]; then
        curl_opts="${curl_opts} -H \"X-License-Key: ${MTT_LICENSE_KEY}\""
    fi
    
    if [ -n "$MTT_SESSION_COOKIE" ]; then
        curl_opts="${curl_opts} -H \"Authorization: Bearer ${MTT_SESSION_COOKIE}\""
    fi
    
    # Add progress bar for interactive terminals
    if [ -t 1 ]; then
        curl_opts="${curl_opts} --progress-bar"
    else
        curl_opts="${curl_opts} -s"
    fi
    
    log_debug "Download command: curl $curl_opts -o $output $url"
    
    # shellcheck disable=SC2086
    if eval curl $curl_opts -o "$output" "$url" 2>>"$LOG_FILE"; then
        return 0
    else
        return 1
    fi
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
        if command -v pkexec >/dev/null 2>&1; then
            log_info "Requesting elevated privileges via pkexec..."
            exec pkexec env MTT_VERSION="$MTT_VERSION" DEBUG="${DEBUG:-0}" "$0" "$@"
        else
            error_exit "This installer requires root privileges and pkexec is not installed."
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

# Download MTT driver source with loginwall/auth support
download_driver() {
    log_info "Downloading Moore Threads driver source..."
    
    mkdir -p "$TEMP_DIR"
    cd "$TEMP_DIR"
    
    # Check if authentication is required
    check_auth_required
    
    if [ $MTT_AUTH_REQUIRED -eq 1 ]; then
        log_info "Loginwall detected - authentication required"
        
        # Try to load existing credentials
        if ! load_credentials; then
            prompt_for_credentials || error_exit "Authentication required but not provided"
        fi
        
        # Attempt to authenticate and get session
        authenticate_session || log_warn "Session authentication failed, trying direct download with credentials"
    fi
    
    # Try authenticated download from official MTT portal first
    if [ $MTT_AUTH_REQUIRED -eq 1 ]; then
        local mtt_direct_url="${MTT_DOWNLOAD_BASE}/driver/${MTT_VERSION}/mtgpu-${MTT_VERSION}.tar.gz"
        log_info "Attempting authenticated download from Moore Threads portal..."
        
        if command -v curl >/dev/null 2>&1; then
            if download_with_auth "$mtt_direct_url" "mtgpu-${MTT_VERSION}.tar.gz"; then
                tar -xzf "mtgpu-${MTT_VERSION}.tar.gz"
                # Handle various extraction scenarios
                if [ -d "mtgpu-${MTT_VERSION}" ]; then
                    mv "mtgpu-${MTT_VERSION}" "${MTT_DRIVER_NAME}-${MTT_VERSION}"
                elif [ -d "mtgpu-drv-${MTT_VERSION}" ]; then
                    mv "mtgpu-drv-${MTT_VERSION}" "${MTT_DRIVER_NAME}-${MTT_VERSION}"
                fi
                log_info "Successfully downloaded driver from authenticated portal"
                return 0
            else
                log_warn "Authenticated download failed, falling back to public sources"
            fi
        fi
    fi
    
    # Fallback to git clone (public mirror)
    if command -v git >/dev/null 2>&1; then
        log_info "Attempting public git clone from $MTT_REPO_URL..."
        if git clone --depth 1 --branch "v${MTT_VERSION}" "$MTT_REPO_URL" "${MTT_DRIVER_NAME}-${MTT_VERSION}" 2>/dev/null; then
            log_info "Successfully cloned driver source from public mirror"
            return 0
        fi
    fi
    
    # Final fallback to GitHub tarball
    local tarball_url="${MTT_REPO_URL}/archive/refs/tags/v${MTT_VERSION}.tar.gz"
    log_info "Attempting to download from GitHub mirror: ${tarball_url}..."
    
    if command -v curl >/dev/null 2>&1; then
        if curl -L --fail --retry 3 -o "mtgpu-${MTT_VERSION}.tar.gz" "$tarball_url" 2>/dev/null; then
            tar -xzf "mtgpu-${MTT_VERSION}.tar.gz"
            if [ -d "mtgpu-drv-${MTT_VERSION}" ]; then
                mv "mtgpu-drv-${MTT_VERSION}" "${MTT_DRIVER_NAME}-${MTT_VERSION}"
            fi
            log_info "Successfully downloaded from GitHub mirror"
            return 0
        fi
    elif command -v wget >/dev/null 2>&1; then
        if wget --tries=3 -O "mtgpu-${MTT_VERSION}.tar.gz" "$tarball_url" 2>/dev/null; then
            tar -xzf "mtgpu-${MTT_VERSION}.tar.gz"
            if [ -d "mtgpu-drv-${MTT_VERSION}" ]; then
                mv "mtgpu-drv-${MTT_VERSION}" "${MTT_DRIVER_NAME}-${MTT_VERSION}"
            fi
            log_info "Successfully downloaded from GitHub mirror"
            return 0
        fi
    fi
    
    error_exit "Failed to download Moore Threads driver source from all available sources"
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
            check_privileges "$command"
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
            check_privileges "$command"
            uninstall_driver
            cleanup
            log_info "Uninstall complete!"
            ;;
        
        update)
            check_privileges "$command"
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
            check_privileges "$command"
            load_module
            ;;
        
        unload)
            check_privileges "$command"
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
