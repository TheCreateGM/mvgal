/**
 * @file mvgal_gpu.h
 * @brief GPU management API
 * 
 * Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * This header provides GPU detection, enumeration, and management functions.
 */

#ifndef MVGAL_GPU_H
#define MVGAL_GPU_H

#include "mvgal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup GPUManagement
 * @{
 */

/**
 * @brief GPU device handle
 */
typedef struct mvgal_gpu_device *mvgal_gpu_t;

/**
 * @brief GPU descriptor structure
 * 
 * Contains detailed information about a GPU device.
 */
typedef struct {
    uint32_t id;                    ///< MVGAL GPU ID (0, 1, 2...)
    char name[256];                ///< GPU name/model
    mvgal_vendor_t vendor;          ///< GPU vendor
    mvgal_gpu_type_t type;          ///< GPU type
    
    // PCI information
    uint16_t pci_domain;           ///< PCI domain
    uint8_t pci_bus;               ///< PCI bus
    uint8_t pci_device;            ///< PCI device
    uint8_t pci_function;          ///< PCI function
    uint16_t vendor_id;            ///< PCI vendor ID
    uint16_t device_id;            ///< PCI device ID
    uint16_t subsystem_vendor_id;  ///< PCI subsystem vendor ID
    uint16_t subsystem_id;         ///< PCI subsystem ID
    mvgal_pcie_generation_t pcie_gen; ///< PCIe generation
    uint8_t pcie_lanes;            ///< Number of PCIe lanes
    
    // Memory information
    uint64_t vram_total;            ///< Total VRAM in bytes
    uint64_t vram_free;             ///< Free VRAM in bytes
    uint64_t vram_used;             ///< Used VRAM in bytes
    mvgal_memory_type_t memory_type; ///< Primary memory type
    
    // Device nodes
    char drm_node[64];              ///< DRM device node (e.g., /dev/dri/card0)
    char drm_render_node[64];       ///< DRM render node (e.g., /dev/dri/renderD128)
    char nvidia_node[64];           ///< NVIDIA device node (e.g., /dev/nvidia0)
    char kfd_node[64];              ///< AMD KFD node (e.g., /dev/kfd)
    
    // Capabilities
    uint64_t features;              ///< Feature flags (mvgal_feature_flags_t)
    mvgal_api_type_t supported_apis[16]; ///< Supported APIs
    uint32_t api_count;             ///< Number of supported APIs
    
    // Performance characteristics
    float compute_score;           ///< Relative compute capability (0.0-100.0)
    float graphics_score;          ///< Relative graphics capability (0.0-100.0)
    float memory_bandwidth_gbps;   ///< Memory bandwidth in Gbps
    float pcie_bandwidth_gbps;     ///< PCIe bandwidth in Gbps
    float thermal_design_power_w;  ///< TDP in watts
    float current_power_w;          ///< Current power usage in watts
    float temperature_celsius;     ///< Current temperature in Celsius
    float utilization_percent;     ///< Current utilization (0.0-100.0)
    
    // Driver information
    char driver_name[64];          ///< Driver name
    char driver_version[64];       ///< Driver version
    bool driver_loaded;             ///< Driver loaded successfully
    
    // State
    bool enabled;                  ///< Whether GPU is enabled in MVGAL
    bool in_use;                    ///< Whether GPU is currently in use
    bool available;                ///< Whether GPU is available
    
    // Reserved for future use
    void *reserved[4];
} mvgal_gpu_descriptor_t;

/**
 * @brief GPU selection criteria
 */
typedef struct {
    bool use_compute_score;        ///< Consider compute score
    bool use_graphics_score;       ///< Consider graphics score
    bool use_memory;               ///< Consider memory requirements
    bool use_features;             ///< Consider feature requirements
    uint64_t required_features;    ///< Required features (mvgal_feature_flags_t)
    uint64_t preferred_features;   ///< Preferred features (mvgal_feature_flags_t)
    uint64_t min_vram;              ///< Minimum VRAM required in bytes
    mvgal_vendor_t preferred_vendor; ///< Preferred vendor
    mvgal_api_type_t required_api;  ///< Required API support
} mvgal_gpu_selection_criteria_t;

/**
 * @brief GPU callback function
 * @param gpu GPU descriptor
 * @param user_data User provided data
 */
typedef void (*mvgal_gpu_callback_t)(const mvgal_gpu_descriptor_t *gpu, void *user_data);

/**
 * @brief Get the number of available GPUs
 * 
 * @return Number of GPUs, or error code if not initialized
 */
int32_t mvgal_gpu_get_count(void);

/**
 * @brief Enumerate all available GPUs
 * 
 * @param gpus Array to store GPU descriptors (out)
 * @param count Size of the array
 * @return Number of GPUs enumerated, or error code
 */
int32_t mvgal_gpu_enumerate(mvgal_gpu_descriptor_t *gpus, uint32_t count);

/**
 * @brief Get a specific GPU by index
 * 
 * @param index GPU index (0-based)
 * @param gpu Descriptor to fill (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_descriptor(uint32_t index, mvgal_gpu_descriptor_t *gpu);

/**
 * @brief Find a GPU by PCI address
 * 
 * @param domain PCI domain
 * @param bus PCI bus
 * @param device PCI device
 * @param function PCI function
 * @param gpu Descriptor to fill (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_find_by_pci(
    uint16_t domain,
    uint8_t bus,
    uint8_t device,
    uint8_t function,
    mvgal_gpu_descriptor_t *gpu
);

/**
 * @brief Find a GPU by device node
 * 
 * @param node_path Device node path (e.g., "/dev/dri/card0")
 * @param gpu Descriptor to fill (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_find_by_node(const char *node_path, mvgal_gpu_descriptor_t *gpu);

/**
 * @brief Find GPUs by vendor
 * 
 * @param vendor Vendor to filter by
 * @param gpus Array to store GPU descriptors (out)
 * @param count Size of the array
 * @return Number of GPUs found, or error code
 */
int32_t mvgal_gpu_find_by_vendor(mvgal_vendor_t vendor, mvgal_gpu_descriptor_t *gpus, uint32_t count);

/**
 * @brief Select the best GPU based on criteria
 * 
 * @param criteria Selection criteria
 * @param gpu Descriptor of selected GPU (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_select_best(const mvgal_gpu_selection_criteria_t *criteria, mvgal_gpu_descriptor_t *gpu);

/**
 * @brief Enable a specific GPU
 * 
 * @param index GPU index
 * @param enable true to enable, false to disable
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_enable(uint32_t index, bool enable);

/**
 * @brief Check if a GPU is enabled
 * 
 * @param index GPU index
 * @return true if enabled, false otherwise
 */
bool mvgal_gpu_is_enabled(uint32_t index);

/**
 * @brief Enable all detected GPUs
 * 
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_enable_all(void);

/**
 * @brief Disable all detected GPUs
 * 
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_disable_all(void);

/**
 * @brief Get the primary (fastest) GPU
 * 
 * @param gpu Descriptor of primary GPU (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_primary(mvgal_gpu_descriptor_t *gpu);

/**
 * @brief Check if a GPU supports a specific feature
 * 
 * @param index GPU index
 * @param feature Feature to check (mvgal_feature_flags_t)
 * @return true if supported, false otherwise
 */
bool mvgal_gpu_has_feature(uint32_t index, uint64_t feature);

/**
 * @brief Check if a GPU supports a specific API
 * 
 * @param index GPU index
 * @param api API to check (mvgal_api_type_t)
 * @return true if supported, false otherwise
 */
bool mvgal_gpu_has_api(uint32_t index, mvgal_api_type_t api);

/**
 * @brief Get GPU memory statistics
 * 
 * @param index GPU index
 * @param total Total memory in bytes (out)
 * @param free Free memory in bytes (out)
 * @param used Used memory in bytes (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_memory_stats(
    uint32_t index,
    uint64_t *total,
    uint64_t *free,
    uint64_t *used
);

/**
 * @brief Get GPU utilization
 * 
 * @param index GPU index
 * @param utilization Utilization percentage 0-100 (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_utilization(uint32_t index, float *utilization);

/**
 * @brief Get GPU temperature
 * 
 * @param index GPU index
 * @param temperature Temperature in Celsius (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_temperature(uint32_t index, float *temperature);

/**
 * @brief Register a callback for GPU events
 * 
 * @param callback Callback function
 * @param user_data User data to pass to callback
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_register_callback(mvgal_gpu_callback_t callback, void *user_data);

/**
 * @brief Unregister a GPU callback
 * 
 * @param callback Callback function to unregister
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_unregister_callback(mvgal_gpu_callback_t callback);

/**
 * @brief Trigger GPU detection/rescan
 * 
 * Rescans the system for GPUs. Useful if GPUs are hot-plugged.
 * 
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_rescan(void);

/**
 * @brief Get GPU handle for direct use
 * 
 * @param index GPU index
 * @return GPU handle, or NULL on failure
 */
mvgal_gpu_t mvgal_gpu_get_handle(uint32_t index);

/**
 * @brief Get GPU handle from device node path
 * 
 * @param node_path Device node path
 * @return GPU handle, or NULL on failure
 */
mvgal_gpu_t mvgal_gpu_get_handle_by_node(const char *node_path);

/**
 * @brief Register a custom GPU driver
 * 
 * Allows adding support for custom/vendor-specific GPUs.
 * 
 * @param name Driver name
 * @param probe_func Probe function
 * @param init_func Initialization function
 * @param user_data Driver-specific data
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_register_driver(
    const char *name,
    void *probe_func,
    void *init_func,
    void *user_data
);

/**
 * @brief Create a logical device spanning multiple GPUs
 * 
 * @param gpu_count Number of GPUs to include
 * @param gpu_indices Array of GPU indices
 * @param device Device handle (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_device_create(
    uint32_t gpu_count,
    const uint32_t *gpu_indices,
    void **device
);

/**
 * @brief Destroy a logical device
 * 
 * @param device Device to destroy
 */
void mvgal_device_destroy(void *device);

/**
 * @brief GPU health status
 */
typedef struct {
    uint32_t gpu_index;              ///< GPU index
    float temperature_celsius;       ///< Current temperature in Celsius
    float temperature_max_celsius;  ///< Maximum safe temperature in Celsius
    float utilization_percent;      ///< Current utilization (0.0-100.0)
    float memory_used_mb;           ///< Memory used in MB
    float memory_total_mb;         ///< Total memory in MB
    bool is_healthy;                ///< Whether GPU is in healthy state
    uint64_t timestamp_ns;           ///< Timestamp of last health check
} mvgal_gpu_health_status_t;

/**
 * @brief GPU health thresholds
 */
typedef struct {
    float temp_warning_celsius;     ///< Temperature warning threshold
    float temp_critical_celsius;    ///< Temperature critical threshold
    float utilization_warning;      ///< Utilization warning threshold (%)
    float utilization_critical;     ///< Utilization critical threshold (%)
    float memory_warning;           ///< Memory usage warning threshold (%)
    float memory_critical;          ///< Memory usage critical threshold (%)
} mvgal_gpu_health_thresholds_t;

/**
 * @brief GPU health callback
 * @param gpu_index GPU index
 * @param status Health status
 * @param thresholds Health thresholds
 * @param user_data User provided data
 */
typedef void (*mvgal_gpu_health_callback_t)(
    uint32_t gpu_index,
    const mvgal_gpu_health_status_t *status,
    const mvgal_gpu_health_thresholds_t *thresholds,
    void *user_data
);

/**
 * @brief Health alert level
 */
typedef enum {
    MVGAL_HEALTH_OK = 0,            ///< Health is normal
    MVGAL_HEALTH_WARNING = 1,       ///< Health warning (e.g., high temp)
    MVGAL_HEALTH_CRITICAL = 2,      ///< Health critical (immediate attention needed)
    MVGAL_HEALTH_UNKNOWN = 3        ///< Health status unknown
} mvgal_gpu_health_level_t;

/**
 * @brief Get health status for a GPU
 *
 * @param index GPU index
 * @param status Health status (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_health_status(
    uint32_t index,
    mvgal_gpu_health_status_t *status
);

/**
 * @brief Get health level for a GPU
 *
 * @param index GPU index
 * @param level Health level (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_health_level(
    uint32_t index,
    mvgal_gpu_health_level_t *level
);

/**
 * @brief Check if all GPUs are healthy
 *
 * @return true if all GPUs are healthy, false otherwise
 */
bool mvgal_gpu_all_healthy(void);

/**
 * @brief Get health threshold for a GPU
 *
 * @param index GPU index
 * @param thresholds Thresholds (out)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_get_health_thresholds(
    uint32_t index,
    mvgal_gpu_health_thresholds_t *thresholds
);

/**
 * @brief Set health thresholds for a GPU
 *
 * @param index GPU index
 * @param thresholds Thresholds to set
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_set_health_thresholds(
    uint32_t index,
    const mvgal_gpu_health_thresholds_t *thresholds
);

/**
 * @brief Register a health callback
 *
 * @param callback Callback function
 * @param user_data User data to pass to callback
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_register_health_callback(
    mvgal_gpu_health_callback_t callback,
    void *user_data
);

/**
 * @brief Unregister a health callback
 *
 * @param callback Callback function to unregister
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_unregister_health_callback(
    mvgal_gpu_health_callback_t callback
);

/**
 * @brief Enable health monitoring
 *
 * @param enabled true to enable, false to disable
 * @param poll_interval_ms Polling interval in milliseconds
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_gpu_enable_health_monitoring(
    bool enabled,
    uint32_t poll_interval_ms
);

/** @} */ // end of GPUManagement

#ifdef __cplusplus
}
#endif

#endif // MVGAL_GPU_H
