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

Requires:      libdrm, libpciaccess, systemd
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
    -DWITH_DAEMON=ON -DWITH_TESTS=OFF -DWITH_BENCHMARKS=OFF -DWITH_DOCS=OFF \
    -DWITH_KERNEL_MODULE=OFF -DWITH_CUDA=OFF
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
for hdr in include/mvgal/*.h; do
    [ -f "$hdr" ] && install -m 644 "$hdr" %{buildroot}%{_includedir}/mvgal/ || true
done

# Daemon
mkdir -p %{buildroot}%{_sbindir}
[ -f build/src/userspace/mvgal-daemon ] && install -m 755 build/src/userspace/mvgal-daemon %{buildroot}%{_sbindir}/ || true

# Config
mkdir -p %{buildroot}%{_sysconfdir}/mvgal
[ -f config/mvgal.conf ] && install -m 644 config/mvgal.conf %{buildroot}%{_sysconfdir}/mvgal/mvgal.conf || true

# udev rules
mkdir -p %{buildroot}%{_udevrulesdir}
[ -f config/99-mvgal.rules ] && install -m 644 config/99-mvgal.rules %{buildroot}%{_udevrulesdir}/99-mvgal.rules || true

# Vulkan layer manifest
mkdir -p %{buildroot}%{_datadir}/vulkan/explicit_layer.d
for manifest in build/src/userspace/VK_LAYER_MVGAL.json build/src/userspace/vulkan/VK_LAYER_MVGAL.json; do
    [ -f "$manifest" ] && install -m 644 "$manifest" %{buildroot}%{_datadir}/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json && break || true
done

# Systemd service
mkdir -p %{buildroot}%{_unitdir}
[ -f src/pkg/systemd/mvgal-dbus.service ] && install -m 644 src/pkg/systemd/mvgal-dbus.service %{buildroot}%{_unitdir}/mvgal-daemon.service || true

%post
mkdir -p /var/run/mvgal
chmod 755 /var/run/mvgal 2>/dev/null || true
mkdir -p /var/log/mvgal
chmod 755 /var/log/mvgal 2>/dev/null || true
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
%defattr(-,root,root,-)
%license LICENSE
%doc README.md CONTRIBUTING.md
%{_libdir}/libmvgal.so*
%{_libdir}/libmvgal_core.a
%{_libdir}/libVK_LAYER_MVGAL.so
%{_libdir}/libmvgal_opencl.so
%{_includedir}/mvgal/
%{_sbindir}/mvgal-daemon
%{_sysconfdir}/mvgal/
%{_udevrulesdir}/99-mvgal.rules
%{_datadir}/vulkan/explicit_layer.d/
%{_unitdir}/mvgal-daemon.service

%changelog
* Thu Apr 23 2026 AxoGM <creategm10@proton.me> - 0.2.0-2
- Fix systemd service path
- Add graceful handling for optional files

* Wed Apr 22 2026 AxoGM <creategm10@proton.me> - 0.2.0-1
- Updated to version 0.2.0 "Health Monitor"