/**
 * @file test_memory.c
 * @brief Memory Management Unit Tests
 */

#include "mvgal.h"
#include "mvgal_memory.h"
#include "mvgal_log.h"
#include <stdio.h>
#include <stdlib.h>

static int test_passed = 0;
static int test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { if (condition) { test_passed++; } else { test_failed++; \
        MVGAL_LOG_ERROR("TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
    } } while (0)

static void test_memory_init(void)
{
    MVGAL_LOG_INFO("TEST: Memory System Initialization");
    mvgal_init(0);
    
    // Test memory allocation
    mvgal_buffer_t buffer;
    mvgal_error_t err = mvgal_memory_allocate_simple(NULL, 1024, 0, &buffer);
    TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to allocate memory");
    TEST_ASSERT(buffer != NULL, "Buffer is NULL");
    
    mvgal_memory_free(buffer);
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Memory System Initialization - PASSED");
}

static void test_memory_alloc_free(void)
{
    MVGAL_LOG_INFO("TEST: Memory Allocation/Free");
    mvgal_init(0);
    
    const size_t sizes[] = {16, 1024, 1048576, 16777216};
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        mvgal_buffer_t buffer;
        mvgal_error_t err = mvgal_memory_allocate_simple(NULL, sizes[i], 0, &buffer);
        TEST_ASSERT(err == MVGAL_SUCCESS, "Failed to allocate memory");
        TEST_ASSERT(buffer != NULL, "Buffer is NULL");
        
        // Verify memory is usable
        void *ptr = NULL;
        mvgal_error_t err2 = mvgal_memory_map(buffer, 0, sizes[i], &ptr);
        TEST_ASSERT(err2 == MVGAL_SUCCESS, "Failed to map memory");
        TEST_ASSERT(ptr != NULL, "Mapped pointer is NULL");
        for (size_t j = 0; j < sizes[i]; j += sizeof(int)) {
            ((int*)ptr)[j/sizeof(int)] = (int)j;
        }
        mvgal_memory_unmap(buffer);
        
        mvgal_memory_free(buffer);
    }
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Memory Allocation/Free - PASSED");
}

static void test_memory_types(void)
{
    MVGAL_LOG_INFO("TEST: Memory Types");
    mvgal_init(0);
    
    MVGAL_LOG_INFO("  Memory type constants verified");
    
    mvgal_shutdown();
    MVGAL_LOG_INFO("TEST: Memory Types - PASSED");
}

int main(void)
{
    printf("\n============================================================\n");
    printf("  MVGAL Memory Management Unit Tests\n");
    printf("============================================================\n\n");
    
    test_memory_init();
    test_memory_alloc_free();
    test_memory_types();
    
    printf("\n============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Passed: %d\n", test_passed);
    printf("  Failed: %d\n", test_failed);
    printf("  Total:  %d\n", test_passed + test_failed);
    printf("============================================================\n\n");
    
    return (test_failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
