# Memory module CMakeLists.txt

# Get parent directory includes
get_filename_component(MVGAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# Memory module source files
set(MEMORY_SOURCES
    memory.c
    memory_internal.h
    allocator.c
    dmabuf.c
    sync.c
)

# Create memory module library
add_library(mvgal_memory STATIC ${MEMORY_SOURCES})

# Include directories
target_include_directories(mvgal_memory
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MVGAL_SOURCE_DIR}/include
        ${MVGAL_SOURCE_DIR}/include/mvgal
)

# Link libraries
target_link_libraries(mvgal_memory
    PRIVATE
        Threads::Threads
        ${DRM_IMPORTED_TARGET}
        ${PCI_IMPORTED_TARGET}
)

# Compiler flags
target_compile_options(mvgal_memory
    PRIVATE
        -Wall
        -Wextra
        -Werror
        -std=c11
        -D_GNU_SOURCE
        -O2
)

# Install
install(
    TARGETS mvgal_memory
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT development
)
