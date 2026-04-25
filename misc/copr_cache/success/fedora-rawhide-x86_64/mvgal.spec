Name:           mvgal
Version:        0.2.0
Release:        1%{?dist}
Summary:        Multi-Vendor GPU Aggregation Layer for Linux

License:        GPLv3
URL:            https://github.com/TheCreateGM/mvgal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc, gcc-c++, make, cmake >= 3.20
BuildRequires:  libdrm-devel, libpciaccess-devel, systemd-devel
BuildRequires:  vulkan-devel, opencl-headers, ocl-icd-devel

Requires:      libdrm, libpciaccess, systemd, systemd-libs
Requires:      vulkan-loader, ocl-icd

Prefix:        /usr

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
cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_VULKAN=ON -DWITH_OPENCL=ON \
    -DWITH_DAEMON=ON -DWITH_TESTS=OFF -DWITH_BENCHMARKS=OFF -DWITH_DOCS=OFF
make -j%{?_smp_ncpus:%{_smp_ncpus}}%{!?_smp_ncpus:1}

%install
rm -rf %{buildroot}

# Libs
mkdir -p %{buildroot}%{_libdir}
for lib in build/src/userspace/libmvgal.so* build/src/userspace/libmvgal_core.a build/src/userspace/libVK_LAYER_MVGAL.so build/src/userspace/libmvgal_opencl.so; do
    [ -f "$lib" ] && install -m 644 "$lib" %{buildroot}%{_libdir}/ || true
done

# Headers
mkdir -p %{buildroot}%{_includedir}/mvgal
install -m 644 include/mvgal/mvgal.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_types.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_gpu.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_memory.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_scheduler.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_log.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_config.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_ipc.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_execution.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_version.h %{buildroot}%{_includedir}/mvgal/
install -m 644 include/mvgal/mvgal_intercept.h %{buildroot}%{_includedir}/mvgal/

# Daemon
mkdir -p %{buildroot}%{_sbindir}
install -m 755 build/src/userspace/mvgal-daemon %{buildroot}%{_sbindir}/

# Config
mkdir -p %{buildroot}%{_sysconfdir}/mvgal
mkdir -p %{buildroot}%{_udevrulesdir}
install -m 644 config/mvgal.conf %{buildroot}%{_sysconfdir}/mvgal/mvgal.conf
install -m 644 config/99-mvgal.rules %{buildroot}%{_udevrulesdir}/99-mvgal.rules

# Vulkan
mkdir -p %{buildroot}%{_datadir}/vulkan/explicit_layer.d
mkdir -p %{buildroot}%{_datadir}/vulkan/icd.d
install -m 644 build/src/userspace/VK_LAYER_MVGAL.json \
    %{buildroot}%{_datadir}/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json

# Systemd
mkdir -p %{buildroot}%{_unitdir}
install -m 644 pkg/systemd/mvgal-dbus.service \
    %{buildroot}%{_unitdir}/mvgal-daemon.service

%post
# Create runtime directory
mkdir -p /var/run/mvgal
chmod 755 /var/run/mvgal

# Create log directory
mkdir -p /var/log/mvgal
chmod 755 /var/log/mvgal

# Update library cache
/sbin/ldconfig

# Reload systemd
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload > /dev/null 2>&1 || :
fi

%preun
if [ -d /run/systemd/system ]; then
    systemctl stop mvgal-daemon.service > /dev/null 2>&1 || :
fi

%postun
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload > /dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%{_libdir}/libmvgal.so*
%{_libdir}/libmvgal_core.a
%{_libdir}/libVK_LAYER_MVGAL.so
%{_libdir}/libmvgal_opencl.so
%{_includedir}/mvgal/*.h
%{_sbindir}/mvgal-daemon
%{_sysconfdir}/mvgal/
%{_udevrulesdir}/99-mvgal.rules
%{_datadir}/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json
%{_unitdir}/mvgal-daemon.service

%changelog
* Mon Apr 20 2026 AxoGM <creategm10@proton.me> - 0.1.0-1
- Initial release
- GPU detection, scheduler, memory manager
- Vulkan layer, CUDA wrapper
- Daemon for background GPU management
