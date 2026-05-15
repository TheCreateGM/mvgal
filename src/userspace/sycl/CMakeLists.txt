# SPDX-License-Identifier: MIT
#
# MVGAL SYCL Backend — CMake Build
# ==================================
# Provides a SYCL 2020 runtime backend over the MVGAL API.
# The backend is compiled as a static convenience library linked
# into the MVGAL userspace library.

add_library(mvgal_sycl_backend STATIC
    mvgal_sycl.c
)

target_include_directories(mvgal_sycl_backend
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${MVGAL_INCLUDE_DIRS}
)

target_link_libraries(mvgal_sycl_backend
    PUBLIC
        mvgal_core  # core MVGAL userspace library
)

# Install
install(TARGETS mvgal_sycl_backend
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT development
)
