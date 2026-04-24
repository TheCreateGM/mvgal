# Simple CMakeLists for mvgal-probe
# Builds a small CLI that exercises /dev/mvgal0 using the mvgal UAPI headers.
#
# Expected source file: mvgal/tools/mvgal-probe/mvgal_probe.c
# The mvgal UAPI headers live at mvgal/include, which is two levels up from this directory.

cmake_minimum_required(VERSION 3.16)
project(mvgal-probe LANGUAGES C)

# Use a modern C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Executable
add_executable(mvgal-probe
    mvgal_probe.c
)

# Include the repository-wide headers (mvgal/include)
target_include_directories(mvgal-probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Compiler warnings - keep these informative but avoid being overly strict for a small probe
if (MSVC)
    target_compile_options(mvgal-probe PRIVATE /W4)
else ()
    target_compile_options(mvgal-probe PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Installation rules
install(TARGETS mvgal-probe
    RUNTIME DESTINATION bin
)

# Convenience target to run the probe from the build tree:
#   cmake --build . --target run
add_custom_target(run
    COMMAND $<TARGET_FILE:mvgal-probe>
    DEPENDS mvgal-probe
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Run mvgal-probe"
)
