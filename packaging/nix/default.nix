{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  libdrm,
  libpciaccess,
  systemd,
  vulkan-headers,
  vulkan-loader,
  ocl-icd,
  opencl-headers,
  rustc ? null,
  cargo ? null,
}:

stdenv.mkDerivation {
  pname = "mvgal";
  version = "0.2.2";

  src = lib.cleanSource ../..;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
  ] ++ lib.optionals (rustc != null && cargo != null) [
    rustc
    cargo
  ];

  buildInputs = [
    libdrm
    libpciaccess
    systemd
    vulkan-headers
    vulkan-loader
    ocl-icd
    opencl-headers
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DMVGAL_BUILD_KERNEL=OFF"
    "-DMVGAL_BUILD_RUNTIME=ON"
    "-DMVGAL_BUILD_API=ON"
    "-DMVGAL_BUILD_TOOLS=ON"
    "-DMVGAL_BUILD_TESTS=OFF"
    "-DMVGAL_ENABLE_RUST=${if rustc != null && cargo != null then "ON" else "OFF"}"
  ];

  meta = with lib; {
    description = "Multi-Vendor GPU Aggregation Layer for Linux";
    homepage = "https://github.com/TheCreateGM/mvgal";
    license = licenses.gpl3Only;
    platforms = platforms.linux;
    maintainers = [ ];
  };
}
