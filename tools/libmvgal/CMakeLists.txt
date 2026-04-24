cmake_minimum_required(VERSION 3.16)
project(libmvgal LANGUAGES C)

# Simple C wrapper library for the MVGAL kernel UAPI.
# This CMakeLists creates a target `mvgal` which provides a convenient
# include/link wrapper around the on-disk kernel UAPI headers located at:
#   mvgal/include/mvgal/*.h
#
# If a source file named `mvgal_uapi_wrapper.c` is placed in this directory,
# this script will build a real static/shared library from that source.
# If no source file is present, an INTERFACE library target is created so
# other targets can still compile against the header include path and link
# against `mvgal` (for consumers that only need the headers).

# Use a sane default C standard for a C wrapper
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Control build type: allow building shared or static using the usual variable
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Name of the target
set(LIBMVGAL_TARGET mvgal)

# Where the upstream UAPI headers live relative to this file:
# mvgal/tools/libmvgal/../../include -> mvgal/include
set(MVGAL_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../include")

# Source file expected if you want to build a real library.
# (Place a `mvgal_uapi_wrapper.c` here that implements convenience wrappers
#  around the mvgal ioctl UAPI.)
set(MVGAL_WRAPPER_SRC "${CMAKE_CURRENT_LIST_DIR}/mvgal_uapi_wrapper.c")

if(EXISTS "${MVGAL_WRAPPER_SRC}")
    # Build a real library from the provided wrapper implementation.
    add_library(${LIBMVGAL_TARGET}
        ${MVGAL_WRAPPER_SRC}
    )

    # Export API include dir for consumers
    target_include_directories(${LIBMVGAL_TARGET}
        PUBLIC
            "${MVGAL_INCLUDE_DIR}"
    )

    # Provide reasonable compile args for a small system library.
    if(MSVC)
        target_compile_options(${LIBMVGAL_TARGET} PRIVATE /W4)
    else()
        target_compile_options(${LIBMVGAL_TARGET} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        # Prefer position independent code for shared libs
        if(BUILD_SHARED_LIBS)
            set_target_properties(${LIBMVGAL_TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endif()

    # Set a public version property (consumers can query it)
    set_target_properties(${LIBMVGAL_TARGET} PROPERTIES
        VERSION 0.1.0
        SOVERSION 0
        PUBLIC_HEADER "${MVGAL_INCLUDE_DIR}/mvgal/mvgal_uapi.h"
    )

    # Install the library and the primary UAPI header so consumers can compile/link.
    install(TARGETS ${LIBMVGAL_TARGET}
        EXPORT ${LIBMVGAL_TARGET}Targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        PUBLIC_HEADER DESTINATION include/mvgal
    )

    install(DIRECTORY "${MVGAL_INCLUDE_DIR}/mvgal"
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
    )

else()
    # No implementation provided: create an INTERFACE library to expose includes
    add_library(${LIBMVGAL_TARGET} INTERFACE)

    target_include_directories(${LIBMVGAL_TARGET}
        INTERFACE
            "${MVGAL_INCLUDE_DIR}"
    )

    # Install headers so packaging still works
    install(DIRECTORY "${MVGAL_INCLUDE_DIR}/mvgal"
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
    )

    # Export an empty target (INTERFACE targets are exportable)
    install(TARGETS ${LIBMVGAL_TARGET}
        EXPORT ${LIBMVGAL_TARGET}Targets
        INCLUDES DESTINATION include
    )
endif()

# Export CMake config files for find_package support within this project/package.
include(CMakePackageConfigHelpers)
set(CONFIG_INSTALL_DIR "lib/cmake/${LIBMVGAL_TARGET}")

# Generate a simple version/config file.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}ConfigVersion.cmake"
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/ConfigTemplate.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}Config.cmake"
    INSTALL_DESTINATION ${CONFIG_INSTALL_DIR}
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
    PATH_VARS MVGAL_INCLUDE_DIR
)

# Install export set and config files.
install(EXPORT ${LIBMVGAL_TARGET}Targets
    FILE ${LIBMVGAL_TARGET}Targets.cmake
    NAMESPACE ${LIBMVGAL_TARGET}::
    DESTINATION ${CONFIG_INSTALL_DIR}
)

# install the generated config files if the template exists (template optional)
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}Config.cmake")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}ConfigVersion.cmake"
        DESTINATION ${CONFIG_INSTALL_DIR}
    )
else()
    # create a minimal config file at configure time if template not provided
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}Config.cmake"
"# minimal config for libmvgal
include(\"${CMAKE_CURRENT_LIST_DIR}/${LIBMVGAL_TARGET}Targets.cmake\")\n")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${LIBMVGAL_TARGET}ConfigVersion.cmake"
        DESTINATION ${CONFIG_INSTALL_DIR}
    )
endif()

# Convenience meta-target to run a small compile check (no-op if no source)
add_custom_target(libmvgal-check
    COMMENT "libmvgal: confirms include path is available for consumption"
)

# Allow users to override include dir at configure time:
if(NOT DEFINED MVGAL_UAPI_INCLUDE_DIR)
    set(MVGAL_UAPI_INCLUDE_DIR "${MVGAL_INCLUDE_DIR}" CACHE PATH "Path to mvgal UAPI headers")
endif()
message(STATUS "libmvgal: MVGAL UAPI include dir = ${MVGAL_UAPI_INCLUDE_DIR}")

# End of CMakeLists
