Name: mvgal
Version: 0.2.1
Release: 1%{?dist}
Summary: Multi-Vendor GPU Aggregation Layer for Linux

License: GPL-3.0-only
URL: https://github.com/TheCreateGM/mvgal
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
%cmake_install

# The cmake install targets handle most files. We supplement here with
# versioned symlinks for shared libraries so ldconfig sees proper .so.N chains.
#
# Pattern: libfoo.so.MAJOR.MINOR.PATCH  (real file, installed by cmake)
#          libfoo.so.MAJOR              (symlink -> versioned, created here)
#          libfoo.so                    (symlink -> .so.MAJOR, created here)
#
# cmake installs unversioned .so files; we rename to versioned and add symlinks.

LIBDIR=%{buildroot}%{_libdir}
VERSION=%{version}
MAJOR=0

make_versioned() {
    local libname="$1"
    local sofile="${LIBDIR}/${libname}.so"

    if [ ! -f "${sofile}" ]; then
        echo "INFO: ${sofile} not found (optional component), skipping"
        return 0
    fi

    # Rename the unversioned .so to the versioned real file
    mv "${sofile}" "${LIBDIR}/${libname}.so.${VERSION}"

    # Create the major-version symlink (.so.N) - ldconfig will keep this in sync,
    # but providing it explicitly avoids "not a symbolic link" warnings.
    ln -sf "${libname}.so.${VERSION}" "${LIBDIR}/${libname}.so.${MAJOR}"

    # Recreate the unversioned symlink for compile-time linking (-lfoo)
    ln -sf "${libname}.so.${MAJOR}" "${LIBDIR}/${libname}.so"
}

# Vulkan layer - cmake installs it as VK_LAYER_MVGAL.so
make_versioned "VK_LAYER_MVGAL"

# Config - cmake_install may not install this; ensure it exists
if [ ! -f %{buildroot}%{_sysconfdir}/mvgal/mvgal.conf ]; then
    install -D -m 0644 \
        packaging/rpm/mvgal.conf \
        %{buildroot}%{_sysconfdir}/mvgal/mvgal.conf
fi

# Systemd service unit - cmake_install may not install this; ensure it exists
if [ ! -f %{buildroot}%{_unitdir}/mvgal-daemon.service ]; then
    install -D -m 0644 \
        packaging/rpm/mvgal-daemon.service \
        %{buildroot}%{_unitdir}/mvgal-daemon.service
fi

# Daemon binary - cmake installs mvgald to bin; also link from sbin
if [ -f %{buildroot}%{_bindir}/mvgald ] && [ ! -f %{buildroot}%{_sbindir}/mvgal-daemon ]; then
    install -d %{buildroot}%{_sbindir}
    ln -sf %{_bindir}/mvgald %{buildroot}%{_sbindir}/mvgal-daemon
fi

# Log directory
install -d -m 0755 %{buildroot}/var/log/mvgal

# ldconfig drop-in so the dynamic linker finds libmvgal without a full
# ldconfig run being required at install time on systems that use
# /etc/ld.so.conf.d/ fragments.
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
# Static core library (always built)
%{_libdir}/libmvgal_core.a
# Vulkan layer shared library - versioned real file
%{_libdir}/VK_LAYER_MVGAL.so.%{version}
# Major-version symlink (.so.N) - ldconfig keeps this in sync
%{_libdir}/VK_LAYER_MVGAL.so.0
# Unversioned symlink for compile-time linking
%{_libdir}/VK_LAYER_MVGAL.so
# Headers
%{_includedir}/mvgal/
# Daemon binary (installed to bin by cmake, sbin symlink added in %install)
%{_bindir}/mvgald
%{_sbindir}/mvgal-daemon
# Config (preserve user edits on upgrade)
%config(noreplace) %{_sysconfdir}/mvgal/mvgal.conf
# ldconfig fragment
%{_sysconfdir}/ld.so.conf.d/mvgal.conf
# Vulkan layer JSON manifest
%{_datadir}/vulkan/implicit_layer.d/VK_LAYER_MVGAL.json
# Systemd service
%{_unitdir}/mvgal-daemon.service
# Log directory
%dir /var/log/mvgal

%changelog
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
