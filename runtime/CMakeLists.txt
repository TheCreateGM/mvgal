# MVGAL Runtime - CMake Build Configuration
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)
project(mvgal-runtime LANGUAGES C CXX)

# =============================================================================
# Options
# =============================================================================

option(MVGAL_ENABLE_RUST "Enable Rust safety components" ON)
option(MVGAL_ENABLE_ZIG "Enable Zig components" OFF)
option(MVGAL_BUILD_TESTS "Build tests" ON)
option(MVGAL_BUILD_BENCHMARKS "Build benchmarks" OFF)

# =============================================================================
# Dependencies
# =============================================================================

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Required system libraries
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(PCIACCESS REQUIRED IMPORTED_TARGET pciaccess)

# Optional dependencies
pkg_check_modules(LIBNL QUIET IMPORTED_TARGET libnl-3.0)
pkg_check_modules(LIBNL_ROUTE QUIET IMPORTED_TARGET libnl-route-3.0)
pkg_check_modules(JEMALLOC QUIET IMPORTED_TARGET jemalloc)

# =============================================================================
# Compiler Settings
# =============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-unused-command-line-argument)
    endif()

    # Treat warnings as errors in Release builds (DISABLED - fallback for conversion warnings)
    # add_compile_options($<$<CONFIG:Release>:-Werror>)
endif()

# Sanitizers for Debug builds
if(MVGAL_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# =============================================================================
# Daemon Executable
# =============================================================================

# Daemon source files
set(DAEMON_SOURCES
    daemon/main.cpp
    daemon/daemon.cpp
    daemon/scheduler.cpp
    daemon/device_registry.cpp
    daemon/memory_manager.cpp
    daemon/power_manager.cpp
    daemon/metrics_collector.cpp
    daemon/ipc_server.cpp
)

# Daemon headers
set(DAEMON_HEADERS
    daemon/daemon.hpp
    daemon/scheduler.hpp
    daemon/device_registry.hpp
    daemon/memory_manager.hpp
    daemon/power_manager.hpp
    daemon/metrics_collector.hpp
    daemon/ipc_server.hpp
)

# Create daemon executable
add_executable(mvgald ${DAEMON_SOURCES} ${DAEMON_HEADERS})

target_compile_features(mvgald PRIVATE cxx_std_20)

target_link_libraries(mvgald PRIVATE
    Threads::Threads
    PkgConfig::LIBDRM
    PkgConfig::PCIACCESS
    ${CMAKE_DL_LIBS}
)

# Installation
install(TARGETS mvgald
    RUNTIME DESTINATION bin
)

# =============================================================================
# Rust Safety Components
# =============================================================================

if(MVGAL_ENABLE_RUST)
    # Try to find Rust toolchain
    find_program(RUSTC rustc)
    find_program(CARGO cargo)

    if(RUSTC AND CARGO)
        # Configure cargo build
        set(CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/rust-target")

        # Build the safe library
        add_custom_command(
            OUTPUT "${CARGO_TARGET_DIR}/debug/deps/libmvgal_safe.rlib"
            COMMAND cargo build --manifest-path runtime/safe/Cargo.toml --target-dir "${CARGO_TARGET_DIR}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Building Rust safety components"
        )

        # Add custom target for Rust build
        add_custom_target(rust-build ALL
            DEPENDS "${CARGO_TARGET_DIR}/debug/deps/libmvgal_safe.rlib"
        )

        # Link Rust library to daemon (when ready)
        # target_link_libraries(mvgald PRIVATE "${CARGO_TARGET_DIR}/debug/libmvgal_safe.a")
    else()
        message(WARNING "Rust not found, safety components will not be built")
    endif()
endif()

# =============================================================================
# API Library (Future)
# =============================================================================

#add_library(mvgal-api SHARED api/mvgal.cpp)
#target_link_libraries(mvgal-api PRIVATE ${CMAKE_DL_LIBS})

# =============================================================================
# Memory Module
# =============================================================================

# add_subdirectory(memory)

# =============================================================================
# Sync Module
# =============================================================================

# add_subdirectory(sync)

# =============================================================================
# Gaming Integration
# =============================================================================

# add_subdirectory(gaming)

# =============================================================================
# Tools
# =============================================================================

# add_subdirectory(tools)
