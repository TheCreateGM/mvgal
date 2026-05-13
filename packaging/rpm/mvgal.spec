# Disable automatic debuginfo generation to avoid conflicts
%global debug_package %{nil}

Name: mvgal
Version: 0.2.1
Release: 13%{?dist}
Summary: Multi-Vendor GPU Aggregation Layer for Linux

License: GPL-3.0-only
URL: https://github.com/TheCreateGM/mvgal
Source0: mvgal-%{version}.tar.gz

# OpenCL support is conditional - enable by default, disable with --without opencl
%bcond_without opencl

BuildRequires: gcc
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: cmake >= 3.20
%if 0%{?suse_version}
BuildRequires: ninja
%else
BuildRequires: ninja-build
%endif
BuildRequires: libdrm-devel
BuildRequires: systemd-devel
BuildRequires: pciutils-devel
BuildRequires: pkgconfig(pciaccess)
BuildRequires: vulkan-headers
BuildRequires: pkgconfig(vulkan)
BuildRequires: opencl-headers
BuildRequires: ocl-icd-devel

Requires: libdrm
Requires: systemd
%if 0%{?suse_version}
Requires: libvulkan1
%else
%if 0%{?mageia}
Requires: lib64vulkan-loader1
%else
Requires: vulkan-loader
%endif
%endif

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
# Log directory (created in the install section)
%dir %{_localstatedir}/log/mvgal

# pkexec and DKMS helper scripts
%{_prefix}/lib/mvgal/mvgal-pkexec-helper.sh
%{_prefix}/lib/mvgal/mtt-dkms-installer.sh
%{_datadir}/mvgal/scripts/mtt-dkms-installer.sh

# udev rules
%{_prefix}/lib/udev/rules.d/99-mvgal.rules

# PolicyKit policy
%{_datadir}/polkit-1/actions/org.freedesktop.policykit.mvgal.policy

# Development headers
%{_includedir}/mvgal/

# Documentation
%{_docdir}/mvgal/

%changelog
* Wed May 13 2026 AxoGM <creategm10@proton.me> - 0.2.1-13
- Fix COPR source script: use packaging/rpm/mvgal.spec instead of
  pkg/rpm/mvgal.spec (broken path preventing SRPM builds)

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-12
- Fix openSUSE documentation directory: use %%{_docdir}/mvgal/ instead of
  %%{_datadir}/doc/mvgal/ (%%{_datadir}/doc/ != %%{_docdir}/ on openSUSE)
- Fix GCC 7.5 (openSUSE Leap 15.6) cmake configure: remove redundant
  target_compile_features(mvgald PRIVATE cxx_std_20) from runtime/CMakeLists.txt
  (CMAKE_CXX_STANDARD 20 already set at project level)
- Fix C17 cmake configure error on older CMake (Mageia 8, CMake 3.20):
  conditionally use C17 only for CMake >= 3.21, fall back to C11

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-11
- Fix distro-specific package names: use pkgconfig(vulkan) instead of
  vulkan-loader for cross-distro portability (Fedora, RHEL, Mageia, openSUSE)
- Use distro conditionals: ninja-build (Fedora/Mageia) vs ninja (openSUSE);
  vulkan-loader (Fedora/RHEL) vs lib64vulkan-loader1 (Mageia) vs libvulkan1
  (openSUSE) for runtime dependency

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-10
- Fix unpackaged files error: add pkexec helper, DKMS installer scripts,
  udev rules, and PolicyKit policy to %%files section

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-9
- Add missing forward declaration for mvgal_get_queue_family_properties
  in device_group.c fixes implicit-declaration error

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-8
- Fix implicit declaration error in device_group.c: rename
  mvgal_gpu_get_queue_family_properties to mvgal_get_queue_family_properties
  (function never existed, was a code artifact)

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-7
- Fix fatal error: mvgal/mvgal.h: No such file or directory when building
  vk_layer.c on Fedora 44+: add missing ${MVGAL_INCLUDE_DIRS} to the
  vulkan layer CMake target include paths

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-6
- Fix implicit declaration error on GCC 15+ (Fedora Rawhide): remove dead
  call to mvgal_rewrite_update_gpu_utilization from load_balancer.c

* Sun May 10 2026 AxoGM <creategm10@proton.me> - 0.2.1-5
- Add CMake compatibility aliases for subdirectory CMakeLists that reference
  old-style find_package patterns (DRM_IMPORTED_TARGET, PCI_IMPORTED_TARGET,
  UDEV_IMPORTED_TARGET)
- Fix unconditional opengl and vulkan_icd subdirectory builds to be
  discoverable from parent CMakeLists.txt

* Thu May 07 2026 AxoGM <creategm10@proton.me> - 0.2.1-4
- Fix unpackaged files error: add headers (%%{_includedir}/mvgal/) and
  documentation (%%{_datadir}/doc/mvgal/) to %%files section
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
