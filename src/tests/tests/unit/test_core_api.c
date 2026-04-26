/**
 * @file test_core_api.c
 * @brief Core API Unit Tests
 */

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { if (condition) { test_passed++; } else { test_failed++; \
        MVGAL_LOG_ERROR("TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
    } } while (0)

static void test_init_shutdown(void)
{
    MVGAL_LOG_INFO("TEST: Init/Shutdown");
    TEST_ASSERT(!mvgal_is_initialized(), "Should not be initialized");
    TEST_ASSERT(mvgal_init(0) == MVGAL_SUCCESS, "Init failed");
    TEST_ASSERT(mvgal_is_initialized(), "Should be initialized");
    mvgal_shutdown();
    TEST_ASSERT(!mvgal_is_initialized(), "Should not be initialized after shutdown");
    TEST_ASSERT(mvgal_init(0) == MVGAL_SUCCESS, "Re-init should work");
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Init/Shutdown - PASSED");
}

static void test_version(void)
{
    MVGAL_LOG_INFO("TEST: Version");
    mvgal_init(0);
    const char *version = mvgal_get_version();
    TEST_ASSERT(version != NULL, "Version is NULL");
    TEST_ASSERT(strlen(version) > 0, "Version is empty");
    uint32_t major, minor, patch;
    mvgal_get_version_numbers(&major, &minor, &patch);
    TEST_ASSERT(major > 0 || minor > 0 || patch > 0, "Invalid version numbers");
    MVGAL_LOG_INFO("  Version: %s (%u.%u.%u)", version, major, minor, patch);
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Version - PASSED");
}

static void test_context(void)
{
    MVGAL_LOG_INFO("TEST: Context");
    mvgal_init(0);
    mvgal_context_t context;
    TEST_ASSERT(mvgal_context_create(&context) == MVGAL_SUCCESS, "Create context failed");
    TEST_ASSERT(context != NULL, "Context is NULL");
    TEST_ASSERT(mvgal_context_set_current(context) == MVGAL_SUCCESS, "Set current failed");
    TEST_ASSERT(mvgal_context_get_current() == context, "Get current failed");
    mvgal_context_destroy(context);
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Context - PASSED");
}

static void test_enabled_state(void)
{
    MVGAL_LOG_INFO("TEST: Enabled State");
    mvgal_init(1);
    TEST_ASSERT(mvgal_is_enabled(), "Should be enabled with flag 1");
    mvgal_set_enabled(false);
    TEST_ASSERT(!mvgal_is_enabled(), "Should be disabled");
    mvgal_set_enabled(true);
    TEST_ASSERT(mvgal_is_enabled(), "Should be enabled");
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Enabled State - PASSED");
}

static void test_flush_finish(void)
{
    MVGAL_LOG_INFO("TEST: Flush/Finish");
    mvgal_init(0);
    mvgal_context_t context;
    mvgal_context_create(&context);
    mvgal_context_set_current(context);
    TEST_ASSERT(mvgal_flush(context) == MVGAL_SUCCESS, "Flush failed");
    TEST_ASSERT(mvgal_finish(context) == MVGAL_SUCCESS, "Finish failed");
    mvgal_context_destroy(context);
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Flush/Finish - PASSED");
}

int main(void)
{
    printf("\n============================================================\n");
    printf("  MVGAL Core API Unit Tests\n");
    printf("============================================================\n\n");
    
    test_init_shutdown();
    test_version();
    test_context();
    test_enabled_state();
    test_flush_finish();
    
    printf("\n============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n\n");
    
    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
