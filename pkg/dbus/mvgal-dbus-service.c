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

#include "../../src/userspace/core/mvgal.h"

#define DBUS_SERVICE_NAME "org.mvgal.MVGAL"
#define DBUS_PATH "/org/mvgal/MVGAL"

dbus_bool_t handle_get_version(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    const char *version = "0.1.0";
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_message_append_args(reply, DBUS_TYPE_STRING, &version, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_get_enabled(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    mvgal_config_t config;
    if (mvgal_get_config(&config) == 0) {
        dbus_bool_t enabled = config.enabled;
        DBusMessage *reply = dbus_message_new_method_return(message);
        dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &enabled, DBUS_TYPE_INVALID);
        dbus_connection_send(connection, reply, NULL);
        dbus_message_unref(reply);
    }
    return TRUE;
}

dbus_bool_t handle_get_gpu_count(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    int count = mvgal_get_gpu_count();
    DBusMessage *reply = dbus_message_new_method_return(message);
    dbus_message_append_args(reply, DBUS_TYPE_INT32, &count, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return TRUE;
}

dbus_bool_t handle_list_gpus(DBusConnection *connection, DBusMessage *message, void *user_data) {
    (void)user_data;
    
    int gpu_count = mvgal_get_gpu_count();
    DBusMessage *reply = dbus_message_new_method_return(message);
    
    DBusMessageIter array_iter;
    dbus_message_iter_init_append(reply, &array_iter);
    dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, NULL, &array_iter);
    
    for (int i = 0; i < gpu_count; i++) {
        mvgal_gpu_info_t info;
        if (mvgal_get_gpu_info(i, &info) == 0) {
            // id, name, vendor, strategy, enabled, memory_used
            int32_t id = info.id;
            const char *name = info.name;
            const char *vendor = info.vendor;
            const char *strategy = "unknown";
            dbus_bool_t enabled = info.enabled;
            double memory_used = (double)info.memory_used / (1024 * 1024); // MB
            
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_INT32, &id);
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &name);
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &vendor);
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &strategy);
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_BOOLEAN, &enabled);
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_DOUBLE, &memory_used);
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
    if (mvgal_get_stats(&stats) == 0) {
        DBusMessage *reply = dbus_message_new_method_return(message);
        DBusMessageIter dict_iter;
        
        dbus_message_iter_init_append(reply, &dict_iter);
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);
        
        // total_workloads
        DBusMessageIter entry_iter;
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        const char *key = "total_workloads";
        uint64_t value = stats.total_workloads;
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        DBusMessageIter variant_iter;
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "u", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_UINT64, &value);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
        
        // load_balance
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        key = "load_balance";
        double load_balance = stats.load_balance;
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "d", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_DOUBLE, &load_balance);
        dbus_message_iter_close_container(&entry_iter, &variant_iter);
        dbus_message_iter_close_container(&dict_iter, &entry_iter);
        
        // memory_used
        dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY, NULL, &entry_iter);
        key = "memory_used";
        uint64_t memory_used = stats.memory_used;
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry_iter, DBUS_TYPE_VARIANT, "u", &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_UINT64, &memory_used);
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
        mvgal_strategy_t strategy;
        if (strcmp(strategy_str, "round_robin") == 0) {
            strategy = MVGAL_STRATEGY_ROUND_ROBIN;
        } else if (strcmp(strategy_str, "afr") == 0) {
            strategy = MVGAL_STRATEGY_AFR;
        } else if (strcmp(strategy_str, "sfr") == 0) {
            strategy = MVGAL_STRATEGY_SFR;
        } else if (strcmp(strategy_str, "single") == 0) {
            strategy = MVGAL_STRATEGY_SINGLE;
        } else if (strcmp(strategy_str, "hybrid") == 0) {
            strategy = MVGAL_STRATEGY_HYBRID;
        } else if (strcmp(strategy_str, "custom") == 0) {
            strategy = MVGAL_STRATEGY_CUSTOM;
        } else {
            strategy = MVGAL_STRATEGY_ROUND_ROBIN;
        }
        
        mvgal_set_strategy(strategy);
    }
    
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
                }
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
    }
    
    return FALSE;
}

int main(int argc, char **argv) {
    DBusError error;
    DBusConnection *connection = NULL;
    DBusObjectPathVTable vtable = { NULL, handle_method_call, NULL, NULL, NULL, NULL };
    
    dbus_error_init(&error);
    
    // Initialize MVGAL
    if (mvgal_init(NULL) != 0) {
        fprintf(stderr, "Failed to initialize MVGAL\n");
        return 1;
    }
    
    // Connect to system bus
    connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", error.message);
        dbus_error_free(&error);
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
        mvgal_shutdown();
        return 1;
    }
    
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "Failed to become primary owner of service name\n");
        dbus_connection_unref(connection);
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
    mvgal_shutdown();
    
    return 0;
}
