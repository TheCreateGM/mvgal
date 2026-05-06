Name: mvgal
Version: 0.2.1
Release: 2%{?dist}
Summary: Multi-Vendor GPU Aggregation Layer for Linux

License: GPL-3.0-only
URL: https://github.com/axogm/mvgal
Source0: mvgal-%{version}.tar.gz

BuildRequires: gcc
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: cmake >= 3.20
BuildRequires: ninja-build
BuildRequires: libdrm-devel
BuildRequires: systemd-devel
BuildRequires: pciutils-devel
BuildRequires: pkgconfig(pciaccess)
BuildRequires: vulkan-headers
BuildRequires: vulkan-loader

Requires: libdrm
Requires: systemd
Requires: vulkan-loader

Obsoletes: mvgallibs <= 0.1.0
Provides: libmvgal = %{version}

# Note: If you have Moore Threads (MTT) GPU drivers installed and see
# ldconfig warnings like "/lib64/libmupp*.so.1 is not a symbolic link",
# those are caused by a packaging issue in the MTT driver itself - the
# .so.1 files are regular files instead of symlinks to versioned libraries.
# This is unrelated to MVGAL. Contact Moore Threads or your distro packager
# to fix the MTT driver package.

%description
MVGAL (Multi-Vendor GPU Aggregation Layer) enables heterogeneous GPUs
(AMD, NVIDIA, Intel, Moore Threads) to function as a single logical compute
and rendering device.

Features:
- Unified abstraction for multiple GPUs
- Smart scheduling with AFR, SFR, and Hybrid strategies
- DMA-BUF based cross-GPU memory management
- Vulkan layer for transparent API interception
- OpenCL and CUDA support (experimental)
- Daemon for background GPU management

%prep
%setup -q

%build
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DMVGAL_BUILD_KERNEL=OFF \
    -DMVGAL_BUILD_RUNTIME=ON \
    -DMVGAL_BUILD_API=ON \
    -DMVGAL_BUILD_TOOLS=OFF \
    -DMVGAL_BUILD_TESTS=OFF \
    -DMVGAL_ENABLE_RUST=OFF
%cmake_build

%install
rm -rf %{buildroot}
%cmake_install

# ldconfig drop-in so the dynamic linker finds mvgal libraries
install -d %{buildroot}%{_sysconfdir}/ld.so.conf.d
echo "%{_libdir}" > %{buildroot}%{_sysconfdir}/ld.so.conf.d/mvgal.conf

%post
# ldconfig - update the dynamic linker cache for libmvgal.so.*
# Using /sbin/ldconfig directly here (not the macro) so we can also
# handle the runtime directory creation in the same scriptlet.
# The ldconfig_scriptlets macro would generate a separate post -p /sbin/ldconfig
# which conflicts with our custom post; instead we call ldconfig explicitly.
/sbin/ldconfig 2>&1 | grep -v "is not a symbolic link" || true
# Create runtime directory (tmpfiles.d would be cleaner but this works
# across all supported distros without extra dependencies)
install -d -m 0755 /var/run/mvgal 2>/dev/null || true
install -d -m 0755 /var/log/mvgal 2>/dev/null || true

if [ -d /run/systemd/system ]; then
    systemctl daemon-reload > /dev/null 2>&1 || :
fi

%preun
if [ $1 -eq 0 ] && [ -d /run/systemd/system ]; then
    systemctl stop mvgal-daemon.service > /dev/null 2>&1 || :
    systemctl disable mvgal-daemon.service > /dev/null 2>&1 || :
fi

%postun
if [ $1 -eq 0 ] && [ -d /run/systemd/system ]; then
    systemctl daemon-reload > /dev/null 2>&1 || :
fi
# Refresh ldconfig cache after uninstall, suppress unrelated MTT warnings
/sbin/ldconfig 2>&1 | grep -v "is not a symbolic link" || true

%files
%license LICENSE
%doc README.md CONTRIBUTING.md
# Static core library (always built with MVGAL_BUILD_RUNTIME=ON)
%{_libdir}/libmvgal_core.a
# Vulkan ICD shared library
%{_libdir}/mvgal_vulkan_icd.so
# Vulkan Layer shared library
%{_libdir}/VK_LAYER_MVGAL.so
# OpenCL interception library (built with MVGAL_BUILD_API=ON)
%{_libdir}/libmvgal_opencl.so
# D3D wrapper library (built with MVGAL_BUILD_API=ON)
%{_libdir}/libmvgal_d3d.so
# Metal wrapper library (built with MVGAL_BUILD_API=ON)
%{_libdir}/libmvgal_metal.so
# WebGPU wrapper library (built with MVGAL_BUILD_API=ON)
%{_libdir}/libmvgal_webgpu.so
# Headers
%{_includedir}/mvgal/
# Daemon binary (installed to bin by cmake)
%{_bindir}/mvgald
%{_sbindir}/mvgal-daemon
# Config (preserve user edits on upgrade)
%config(noreplace) %{_sysconfdir}/mvgal/mvgal.conf
# ldconfig fragment
%{_sysconfdir}/ld.so.conf.d/mvgal.conf
# Vulkan ICD manifest (installed by vulkan_icd CMakeLists.txt)
%{_datadir}/vulkan/icd.d/mvgal_icd.json
# Vulkan Layer manifest (installed by userspace CMakeLists.txt)
%{_datadir}/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json
# Systemd service
%{_unitdir}/mvgal-daemon.service
# Log directory
%dir /var/log/mvgal

%changelog
* Wed May 06 2026 AxoGM <creategm10@proton.me> - 0.2.1-2
- Fix CMake install paths to use CMAKE_INSTALL_LIBDIR instead of hardcoded "lib"
  This ensures libraries install to lib64 on 64-bit systems automatically
- Remove complex versioned symlink logic from spec %%install (now handled by CMake)
- Add missing API interception libraries to %%files section:
  libmvgal_opencl.so, libmvgal_d3d.so, libmvgal_metal.so, libmvgal_webgpu.so
- Simplify spec file by relying on CMake GNUInstallDirs for proper paths

* Mon May 04 2026 AxoGM <creategm10@proton.me> - 0.2.1-1
- Fix ldconfig "not a symbolic link" warnings for libmvgal shared libs
  by installing versioned .so.MAJOR.MINOR.PATCH files with proper symlinks
- Switch from manual /sbin/ldconfig call to %%ldconfig_scriptlets macro
  to avoid surfacing unrelated MTT driver packaging issues
- Fix config file path: packaging/rpm/mvgal.conf (was rpm/mvgal.conf)
- Fix service file path: packaging/rpm/mvgal-daemon.service
- Switch %%build/%%install to %%cmake/%%cmake_build/%%cmake_install macros
- Add pciutils-devel and pciaccess to BuildRequires
- Add /etc/ld.so.conf.d/mvgal.conf fragment for reliable library discovery
- Add %%preun service disable on uninstall

* Tue Apr 21 2026 AxoGM <creategm10@proton.me> - 0.2.0-1
- Updated to version 0.2.0 "Health Monitor"
- GPU health monitoring with temperature, utilization tracking
- Comprehensive scheduler with 7 strategies
- DMA-BUF based cross-GPU memory management
- OpenCL interception layer

* Sun Apr 19 2026 AxoGM <creategm10@proton.me> - 0.1.0-1
- Initial release
- GPU detection, scheduler, memory manager
- Vulkan layer, CUDA wrapper
- Daemon for background GPU management
