/**
 * @file mvgal-dbus-service.c
 * @brief MVGAL DBus Service Implementation
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * DBus service for remote management of MVGAL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dbus/dbus.h>

#include "../../include/mvgal/mvgal.h"
#include "../../include/mvgal/mvgal_gpu.h"
#include "../../include/mvgal/mvgal_config.h"

#define DBUS_SERVICE_NAME "org.mvgal.MVGAL"
#define DBUS_PATH "/org/mvgal/MVGAL"

static mvgal_context_t g_context = NULL;

static const char *strategy_to_string(mvgal_distribution_strategy_t strategy) {
    switch (strategy) {
        case MVGAL_STRATEGY_ROUND_ROBIN: return "round_robin";
        case MVGAL_STRATEGY_AFR: return "afr";
        case MVGAL_STRATEGY_SFR: return "sfr";
        case MVGAL_STRATEGY_SINGLE_GPU: return "single_gpu";
        case MVGAL_STRATEGY_HYBRID: return "hybrid";
        case MVGAL_STRATEGY_TASK: return "task";
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD: return "compute_offload";
        case MVGAL_STRATEGY_AUTO: return "auto";
        case MVGAL_STRATEGY_CUSTOM: return "custom";
        default: return "unknown";
    }
}

static mvgal_distribution_strategy_t string_to_strategy(const char *name) {
    if (strcmp(name, "round_robin") == 0) return MVGAL_STRATEGY_ROUND_ROBIN;
    if (strcmp(name, "afr") == 0) return MVGAL_STRATEGY_AFR;
    if (strcmp(name, "sfr") == 0) return MVGAL_STRATEGY_SFR;
    if (strcmp(name, "single_gpu") == 0 || strcmp(name, "single") == 0) return MVGAL_STRATEGY_SINGLE_GPU;
    if (strcmp(name, "hybrid") == 0) return MVGAL_STRATEGY_HYBRID;
    if (strcmp(name, "task") == 0) return MVGAL_STRATEGY_TASK;
    if (strcmp(name, "compute_offload") == 0) return MVGAL_STRATEGY_COMPUTE_OFFLOAD;
    if (strcmp(name, "auto") == 0) return MVGAL_STRATEGY_AUTO;
    if (strcmp(name, "custom") == 0) return MVGAL_STRATEGY_CUSTOM;
    return MVGAL_STRATEGY_HYBRID;
}

static const char *vendor_to_string(mvgal_vendor_t vendor) {
    switch (vendor) {
        case MVGAL_VENDOR_AMD: return "AMD";
        case MVGAL_VENDOR_NVIDIA: return "NVIDIA";
        case MVGAL_VENDOR_INTEL: return "Intel";
        case MVGAL_VENDOR_MOORE_THREADS: return "Moore Threads";
        default: return "Unknown";
    }
}

dbus_bool_t handle_get_version(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    const char *version = mvgal_get_version();
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_message_append_args(reply, DBUS_TYPE_STRING, &version, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_get_enabled(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    dbus_bool_t enabled = mvgal_is_enabled();
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &enabled, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_get_gpu_count(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    int32_t count = mvgal_gpu_get_count();
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_message_append_args(reply, DBUS_TYPE_INT32, &count, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_list_gpus(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    int32_t gpu_count = mvgal_gpu_get_count();
    DBusMessage *reply = dbus_message_new_method_return(message);
    
    DBusMessageIter array_iter;
    dbus_message_iter_init_append(reply, &array_iter);
    dbus_message_iter_open_container(&array_iter, DBUS_TYPE_ARRAY, "(isssbd)", &array_iter);
    
    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t desc;
        if (mvgal_gpu_get_descriptor(i, &desc) == MVGAL_SUCCESS) {
            DBusMessageIter struct_iter;
            dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, NULL, &struct_iter);
            
            // id, name, vendor, strategy, enabled, memory_used
            int32_t id = desc.id;
            const char *name = desc.name;
            const char *vendor = vendor_to_string(desc.vendor);
            const char *strategy = strategy_to_string(desc.enabled ? MVGAL_STRATEGY_ROUND_ROBIN : MVGAL_STRATEGY_SINGLE_GPU);
            dbus_bool_t enabled = desc.enabled;
            double memory_used = (double)desc.vram_used / (1024 * 1024); // MB
            
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &id);
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &name);
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &vendor);
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &strategy);
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_BOOLEAN, &enabled);
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE, &memory_used);
            
            dbus_message_iter_close_container(&array_iter, &struct_iter);
        }
    }
    
    dbus_message_iter_close_container(&array_iter, &array_iter);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_get_stats(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    mvgal_stats_t stats;
    if (mvgal_get_stats(g_context, &stats) == MVGAL_SUCCESS) {
        DBusMessage *reply = dbus_message_new_method_return(message);
        DBusMessageIter dict_iter;
        
        dbus_message_iter_init_append(reply, &dict_iter);
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);
        
        // frames_submitted
        DBusMessageIter entry_iter;
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        const char *key = "frames_submitted";
        uint64_t value = stats.frames_submitted;
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        DBusMessageIter variant_iter;
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "u", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_UINT64, &value);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
        
        // frames_completed
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        key = "frames_completed";
        value = stats.frames_completed;
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "u", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_UINT64, &value);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
        
        // workloads_distributed
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        key = "workloads_distributed";
        value = stats.workloads_distributed;
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "u", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_UINT64, &value);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
        
        // bytes_transferred
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        key = "bytes_transferred";
        value = stats.bytes_transferred;
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "u", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_UINT64, &value);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
        
        // Calculate load balance (simplified)
        float balance = 100.0f;
        
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        key = "load_balance";
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "d", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_DOUBLE, &balance);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
        
        dbus_message_iter_close_container(&dict_iter, &dict_iter);
        
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);
    }
    return TRUE;
}

dbus_bool_t handle_set_strategy(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    const char *strategy_str = NULL;
    DBusMessageIter args;
    
    if (dbus_message_iter_init(message, &args)) {
        dbus_message_iter_get_basic(&args, &strategy_str);
    }
    
    if (strategy_str) {
        mvgal_distribution_strategy_t strategy = string_to_strategy(strategy_str);
        mvgal_set_strategy(g_context, strategy);
    }
    
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_get_strategy(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    mvgal_distribution_strategy_t strategy = mvgal_get_strategy(g_context);
    const char *strategy_str = strategy_to_string(strategy);
    
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_message_append_args(reply, DBUS_TYPE_STRING, &strategy_str, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_reset_stats(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    mvgal_reset_stats(g_context);
    
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_method_call(DBusConnection *connection, DBusMessage *message, void *user_data) {
    const char *method = dbus_message_get_member(message);
    const char *interface = dbus_message_get_interface(message);
    
    if (strcmp(interface, DBUS_INTERFACE_PROPERTIES) == 0) {
        if (strcmp(method, "Get") == 0) {
            const char *property = NULL;
            DBusMessageIter args;
            if (dbus_message_iter_init(message, &args)) {
                DBusMessageIter sub_iter;
                dbus_message_iter_recurse(&args, &sub_iter);
                dbus_message_iter_get_basic(&sub_iter, &property);
            }
            
            if (property) {
                if (strcmp(property, "Version") == 0) {
                    return handle_get_version(connection, message, user_data);
                } else if (strcmp(property, "Enabled") == 0) {
                    return handle_get_enabled(connection, message, user_data);
                } else if (strcmp(property, "GPUCount") == 0) {
                    return handle_get_gpu_count(connection, message, user_data);
                } else if (strcmp(property, "Strategy") == 0) {
                    return handle_get_strategy(connection, message, user_data);
                }
            }
        } else if (strcmp(method, "Set") == 0) {
            const char *property = NULL;
            DBusMessageIter args;
            if (dbus_message_iter_init(message, &args)) {
                DBusMessageIter sub_iter;
                dbus_message_iter_recurse(&args, &sub_iter);
                dbus_message_iter_get_basic(&sub_iter, &property);
            }
            
            if (property && strcmp(property, "Strategy") == 0) {
                return handle_set_strategy(connection, message, user_data);
            }
        }
        return TRUE;
    }
    
    if (strcmp(method, "ListGPUs") == 0) {
        return handle_list_gpus(connection, message, user_data);
    } else if (strcmp(method, "GetStats") == 0) {
        return handle_get_stats(connection, message, user_data);
    } else if (strcmp(method, "SetDefaultStrategy") == 0) {
        return handle_set_strategy(connection, message, user_data);
    } else if (strcmp(method, "ResetStats") == 0) {
        return handle_reset_stats(connection, message, user_data);
    }
    
    return FALSE;
}

int main(int argc, char **argv) {
    DBusError error;
    DBusConnection *connection = NULL;
    DBusObjectPathVTable vtable = { NULL, handle_method_call, NULL, NULL, NULL, NULL };
    
    dbus_error_init(&error);
    
    // Initialize MVGAL
    mvgal_error_t mvgal_err = mvgal_init(NULL);
    if (mvgal_err != MVGAL_SUCCESS) {
        fprintf(stderr, "Failed to initialize MVGAL: %d\n", mvgal_err);
        return 1;
    }
    
    // Create context
    mvgal_context_create(&g_context);
    
    // Connect to system bus
    connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", error.message);
        dbus_error_free(&error);
        mvgal_context_destroy(g_context);
        mvgal_shutdown();
        return 1;
    }
    
    // Request service name
    int ret = dbus_bus_request_name(connection, DBUS_SERVICE_NAME,
                                     DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Failed to request service name: %s\n", error.message);
        dbus_error_free(&error);
        dbus_connection_unref(connection);
        mvgal_context_destroy(g_context);
        mvgal_shutdown();
        return 1;
    }
    
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "Failed to become primary owner of service name\n");
        dbus_connection_unref(connection);
        mvgal_context_destroy(g_context);
        mvgal_shutdown();
        return 1;
    }
    
    // Register object path
    dbus_connection_register_object_path(connection, DBUS_PATH, &vtable, NULL);
    
    printf("MVGAL DBus service running on system bus\n");
    printf("Service name: %s\n", DBUS_SERVICE_NAME);
    printf("Object path: %s\n", DBUS_PATH);
    
    // Main loop
    while (dbus_connection_read_write_dispatch(connection, 1000)) {
        // Process pending events
    }
    
    // Cleanup
    dbus_connection_unregister_object_path(connection, DBUS_PATH);
    dbus_connection_unref(connection);
    mvgal_context_destroy(g_context);
    mvgal_shutdown();
    
    return 0;
}
