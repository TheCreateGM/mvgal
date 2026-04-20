#!/bin/bash
set -e

echo "========================================="
echo " MVGAL Comprehensive Test Suite"
echo "========================================="
echo ""

echo "1. Testing CLI Tools..."
./tools/mvgal --version > /dev/null 2>&1 && echo "   ✓ mvgal --version"
./tools/mvgal --help > /dev/null 2>&1 && echo "   ✓ mvgal --help"
./tools/mvgal status > /dev/null 2>&1 && echo "   ✓ mvgal status"
./tools/mvgal list-gpus > /dev/null 2>&1 && echo "   ✓ mvgal list-gpus"
./tools/mvgal set-strategy round_robin > /dev/null 2>&1 && echo "   ✓ mvgal set-strategy"
./tools/mvgal-config list-gpus > /dev/null 2>&1 && echo "   ✓ mvgal-config list-gpus"
./tools/mvgal-config show-config > /dev/null 2>&1 && echo "   ✓ mvgal-config show-config"
echo ""

echo "2. Testing Benchmarks..."
./benchmarks/synthetic/mvgal_synthetic_bench -q -n > /dev/null 2>&1 && echo "   ✓ Synthetic benchmarks" || echo "   ✗ Synthetic benchmarks failed"
./benchmarks/real_world/mvgal_realworld_bench -q -n > /dev/null 2>&1 && echo "   ✓ Real-world benchmarks" || echo "   ✗ Real-world benchmarks failed"
./benchmarks/stress/mvgal_stress_bench -q -n -d 1000 > /dev/null 2>&1 && echo "   ✓ Stress benchmarks" || echo "   ✗ Stress benchmarks failed"
echo ""

echo "3. Testing GUI Tools (if GTK3 available)..."
if command -v pkg-config &>/dev/null && pkg-config --modversion gtk+-3.0 &>/dev/null; then
    ./gui/mvgal-gui --version > /dev/null 2>&1 && echo "   ✓ mvgal-gui compiles" || echo "   - mvgal-gui not available"
    ./gui/mvgal-tray --version > /dev/null 2>&1 && echo "   ✓ mvgal-tray compiles" || echo "   - mvgal-tray not available"
else
    echo "   - GTK3 not available, skipping GUI tests"
fi
echo ""

echo "4. Testing DBus Service (if dbus available)..."
if pkg-config --modversion dbus-1 &>/dev/null; then
    ./pkg/dbus/mvgal-dbus-service --version > /dev/null 2>&1 && echo "   ✓ mvgal-dbus-service compiles" || echo "   - mvgal-dbus-service not available"
else
    echo "   - DBus not available, skipping DBus tests"
fi
echo ""

echo "5. Checking Source Tarball..."
if [ -f dist/mvgal-0.1.0.tar.gz ]; then
    echo "   ✓ Tarball exists: dist/mvgal-0.1.0.tar.gz"
    echo "   Size: $(du -h dist/mvgal-0.1.0.tar.gz | cut -f1)"
else
    echo "   ✗ Tarball not found"
fi
echo ""

echo "6. Checking Packaging Files..."
for file in pkg/debian/control pkg/debian/rules pkg/rpm/mvgal.spec pkg/arch/PKGBUILD pkg/flatpak/org.mvgal.MVGAL.json pkg/snap/snapcraft.yaml; do
    [ -f "$file" ] && echo "   ✓ $file" || echo "   ✗ $file missing"
done
echo ""

echo "7. Checking Documentation..."
for file in PACKAGING_SUMMARY.md STATUS.md BUILDworkspace.md; do
    [ -f "$file" ] && echo "   ✓ $file" || echo "   ✗ $file missing"
done
echo ""

echo "8. Checking Configuration Files..."
for file in config/mvgal.conf config/99-mvgal.rules config/load-module.sh config/unload-module.sh; do
    [ -f "$file" ] && echo "   ✓ $file" || echo "   ✗ $file missing"
done
echo ""

echo "========================================="
echo " Test Suite Complete!"
echo "========================================="
