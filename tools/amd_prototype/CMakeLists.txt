cmake_minimum_required(VERSION 3.16)
project(mvgal_amd_prototype LANGUAGES C)

# Simple AMD prototype that demonstrates (best-effort) Vulkan external memory
# export/import using dma-buf FDs and semaphore/syncfd primitives.
#
# Expected source file (create alongside this CMakeLists.txt):
#   - amd_prototype.c
#
# This CMakeLists is intentionally conservative:
# - It looks for the Vulkan SDK; if found it links to Vulkan::Vulkan.
# - If Vulkan isn't available at configure time, the target is still generated
#   (so the project can be configured) but a helpful message is printed and
#   compilation will likely fail until the SDK is provided.
#
# Usage:
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build .
#   ./mvgal_amd_external_mem   # run the prototype (may require root / device perms)

option(BUILD_MVGAL_AMD_PROTOTYPE "Build the MVGAL AMD external-memory prototype" ON)

if(NOT BUILD_MVGAL_AMD_PROTOTYPE)
    message(STATUS "mvgal/amd_prototype: build disabled by option")
    return()
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Prefer reproducible and visible warnings by default
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Locate Vulkan (optional but strongly recommended)
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
    message(WARNING "Vulkan SDK not found at configure time. "
                    "The AMD prototype requires Vulkan headers & loader to build and run. "
                    "Install the Vulkan SDK or a system Vulkan development package.")
else()
    message(STATUS "Found Vulkan: ${Vulkan_VERSION}")
endif()

# Primary include dirs: ensure the MVGAL include path (project root) is available
# This allows sources to include "mvgal/mvgal_uapi.h" and other shared headers.
# Adjust path relative to this directory (../../include -> mvgal/include).
target_include_directories_dummy() # no-op placeholder for generator formatting

set(MVGAL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
if(EXISTS "${MVGAL_INCLUDE_DIR}")
    message(STATUS "MVGAL UAPI include dir: ${MVGAL_INCLUDE_DIR}")
else()
    message(WARNING "MVGAL include dir not found at ${MVGAL_INCLUDE_DIR}. "
                    "If headers are located elsewhere, set MVGAL_INCLUDE_DIR manually.")
endif()

# The prototype executable
set(PROTO_SRCS
    amd_prototype.c
)

add_executable(mvgal_amd_external_mem ${PROTO_SRCS})

# Provide include dir(s) for the prototype
target_include_directories(mvgal_amd_external_mem
    PRIVATE
        "${MVGAL_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Link Vulkan if available; otherwise allow configure but the build may fail
if(Vulkan_FOUND)
    target_link_libraries(mvgal_amd_external_mem PRIVATE Vulkan::Vulkan)
else()
    # If the Vulkan SDK is unavailable, do not attempt to link Vulkan.
    # The compile will probably fail due to missing headers; that's expected.
endif()

# Helpful defines available for source code to gate behaviors
if(Vulkan_FOUND)
    target_compile_definitions(mvgal_amd_external_mem PRIVATE MVGAL_HAVE_VULKAN=1)
else()
    target_compile_definitions(mvgal_amd_external_mem PRIVATE MVGAL_HAVE_VULKAN=0)
endif()

# A convenience target for running the program from the build tree
add_custom_target(run-amd-prototype
    COMMAND $<TARGET_FILE:mvgal_amd_external_mem>
    DEPENDS mvgal_amd_external_mem
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Run MVGAL AMD external-memory prototype"
)

# Install rules (optional)
install(TARGETS mvgal_amd_external_mem
    RUNTIME DESTINATION bin
    COMPONENT runtime
)

# Add a CTest entry so `ctest -R amd_prototype` can run it
enable_testing()
add_test(NAME mvgal_amd_external_mem
    COMMAND $<TARGET_FILE:mvgal_amd_external_mem>
)

# Provide a small summary at configure time
message(STATUS "mvgal/amd_prototype: target 'mvgal_amd_external_mem' created")
message(STATUS "  Source files: ${PROTO_SRCS}")
message(STATUS "  MVGAL include: ${MVGAL_INCLUDE_DIR}")
if(Vulkan_FOUND)
    message(STATUS "  Vulkan: ${Vulkan_VERSION} (linked)")
else()
    message(STATUS "  Vulkan: NOT FOUND (prototype will not link/run until Vulkan SDK is available)")
endif()

# End of CMakeLists.txt
