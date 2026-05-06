# Multi-Vendor GPU Aggregation Layer (MVGAL)
# Root CMake Configuration
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)
project(mvgal VERSION 0.2.1 LANGUAGES C CXX)

# =============================================================================
# Project Metadata
# =============================================================================

set(PROJECT_DESCRIPTION "Multi-Vendor GPU Aggregation Layer for Linux")
set(PROJECT_HOMEPAGE "https://github.com/mvgal/mvgal")
set(PROJECT_LICENSE "MIT")

# =============================================================================
# Global Options
# =============================================================================

option(MVGAL_BUILD_KERNEL "Build kernel module" ON)
option(MVGAL_BUILD_RUNTIME "Build runtime daemon and libraries" ON)
option(MVGAL_BUILD_API "Build API layers (OpenGL, OpenCL, CUDA, SYCL, Vulkan)" OFF)
option(MVGAL_BUILD_GAMING "Build gaming integration (Wine, DXVK, Proton)" OFF)
option(MVGAL_BUILD_TOOLS "Build tools and utilities" ON)
option(MVGAL_ENABLE_RUST "Enable Rust safety components" ON)
option(MVGAL_ENABLE_ZIG "Enable Zig components" OFF)
option(MVGAL_BUILD_TESTS "Build tests" ON)
option(MVGAL_ENABLE_COVERAGE "Enable code coverage" OFF)
option(MVGAL_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers" OFF)
option(MVGAL_USE_CCACHE "Use ccache for faster rebuilds" ON)
option(MVGAL_INSTALL "Enable installation" ON)

# Installation paths
include(GNUInstallDirs)
# Use GNUInstallDirs defaults, allow override via -DCMAKE_INSTALL_*DIR=...
if(NOT CMAKE_INSTALL_INCLUDEDIR)
  set(CMAKE_INSTALL_INCLUDEDIR include CACHE PATH "Include directory")
endif()
if(NOT CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR lib CACHE PATH "Library directory")
endif()
if(NOT CMAKE_INSTALL_BINDIR)
  set(CMAKE_INSTALL_BINDIR bin CACHE PATH "Binary directory")
endif()
if(NOT CMAKE_INSTALL_DOCDIR)
  set(CMAKE_INSTALL_DOCDIR share/doc/mvgal CACHE PATH "Documentation directory")
endif()
if(NOT CMAKE_INSTALL_MANDIR)
  set(CMAKE_INSTALL_MANDIR share/man CACHE PATH "Man pages directory")
endif()

# =============================================================================
# Dependencies
# =============================================================================

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Required system libraries
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(PCIACCESS REQUIRED IMPORTED_TARGET pciaccess)

# Optional dependencies
pkg_check_modules(LIBUDEV QUIET IMPORTED_TARGET libudev udev)
pkg_check_modules(OPENCL QUIET IMPORTED_TARGET OpenCL)
pkg_check_modules(LIBNL QUIET IMPORTED_TARGET libnl-3.0)
pkg_check_modules(JEMALLOC QUIET IMPORTED_TARGET jemalloc)

# Find Vulkan - try pkg-config first, then CMake's find_package
pkg_check_modules(VULKAN QUIET vulkan)
if(VULKAN_FOUND)
    # pkg-config found vulkan, variables are VULKAN_INCLUDE_DIRS, VULKAN_LINK_LIBRARIES
    message(STATUS "Vulkan found via pkg-config: ${VULKAN_INCLUDE_DIRS}")
else()
    # Try CMake's find_package (finds Vulkan headers and loader)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        # Set VULKAN_FOUND so the rest of the codebase knows Vulkan is available
        set(VULKAN_FOUND TRUE)
        # find_package sets Vulkan_INCLUDE_DIR (singular) and Vulkan::Vulkan target
        # Convert to the variable names used by the rest of the codebase
        if(Vulkan_INCLUDE_DIR)
            set(VULKAN_INCLUDE_DIRS ${Vulkan_INCLUDE_DIR})
        elseif(Vulkan_INCLUDE_DIRS)
            set(VULKAN_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS})
        endif()
        if(Vulkan_LIBRARY)
            set(VULKAN_LINK_LIBRARIES ${Vulkan_LIBRARY})
        elseif(Vulkan_LIBRARIES)
            set(VULKAN_LINK_LIBRARIES ${Vulkan_LIBRARIES})
        endif()
        message(STATUS "Vulkan found via find_package: ${VULKAN_INCLUDE_DIRS}")
    else()
        message(WARNING "Vulkan not found - Vulkan ICD and layer will not be built")
    endif()
endif()

# Find OpenCL (alternative method)
if(NOT OPENCL_FOUND)
    find_package(OpenCL QUIET)
endif()

# CUDA
if(MVGAL_BUILD_API)
    find_package(CUDAToolkit QUIET)
endif()

# Rust
if(MVGAL_ENABLE_RUST)
    find_program(RUSTC rustc)
    find_program(CARGO cargo)
    if(RUSTC AND CARGO)
        set(RUST_FOUND TRUE)
    endif()
endif()

# Zig
if(MVGAL_ENABLE_ZIG)
    find_program(ZIG zig)
endif()

# ccache
if(MVGAL_USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    endif()
endif()

# =============================================================================
# Global Compiler Settings
# =============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Position Independent Code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Use modern CMake practices
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable coloring
if(NOT DEFINED ENV{CMAKE_NO_COLOR})
    set(CMAKE_COLOR_MAKEFILE ON)
endif()

# =============================================================================
# Compiler Warnings and Optimization
# =============================================================================

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()

    # Treat warnings as errors for Release builds (DISABLED - fallback for conversion warnings)
    # add_compile_options($<$<CONFIG:Release>:-Werror>)

    # Debug/Release specific
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g3 -O0 -DDEBUG -D_GLIBCXX_DEBUG)
        add_definitions(-DDEBUG=1)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG -flto)
        add_link_options(-flto)
    endif()

    # Platform-specific
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_compile_options(-D_GNU_SOURCE -D_FILE_OFFSET_BITS=64)
    endif()

    # Sanitizers
    if(MVGAL_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()

    # Coverage
    if(MVGAL_ENABLE_COVERAGE)
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage -fprofile-arcs)
    endif()
endif()

# MSVC specific settings
if(MSVC)
    add_compile_options(/W4 /WX /sdl)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Od /Zi)
    else()
        add_compile_options(/O2)
    endif()
endif()

# =============================================================================
# Version Configuration
# =============================================================================

# Generate version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/mvgal_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/kernel/mvgal_version.h"
)

# =============================================================================
# Include Directories
# =============================================================================

set(MVGAL_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/kernel"
)

if(VULKAN_FOUND)
    list(APPEND MVGAL_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS})
endif()

if(OPENCL_FOUND)
    list(APPEND MVGAL_INCLUDE_DIRS ${OpenCL_INCLUDE_DIRS})
endif()

if(PCIACCESS_FOUND)
    list(APPEND MVGAL_INCLUDE_DIRS ${PCIACCESS_INCLUDE_DIRS})
endif()

# =============================================================================
# Subdirectories
# =============================================================================

# Kernel module (requires root for installation)
if(MVGAL_BUILD_KERNEL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kernel/CMakeLists.txt")
    add_subdirectory(kernel)
endif()

# Runtime daemon and libraries
if(MVGAL_BUILD_RUNTIME AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/CMakeLists.txt")
    add_subdirectory(runtime)
endif()

# API layers (OpenGL, OpenCL, CUDA, SYCL, Vulkan)
if(MVGAL_BUILD_API AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/userspace/CMakeLists.txt")
    # Propagate OpenCL_FOUND to subdirectory
    set(OpenCL_FOUND ${OPENCL_FOUND})
    add_subdirectory(src/userspace)
endif()

# Vulkan ICD (Phase 5 - Virtual VkPhysicalDevice)
if(MVGAL_BUILD_API AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/userspace/vulkan_icd/CMakeLists.txt")
    add_subdirectory(src/userspace/vulkan_icd)
endif()

# Memory management module
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/memory/CMakeLists.txt")
    add_subdirectory(memory)
endif()

# Synchronization module
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sync/CMakeLists.txt")
    add_subdirectory(sync)
endif()

# Gaming integration
if(MVGAL_BUILD_GAMING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gaming/CMakeLists.txt")
    add_subdirectory(gaming)
endif()

# Steam integration
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/steam/CMakeLists.txt")
    add_subdirectory(steam)
endif()

# OpenGL translation layer
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/opengl/CMakeLists.txt")
    add_subdirectory(opengl)
endif()

# UI dashboard (optional)
option(MVGAL_BUILD_UI "Build UI dashboard" OFF)
if(MVGAL_BUILD_UI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui/CMakeLists.txt")
    add_subdirectory(ui)
endif()

# Compatibility layer
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/compat/CMakeLists.txt")
    add_subdirectory(compat)
endif()

# Bindings (Python, Java, Go, .NET)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bindings/CMakeLists.txt")
    add_subdirectory(bindings)
endif()

# Tools and utilities
if(MVGAL_BUILD_TOOLS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()

# Packaging
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/CMakeLists.txt")
    add_subdirectory(packaging)
endif()

# Tests
if(MVGAL_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(src/tests/tests)
endif()

# Documentation
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/CMakeLists.txt")
    add_subdirectory(docs)
endif()

# =============================================================================
# Installation
# =============================================================================

# Install public headers
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT development)

# Install documentation
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT docs)
endif()

# Install man pages
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/man")
    install(DIRECTORY man/ DESTINATION ${CMAKE_INSTALL_MANDIR} COMPONENT docs)
endif()

# =============================================================================
# CPack Packaging
# =============================================================================

include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_NAME "mvgal")
set(CPACK_PACKAGE_VENDOR "MVGAL Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE}")

include(CPack)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "MVGAL Configuration Summary")
message(STATUS "================================")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  Kernel Module: ${MVGAL_BUILD_KERNEL} (${LIBDRM_FOUND} ${LIBPCI_FOUND})")
message(STATUS "  Runtime Daemon: ${MVGAL_BUILD_RUNTIME}")
message(STATUS "  API Layers: ${MVGAL_BUILD_API}")
message(STATUS "  Gaming Integration: ${MVGAL_BUILD_GAMING}")
message(STATUS "  Tools: ${MVGAL_BUILD_TOOLS}")
message(STATUS "  Tests: ${MVGAL_BUILD_TESTS}")
message(STATUS "")
message(STATUS "Rust Support:")
message(STATUS "  Rust: ${RUST_FOUND} (${RUSTC} ${CARGO})")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  libdrm: ${LIBDRM_FOUND}")
message(STATUS "  libpci: ${PCIACCESS_FOUND}")
message(STATUS "  libudev: ${LIBUDEV_FOUND}")
message(STATUS "  Vulkan: ${VULKAN_FOUND}")
message(STATUS "  OpenCL: ${OPENCL_FOUND}")
message(STATUS "")
