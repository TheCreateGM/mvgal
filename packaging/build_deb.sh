#!/bin/bash
# Build Debian package for MVGAL

set -e

PACKAGE_NAME="mvgal"
VERSION="0.1.0"
ARCH="amd64"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-deb"
INSTALL_DIR="$BUILD_DIR/$PACKAGE_NAME"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_header() {
    echo -e "${GREEN}=========================================="
    echo "  $1"
    echo "==========================================${NC}"
}

print_status() {
    echo -e "${YELLOW}  → ${NC}$1"
}

print_success() {
    echo -e "  ${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "  ${RED}✗${NC} $1" >&2
}

# Clean up
print_header "Cleaning up"
rm -rf "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

# Build the project first
print_header "Building MVGAL"
cd "$SOURCE_DIR"
if [ ! -d build ]; then
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_VULKAN=ON -DWITH_OPENCL=ON -DWITH_DAEMON=ON 2>&1 | tail -5
    make -j$(nproc) 2>&1 | tail -3
else
    cd build
fi

# Create directory structure
print_header "Creating package structure"

# usr/lib - Libraries
mkdir -p "$INSTALL_DIR/usr/lib"
print_status "Copying libraries..."
for lib in libmvgal.so* libmvgal_core.a libVK_LAYER_MVGAL.so; do
    if [ -f "$lib" ]; then
        cp "$lib" "$INSTALL_DIR/usr/lib/"
        print_success "  Copied: $lib"
    fi
done

# usr/include - Headers
mkdir -p "$INSTALL_DIR/usr/include/mvgal"
print_status "Copying headers..."
cp -r "$SOURCE_DIR/include/mvgal/"*.h "$INSTALL_DIR/usr/include/mvgal/" 2>/dev/null || true
for hdr in "$SOURCE_DIR/include/mvgal/"*.h; do
    if [ -f "$hdr" ]; then
        cp "$hdr" "$INSTALL_DIR/usr/include/mvgal/"
        print_success "  Copied: $(basename "$hdr")"
    fi
done

# usr/sbin - Daemon
mkdir -p "$INSTALL_DIR/usr/sbin"
print_status "Copying daemon..."
if [ -f "mvgal-daemon" ]; then
    cp "mvgal-daemon" "$INSTALL_DIR/usr/sbin/"
    print_success "  Copied: mvgal-daemon"
fi

# etc - Configuration
mkdir -p "$INSTALL_DIR/etc/mvgal"
print_status "Copying configuration..."
CONFIG_FILE="$SCRIPT_DIR/deb/mvgal.conf"
if [ -f "$CONFIG_FILE" ]; then
    cp "$CONFIG_FILE" "$INSTALL_DIR/etc/mvgal/"
    print_success "  Copied: mvgal.conf"
else
    if [ -f "$SOURCE_DIR/etc/mvgal.conf" ]; then
        cp "$SOURCE_DIR/etc/mvgal.conf" "$INSTALL_DIR/etc/mvgal/"
        print_success "  Copied: mvgal.conf (from etc/)"
    else
        print_status "  No configuration file found, skipping..."
    fi
fi

# Vulkan layer manifest
mkdir -p "$INSTALL_DIR/usr/share/vulkan/explicit_layer.d"
mkdir -p "$INSTALL_DIR/usr/share/vulkan/icd.d"
print_status "Copying Vulkan files..."
if [ -f "$SOURCE_DIR/src/userspace/intercept/vulkan/manifest.json" ]; then
    cp "$SOURCE_DIR/src/userspace/intercept/vulkan/manifest.json" \
        "$INSTALL_DIR/usr/share/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json"
    print_success "  Copied: Vulkan layer manifest"
fi

# Systemd service
mkdir -p "$INSTALL_DIR/usr/lib/systemd/system"
cat > "$INSTALL_DIR/usr/lib/systemd/system/mvgal-daemon.service" << 'EOF'
[Unit]
Description=Multi-Vendor GPU Aggregation Layer Daemon
After=syslog.target network.target

[Service]
Type=simple
User=root
Group=root
ExecStart=/usr/sbin/mvgal-daemon
Restart=on-failure
RestartSec=5
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=mvgal-daemon

[Install]
WantedBy=multi-user.target
EOF
print_success "  Created: systemd service"

# Documentation
mkdir -p "$INSTALL_DIR/usr/share/doc/mvgal"
print_status "Copying documentation..."
for doc in README.md QUICKSTART.md PROGRESS.md CHANGES_2025.md LICENSE; do
    if [ -f "$SOURCE_DIR/$doc" ]; then
        cp "$SOURCE_DIR/$doc" "$INSTALL_DIR/usr/share/doc/mvgal/"
        print_success "  Copied: $doc"
    fi
done

# Build DEB package
print_header "Building DEB package"
cd "$SOURCE_DIR"

# Create DEBIAN directory
mkdir -p "$INSTALL_DIR/DEBIAN"

# Copy control files
DEBIAN_CTRL_DIR="$SCRIPT_DIR/deb/DEBIAN"
if [ -d "$DEBIAN_CTRL_DIR" ]; then
    cp "$DEBIAN_CTRL_DIR/"* "$INSTALL_DIR/DEBIAN/" 2>/dev/null
    chmod 755 "$INSTALL_DIR/DEBIAN/"* 2>/dev/null || true
else
    print_error "DEBIAN control files not found at $DEBIAN_CTRL_DIR"
    exit 1
fi

# Fix permissions
chmod -R 755 "$INSTALL_DIR/usr/sbin/" 2>/dev/null || true
chmod -R 644 "$INSTALL_DIR/usr/lib/"*.so* 2>/dev/null || true
chmod -R 644 "$INSTALL_DIR/usr/share/"* 2>/dev/null || true

# Calculate package size
dpkg-deb --build "$INSTALL_DIR" "$BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"

if [ -f "$BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb" ]; then
    print_success "Package built: $BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
    ls -lh "$BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
else
    print_error "Failed to build package"
    exit 1
fi

print_header "DEB Package Ready"
echo ""
echo "File: $BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
echo "Size: $(du -h "$BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb" | cut -f1)"
echo ""
echo "To install:"
echo "  sudo dpkg -i $BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
echo ""
echo "To verify:"
echo "  dpkg -c $BUILD_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
