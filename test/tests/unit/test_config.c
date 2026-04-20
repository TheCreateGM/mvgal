/**
 * @file test_config.c
 * @brief Configuration Unit Tests
 */

#include "mvgal.h"
#include "mvgal_config.h"
#include "mvgal_log.h"
#include <stdio.h>
#include <stdlib.h>

static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { if (condition) { test_passed++; } else { test_failed++; \
        MVGAL_LOG_ERROR("TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
    } } while (0)

static void test_config_init(void)
{
    MVGAL_LOG_INFO("TEST: Config Initialization");
    TEST_ASSERT(mvgal_config_init() == MVGAL_SUCCESS, "Config init failed");
    mvgal_config_t config;
    mvgal_config_get(&config);
    TEST_ASSERT(config.log_level >= 0, "Invalid log level");
    mvgal_config_shutdown();
    MVGAL_LOG_INFO("TEST: Config Initialization - PASSED");
}

static void test_config_defaults(void)
{
    MVGAL_LOG_INFO("TEST: Config Defaults");
    mvgal_config_init();
    mvgal_config_t config;
    mvgal_config_get(&config);
    TEST_ASSERT(config.enabled == true, "Default enabled should be true");
    TEST_ASSERT(config.log_level >= 0, "Default log level should be >= 0");
    mvgal_config_set(&config);
    mvgal_config_shutdown();
    MVGAL_LOG_INFO("TEST: Config Defaults - PASSED");
}

static void test_config_set_get(void)
{
    MVGAL_LOG_INFO("TEST: Config Set/Get");
    mvgal_config_init();
    mvgal_config_t config1 = {
        .enabled = false,
        .log_level = 5
    };
    mvgal_error_t err2 = mvgal_config_set(&config1);
    TEST_ASSERT(err2 == MVGAL_SUCCESS, "Config set failed");
    mvgal_config_t config2;
    mvgal_config_get(&config2);
    TEST_ASSERT(config2.enabled == config1.enabled, "Enabled mismatch");
    TEST_ASSERT(config2.log_level == config1.log_level, "Log level mismatch");
    mvgal_config_shutdown();
    MVGAL_LOG_INFO("TEST: Config Set/Get - PASSED");
}

int main(void)
{
    printf("\n============================================================\n");
    printf("  MVGAL Configuration Unit Tests\n");
    printf("============================================================\n\n");
    
    test_config_init();
    test_config_defaults();
    test_config_set_get();
    
    printf("\n============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n\n");
    
    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
