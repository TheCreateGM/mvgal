# SPDX-License-Identifier: MIT
#
# MVGAL Monitoring Components
# ============================
# Prometheus metrics endpoint and other monitoring infrastructure.

add_library(mvgal_prometheus STATIC
    mvgal_prometheus.c
)

target_include_directories(mvgal_prometheus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${MVGAL_INCLUDE_DIRS}
)

target_link_libraries(mvgal_prometheus
    PUBLIC
        mvgal_core  # core MVGAL userspace library
)

# Install
install(TARGETS mvgal_prometheus
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT development
)
