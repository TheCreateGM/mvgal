# Copyright 2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit cmake systemd udev

DESCRIPTION="Multi-Vendor GPU Aggregation Layer for Linux"
HOMEPAGE="https://github.com/TheCreateGM/mvgal"
SRC_URI="https://github.com/TheCreateGM/mvgal/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"
IUSE="opencl rust tools"

DEPEND="
	dev-libs/libdrm
	sys-apps/pciutils
	media-libs/vulkan-loader
	media-libs/vulkan-headers
	opencl? (
		virtual/opencl
		dev-util/opencl-headers
	)
"
RDEPEND="${DEPEND}
	sys-auth/polkit
	virtual/udev
"
BDEPEND="
	dev-build/cmake
	dev-build/ninja
	virtual/pkgconfig
	rust? (
		dev-lang/rust
	)
"

src_configure() {
	local mycmakeargs=(
		-DCMAKE_BUILD_TYPE=Release
		-DMVGAL_BUILD_KERNEL=OFF
		-DMVGAL_BUILD_RUNTIME=ON
		-DMVGAL_BUILD_API=ON
		-DMVGAL_BUILD_TOOLS=$(usex tools ON OFF)
		-DMVGAL_BUILD_TESTS=OFF
		-DMVGAL_ENABLE_RUST=$(usex rust ON OFF)
	)

	cmake_src_configure
}

src_install() {
	cmake_src_install

	insinto /etc/mvgal
	doins config/mvgal.conf

	systemd_dounit packaging/rpm/mvgal-daemon.service
	udev_dorules config/99-mvgal.rules

	exeinto /usr/libexec/mvgal
	doexe config/mvgal-pkexec-helper.sh
	doexe scripts/mtt-dkms-installer.sh

	insinto /usr/share/polkit-1/actions
	doins config/org.freedesktop.policykit.mvgal.policy
}

pkg_postinst() {
	udev_reload
}

pkg_postrm() {
	udev_reload
}
