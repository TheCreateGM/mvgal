#!/bin/bash
# Universal MVGAL Package Builder
# Usage: ./packaging/build_package.sh [deb|rpm|arch]
# Run from mvgal project root directory

set -e

PACKAGE_NAME="mvgal"
VERSION="0.1.0"
ARCH="$(uname -m)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-package"

G='\033[0;32m'; R='\033[0;31m'; NC='\033[0m'
echo_header() { echo -e "${G}==========================================\n  $1\n==========================================${NC}"; }
echo_error() { echo -e "  ${R}[ERROR]${NC} $1" >&2; }
echo_ok() { echo -e "  ${G}[OK]${NC} $1"; }

cleanup() {
    rm -rf "$BUILD_DIR" 2>/dev/null || true
    mkdir -p "$BUILD_DIR"
}

build_mvgal() {
    echo_header "Building MVGAL"
    cd "$SOURCE_DIR"
    [ ! -d build ] && mkdir -p build
    cd build
    [ ! -f Makefile ] && cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_VULKAN=ON -DWITH_OPENCL=ON -DWITH_DAEMON=ON -DWITH_TESTS=OFF -DWITH_BENCHMARKS=OFF -DWITH_DOCS=OFF
    make -j$(nproc)
    cd "$SOURCE_DIR"
    echo_ok "MVGAL built successfully"
}

prepare_install_tree() {
    local DST="$1"
    cd "$SOURCE_DIR/build"
    mkdir -p "$DST/usr/lib" "$DST/usr/include/mvgal" "$DST/usr/sbin" "$DST/etc/mvgal" "$DST/usr/share/vulkan/explicit_layer.d" "$DST/usr/lib/systemd/system" "$DST/usr/share/doc/mvgal"
    for lib in libmvgal.so* libmvgal_core.a libVK_LAYER_MVGAL.so; do
        [ -f "$lib" ] && cp "$lib" "$DST/usr/lib/"
    done
    cp "$SOURCE_DIR/include/mvgal/"*.h "$DST/usr/include/mvgal/" 2>/dev/null || true
    [ -f mvgal-daemon ] && cp mvgal-daemon "$DST/usr/sbin/"
    [ -f "$SOURCE_DIR/src/userspace/intercept/vulkan/manifest.json" ] && \
        cp "$SOURCE_DIR/src/userspace/intercept/vulkan/manifest.json" "$DST/usr/share/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json"
    cat > "$DST/usr/lib/systemd/system/mvgal-daemon.service" << 'SERVICEEOF'
[Unit]
Description=Multi-Vendor GPU Aggregation Layer Daemon
After=syslog.target network.target
[Service]
Type=simple
ExecStart=/usr/sbin/mvgal-daemon
Restart=on-failure
RestartSec=5
StandardOutput=syslog
StandardError=syslog
[Install]
WantedBy=multi-user.target
SERVICEEOF
    [ -f "$SOURCE_DIR/README.md" ] && cp "$SOURCE_DIR/README.md" "$DST/usr/share/doc/mvgal/"
}

build_deb() {
    echo_header "Building DEB Package"
    command -v dpkg-deb >/dev/null 2>&1 || { echo_error "dpkg-deb required. Install: sudo apt-get install dpkg-dev"; exit 1; }
    local D="$BUILD_DIR/deb/$PACKAGE_NAME"
    cleanup
    prepare_install_tree "$D"
    mkdir -p "$D/DEBIAN"
    [ -f "$SCRIPT_DIR/deb/mvgal.conf" ] && cp "$SCRIPT_DIR/deb/mvgal.conf" "$D/etc/mvgal/"
    cp "$SCRIPT_DIR/deb/DEBIAN/"* "$D/DEBIAN/" 2>/dev/null || true
    chmod 755 "$D/DEBIAN/"* 2>/dev/null || true
    dpkg-deb --build "$D" "$BUILD_DIR/deb/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
    echo_ok "DEB package: $BUILD_DIR/deb/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
    echo "Install: sudo dpkg -i $BUILD_DIR/deb/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
    echo "Or view: ls -la $BUILD_DIR/deb/"
}

build_rpm() {
    echo_header "Building RPM Package"
    command -v rpm >/dev/null 2>&1 || { echo_error "rpm required"; exit 1; }
    cleanup

    # For RPM: use standard rpmbuild process
    local RPM_DIR="$HOME/rpmbuild"
    mkdir -p "$RPM_DIR/BUILD" "$RPM_DIR/RPMS" "$RPM_DIR/SOURCES" "$RPM_DIR/SPECS" "$RPM_DIR/SRPMS"

    # Copy spec file with updated metadata
    cp "$SCRIPT_DIR/rpm/mvgal.spec" "$RPM_DIR/SPECS/"

    # Create source tarball with proper prefix
    cd "$SOURCE_DIR"
    # Exclude already built files - let rpmbuild do a clean build
    # Remove build dir temporarily to avoid conflicts
    local HAS_BUILD=0
    [ -d "$SOURCE_DIR/build" ] && HAS_BUILD=1

    # Create source without build directory
    local SOURCE_TAR="$RPM_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz"
    # Use git if available, otherwise manual tar
    if command -v git &>/dev/null && [ -d "$SOURCE_DIR/.git" ]; then
        git archive --format=tar --prefix="${PACKAGE_NAME}-${VERSION}/" HEAD | gzip > "$SOURCE_TAR"
    else
        cd "$SOURCE_DIR"
        tar -czf "$SOURCE_TAR" --transform "s|^|${PACKAGE_NAME}-${VERSION}/|" \
            --exclude="./build" --exclude="./packaging/build-*" --exclude="./docs" \
            --exclude="./packaging/deb" --exclude="./packaging/arch" .
    fi

    cd "$RPM_DIR/SPECS"
    echo "Building RPM (this may take a while)..."
    rpmbuild -bb mvgal.spec 2>&1 | tail -5

    RPM_FILE=$(find "$RPM_DIR/RPMS/${ARCH}/" -name "${PACKAGE_NAME}-${VERSION}-1.*.rpm" | head -1)
    if [ -n "$RPM_FILE" ] && [ -f "$RPM_FILE" ]; then
        mkdir -p "$BUILD_DIR/rpm"
        cp "$RPM_FILE" "$BUILD_DIR/rpm/"
        echo_ok "RPM package: $BUILD_DIR/rpm/$(basename $RPM_FILE)"
        echo "Install: sudo rpm -ivh $BUILD_DIR/rpm/$(basename $RPM_FILE)"
        # Restore build dir if it was there
        [ $HAS_BUILD -eq 1 ] && mkdir -p "$SOURCE_DIR/build"
    else
        echo_error "rpmbuild failed - no RPM file found"
        echo "Check: ls -la $RPM_DIR/RPMS/${ARCH}/"
        # Restore build dir
        [ $HAS_BUILD -eq 1 ] && mkdir -p "$SOURCE_DIR/build"
        exit 1
    fi
}

build_arch() {
    echo_header "Building Arch Package"
    command -v makepkg >/dev/null 2>&1 || { echo_error "makepkg required. Install: sudo pacman -S base-devel"; exit 1; }
    local D="$BUILD_DIR/arch"
    rm -rf "$D" 2>/dev/null || true
    mkdir -p "$D"
    cp "$SCRIPT_DIR/arch/"* "$D/" 2>/dev/null || true
    sed -i 's|github.com/your-repo/mvgal|github.com/TheCreateGM/mvgal|g' "$D/PKGBUILD" 2>/dev/null || true
    cd "$D"
    makepkg --noconfirm --noprogress 2>&1 | tail -3
    PKG=$(ls *.pkg.tar.* 2>/dev/null | head -1)
    if [ -n "$PKG" ]; then
        echo_ok "Arch package: $D/$PKG"
        echo "Install: sudo pacman -U $D/$PKG"
    else
        echo_error "Arch build failed"
        exit 1
    fi
}

main() {
    echo_header "MVGAL Universal Package Builder"
    cleanup
    build_mvgal
    echo
    case "${1:-auto}" in
        deb|DEB) build_deb ;;
        rpm|RPM) build_rpm ;;
        arch|ARCH) build_arch ;;
        auto)
            if command -v dpkg >/dev/null 2>&1; then
                build_deb
            elif command -v rpm >/dev/null 2>&1; then
                build_rpm
            elif command -v pacman >/dev/null 2>&1; then
                build_arch
            else
                echo_error "Cannot detect package format for this system"
                exit 1
            fi
            ;;
        *)
            echo "Usage: $0 [deb|rpm|arch]"
            echo "  deb  - Debian/Ubuntu (.deb)"
            echo "  rpm  - Fedora/RHEL (.rpm)"
            echo "  arch - Arch Linux (.pkg.tar.zst)"
            exit 1
            ;;
    esac
}

main "$@"
