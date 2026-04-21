/**
 * @file vk_queue.c
 * @brief Vulkan Queue Functions Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * This file implements the intercepted Vulkan queue and command submission functions.
 * When Vulkan SDK is not available, this provides minimal stub implementations
 * to allow compilation without Vulkan headers.
 */

#include "vk_layer.h"
#include <string.h>

static mvgal_vk_sync_handle_t *mvgal_vk_get_sync_handle(void *handle) {
    mvgal_vk_sync_handle_t *sync_handle = (mvgal_vk_sync_handle_t *)handle;
    if (sync_handle == NULL || sync_handle->magic != MVGAL_VK_SYNC_MAGIC) {
        return NULL;
    }
    return sync_handle;
}

static VkResult mvgal_vk_begin_queue_frame(
    mvgal_vk_queue_handle_t *queue_handle
) {
    mvgal_execution_frame_begin_info_t begin_info;
    mvgal_error_t err;

    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.api = MVGAL_API_VULKAN;
    begin_info.requested_strategy = queue_handle->device->strategy;
    begin_info.application_name = "vulkan";

    err = mvgal_execution_begin_frame(
        queue_handle->device->mvgal_context,
        &begin_info,
        &queue_handle->current_frame_id
    );
    if (err != MVGAL_SUCCESS) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_layer_state.frames_submitted++;
    return VK_SUCCESS;
}

/**
 * @addtogroup VulkanLayer
 * @{
 */

// =============================================================================
// vkQueueSubmit
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const void *pSubmits,
    VkFence fence
) {
    mvgal_vk_queue_handle_t *queue_handle = (mvgal_vk_queue_handle_t *)queue;
    mvgal_execution_submit_info_t submit_info;
    mvgal_error_t err;

    (void)pSubmits;

    if (queue_handle == NULL || queue_handle->magic != MVGAL_VK_QUEUE_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (queue_handle->current_frame_id == 0) {
        VkResult frame_result = mvgal_vk_begin_queue_frame(queue_handle);
        if (frame_result != VK_SUCCESS) {
            return frame_result;
        }
    }

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.frame_id = queue_handle->current_frame_id;
    submit_info.api = MVGAL_API_VULKAN;
    submit_info.requested_strategy = queue_handle->device->strategy;
    submit_info.telemetry.type = (queue_handle->flags & VK_QUEUE_COMPUTE_BIT) != 0U &&
                                 (queue_handle->flags & VK_QUEUE_GRAPHICS_BIT) == 0U ?
                                 MVGAL_WORKLOAD_COMPUTE : MVGAL_WORKLOAD_GRAPHICS;
    submit_info.telemetry.gpu_index = 0;
    submit_info.telemetry.step_name = "vkQueueSubmit";
    submit_info.telemetry.data_size = (mvgal_size_t)((submitCount == 0U ? 1U : submitCount) * 4U * 1024U * 1024U);
    submit_info.telemetry.timestamp = 0;
    submit_info.telemetry.flags.is_commit = 1;
    submit_info.telemetry.flags.is_frame_start = 1;
    submit_info.command_buffer_count = (submitCount == 0U) ? 1U : submitCount;
    submit_info.queue_family_flags = queue_handle->flags;
    submit_info.resource_bytes = (size_t)submit_info.telemetry.data_size;
    submit_info.gpu_mask = (uint32_t)(queue_handle->device->descriptor.gpu_mask & 0xFFFFFFFFULL);

    err = mvgal_execution_submit(
        queue_handle->device->mvgal_context,
        &submit_info,
        &queue_handle->last_plan
    );
    if (err != MVGAL_SUCCESS) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_layer_state.workloads_submitted++;

    mvgal_vk_sync_handle_t *fence_handle = mvgal_vk_get_sync_handle((void *)fence);
    if (fence_handle != NULL) {
        fence_handle->signaled = true;
    }

    return VK_SUCCESS;
}

// =============================================================================
// vkQueuePresentKHR
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue,
    const void *pPresentInfo
) {
    mvgal_vk_queue_handle_t *queue_handle = (mvgal_vk_queue_handle_t *)queue;
    mvgal_error_t err;

    (void)pPresentInfo;

    if (queue_handle == NULL || queue_handle->magic != MVGAL_VK_QUEUE_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (queue_handle->current_frame_id == 0) {
        VkResult frame_result = mvgal_vk_begin_queue_frame(queue_handle);
        if (frame_result != VK_SUCCESS) {
            return frame_result;
        }
    }

    err = mvgal_execution_present(
        queue_handle->device->mvgal_context,
        queue_handle->current_frame_id,
        MVGAL_API_VULKAN,
        &queue_handle->last_plan
    );
    if (err != MVGAL_SUCCESS) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_layer_state.frames_completed++;
    queue_handle->current_frame_id = 0;
    return VK_SUCCESS;
}

// =============================================================================
// vkQueueWaitIdle
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkQueueWaitIdle(
    VkQueue queue
) {
    mvgal_vk_queue_handle_t *queue_handle = (mvgal_vk_queue_handle_t *)queue;

    if (queue_handle == NULL || queue_handle->magic != MVGAL_VK_QUEUE_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return VK_SUCCESS;
}

// =============================================================================
// vkQueueBindSparse
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkQueueBindSparse(
    VkQueue queue,
    uint32_t bindInfoCount,
    const void *pBindInfo,
    VkFence fence
) {
    mvgal_vk_queue_handle_t *queue_handle = (mvgal_vk_queue_handle_t *)queue;
    mvgal_execution_submit_info_t submit_info;
    mvgal_error_t err;

    (void)pBindInfo;

    if (queue_handle == NULL || queue_handle->magic != MVGAL_VK_QUEUE_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.frame_id = queue_handle->current_frame_id;
    submit_info.api = MVGAL_API_VULKAN;
    submit_info.requested_strategy = MVGAL_STRATEGY_TASK;
    submit_info.telemetry.type = MVGAL_WORKLOAD_TRANSFER;
    submit_info.telemetry.step_name = "vkQueueBindSparse";
    submit_info.telemetry.data_size = (mvgal_size_t)((bindInfoCount == 0U ? 1U : bindInfoCount) * 1048576U);
    submit_info.command_buffer_count = (bindInfoCount == 0U) ? 1U : bindInfoCount;
    submit_info.queue_family_flags = queue_handle->flags;
    submit_info.resource_bytes = (size_t)submit_info.telemetry.data_size;
    submit_info.gpu_mask = (uint32_t)(queue_handle->device->descriptor.gpu_mask & 0xFFFFFFFFULL);

    err = mvgal_execution_submit(
        queue_handle->device->mvgal_context,
        &submit_info,
        &queue_handle->last_plan
    );
    if (err != MVGAL_SUCCESS) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_sync_handle_t *fence_handle = mvgal_vk_get_sync_handle((void *)fence);
    if (fence_handle != NULL) {
        fence_handle->signaled = true;
    }

    return VK_SUCCESS;
}

// =============================================================================
// vkCreateFence
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateFence(
    VkDevice device,
    const void *pCreateInfo,
    const void *pAllocator,
    VkFence *pFence
) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    if (pFence == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_sync_handle_t *fence_handle = calloc(1, sizeof(*fence_handle));
    if (fence_handle == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    fence_handle->magic = MVGAL_VK_SYNC_MAGIC;
    fence_handle->signaled = false;
    *pFence = (VkFence)fence_handle;
    return VK_SUCCESS;
}

// =============================================================================
// vkDestroyFence
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const void *pAllocator
) {
    (void)device;
    (void)pAllocator;

    mvgal_vk_sync_handle_t *fence_handle = mvgal_vk_get_sync_handle((void *)fence);
    if (fence_handle != NULL) {
        free(fence_handle);
    }
}

// =============================================================================
// vkWaitForFences
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence *pFences,
    VkBool32 waitAll,
    uint64_t timeout
) {
    (void)device;
    (void)waitAll;
    (void)timeout;

    if (pFences == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    for (uint32_t i = 0; i < fenceCount; i++) {
        mvgal_vk_sync_handle_t *fence_handle = mvgal_vk_get_sync_handle((void *)pFences[i]);
        if (fence_handle == NULL) {
            continue;
        }
        fence_handle->signaled = true;
    }

    return VK_SUCCESS;
}

// =============================================================================
// vkCreateSemaphore
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device,
    const void *pCreateInfo,
    const void *pAllocator,
    VkSemaphore *pSemaphore
) {
    (void)device;
    (void)pCreateInfo;
    (void)pAllocator;

    if (pSemaphore == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    mvgal_vk_sync_handle_t *semaphore_handle = calloc(1, sizeof(*semaphore_handle));
    if (semaphore_handle == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    semaphore_handle->magic = MVGAL_VK_SYNC_MAGIC;
    semaphore_handle->signaled = false;
    *pSemaphore = (VkSemaphore)semaphore_handle;
    return VK_SUCCESS;
}

// =============================================================================
// vkDestroySemaphore
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkDestroySemaphore(
    VkDevice device,
    VkSemaphore semaphore,
    const void *pAllocator
) {
    (void)device;
    (void)pAllocator;

    mvgal_vk_sync_handle_t *semaphore_handle = mvgal_vk_get_sync_handle((void *)semaphore);
    if (semaphore_handle != NULL) {
        free(semaphore_handle);
    }
}

// =============================================================================
// vkCreateEvent
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkCreateEvent(
    VkDevice device,
    const void *pCreateInfo,
    const void *pAllocator,
    void *pEvent
) {
    if (pEvent) {
        mvgal_vk_sync_handle_t *event_handle = calloc(1, sizeof(*event_handle));
        if (event_handle == NULL) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        event_handle->magic = MVGAL_VK_SYNC_MAGIC;
        event_handle->signaled = false;
        *(void **)pEvent = event_handle;
    }
    return VK_SUCCESS;
}

// =============================================================================
// vkDestroyEvent
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkDestroyEvent(
    VkDevice device,
    void *event,
    const void *pAllocator
) {
    (void)device;
    (void)pAllocator;

    mvgal_vk_sync_handle_t *event_handle = mvgal_vk_get_sync_handle(event);
    if (event_handle != NULL) {
        free(event_handle);
    }
}

// =============================================================================
// vkSetEvent
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkSetEvent(
    VkDevice device,
    void *event
) {
    (void)device;

    mvgal_vk_sync_handle_t *event_handle = mvgal_vk_get_sync_handle(event);
    if (event_handle != NULL) {
        event_handle->signaled = true;
    }
    return VK_SUCCESS;
}

// =============================================================================
// vkResetEvent
// ============================================================================= 

VK_LAYER_EXPORT VkResult VKAPI_CALL vkResetEvent(
    VkDevice device,
    void *event
) {
    (void)device;

    mvgal_vk_sync_handle_t *event_handle = mvgal_vk_get_sync_handle(event);
    if (event_handle != NULL) {
        event_handle->signaled = false;
    }
    return VK_SUCCESS;
}

// =============================================================================
// vkGetEventStatus
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkGetEventStatus(
    VkDevice device,
    void *event
) {
    (void)device;

    mvgal_vk_sync_handle_t *event_handle = mvgal_vk_get_sync_handle(event);
    if (event_handle != NULL && event_handle->signaled) {
        return VK_EVENT_SET;
    }
    return VK_SUCCESS;
}

/** @} */ // end of VulkanLayer
