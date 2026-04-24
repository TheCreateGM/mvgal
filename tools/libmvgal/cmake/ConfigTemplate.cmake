@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# Minimal package config for libmvgal generated during configure time.
# This template is configured to set the include directory variable
# MVGAL_INCLUDE_DIR and create an imported target `mvgal` that provides
# the INTERFACE include path for consumers.
#
# Variables substituted by configure_file:
#   @MVGAL_INCLUDE_DIR@ - the path to the mvgal include directory.

if(NOT DEFINED MVGAL_INCLUDE_DIR)
    set(MVGAL_INCLUDE_DIR "@MVGAL_INCLUDE_DIR@" CACHE PATH "Path to mvgal UAPI headers")
endif()

# Provide an imported target for consumers of the package.
# If the real library is available in the installation tree, package
# installation should add an actual library target and override this.
if(NOT TARGET mvgal)
    add_library(mvgal UNKNOWN IMPORTED)
    set_target_properties(mvgal PROPERTIES
        IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libmvgal.so"
        INTERFACE_INCLUDE_DIRECTORIES "${MVGAL_INCLUDE_DIR}"
    )
endif()

# Optional: provide a convenient alias for imported targets in older CMake versions
if(NOT TARGET mvgal::mvgal)
    add_library(mvgal::mvgal ALIAS mvgal)
endif()

# Export package variables for consumers
set(MVGAL_PACKAGE_VERSION "@PACKAGE_VERSION@" CACHE STRING "MVGAL package version")
set(MVGAL_INSTALL_INCLUDEDIR "${MVGAL_INCLUDE_DIR}" CACHE PATH "MVGAL include directory")

# End of config template
