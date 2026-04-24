cmake_minimum_required(VERSION 3.16)
project(mvgal_vulkan_layer LANGUAGES C)

# Minimal pass-through Vulkan layer CMake project
# - Builds a shared library implementing a skeleton Vulkan layer
# - Generates a JSON layer manifest and installs it to the Vulkan layer directory
#
# This CMakeLists is intentionally lightweight and self-contained so you can
# configure & build the layer without having to add other files first. If the
# minimal source or manifest files are missing they will be generated during
# configure time so that a developer can immediately build the project.
#
# Install layout:
#   ${CMAKE_INSTALL_LIBDIR} (lib/lib64) - contains the shared object
#   ${CMAKE_INSTALL_DATAROOTDIR}/vulkan/explicit_layer.d - contains the JSON manifest

include(GNUInstallDirs)

set(LIB_NAME MVGAL_vulkan_layer)
set(SOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/mvgal_vulkan_layer.c")
set(MANIFEST_IN "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json.in")
set(MANIFEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/MVGAL_layer.json")

# Allow override of installed dataroot (useful for packaging)
if(NOT DEFINED MVGAL_VULKAN_LAYER_INSTALL_DATAROOT)
    set(MVGAL_VULKAN_LAYER_INSTALL_DATAROOT "${CMAKE_INSTALL_DATAROOTDIR}/vulkan/explicit_layer.d")
endif()

# Find Vulkan if available. Layer can still be configured without Vulkan for packaging.
find_package(Vulkan QUIET)

if(NOT EXISTS "${SOURCE_FILE}")
    file(WRITE "${SOURCE_FILE}" "/* Auto-generated minimal MVGAL Vulkan layer stub
 *
 * This file intentionally contains a very small symbol so the shared object can
 * be built and installed for development. A real implementation should provide
 * the full loader entry points and dispatch logic.
 */
#include <stdint.h>

#ifdef __GNUC__
#define MVGAL_EXPORT __attribute__((visibility(\"default\")))
#else
#define MVGAL_EXPORT
#endif

/* Minimal exported sentinel symbol used for quick validation. */
MVGAL_EXPORT void MVGAL_vulkan_layer_sentinel(void) { (void)0; }
")
    message(STATUS "Created stub source: ${SOURCE_FILE}")
endif()

# Create a default manifest.json.in if missing. This will be configured to embed the
# SO filename produced by this build. A production manifest should include accurate
# layer metadata and proper layer name strings.
if(NOT EXISTS "${MANIFEST_IN}")
    file(WRITE "${MANIFEST_IN}" "{
  \"file\": \"@LIB_FILENAME@\",
  \"layers\": [
    {
      \"name\": \"VK_LAYER_MVGAL_multi_vendor\",
      \"type\": \"GLOBAL\",
      \"library_path\": \"@LIB_FILENAME@\",
      \"api_version\": \"1.2.0\"
    }
  ]
}
")
    message(STATUS "Created default manifest template: ${MANIFEST_IN}")
endif()

# Build the shared library
add_library(${LIB_NAME} SHARED ${SOURCE_FILE})

# If Vulkan found, add include/link info - optional but useful for real layer implementation
if(Vulkan_FOUND)
    target_include_directories(${LIB_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(${LIB_NAME} PRIVATE Vulkan::Vulkan)
else()
    message(WARNING "Vulkan SDK not found at configure time. Building a minimal stub library only.")
endif()

# Compiler options
if(MSVC)
    target_compile_options(${LIB_NAME} PRIVATE /W4)
else()
    target_compile_options(${LIB_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden)
endif()

# Set predictable soname and output name
set_target_properties(${LIB_NAME} PROPERTIES
    OUTPUT_NAME "libMVGAL_vulkan"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Configure the runtime manifest to reference the installed library filename.
# Use the library filename only (loader will search the loader path).
get_target_property(_libfile_name ${LIB_NAME} OUTPUT_NAME)
if(NOT _libfile_name)
    set(_libfile_name "libMVGAL_vulkan")
endif()
# Append platform library suffix if needed
if(CMAKE_SHARED_LIBRARY_SUFFIX)
    set(_libfile_name "${_libfile_name}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

configure_file("${MANIFEST_IN}" "${MANIFEST_OUT}" @ONLY)
file(READ "${MANIFEST_OUT}" _manifest_content)
string(REPLACE "@LIB_FILENAME@" "${_libfile_name}" _manifest_content "${_manifest_content}")
file(WRITE "${MANIFEST_OUT}" "${_manifest_content}")

# Installation
install(TARGETS ${LIB_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install manifest into the explicit layer directory so loader can find it.
install(FILES "${MANIFEST_OUT}"
    DESTINATION "${MVGAL_VULKAN_LAYER_INSTALL_DATAROOT}"
    RENAME "VK_LAYER_MVGAL.json"
)

# Provide a convenience target to print build/install info
add_custom_target(mvgal-info
    COMMAND ${CMAKE_COMMAND} -E echo \"MVGAL Vulkan Layer:\"
    COMMAND ${CMAKE_COMMAND} -E echo \"  Library target: ${LIB_NAME}\"
    COMMAND ${CMAKE_COMMAND} -E echo \"  Shared object will be installed to: \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\"
    COMMAND ${CMAKE_COMMAND} -E echo \"  Manifest will be installed to: \${CMAKE_INSTALL_PREFIX}/${MVGAL_VULKAN_LAYER_INSTALL_DATAROOT}\"
    COMMENT \"MVGAL layer info\"
)

# End of CMakeLists.txt
