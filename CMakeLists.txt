cmake_minimum_required(VERSION 3.20)

# Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
# Main CMake configuration file

project(MVGAL
    VERSION 0.2.0
    DESCRIPTION "Multi-Vendor GPU Aggregation Layer for Linux"
    HOMEPAGE_URL "https://github.com/TheCreateGM/mvgal"
    LANGUAGES C CXX
)

# =============================================================================
# Project Configuration
# =============================================================================

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable position-independent code for shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Enable C++ exceptions and RTTI (can be disabled for performance)
option(WITH_EXCEPTIONS "Enable C++ exceptions" ON)
option(WITH_RTTI "Enable C++ RTTI" ON)

if(NOT WITH_EXCEPTIONS)
  add_compile_options(-fno-exceptions)
endif()

if(NOT WITH_RTTI)
  add_compile_options(-fno-rtti)
endif()

# =============================================================================
# Options
# =============================================================================

# Build components
option(WITH_VULKAN "Build Vulkan interception layer" ON)
option(WITH_OPENCL "Build OpenCL interception layer" ON)
option(WITH_CUDA "Build CUDA wrapper (experimental)" OFF)
option(WITH_KERNEL_MODULE "Build kernel module (requires root)" OFF)
option(WITH_DAEMON "Build MVGAL daemon" ON)
option(WITH_TESTS "Build tests" ON)
option(WITH_BENCHMARKS "Build benchmarks" OFF)
option(WITH_DOCS "Build documentation" OFF)
option(WITH_STATIC_LIBS "Build static libraries" OFF)

# Debug and optimization options
option(WITH_DEBUG "Enable debug build" OFF)
option(WITH_ASAN "Enable AddressSanitizer" OFF)
option(WITH_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(WITH_TSAN "Enable ThreadSanitizer" OFF)
option(WITH_CCACHE "Enable ccache for faster rebuilds" ON)

# Installation options
option(WITH_INSTALL "Enable installation" ON)
set(INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix")

# =============================================================================
# Sanitizers
# =============================================================================

if(WITH_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if(WITH_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined)
endif()

if(WITH_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

# =============================================================================
# ccache
# =============================================================================

if(WITH_CCACHE)
  find_program(CCACHE_PROGRAM ccache)
  if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  endif()
endif()

# =============================================================================
# Dependencies
# =============================================================================

# Required dependencies
find_package(Threads REQUIRED)

# Optional dependencies
if(WITH_VULKAN)
  find_package(Vulkan)
  if(NOT Vulkan_FOUND)
    message(WARNING "Vulkan not found, Vulkan layer will not be built")
    set(WITH_VULKAN OFF)
  endif()
endif()

if(WITH_OPENCL)
  find_package(OpenCL)
  if(NOT OpenCL_FOUND)
    message(WARNING "OpenCL not found, OpenCL interception will not be built")
    set(WITH_OPENCL OFF)
  endif()
endif()

if(WITH_CUDA)
  find_package(CUDAToolkit REQUIRED)
  if(NOT CUDAToolkit_FOUND)
    message(WARNING "CUDA not found, CUDA wrapper will not be built")
    set(WITH_CUDA OFF)
  endif()
endif()

# libdrm for DRM interactions
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM QUIET IMPORTED_TARGET libdrm)
if(NOT DRM_FOUND)
  message(FATAL_ERROR "libdrm development package is required. Install libdrm-dev (Debian) or libdrm-devel (Fedora/centos)")
endif()

# libpci for PCI bus access (called pciaccess on Fedora/Arch, libpci on others)
find_package(PkgConfig REQUIRED)
# Try multiple names for libpci
pkg_check_modules(PCI QUIET IMPORTED_TARGET libpci)
if(NOT PCI_FOUND)
  pkg_check_modules(PCI QUIET IMPORTED_TARGET pciaccess)
endif()
if(NOT PCI_FOUND)
  pkg_check_modules(PCI QUIET IMPORTED_TARGET pci)
endif()
if(NOT PCI_FOUND)
  message(WARNING "libpci not found - PCI functionality will be limited")
  set(PCI_IMPORTED_TARGET "")
endif()

# libudev for device detection
pkg_check_modules(UDEV QUIET IMPORTED_TARGET libudev)
if(NOT UDEV_FOUND)
  pkg_check_modules(UDEV QUIET IMPORTED_TARGET udev)
endif()
if(NOT UDEV_FOUND)
  message(WARNING "libudev not found - device detection may be limited")
  set(UDEV_IMPORTED_TARGET "")
endif()

# =============================================================================
# Compiler Flags
# =============================================================================

# Common compiler flags
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wformat=2
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-unused-command-line-argument)
endif()

# Debug build flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR WITH_DEBUG)
  add_compile_options(
        -g3
        -O0
        -DDEBUG
        -D_GLIBCXX_DEBUG
    )
  add_definitions(-DDEBUG=1)
else()
  add_compile_options(
        -O3
        -DNDEBUG
    )
endif()

# Release build flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(
        -O3
        -DNDEBUG
        -flto
    )
  add_link_options(-flto)
endif()

# Platform-specific flags
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_compile_options(
        -D_GNU_SOURCE
        -D_FILE_OFFSET_BITS=64
    )
endif()

# =============================================================================
# Version Info
# =============================================================================

# Generate version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/mvgal/mvgal_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/mvgal/mvgal_version.h"
)

# =============================================================================
# Include Directories
# =============================================================================

set(MVGAL_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/mvgal"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/include/mvgal"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)

if(Vulkan_FOUND)
  list(APPEND MVGAL_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS})
endif()

if(OpenCL_FOUND)
  list(APPEND MVGAL_INCLUDE_DIRS ${OpenCL_INCLUDE_DIRS})
endif()

# =============================================================================
# Source Files
# =============================================================================

# Public API headers
set(MVGAL_PUBLIC_HEADERS
    include/mvgal/mvgal.h
    include/mvgal/mvgal_types.h
    include/mvgal/mvgal_gpu.h
    include/mvgal/mvgal_memory.h
    include/mvgal/mvgal_scheduler.h
    include/mvgal/mvgal_log.h
    include/mvgal/mvgal_config.h
    include/mvgal/mvgal_ipc.h
)

# Core library sources
set(MVGAL_CORE_SOURCES
    src/userspace/api/mvgal_api.c
    src/userspace/api/mvgal_log.c
    src/userspace/daemon/gpu_manager.c
    src/userspace/daemon/config.c
    src/userspace/daemon/ipc.c
    src/userspace/daemon/main.c
    src/userspace/memory/memory.c
    src/userspace/memory/dmabuf.c
    src/userspace/memory/allocator.c
    src/userspace/memory/sync.c
    src/userspace/scheduler/scheduler.c
    src/userspace/scheduler/load_balancer.c
    src/userspace/scheduler/strategy/afr.c
    src/userspace/scheduler/strategy/sfr.c
    src/userspace/scheduler/strategy/task.c
    src/userspace/scheduler/strategy/compute_offload.c
    src/userspace/scheduler/strategy/hybrid.c
)

# =============================================================================
# Subdirectories
# =============================================================================

# Add source subdirectories
add_subdirectory(src/userspace)

# Kernel module (optional)
if(WITH_KERNEL_MODULE)
  add_subdirectory(src/kernel)
endif()

# Tests
if(WITH_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/tests/CMakeLists.txt")
  enable_testing()
  add_subdirectory(test/tests)
endif()

# Benchmarks
if(WITH_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
  add_subdirectory(benchmarks)
endif()

# Documentation
if(WITH_DOCS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/CMakeLists.txt")
  add_subdirectory(docs)
endif()

# Packaging
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/CMakeLists.txt")
  add_subdirectory(packaging)
endif()

# =============================================================================
# Install Targets
# =============================================================================

# Install headers
install(
    DIRECTORY include/mvgal
    DESTINATION include
    COMPONENT development
)

# Install config files
install(
    DIRECTORY etc
    DESTINATION etc/mvgal
    COMPONENT runtime
)

# Install scripts
install(
    DIRECTORY scripts
    DESTINATION share/mvgal
    COMPONENT runtime
    PATTERN ".gitignore" EXCLUDE
)

# =============================================================================
# Export Targets
# =============================================================================

# Export for use by other projects (commented out - no cmake config files exist)
# install(EXPORT MVGALTargets
#     FILE MVGALConfig.cmake
#     NAMESPACE MVGAL::
#     DESTINATION lib/cmake/MVGAL
# )

# Generate package config (only if cmake directory exists)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MVGALConfig.cmake.in")
  include(CMakePackageConfigHelpers)
  configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MVGALConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/MVGALConfig.cmake"
        INSTALL_DESTINATION lib/cmake/MVGAL
    )
endif()

# write_basic_package_version_file is not available in all CMake versions
# if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
#     write_basic_package_version_file(
#         "${CMAKE_CURRENT_BINARY_DIR}/MVGALConfigVersion.cmake"
#         VERSION ${PROJECT_VERSION}
#         COMPATIBILITY SameMajorVersion
#     )
# endif()

# Install cmake config files (only if they were generated)
# install(
#     FILES
#         "${CMAKE_CURRENT_BINARY_DIR}/MVGALConfig.cmake"
#         "${CMAKE_CURRENT_BINARY_DIR}/MVGALConfigVersion.cmake"
#     DESTINATION lib/cmake/MVGAL
#     COMPONENT development
# )

# =============================================================================
# CPack Configuration
# =============================================================================

include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "AxoGM")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multi-Vendor GPU Aggregation Layer for Linux")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_CONTACT "creategm10@proton.me")

# Generate package
include(CPack)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "MVGAL Configuration Summary")
message(STATUS "========================================")
message(STATUS "Project Version: ${PROJECT_VERSION}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  Vulkan Layer: ${WITH_VULKAN}")
message(STATUS "  OpenCL Layer: ${WITH_OPENCL}")
message(STATUS "  CUDA Wrapper: ${WITH_CUDA}")
message(STATUS "  Kernel Module: ${WITH_KERNEL_MODULE}")
message(STATUS "  Daemon: ${WITH_DAEMON}")
message(STATUS "  Tests: ${WITH_TESTS}")
message(STATUS "  Benchmarks: ${WITH_BENCHMARKS}")
message(STATUS "  Documentation: ${WITH_DOCS}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  Vulkan: ${Vulkan_FOUND}")
message(STATUS "  OpenCL: ${OpenCL_FOUND}")
message(STATUS "  CUDA: ${CUDAToolkit_FOUND}")
message(STATUS "  libdrm: ${DRM_FOUND}")
message(STATUS "  libpci: ${PCI_FOUND}")
message(STATUS "  libudev: ${UDEV_FOUND}")
message(STATUS "")
message(STATUS "========================================")
