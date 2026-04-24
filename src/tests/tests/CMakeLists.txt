# Tests CMakeLists.txt
# This file configures all test suites for MVGAL

cmake_minimum_required(VERSION 3.20)
project(MVGAL-Tests)

# Find required packages
find_package(Threads REQUIRED)

# Enable CTest
enable_testing()

# =============================================================================
# Test Options
# =============================================================================

option(WITH_UNIT_TESTS "Build unit tests" ON)
option(WITH_INTEGRATION_TESTS "Build integration tests" ON)

# =============================================================================
# Unit Tests
# =============================================================================

if(WITH_UNIT_TESTS)
    add_subdirectory(unit)
endif()

# =============================================================================
# Integration Tests
# =============================================================================

if(WITH_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
