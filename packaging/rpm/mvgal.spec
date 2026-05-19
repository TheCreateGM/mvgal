# Disable automatic debuginfo generation to avoid conflicts
%global debug_package %{nil}

# RHEL 8 / GCC 8 LTO: handled at the cmake level (src/userspace/CMakeLists.txt)
# by building libmvgal_core.a objects with -fno-lto, so the LTO plugin never
# encounters bytecode it can't process from the static archive.

Name: mvgal
Version: 0.2.3
Release: 2%{?dist}
Summary: Multi-Vendor GPU Aggregation Layer for Linux

License: GPL-3.0-only
URL: https://github.com/TheCreateGM/mvgal
Source0: https://github.com/TheCreateGM/mvgal/archive/v%{version}.tar.gz
# Vendored Rust dependencies for offline COPR builds (no network access)
Source1: https://github.com/TheCreateGM/mvgal/releases/download/v%{version}/mvgal-vendor-%{version}.tar.gz

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
BuildRequires: rust
BuildRequires: cargo

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

# Extract vendored Rust dependencies for offline cargo builds
tar xzf %{SOURCE1}
mkdir -p .cargo
cat > .cargo/config.toml << 'EOF'
[source.crates-io]
replace-with = "vendored-sources"

[source.vendored-sources]
directory = "vendor"
EOF

%build
%if 0%{?rhel} == 8
# cmake-rpm-macros on RHEL 8 (cmake 3.26 EPEL) has broken %%cmake macro
# that emits "does in-source builds." as literal arguments.
# Invoke cmake directly instead, then strip -flto for GCC 8.
cmake \
    -DCMAKE_C_FLAGS_RELEASE:STRING=-DNDEBUG \
    -DCMAKE_CXX_FLAGS_RELEASE:STRING=-DNDEBUG \
    -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
    -DCMAKE_INSTALL_PREFIX:PATH=%{_prefix} \
    -DINCLUDE_INSTALL_DIR:PATH=%{_includedir} \
    -DLIB_INSTALL_DIR:PATH=%{_libdir} \
    -DSYSCONF_INSTALL_DIR:PATH=%{_sysconfdir} \
    -DSHARE_INSTALL_PREFIX:PATH=%{_datadir} \
    -DLIB_SUFFIX=64 \
    -DBUILD_SHARED_LIBS:BOOL=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DMVGAL_BUILD_KERNEL=OFF \
    -DMVGAL_BUILD_RUNTIME=ON \
    -DMVGAL_BUILD_API=ON \
    -DMVGAL_BUILD_TOOLS=ON \
    -DMVGAL_BUILD_TESTS=OFF \
    -DMVGAL_ENABLE_RUST=ON

# RHEL 8 / GCC 8: strip -flto from cmake-generated build files.
# Use individual find commands (no \(\)) to avoid RHEL 8 RPM parser bug.
# Use . because RHEL 8 cmake does in-source builds.
find . -type f -name 'flags.make' -exec sed -i 's/-flto[^ ]*//g' {} \; 2>/dev/null || :
find . -type f -name 'build.make' -exec sed -i 's/-flto[^ ]*//g' {} \; 2>/dev/null || :
find . -type f -name '*.link.txt' -exec sed -i 's/-flto[^ ]*//g' {} \; 2>/dev/null || :
find . -type f -name '*.rsp' -exec sed -i 's/-flto[^ ]*//g' {} \; 2>/dev/null || :
%else
%cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DMVGAL_BUILD_KERNEL=OFF \
    -DMVGAL_BUILD_RUNTIME=ON \
    -DMVGAL_BUILD_API=ON \
    -DMVGAL_BUILD_TOOLS=ON \
    -DMVGAL_BUILD_TESTS=OFF \
    -DMVGAL_ENABLE_RUST=ON
%endif
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

# Install D-Bus policy file
install -d %{buildroot}%{_sysconfdir}/dbus-1/system.d
install -m 644 %{_builddir}/%{name}-%{version}/config/org.mvgal.MVGAL.conf %{buildroot}%{_sysconfdir}/dbus-1/system.d/org.mvgal.MVGAL.conf

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
# CLI tools
%{_bindir}/mvgal
%{_bindir}/mvgal-info
%{_bindir}/mvgal-status
%{_bindir}/mvgal-bench
%{_bindir}/mvgal-compat
%{_bindir}/mvgal-config
%{_bindir}/mvgal-steam-setup
# Additional tools enabled by MVGAL_BUILD_TOOLS=ON
%{_bindir}/mvgal-probe
%{_bindir}/mvgal_amd_external_mem
# Core library (static)
%{_libdir}/libmvgal_core.a
# libmvgal UAPI wrapper shared library (from tools/libmvgal/)
%{_libdir}/libmvgal.so*
# CMake config files for libmvgal
%{_libdir}/cmake/mvgal/
# API interception libraries
%{_libdir}/libVK_LAYER_MVGAL.so*
%{_libdir}/libmvgal_d3d.so*
%{_libdir}/libmvgal_metal.so*
%{_libdir}/libmvgal_webgpu.so*
%{_libdir}/libmvgal_gl.so*
# Additional static libraries
%{_libdir}/libmvgal_prometheus.a
%{_libdir}/libmvgal_sycl_backend.a
# Conditionally built (OpenCL support - enabled by default)
%if %{with opencl}
%{_libdir}/libmvgal_opencl.so*
%endif
# Conditionally built (Vulkan ICD)
%{_libdir}/mvgal_vulkan_icd.so*
# Vulkan layer manifest
%{_datadir}/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json
%{_datadir}/vulkan/icd.d/mvgal_icd.json
# Config file
%config(noreplace) %{_sysconfdir}/mvgal/mvgal.conf
# Systemd service
%{_unitdir}/mvgal-daemon.service
# D-Bus policy
%config(noreplace) %{_sysconfdir}/dbus-1/system.d/org.mvgal.MVGAL.conf
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
* Tue May 19 2026 AxoGM <creategm10@proton.me> - 0.2.3-2
- Fix RHEL 8 %%cmake macro breakage: cmake-rpm-macros 3.26 from EPEL emits
  "does in-source builds." as literal cmake arguments. Bypass %%cmake on RHEL 8
  and invoke cmake directly with equivalent flags.
- Keep %%cmake for all other chroots (Fedora, RHEL 9+, openSUSE, etc.)
- Retain RHEL 8 LTO workaround (sed strip -flto from cmake build files)

* Tue May 19 2026 AxoGM <creategm10@proton.me> - 0.2.3-1
- Updated to version 0.2.3
- Fix SYCL backend: use correct mvgal_gpu_descriptor_t/mvgal_gpu_get_descriptor API
- Fix Source0 to use GitHub archive URL
- Enable Rust safety crates (BuildRequires rust cargo)

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-21
- Fix RHEL 8 LTO: strip -flto from cmake-generated build files (flags.make,
  build.make, link.txt, .rsp) via sed after %%cmake configures but before
  %%cmake_build runs.  Fresh mock chroot — no side effects.  This bypasses
  the cmake-rpm-macros Lua %%cmake which injects -flto immune to all other
  override approaches (env CFLAGS, %%_lto_cflags, rm redhat-lto.cmake,
  -D cache variable overrides, -fno-lto compile/link options)

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-20
- Fix RHEL 8 LTO: bypass %%cmake Lua macro entirely — invoke cmake directly
  with CFLAGS="%%{optflags}" only (no %%{_lto_cflags}).  The %%cmake macro on
  RHEL 8 cmake 3.26 EPEL injects -flto in a way immune to all overrides (env,
  cache vars, system file patching).  Direct cmake invocation with optflags on
  RHEL 8 does NOT include -flto (it comes from _lto_cflags which we bypass).
  On Fedora 44+, optflags include -flto=auto which modern GCC handles fine.

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-19
- Fix RHEL 8 LTO: bypass %%cmake Lua macro entirely (cmake-rpm-macros on
  RHEL 8 cmake 3.26 from EPEL injects -flto immune to all normal overrides);
  invoke cmake directly with clean CFLAGS (optflags only, no _lto_cflags)
- Extract common cmake args into %%mvgal_cmake_args macro shared by both
  RHEL 8 and non-RHEL build paths

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-18
- Fix RHEL 8 LTO: build libmvgal_core.a with -fno-lto via GCC version check
  (GCC < 9) in src/userspace/CMakeLists.txt so static archive contains no LTO
  bytecode — GCC 8's LTO plugin cannot misinterpret regular bytecode.
  Remove all spec-level LTO workarounds (rhel_cmake_no_lto, CFLAGS stripping,
  redhat-lto.cmake removal) that fought the %%cmake macro in vain.

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-17
- Fix RHEL 8 LTO: override CMAKE_C_FLAGS/CXX_FLAGS/C_FLAGS_RELEASE/
  CXX_FLAGS_RELEASE via cmake -D cache vars instead of env CFLAGS sed
  (%%cmake macro assigns CFLAGS directly overriding env overrides)
- Remove redhat-lto.cmake rm and CFLAGS env stripping (both ineffective)

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-16
- Fix RHEL 8 LTO: strip -flto from CFLAGS/CXXFLAGS in %%build section
  via sed before %%cmake, and use set_target_properties(LINK_FLAGS "-fno-lto")
  unconditionally (removes cmake version conditional) — the RPM optflags
  persist -flto in CMAKE_C_FLAGS regardless of redhat-lto.cmake removal,
  and target_link_options on cmake 3.26 places -fno-lto before -flto
  in the link command so GCC picks -flto (last flag wins)

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-15
- Fix RHEL 8 LTO: use LINK_FLAGS property (cmake <3.13) and
  target_link_options (cmake >=3.13) to add -fno-lto at link time
  in add_mvgal_tool(); remove redhat-lto.cmake in %%prep (sed didn't
  match file content); add_mvgal_tool handles compile + link separately
  so LTO is fully disabled for mvgal-config and mvgal-bin targets

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-14
- Fix RHEL 8 LTO: patch redhat-lto.cmake in %%prep to set
  CMAKE_INTERPROCEDURAL_OPTIMIZATION=FALSE (system cmake module overrides
  both per-target properties and cache variables; GCC 8 LTO plugin drops
  static archive symbols regardless of workarounds)

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-13
- Fix RHEL 8 LTO: disable globally via -DCMAKE_INTERPROCEDURAL_OPTIMIZATION:BOOL=OFF
  instead of per-target properties; redhat-lto.cmake on cmake 3.26 overrides
  per-target IPO settings and GCC 8 LTO plugin drops static archive symbols
  regardless of --whole-archive/-fno-lto tricks

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-12
- Fix RHEL 8 LTO: use INTERPROCEDURAL_OPTIMIZATION per-target property
  (cmake 3.9+) in tools/CMakeLists.txt instead of CMAKE_C_FLAGS_RELEASE
  override; target_link_options requires cmake 3.13+ (RHEL 8 ships 3.11)

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-11
- Fix RHEL 8 LTO: use single-line %%define to avoid shell expansion bug
- Override CMAKE_C_FLAGS_RELEASE to strip -flto on RHEL 8 only

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-10
- RHEL 8: override CMAKE_C_FLAGS_RELEASE to strip -flto (RPM _lto_cflags
  macro unreliable on COPR mock); GCC 8 LTO plugin drops static archive symbols
* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-9
- Fix RHEL 8 LTO: disable LTO entirely via %%_lto_cflags on RHEL 8
- GCC 8 / older binutils LTO plugin drops symbols from static archives
  even with --whole-archive or per-target -fno-lto; the RPM-level
  %%_lto_cflags macro is the only reliable way to suppress -flto

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-8
- Fix RHEL 8 LTO linker: add -fno-lto to both compile and link for mvgal tools
- RHEL 8 / GCC 8 LTO plugin drops static archive symbols even with --whole-archive;
  per-target -fno-lto at compile + link suppresses the LTO plugin entirely

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-7
- Fix RHEL 8 LTO: disable per-target -flto for mvgal-config and mvgal-bin
  with -fno-lto override; GCC 8 LTO plugin drops unresolved symbols from
  static archives even with --whole-archive wrapper

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-6
- Fix RHEL 8 LTO linker: use -Wl,--push-state,--whole-archive flags directly
  in target_link_libraries (not target_link_options) so --whole-archive
  is placed right before libmvgal_core.a in the library link order
- Also apply to mvgal-config and mvgal-bin tools

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-5
- Fix source tarball: use --prefix=mvgal-0.2.2/ so %%setup finds source dir
- Enable CLI tools (MVGAL_BUILD_TOOLS=ON) with all binaries in %%files
- Fix RHEL 8 LTO: wrap libmvgal_core with --whole-archive in link step
- Add mvgal-probe, mvgal_amd_external_mem, libmvgal.so*, cmake config to %%files
- Update shell-completion %%files to include bash/zsh completions

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-4
- Fix RHEL 8 LTO linker error: wrap mvgal_core with --whole-archive
  in tools/CMakeLists.txt so older GCC 8.x resolves mvgal_* symbols
  from the static archive during LTO link phase

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-3
- Fix unpackaged files from MVGAL_BUILD_TOOLS=ON: add mvgal-probe,
  mvgal_amd_external_mem, libmvgal.so*, and CMake config files to %%files

* Mon May 18 2026 AxoGM <creategm10@proton.me> - 0.2.2-2
- Enable MVGAL_BUILD_TOOLS=ON to build and package CLI tools
  (mvgal, mvgal-info, mvgal-status, mvgal-bench, mvgal-compat,
  mvgal-config, mvgal-steam-setup)

* Sat May 16 2026 AxoGM <creategm10@proton.me> - 0.2.2-1
- Version bump to 0.2.2
- Add libmvgal_prometheus.a and libmvgal_sycl_backend.a to %%files
  (fix unpackaged files error on COPR)
- Fix Release numbering for 0.2.2

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