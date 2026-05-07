Name: mvgal
Version: 0.2.1
Release: 3%{?dist}
Summary: Multi-Vendor GPU Aggregation Layer for Linux

License: GPL-3.0-only
URL: https://github.com/axogm/mvgal
Source0: mvgal-%{version}.tar.gz

# OpenCL support is conditional - enable by default, disable with --without opencl
%bcond_without opencl

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
BuildRequires: opencl-headers
BuildRequires: ocl-icd-devel

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
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
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

# Install config file
install -d %{buildroot}%{_sysconfdir}/mvgal
install -m 644 %{_builddir}/%{name}-%{version}/config/mvgal.conf %{buildroot}%{_sysconfdir}/mvgal/mvgal.conf

# Install systemd service file
install -d %{buildroot}%{_unitdir}
install -m 644 %{_builddir}/%{name}-%{version}/packaging/rpm/mvgal-daemon.service %{buildroot}%{_unitdir}/mvgal-daemon.service

# Create symlink for daemon binary name expected by service file
ln -sf mvgald %{buildroot}%{_bindir}/mvgal-daemon

# ldconfig drop-in so the dynamic linker finds mvgal libraries
install -d %{buildroot}%{_sysconfdir}/ld.so.conf.d
echo "%{_libdir}" > %{buildroot}%{_sysconfdir}/ld.so.conf.d/mvgal.conf

# Create log directory (owned by package)
install -d -m 0755 %{buildroot}%{_localstatedir}/log/mvgal

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
# Daemon binary (with symlink for service file compatibility)
%{_bindir}/mvgald
%{_bindir}/mvgal-daemon
# Core library (static)
%{_libdir}/libmvgal_core.a
# API interception libraries
%{_libdir}/libVK_LAYER_MVGAL.so*
%{_libdir}/libmvgal_d3d.so
%{_libdir}/libmvgal_metal.so
%{_libdir}/libmvgal_webgpu.so
%{_libdir}/libmvgal_gl.so
# Conditionally built (OpenCL support - enabled by default)
%if %{with opencl}
%{_libdir}/libmvgal_opencl.so
%endif
# Conditionally built (Vulkan ICD)
%{_libdir}/mvgal_vulkan_icd.so
# Vulkan layer manifest
%{_datadir}/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json
%{_datadir}/vulkan/icd.d/mvgal_icd.json
# Config file
%config(noreplace) %{_sysconfdir}/mvgal/mvgal.conf
# Systemd service
%{_unitdir}/mvgal-daemon.service
# ldconfig config
%config(noreplace) %{_sysconfdir}/ld.so.conf.d/mvgal.conf
# Log directory (created in %install)
%dir %{_localstatedir}/log/mvgal

%changelog
* Thu May 07 2026 AxoGM <creategm10@proton.me> - 0.2.1-3
- Fix missing files in COPR build: libmvgal_opencl.so, libmvgal_gl.so,
  mvgal_vulkan_icd.so, and /var/log/mvgal directory
- Add proper %%bcond_without opencl for conditional OpenCL library inclusion
- Add ocl-icd-devel to BuildRequires (was missing for OpenCL support)
- Remove duplicate BuildRequires: opencl-headers
- Add %%dir %%{_localstatedir}/log/mvgal to %%files and %%install sections
- Fix %%files section to properly conditionally include libmvgal_opencl.so

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
