Name: mvgal
Version: 0.2.0
Release: 1%{?dist}
Summary: Multi-Vendor GPU Aggregation Layer for Linux

License: GPL-3.0-only
URL: https://github.com/TheCreateGM/mvgal
Source0: mvgal-%{version}.tar.gz

BuildRequires: gcc, gcc-c++, make, cmake >= 3.20, libdrm-devel, systemd-devel

Requires: libdrm, systemd
Requires: vulkan-loader

Obsoletes: mvgallibs <= 0.1.0
Provides: libmvgal = %{version}

Prefix: /usr

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
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DMVGAL_BUILD_KERNEL=OFF \
    -DMVGAL_BUILD_RUNTIME=ON \
    -DMVGAL_BUILD_API=OFF \
    -DMVGAL_BUILD_TOOLS=OFF \
    -DMVGAL_BUILD_TESTS=OFF \
    -DMVGAL_ENABLE_RUST=OFF
make

%install
rm -rf %{buildroot}

# Libs (755 for shared libs, 644 for static)
mkdir -p %{buildroot}%{_libdir}
[ -f build/src/userspace/libmvgal.so ] && install -m 755 build/src/userspace/libmvgal.so %{buildroot}%{_libdir}/
[ -f build/src/userspace/libmvgal_vulkan_layer.so ] && install -m 755 build/src/userspace/libmvgal_vulkan_layer.so %{buildroot}%{_libdir}/
[ -f build/src/userspace/libmvgal_core.a ] && install -m 644 build/src/userspace/libmvgal_core.a %{buildroot}%{_libdir}/

# Headers
mkdir -p %{buildroot}%{_includedir}/mvgal
install -m 644 include/mvgal/mvgal.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_types.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_gpu.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_memory.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_scheduler.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_log.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_config.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_version.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_ipc.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_intercept.h %{buildroot}%{_includedir}/mvgal/

# Daemon
mkdir -p %{buildroot}%{_sbindir}
install -m 755 build/runtime/mvgald %{buildroot}%{_sbindir}/mvgal-daemon

# Config
mkdir -p %{buildroot}%{_sysconfdir}/mvgal
install -m 644 rpm/mvgal.conf %{buildroot}%{_sysconfdir}/mvgal/mvgal.conf

# Vulkan
mkdir -p %{buildroot}%{_datadir}/vulkan/explicit_layer.d
mkdir -p %{buildroot}%{_datadir}/vulkan/icd.d
install -m 644 src/userspace/intercept/vulkan/manifest.json \
    %{buildroot}%{_datadir}/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json

# Systemd
mkdir -p %{buildroot}%{_unitdir}
install -m 644 rpm/mvgal-daemon.service \
    %{buildroot}%{_unitdir}/mvgal-daemon.service

%post
mkdir -p /var/run/mvgal
mkdir -p /var/log/mvgal
/sbin/ldconfig
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload > /dev/null 2>&1 || :
fi

%preun
if [ $1 -eq 0 ] && [ -d /run/systemd/system ]; then
    systemctl stop mvgal-daemon.service > /dev/null 2>&1 || :
fi

%postun
if [ $1 -eq 0 ] && [ -d /run/systemd/system ]; then
    systemctl daemon-reload > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,755)
%license LICENSE
%doc README.md CONTRIBUTING.md
%{_libdir}/libmvgal.so
%{_libdir}/libmvgal_core.a
%{_libdir}/libmvgal_vulkan_layer.so
%{_includedir}/mvgal/
%{_sbindir}/mvgal-daemon
%config(noreplace) %{_sysconfdir}/mvgal/
%{_datadir}/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json
%{_unitdir}/mvgal-daemon.service

%changelog
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
