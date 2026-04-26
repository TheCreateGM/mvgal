# MVGAL Kernel Module - CMake Build Configuration
# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.16)
project(mvgal-kernel LANGUAGES C)

# =============================================================================
# Kernel Module Configuration
# =============================================================================

# Check if we're building on Linux
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "Kernel module can only be built on Linux")
    return()
endif()

# Find kernel headers
find_path(KERNEL_HEADERS Linux/version.h
    PATHS /usr/src/linux-headers-${CMAKE_SYSTEM_VERSION} /usr/src/linux
)

if(NOT KERNEL_HEADERS)
    message(WARNING "Linux kernel headers not found. Kernel module will not be built.")
    return()
endif()

message(STATUS "Found kernel headers at: ${KERNEL_HEADERS}")

# Get kernel version
file(READ "${KERNEL_HEADERS}/include/generated/utsrelease.h" KERNEL_VERSION_FILE)
string(REGEX MATCH "UTS_RELEASE \"([0-9]+\.[0-9]+\.[0-9]+)\"" KERNEL_VERSION "${KERNEL_VERSION_FILE}")
set(KERNEL_VERSION ${CMAKE_MATCH_1})
message(STATUS "Building for kernel version: ${KERNEL_VERSION}")

# =============================================================================
# Module Sources
# =============================================================================

# Core module files
set(MODULE_SOURCES
    mvgal_core.c
    mvgal_device.c
    mvgal_scheduler.c
    mvgal_memory.c
    mvgal_sync.c
)

# Vendor-specific shims
set(VENDOR_SOURCES
    vendors/mvgal_nvidia.c
    vendors/mvgal_amd.c
    vendors/mvgal_intel.c
    vendors/mvgal_mtt.c
)

# Header files
set(MODULE_HEADERS
    mvgal_core.h
    mvgal_device.h
    mvgal_scheduler.h
    mvgal_memory.h
    mvgal_sync.h
)

set(VENDOR_HEADERS
    vendors/mvgal_nvidia.h
    vendors/mvgal_amd.h
    vendors/mvgal_intel.h
    vendors/mvgal_mtt.h
)

# =============================================================================
# Compiler Settings
# =============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Kernel module specific flags
set(KERNEL_CFLAGS
    -D__KERNEL__
    -DMODULE
    -DKBUILD_MODNAME=\"mvgal\"
    -DKBUILD_BASENAME=\"mvgal\"
    -DLINUX
    -O2
    -Wall
    -Wstrict-prototypes
    -Wno-trigraphs
    -fno-strict-aliasing
    -fno-common
    -fshort-wchar
    -Werror-implicit-function-declaration
    -Wno-format-security
    -Wno-sign-compare
    -Wno-pointer-sign
    -Wno-old-style-declaration
    -Wno-unused-but-set-variable
    -Wno-unused-const-variable
)

# Add warnings as errors
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND KERNEL_CFLAGS -Werror)
endif()

# Include paths
include_directories(
    ${KERNEL_HEADERS}
    ${KERNEL_HEADERS}/arch/x86/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# =============================================================================
# Version Header
# =============================================================================

# Create version header if not exists
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mvgal_version.h"
    COMMAND echo "// Auto-generated version header" > "${CMAKE_CURRENT_BINARY_DIR}/mvgal_version.h"
    COMMAND echo "#define MVGAL_VERSION \"${PROJECT_VERSION}\"" >> "${CMAKE_CURRENT_BINARY_DIR}/mvgal_version.h"
    COMMAND echo "#define MVGAL_KERNEL_VERSION \"${KERNEL_VERSION}\"" >> "${CMAKE_CURRENT_BINARY_DIR}/mvgal_version.h"
    COMMENT "Generating kernel version header"
)

# =============================================================================
# Build the module
# =============================================================================

# For out-of-tree kernel modules, we use a custom approach
# The standard kernel build system doesn't work with CMake easily

# Option 1: Use Kbuild directly via make
option(MVGAL_KERNEL_USE_MAKE "Use make/Kbuild for kernel module" ON)

if(MVGAL_KERNEL_USE_MAKE)
    # Create Makefile symlink to Kbuild
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Makefile"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_CURRENT_SOURCE_DIR}/Kbuild" "${CMAKE_CURRENT_BINARY_DIR}/Makefile"
        COMMENT "Creating Makefile symlink"
    )

    # Build with make
    add_custom_target(mvgal-kernel ALL
        DEPENDS
            "${CMAKE_CURRENT_BINARY_DIR}/Makefile"
            "${CMAKE_CURRENT_BINARY_DIR}/mvgal_version.h"
        COMMAND make -C ${KERNEL_HEADERS} M=${CMAKE_CURRENT_BINARY_DIR} modules
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Building MVGAL kernel module"
    )

    set_target_properties(mvgal-kernel PROPERTIES
        FOLDER "Kernel"
    )
else()
    # Option 2: Try to build with CMake (limited support)
    # This is experimental and may not work for all kernel versions

    add_library(mvgal-module MODULE
        ${MODULE_SOURCES}
        ${VENDOR_SOURCES}
        ${MODULE_HEADERS}
        ${VENDOR_HEADERS}
    )

    target_compile_options(mvgal-module PRIVATE ${KERNEL_CFLAGS})
    target_include_directories(mvgal-module PRIVATE ${KERNEL_HEADERS})

    set_target_properties(mvgal-module PROPERTIES
        PREFIX ""
        SUFFIX ".ko"
        OUTPUT_NAME mvgal
        FOLDER "Kernel"
    )
endif()

# =============================================================================
# Installation
# =============================================================================

if(MVGAL_KERNEL_USE_MAKE)
    # Install the .ko file
    install(CODE "
        file(INSTALL DESTINATION \"${CMAKE_INSTALL_LIBDIR}/modules/${KERNEL_VERSION}/kernel/drivers/gpu/drm\"
            TYPE FILE
            FILES \"${CMAKE_CURRENT_BINARY_DIR}/mvgal.ko\"
        )
    " COMPONENT kernel)

    # Install module firmware (if any)
    install(DIRECTORY firmware/ DESTINATION lib/firmware/mvgal COMPONENT kernel)

    # Install modprobe configuration
    install(FILES mvgal.modprobe.conf DESTINATION etc/modprobe.d RENAME mvgal.conf COMPONENT kernel)

    # Install udev rules
    install(FILES 99-mvgal.rules DESTINATION etc/udev/rules.d COMPONENT kernel)
else()
    install(TARGETS mvgal-module
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/modules/${KERNEL_VERSION}/kernel/drivers/gpu/drm
        COMPONENT kernel
    )
endif()

# Install headers for userspace development
install(
    FILES ${MODULE_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mvgal/kernel
    COMPONENT development
)

# =============================================================================
# Module Loading Scripts
# =============================================================================

# Load script
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/load-module.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/load-module.sh"
    @ONLY
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/load-module.sh"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME mvgal-load
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    COMPONENT kernel
)

# Unload script
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/unload-module.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/unload-module.sh"
    @ONLY
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/unload-module.sh"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME mvgal-unload
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    COMPONENT kernel
)

# =============================================================================
# DKMS Support (Optional)
# =============================================================================

option(MVGAL_DKMS "Build DKMS package" OFF)

if(MVGAL_DKMS)
    find_package(DKMS QUIET)
    if(DKMS_FOUND)
        # Create DKMS configuration
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/dkms/dkms.conf.in"
            "${CMAKE_CURRENT_BINARY_DIR}/dkms.conf"
            @ONLY
        )

        install(
            FILES "${CMAKE_CURRENT_BINARY_DIR}/dkms.conf"
            DESTINATION usr/src/mvgal-${PROJECT_VERSION}/dkms
            COMPONENT dkms
        )

        install(
            DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
            DESTINATION usr/src/mvgal-${PROJECT_VERSION}
            PATTERN ".git" EXCLUDE
            PATTERN "build-*" EXCLUDE
            COMPONENT dkms
        )
    endif()
endif()

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "Kernel Module Configuration:")
message(STATUS "  Kernel headers: ${KERNEL_HEADERS}")
message(STATUS "  Kernel version: ${KERNEL_VERSION}")
message(STATUS "  Module name: mvgal.ko")
message(STATUS "  DKMS support: ${MVGAL_DKMS}")
message(STATUS "")
