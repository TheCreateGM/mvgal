/**
 * @file test_gpu_detection.c
 * @brief GPU Detection Unit Tests
 * 
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Tests for GPU detection and management functionality.
 */

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test counters
static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            test_passed++; \
        } else { \
            test_failed++; \
            MVGAL_LOG_ERROR("TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
        } \
    } while (0)

// =============================================================================
// Test: GPU Detection Initialization
// =============================================================================

static void test_gpu_init(void)
{
    MVGAL_LOG_INFO("TEST: GPU Detection Initialization");
    
    // Initialize MVGAL
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    // Check if initialized
    TEST_ASSERT(mvgal_is_initialized(), "MVGAL not initialized");
    
    // Shutdown
    mvgal_shutdown();
    TEST_ASSERT(!mvgal_is_initialized(), "MVGAL still initialized after shutdown");
    
    MVGAL_LOG_INFO("TEST: GPU Detection Initialization - PASSED");
}

// =============================================================================
// Test: GPU Count
// =============================================================================

static void test_gpu_count(void)
{
    MVGAL_LOG_INFO("TEST: GPU Count");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    int count = mvgal_gpu_get_count();
    TEST_ASSERT(count >= 0, "GPU count is negative");
    
    MVGAL_LOG_INFO("Detected %d GPU(s)", count);
    
    // Shutdown
    mvgal_shutdown();
    
    MVGAL_LOG_INFO("TEST: GPU Count - PASSED");
}

// =============================================================================
// Test: GPU Enumeration
// =============================================================================

static void test_gpu_enumeration(void)
{
    MVGAL_LOG_INFO("TEST: GPU Enumeration");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    int count = mvgal_gpu_get_count();
    if (count == 0) {
        MVGAL_LOG_WARN("No GPUs detected, skipping enumeration test");
        mvgal_shutdown();
        return;
    }
    
    // Allocate buffer for GPU descriptors
    mvgal_gpu_descriptor_t *gpus = (mvgal_gpu_descriptor_t *)
        malloc((size_t)count * sizeof(mvgal_gpu_descriptor_t));
    TEST_ASSERT(gpus != NULL, "Failed to allocate memory for GPU descriptors");
    
    // Enumerate GPUs
    int enumerated = mvgal_gpu_enumerate(gpus, (uint32_t)count);
    TEST_ASSERT(enumerated >= 0, "GPU enumeration failed");
    TEST_ASSERT(enumerated <= count, "Enumerated more GPUs than requested");
    
    // Check each GPU descriptor
    for (int i = 0; i < enumerated; i++) {
        /* ID is an unsigned value; `>= 0` is always true. Check for an invalid sentinel instead. */
        TEST_ASSERT(gpus[i].id != (uint32_t)UINT32_MAX, "Invalid GPU ID");
        TEST_ASSERT(gpus[i].name[0] != '\0', "Empty GPU name");
        TEST_ASSERT(gpus[i].vram_total > 0, "Zero VRAM reported");
        
        MVGAL_LOG_INFO("  GPU %d: %s (ID: %d, Vendor: %d, VRAM: %llu MB)",
                      i, gpus[i].name, gpus[i].id, gpus[i].vendor,
                      (unsigned long long)(gpus[i].vram_total / (1024ULL * 1024ULL)));
    }
    
    free(gpus);
    mvgal_shutdown();
    
    MVGAL_LOG_INFO("TEST: GPU Enumeration - PASSED");
}

// =============================================================================
// Test: GPU Descriptor Retrieval
// =============================================================================

static void test_gpu_get_descriptor(void)
{
    MVGAL_LOG_INFO("TEST: GPU Descriptor Retrieval");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    int count = mvgal_gpu_get_count();
    if (count == 0) {
        MVGAL_LOG_WARN("No GPUs detected, skipping descriptor test");
        mvgal_shutdown();
        return;
    }
    
    // Get descriptor for each GPU
    for (int i = 0; i < count; i++) {
        mvgal_gpu_descriptor_t gpu;
        err = mvgal_gpu_get_descriptor((uint32_t)i, &gpu);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to get GPU descriptor");
        
        /* `gpu.id` is unsigned; cast the loop index for a proper comparison. */
        TEST_ASSERT(gpu.id == (uint32_t)i, "GPU ID mismatch");
        TEST_ASSERT(gpu.name[0] != '\0', "Empty GPU name");
        
        MVGAL_LOG_INFO("  Retrieved GPU %d: %s", i, gpu.name);
    }
    
    // Test invalid index
    mvgal_gpu_descriptor_t gpu;
    err = mvgal_gpu_get_descriptor((uint32_t)(count + 1), &gpu);
    TEST_ASSERT(err != MVGAL_SUCCESS, "Should fail for invalid GPU index");
    
    mvgal_shutdown();
    
    MVGAL_LOG_INFO("TEST: GPU Descriptor Retrieval - PASSED");
}

// =============================================================================
// Test: GPU Enable/Disable
// =============================================================================

static void test_gpu_enable_disable(void)
{
    MVGAL_LOG_INFO("TEST: GPU Enable/Disable");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    int count = mvgal_gpu_get_count();
    if (count == 0) {
        MVGAL_LOG_WARN("No GPUs detected, skipping enable/disable test");
        mvgal_shutdown();
        return;
    }
    
    // Test enabling (should already be enabled)
    err = mvgal_gpu_enable(0, true);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to enable GPU 0");
    TEST_ASSERT(mvgal_gpu_is_enabled(0), "GPU 0 should be enabled");
    
    // Test disabling
    err = mvgal_gpu_enable(0, false);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to disable GPU 0");
    TEST_ASSERT(!mvgal_gpu_is_enabled(0), "GPU 0 should be disabled");
    
    // Re-enable
    err = mvgal_gpu_enable(0, true);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to re-enable GPU 0");
    TEST_ASSERT(mvgal_gpu_is_enabled(0), "GPU 0 should be re-enabled");
    
    // Test invalid index
    err = mvgal_gpu_enable((uint32_t)(count + 1), true);
    TEST_ASSERT(err != MVGAL_SUCCESS, "Should fail for invalid GPU index");
    
    mvgal_shutdown();
    
    MVGAL_LOG_INFO("TEST: GPU Enable/Disable - PASSED");
}

// =============================================================================
// Test: GPU Memory Info
// =============================================================================

static void test_gpu_memory_info(void)
{
    MVGAL_LOG_INFO("TEST: GPU Memory Info");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    int count = mvgal_gpu_get_count();
    if (count == 0) {
        MVGAL_LOG_WARN("No GPUs detected, skipping memory info test");
        mvgal_shutdown();
        return;
    }
    
    for (int i = 0; i < count; i++) {
        uint64_t total, used, free;
        err = mvgal_gpu_get_memory_stats((uint32_t)i, &total, &used, &free);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to get memory info");
        
        TEST_ASSERT(total > 0, "Total memory should be > 0");
        TEST_ASSERT(used <= total, "Used memory should be <= total");
        TEST_ASSERT(free <= total, "Free memory should be <= total");
        
        MVGAL_LOG_INFO("  GPU %d: Total=%llu MB, Used=%llu MB, Free=%llu MB",
                      i,
                      (unsigned long long)(total / (1024ULL * 1024ULL)),
                      (unsigned long long)(used / (1024ULL * 1024ULL)),
                      (unsigned long long)(free / (1024ULL * 1024ULL)));
    }
    
    mvgal_shutdown();
    
    MVGAL_LOG_INFO("TEST: GPU Memory Info - PASSED");
}

// =============================================================================
// Test: GPU Selection
// =============================================================================

static void test_gpu_select_best(void)
{
    MVGAL_LOG_INFO("TEST: GPU Select Best");
    
    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");
    
    int count = mvgal_gpu_get_count();
    if (count == 0) {
        MVGAL_LOG_WARN("No GPUs detected, skipping select best test");
        mvgal_shutdown();
        return;
    }
    
    mvgal_gpu_selection_criteria_t criteria = {
        .use_compute_score = true,
        .use_graphics_score = true,
        .use_memory = true,
        .use_features = true,
        .required_features = MVGAL_FEATURE_COMPUTE,
        .preferred_features = 0,
        .min_vram = 0,
        .preferred_vendor = MVGAL_VENDOR_UNKNOWN,
        .required_api = MVGAL_API_NONE
    };
    
    mvgal_gpu_descriptor_t selected;
    err = mvgal_gpu_select_best(&criteria, &selected);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to select best GPU");
    
    /* `selected.id` is unsigned; check against an invalid sentinel instead of >= 0. */
    TEST_ASSERT(selected.id != (uint32_t)UINT32_MAX, "Invalid selected GPU ID");
    MVGAL_LOG_INFO("  Selected best GPU: %s (ID: %d)", selected.name, selected.id);
    
    mvgal_shutdown();
    
    MVGAL_LOG_INFO("TEST: GPU Select Best - PASSED");
}

// =============================================================================
// Test: GPU Find By Node
// =============================================================================

static void test_gpu_find_by_node(void)
{
    MVGAL_LOG_INFO("TEST: GPU Find By Node");

    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");

    int count = mvgal_gpu_get_count();
    if (count == 0) {
        MVGAL_LOG_WARN("No GPUs detected, skipping node lookup test");
        mvgal_shutdown();
        return;
    }

    mvgal_gpu_descriptor_t gpu0;
    err = mvgal_gpu_get_descriptor(0, &gpu0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to get first GPU descriptor");

    if (gpu0.drm_node[0] != '\0') {
        mvgal_gpu_descriptor_t by_card;
        err = mvgal_gpu_find_by_node(gpu0.drm_node, &by_card);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to find GPU by DRM card node");
        TEST_ASSERT(by_card.id == gpu0.id, "DRM card node resolved to wrong GPU");
    }

    if (gpu0.drm_render_node[0] != '\0') {
        mvgal_gpu_descriptor_t by_render;
        err = mvgal_gpu_find_by_node(gpu0.drm_render_node, &by_render);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to find GPU by DRM render node");
        TEST_ASSERT(by_render.id == gpu0.id, "DRM render node resolved to wrong GPU");
    }

    if (gpu0.drm_node[0] != '\0' && gpu0.drm_render_node[0] != '\0') {
        mvgal_gpu_descriptor_t by_card;
        mvgal_gpu_descriptor_t by_render;
        err = mvgal_gpu_find_by_node(gpu0.drm_node, &by_card);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Card node lookup failed");
        err = mvgal_gpu_find_by_node(gpu0.drm_render_node, &by_render);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Render node lookup failed");
        TEST_ASSERT(by_card.id == by_render.id,
                    "Card and render nodes should map to the same physical GPU");
    }

    mvgal_shutdown();

    MVGAL_LOG_INFO("TEST: GPU Find By Node - PASSED");
}

// =============================================================================
// Test: GPU Rescan
// =============================================================================

static void test_gpu_rescan(void)
{
    MVGAL_LOG_INFO("TEST: GPU Rescan");

    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");

    int before = mvgal_gpu_get_count();
    TEST_ASSERT(before >= 0, "Initial GPU count invalid");

    err = mvgal_gpu_rescan();
    TEST_ASSERT(err == MVGAL_SUCCESS, "GPU rescan failed");

    int after = mvgal_gpu_get_count();
    TEST_ASSERT(after >= 0, "Rescanned GPU count invalid");
    TEST_ASSERT(after == before, "GPU count should remain stable across a local rescan");

    mvgal_shutdown();

    MVGAL_LOG_INFO("TEST: GPU Rescan - PASSED");
}

// =============================================================================
// Test: Logical Device Creation
// =============================================================================

static void test_logical_device(void)
{
    MVGAL_LOG_INFO("TEST: Logical Device");

    mvgal_error_t err = mvgal_init(0);
    TEST_ASSERT(err == MVGAL_SUCCESS, "mvgal_init failed");

    int count = mvgal_gpu_get_count();
    if (count == 0) {
        MVGAL_LOG_WARN("No GPUs detected, skipping logical device test");
        mvgal_shutdown();
        return;
    }

    uint32_t gpu_indices[2] = {0, 0};
    uint32_t logical_gpu_count = count >= 2 ? 2U : 1U;
    void *device = NULL;
    mvgal_logical_device_descriptor_t logical_desc;

    for (uint32_t i = 0; i < logical_gpu_count; ++i) {
        gpu_indices[i] = i;
    }

    err = mvgal_device_create(logical_gpu_count, gpu_indices, &device);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to create logical device");
    TEST_ASSERT(device != NULL, "Logical device is NULL");

    err = mvgal_device_get_descriptor(device, &logical_desc);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to get logical device descriptor");
    TEST_ASSERT(logical_desc.gpu_count == logical_gpu_count,
                "Logical device GPU count mismatch");
    TEST_ASSERT(logical_desc.descriptor.type == MVGAL_GPU_TYPE_VIRTUAL,
                "Logical device type should be virtual");
    TEST_ASSERT(logical_desc.descriptor.vram_total > 0,
                "Logical device should aggregate non-zero VRAM");
    TEST_ASSERT(logical_desc.primary_gpu_index < (uint32_t)count,
                "Primary GPU index should refer to a member GPU");

    mvgal_device_destroy(device);
    mvgal_shutdown();

    MVGAL_LOG_INFO("TEST: Logical Device - PASSED");
}

// =============================================================================
// Main
// =============================================================================

int main(void)
{
    printf("\n");
    printf("============================================================\n");
    printf("  MVGAL GPU Detection Unit Tests\n");
    printf("============================================================\n");
    printf("\n");
    
    // Run all tests
    test_gpu_init();
    test_gpu_count();
    test_gpu_enumeration();
    test_gpu_get_descriptor();
    test_gpu_enable_disable();
    test_gpu_memory_info();
    test_gpu_select_best();
    test_gpu_find_by_node();
    test_gpu_rescan();
    test_logical_device();
    
    // Print summary
    printf("\n");
    printf("============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n");
    printf("\n");
    
    // Return exit code
    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
