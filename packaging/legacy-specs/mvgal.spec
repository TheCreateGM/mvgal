Name:           mvgal
Version:        0.2.1
Release:        1%{?dist}
Summary:        Multi-Vendor GPU Aggregation Layer for Linux

License:        GPLv3
URL:            https://github.com/TheCreateGM/mvgal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc, gcc-c++, make, cmake >= 3.20
BuildRequires:  libdrm-devel, libpciaccess-devel, systemd-devel
BuildRequires:  vulkan-devel

Requires:      libdrm, libpciaccess, systemd
Requires:      vulkan-loader

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
cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_VULKAN=ON -DWITH_OPENCL=OFF \
    -DWITH_DAEMON=ON -DWITH_TESTS=OFF -DWITH_BENCHMARKS=OFF -DWITH_DOCS=OFF \
    -DWITH_KERNEL_MODULE=OFF -DWITH_CUDA=OFF -DMVGAL_BUILD_API=ON
make -j%{?_smp_ncpus:%{_smp_ncpus}}%{!?_smp_ncpus:1}

%install
rm -rf %{buildroot}

# Libs
mkdir -p %{buildroot}%{_libdir}
# Core library (static)
[ -f build/src/userspace/libmvgal_core.a ] && install -m 644 build/src/userspace/libmvgal_core.a %{buildroot}%{_libdir}/ || true
# Vulkan layer (shared)
[ -f build/src/userspace/libVK_LAYER_MVGAL.so ] && install -m 644 build/src/userspace/libVK_LAYER_MVGAL.so %{buildroot}%{_libdir}/ || true
# D3D wrapper (shared)
[ -f build/src/userspace/libmvgal_d3d.so ] && install -m 644 build/src/userspace/libmvgal_d3d.so %{buildroot}%{_libdir}/ || true
# Metal wrapper (shared)
[ -f build/src/userspace/libmvgal_metal.so ] && install -m 644 build/src/userspace/libmvgal_metal.so %{buildroot}%{_libdir}/ || true
# WebGPU wrapper (shared)
[ -f build/src/userspace/libmvgal_webgpu.so ] && install -m 644 build/src/userspace/libmvgal_webgpu.so %{buildroot}%{_libdir}/ || true

# Headers
mkdir -p %{buildroot}%{_includedir}/mvgal
for hdr in include/mvgal/*.h; do
    [ -f "$hdr" ] && install -m 644 "$hdr" %{buildroot}%{_includedir}/mvgal/ || true
done

# Daemon (from runtime)
mkdir -p %{buildroot}%{_sbindir}
[ -f build/runtime/mvgald ] && install -m 755 build/runtime/mvgald %{buildroot}%{_sbindir}/mvgald || true

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
[ -f packaging/rpm/mvgal-daemon.service ] && install -m 644 packaging/rpm/mvgal-daemon.service %{buildroot}%{_unitdir}/mvgal-daemon.service || true

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
%{_libdir}/libmvgal_core.a
%{_libdir}/libVK_LAYER_MVGAL.so
%{_libdir}/libmvgal_d3d.so
%{_libdir}/libmvgal_metal.so
%{_libdir}/libmvgal_webgpu.so
%{_includedir}/mvgal/
%{_sbindir}/mvgald
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
