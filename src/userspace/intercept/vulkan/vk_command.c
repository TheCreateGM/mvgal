/**
 * @file vk_command.c
 * @brief Vulkan Command Buffer Functions Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux (MVGAL)
 *
 * This file implements the intercepted Vulkan command buffer functions.
 * When Vulkan SDK is not available, this provides minimal stub implementations
 * to allow compilation without Vulkan headers.
 */

#include "vk_layer.h"

/**
 * @addtogroup VulkanLayer
 * @{
 */

// =============================================================================
// vkAllocateCommandBuffers
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device,
    const void *pAllocateInfo,
    VkCommandBuffer *pCommandBuffers
) {
    mvgal_vk_device_handle_t *device_handle = (mvgal_vk_device_handle_t *)device;
    mvgal_vk_command_buffer_handle_t *command_buffer;

    (void)pAllocateInfo;

    if (pCommandBuffers == NULL || device_handle == NULL ||
        device_handle->magic != MVGAL_VK_DEVICE_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    command_buffer = calloc(1, sizeof(*command_buffer));
    if (command_buffer == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    command_buffer->magic = MVGAL_VK_COMMAND_BUFFER_MAGIC;
    command_buffer->device = device_handle;
    command_buffer->workload_type = MVGAL_WORKLOAD_GRAPHICS;
    pCommandBuffers[0] = (VkCommandBuffer)command_buffer;

    return VK_SUCCESS;
}

// =============================================================================
// vkFreeCommandBuffers
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer *pCommandBuffers
) {
    (void)device;
    (void)commandPool;

    if (pCommandBuffers == NULL) {
        return;
    }

    for (uint32_t i = 0; i < commandBufferCount; i++) {
        mvgal_vk_command_buffer_handle_t *command_buffer =
            (mvgal_vk_command_buffer_handle_t *)pCommandBuffers[i];
        if (command_buffer != NULL &&
            command_buffer->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
            free(command_buffer);
        }
    }
}

// =============================================================================
// vkBeginCommandBuffer
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const void *pBeginInfo
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)pBeginInfo;

    if (command_buffer_handle == NULL ||
        command_buffer_handle->magic != MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    command_buffer_handle->recording = true;
    command_buffer_handle->operation_count = 0;
    command_buffer_handle->estimated_bytes = 0;
    command_buffer_handle->workload_type = MVGAL_WORKLOAD_GRAPHICS;
    return VK_SUCCESS;
}

// =============================================================================
// vkEndCommandBuffer
// =============================================================================

VK_LAYER_EXPORT VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer commandBuffer
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    if (command_buffer_handle == NULL ||
        command_buffer_handle->magic != MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    command_buffer_handle->recording = false;
    return VK_SUCCESS;
}

// =============================================================================
// vkResetCommandBuffer
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer,
    uint32_t flags
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)flags;

    if (command_buffer_handle == NULL ||
        command_buffer_handle->magic != MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        return;
    }

    command_buffer_handle->recording = false;
    command_buffer_handle->operation_count = 0;
    command_buffer_handle->estimated_bytes = 0;
    command_buffer_handle->workload_type = MVGAL_WORKLOAD_GRAPHICS;
}

// =============================================================================
// vkCmdPipelineBarrier
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer,
    uint32_t srcStageMask,
    uint32_t dstStageMask,
    uint32_t dependencyFlags,
    uint32_t memoryBarrierCount,
    const void *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const void *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const void *pImageMemoryBarriers
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount;
    (void)pImageMemoryBarriers;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count++;
        command_buffer_handle->estimated_bytes += 65536U;
    }
}

// =============================================================================
// vkCmdWaitEvents
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdWaitEvents(
    VkCommandBuffer commandBuffer,
    uint32_t eventCount,
    const void *pEvents,
    uint32_t srcStageMask,
    uint32_t dstStageMask,
    uint32_t memoryBarrierCount,
    const void *pMemoryBarriers
) {
    // Stub implementation
    (void)commandBuffer;
    (void)eventCount;
    (void)pEvents;
    (void)srcStageMask;
    (void)dstStageMask;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
}

// =============================================================================
// vkCmdClearColorImage
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout imageLayout,
    const void *pColor,
    uint32_t rangeCount,
    const void *pRanges
) {
    // Stub implementation
    (void)commandBuffer;
    (void)image;
    (void)imageLayout;
    (void)pColor;
    (void)rangeCount;
    (void)pRanges;
}

// =============================================================================
// vkCmdClearDepthStencilImage
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdClearDepthStencilImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout imageLayout,
    const void *pDepthStencil,
    uint32_t rangeCount,
    const void *pRanges
) {
    // Stub implementation
    (void)commandBuffer;
    (void)image;
    (void)imageLayout;
    (void)pDepthStencil;
    (void)rangeCount;
    (void)pRanges;
}

// =============================================================================
// vkCmdCopyBuffer
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const void *pRegions
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)srcBuffer;
    (void)dstBuffer;
    (void)pRegions;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count += regionCount;
        command_buffer_handle->estimated_bytes += (size_t)regionCount * 1048576U;
        command_buffer_handle->workload_type = MVGAL_WORKLOAD_TRANSFER;
    }
}

// =============================================================================
// vkCmdCopyImage
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const void *pRegions
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)srcImage;
    (void)srcImageLayout;
    (void)dstImage;
    (void)dstImageLayout;
    (void)pRegions;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count += regionCount;
        command_buffer_handle->estimated_bytes += (size_t)regionCount * 4194304U;
        command_buffer_handle->workload_type = MVGAL_WORKLOAD_GRAPHICS;
    }
}

// =============================================================================
// vkCmdBlitImage
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdBlitImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const void *pRegions,
    VkFilter filter
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)srcImage;
    (void)srcImageLayout;
    (void)dstImage;
    (void)dstImageLayout;
    (void)pRegions;
    (void)filter;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count += regionCount;
        command_buffer_handle->estimated_bytes += (size_t)regionCount * 8388608U;
        command_buffer_handle->workload_type = MVGAL_WORKLOAD_GRAPHICS;
    }
}

// =============================================================================
// vkCmdCopyBufferToImage
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const void *pRegions
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)srcBuffer;
    (void)dstImage;
    (void)dstImageLayout;
    (void)pRegions;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count += regionCount;
        command_buffer_handle->estimated_bytes += (size_t)regionCount * 2097152U;
        command_buffer_handle->workload_type = MVGAL_WORKLOAD_TRANSFER;
    }
}

// =============================================================================
// vkCmdCopyImageToBuffer
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const void *pRegions
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)srcImage;
    (void)srcImageLayout;
    (void)dstBuffer;
    (void)pRegions;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count += regionCount;
        command_buffer_handle->estimated_bytes += (size_t)regionCount * 2097152U;
        command_buffer_handle->workload_type = MVGAL_WORKLOAD_TRANSFER;
    }
}

// =============================================================================
// vkCmdFillBuffer
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdFillBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize size,
    uint32_t data
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)dstBuffer;
    (void)dstOffset;
    (void)data;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count++;
        command_buffer_handle->estimated_bytes += (size_t)size;
        command_buffer_handle->workload_type = MVGAL_WORKLOAD_TRANSFER;
    }
}

// =============================================================================
// vkCmdUpdateBuffer
// =============================================================================

VK_LAYER_EXPORT void VKAPI_CALL vkCmdUpdateBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize dataSize,
    const void *pData
) {
    mvgal_vk_command_buffer_handle_t *command_buffer_handle =
        (mvgal_vk_command_buffer_handle_t *)commandBuffer;

    (void)dstBuffer;
    (void)dstOffset;
    (void)pData;

    if (command_buffer_handle != NULL &&
        command_buffer_handle->magic == MVGAL_VK_COMMAND_BUFFER_MAGIC) {
        command_buffer_handle->operation_count++;
        command_buffer_handle->estimated_bytes += (size_t)dataSize;
        command_buffer_handle->workload_type = MVGAL_WORKLOAD_TRANSFER;
    }
}

/** @} */ // end of VulkanLayer
