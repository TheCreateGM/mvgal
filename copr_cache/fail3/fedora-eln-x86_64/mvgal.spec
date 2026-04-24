Name:           mvgal
Version:        0.2.0
Release:        1%{?dist}
Summary:        Multi-Vendor GPU Aggregation Layer for Linux

License:        GPL-3.0-only
URL:            https://github.com/TheCreateGM/mvgal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc, gcc-c++, make, cmake
BuildRequires:  libdrm-devel, libpciaccess-devel, systemd-devel, libudev-devel
BuildRequires:  vulkan-devel, opencl-headers, ocl-icd-devel

Requires:       libdrm, libpciaccess, systemd, libudev
Requires:       vulkan-loader, ocl-icd

Obsoletes:      mvgallibs <= 0.1.0
Provides:       libmvgal = %{version}

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
%cmake -DWITH_VULKAN=ON -DWITH_OPENCL=ON -DWITH_DAEMON=ON \
    -DWITH_TESTS=OFF -DWITH_BENCHMARKS=OFF -DWITH_DOCS=OFF \
    -DWITH_KERNEL_MODULE=OFF -DWITH_CUDA=OFF
%cmake_build

%install
%cmake_install

# Install udev rules if exists
mkdir -p %{buildroot}%{_udevrulesdir}
install -m 644 config/99-mvgal.rules %{buildroot}%{_udevrulesdir}/99-mvgal.rules || true

# Install systemd service
mkdir -p %{buildroot}%{_unitdir}
install -m 644 rpm/mvgal-daemon.service %{buildroot}%{_unitdir}/mvgal-daemon.service || true

# Install config file
mkdir -p %{buildroot}%{_sysconfdir}/mvgal
install -m 644 rpm/mvgal.conf %{buildroot}%{_sysconfdir}/mvgal/mvgal.conf || true

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
%{_datadir}/vulkan/explicit_layer.d/VK_LAYER_MVGAL.json
%{_unitdir}/mvgal-daemon.service

%changelog
* Wed Apr 22 2026 AxoGM <creategm10@proton.me> - 0.2.0-1
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