// SPDX-License-Identifier: MIT OR Apache-2.0
/**
 * @file mvgal_layer.cpp
 * @brief Vulkan explicit layer C++ compilation unit
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This file provides a C++ compilation unit for the explicit Vulkan layer
 * (VK_LAYER_MVGAL_aggregation). All Vulkan entry points are implemented in
 * vk_layer.c with C linkage; this file exists to allow C++ consumers to
 * include vk_layer.h and to expose future C++-specific utilities.
 *
 * The explicit layer can be injected via:
 *   VK_INSTANCE_LAYERS=VK_LAYER_MVGAL_aggregation application
 *
 * Compatibility note:
 *   vk_layer.h uses C11 <stdatomic.h> types (atomic_uint_fast64_t).
 *   In C++ these are not available from <stdatomic.h>; we provide a
 *   typedef shim via <atomic> before including the C header.
 */

#include <atomic>
#include <cstdint>

/* Provide C11 atomic typedef aliases for C++ so vk_layer.h compiles cleanly
 * when included from a .cpp translation unit. */
#ifndef __cplusplus
#  error "This file must be compiled as C++"
#endif

/* Map C11 _Atomic / stdatomic types to std::atomic equivalents.
 * These are only needed when the C header is included from C++. */
using atomic_uint_fast64_t = std::atomic<uint_fast64_t>;  // NOLINT

extern "C" {
#include "../../src/userspace/intercept/vulkan/vk_layer.h"
}

// All Vulkan entry points are defined in vk_layer.c with C linkage.
// No additional symbols are needed here.
