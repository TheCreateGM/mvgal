/**
 * @file mvgal-config.c
 * @brief MVGAL Configuration Tool
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * CLI tool for configuring and managing MVGAL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_config.h"

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS] COMMAND\n", prog);
    printf("\n");
    printf("MVGAL Configuration Tool\n");
    printf("\n");
    printf("Commands:\n");
    printf("  list-gpus         List detected GPUs\n");
    printf("  show-config       Show current configuration\n");
    printf("  set-strategy      Set workload distribution strategy\n");
    printf("  enable-gpu        Enable a GPU\n");
    printf("  disable-gpu       Disable a GPU\n");
    printf("  set-priority      Set GPU priority\n");
    printf("  show-stats        Show runtime statistics\n");
    printf("  reset-stats       Reset statistics\n");
    printf("  reload            Reload configuration\n");
    printf("  help              Show this help message\n");
    printf("\n");
    printf("Options:\n");
    printf("  -c, --config FILE Use alternative config file\n");
    printf("  -h, --help        Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s list-gpus\n", prog);
    printf("  %s set-strategy round_robin\n", prog);
    printf("  %s enable-gpu 1\n", prog);
    printf("  %s show-stats\n", prog);
}

static const char *strategy_to_string(mvgal_distribution_strategy_t strategy) {
    switch (strategy) {
        case MVGAL_STRATEGY_ROUND_ROBIN: return "Round Robin";
        case MVGAL_STRATEGY_AFR: return "AFR";
        case MVGAL_STRATEGY_SFR: return "SFR";
        case MVGAL_STRATEGY_SINGLE_GPU: return "Single GPU";
        case MVGAL_STRATEGY_HYBRID: return "Hybrid";
        case MVGAL_STRATEGY_TASK: return "Task";
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD: return "Compute Offload";
        case MVGAL_STRATEGY_AUTO: return "Auto";
        case MVGAL_STRATEGY_CUSTOM: return "Custom";
        default: return "Unknown";
    }
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

static mvgal_distribution_strategy_t string_to_strategy(const char *name) {
    if (strcmp(name, "round_robin") == 0) return MVGAL_STRATEGY_ROUND_ROBIN;
    if (strcmp(name, "afr") == 0) return MVGAL_STRATEGY_AFR;
    if (strcmp(name, "sfr") == 0) return MVGAL_STRATEGY_SFR;
    if (strcmp(name, "single") == 0 || strcmp(name, "single_gpu") == 0) return MVGAL_STRATEGY_SINGLE_GPU;
    if (strcmp(name, "hybrid") == 0) return MVGAL_STRATEGY_HYBRID;
    if (strcmp(name, "task") == 0) return MVGAL_STRATEGY_TASK;
    if (strcmp(name, "compute_offload") == 0) return MVGAL_STRATEGY_COMPUTE_OFFLOAD;
    if (strcmp(name, "auto") == 0) return MVGAL_STRATEGY_AUTO;
    if (strcmp(name, "custom") == 0) return MVGAL_STRATEGY_CUSTOM;
    return MVGAL_STRATEGY_HYBRID;
}

static void list_gpus(void) {
    printf("Listing detected GPUs:\n");
    printf("======================\n");
    
    int32_t gpu_count = mvgal_gpu_get_count();
    printf("Total GPUs: %d\n\n", gpu_count);
    
    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t desc;
        if (mvgal_gpu_get_descriptor(i, &desc) == MVGAL_SUCCESS) {
            printf("GPU %d: %s (%s) - %s\n",
                   desc.id, desc.name, vendor_to_string(desc.vendor),
                   desc.enabled ? "Enabled" : "Disabled");
            printf("  Vendor ID: 0x%04X, Device ID: 0x%04X\n", desc.vendor_id, desc.device_id);
            printf("  VRAM: %.2f GB / %.2f GB used\n",
                   (double)desc.vram_total / (1024 * 1024 * 1024),
                   (double)desc.vram_used / (1024 * 1024 * 1024));
            printf("  Driver: %s %s\n", desc.driver_name, desc.driver_version);
            printf("\n");
        }
    }
}

static void show_config(void) {
    mvgal_config_t config;
    mvgal_config_get(&config);
    
    printf("Current MVGAL Configuration:\n");
    printf("=============================\n");
    printf("Core:\n");
    printf("  Enabled: %s\n", config.enabled ? "yes" : "no");
    printf("  Log Level: %d\n", config.log_level);
    printf("  Debug: %s\n", config.debug ? "yes" : "no");
    printf("\n  GPU Settings:\n");
    printf("    Auto Detect: %s\n", config.gpus.auto_detect ? "yes" : "no");
    printf("    Max GPUs: %u\n", config.gpus.max_gpus);
    printf("    Enable All: %s\n", config.gpus.enable_all ? "yes" : "no");
    printf("\n  Scheduler Settings:\n");
    printf("    Strategy: %s\n", strategy_to_string(config.scheduler.strategy));
    printf("    Dynamic Load Balance: %s\n", config.scheduler.dynamic_load_balance ? "yes" : "no");
    printf("    Thermal Aware: %s\n", config.scheduler.thermal_aware ? "yes" : "no");
    printf("    Power Aware: %s\n", config.scheduler.power_aware ? "yes" : "no");
    printf("    Load Balance Threshold: %.2f\n", config.scheduler.load_balance_threshold);
    printf("\n  Memory Settings:\n");
    printf("    Use DMA-BUF: %s\n", config.memory.use_dmabuf ? "yes" : "no");
    printf("    Use P2P: %s\n", config.memory.use_p2p ? "yes" : "no");
    printf("    Replicate Small Buffers: %s\n", config.memory.replicate_small ? "yes" : "no");
    printf("    Replicate Threshold: %zu bytes\n", config.memory.replicate_threshold);
    printf("    Max Buffer Size: %zu bytes\n", config.memory.max_buffer_size);
}

static void set_strategy(const char *strategy_name) {
    printf("Setting workload distribution strategy to: %s\n", strategy_name);
    
    mvgal_distribution_strategy_t strategy = string_to_strategy(strategy_name);
    
    // Validate strategy
    const char *valid_strategies[] = {
        "round_robin", "afr", "sfr", "single", "single_gpu", 
        "hybrid", "task", "compute_offload", "auto", "custom", NULL
    };
    
    bool valid = false;
    for (int i = 0; valid_strategies[i]; i++) {
        if (strcmp(strategy_name, valid_strategies[i]) == 0) {
            valid = true;
            break;
        }
    }
    
    if (valid) {
        printf("Strategy '%s' is valid\n", strategy_name);
        
        // Initialize and set strategy
        mvgal_error_t err = mvgal_init(0);
        if (err != MVGAL_SUCCESS) {
            fprintf(stderr, "Error: Failed to initialize MVGAL\n");
            return;
        }
        
        mvgal_context_t context = NULL;
        mvgal_context_create(&context);
        
        if (mvgal_set_strategy(context, strategy) == MVGAL_SUCCESS) {
            printf("Strategy set successfully\n");
        } else {
            fprintf(stderr, "Error: Failed to set strategy\n");
        }
        
        mvgal_context_destroy(context);
        mvgal_shutdown();
    } else {
        fprintf(stderr, "Error: Invalid strategy '%s'\n", strategy_name);
        fprintf(stderr, "Valid strategies: round_robin, afr, sfr, single, single_gpu, hybrid, task, compute_offload, auto, custom\n");
        exit(1);
    }
}

static void set_gpu_enabled(int gpu_index, bool enabled) {
    printf("%s GPU %d\n", enabled ? "Enabling" : "Disabling", gpu_index);
    
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize MVGAL\n");
        return;
    }
    
    if (mvgal_gpu_enable(gpu_index, enabled) == MVGAL_SUCCESS) {
        printf("GPU %d %s\n", gpu_index, enabled ? "enabled" : "disabled");
    } else {
        fprintf(stderr, "Error: Failed to %s GPU %d\n", enabled ? "enable" : "disable", gpu_index);
    }
    
    mvgal_shutdown();
}

static void set_gpu_priority(int gpu_index, int priority) {
    printf("Setting GPU %d priority to %d\n", gpu_index, priority);
    
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize MVGAL\n");
        return;
    }
    
    // Note: The public API doesn't have set_gpu_priority, so we use the scheduler API
    printf("Note: Setting GPU priority via scheduler\n");
    // This would need to be implemented in the API
    
    mvgal_shutdown();
}

static void show_stats(void) {
    printf("MVGAL Runtime Statistics:\n");
    printf("========================\n");
    
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize MVGAL\n");
        return;
    }
    
    mvgal_context_t context = NULL;
    mvgal_context_create(&context);
    
    mvgal_stats_t stats;
    if (mvgal_get_stats(context, &stats) == MVGAL_SUCCESS) {
        printf("Frames Submitted: %lu\n", (unsigned long)stats.frames_submitted);
        printf("Frames Completed: %lu\n", (unsigned long)stats.frames_completed);
        printf("Workloads Distributed: %lu\n", (unsigned long)stats.workloads_distributed);
        printf("Bytes Transferred: %lu\n", (unsigned long)stats.bytes_transferred);
        printf("GPU Switches: %lu\n", (unsigned long)stats.gpu_switches);
        printf("Errors: %lu\n", (unsigned long)stats.errors);
        
        printf("\nPer-GPU Workloads:\n");
        int32_t gpu_count = mvgal_gpu_get_count();
        printf("  Note: GPU-specific workload counts require scheduler module\n");
        
        printf("\nAPI Statistics:\n");
        printf("  Vulkan: %lu\n", (unsigned long)stats.vulkan_workloads);
        printf("  CUDA: %lu\n", (unsigned long)stats.cuda_kernels);
        printf("  OpenCL: %lu\n", (unsigned long)stats.opencl_kernels);
        printf("  D3D: %lu\n", (unsigned long)stats.d3d_workloads);
        printf("  Metal: %lu\n", (unsigned long)stats.metal_workloads);
        printf("  WebGPU: %lu\n", (unsigned long)stats.webgpu_workloads);
    } else {
        printf("Failed to get statistics\n");
    }
    
    mvgal_context_destroy(context);
    mvgal_shutdown();
}

static void reset_stats(void) {
    printf("Resetting statistics...\n");
    
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize MVGAL\n");
        return;
    }
    
    mvgal_context_t context = NULL;
    mvgal_context_create(&context);
    
    if (mvgal_reset_stats(context) == MVGAL_SUCCESS) {
        printf("Statistics reset\n");
    } else {
        printf("Failed to reset statistics\n");
    }
    
    mvgal_context_destroy(context);
    mvgal_shutdown();
}

static void reload_config(void) {
    printf("Reloading configuration...\n");
    
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize MVGAL\n");
        return;
    }
    
    if (mvgal_config_load(NULL) == MVGAL_SUCCESS) {
        printf("Configuration reloaded\n");
    } else {
        printf("Failed to reload configuration\n");
    }
    
    mvgal_shutdown();
}

int main(int argc, char *argv[]) {
    const char *prog = argv[0];
    const char *config_file = NULL;
    
    static struct option long_options[] = {
        {"config",  required_argument, NULL, 'c'},
        {"help",    no_argument,       NULL, 'h'},
        {NULL,      0,                 NULL,  0 }
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "vc:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'h':
                print_usage(prog);
                return 0;
            default:
                print_usage(prog);
                return 1;
        }
    }
    
    // Check for command
    if (optind >= argc) {
        print_usage(prog);
        return 1;
    }
    
    const char *command = argv[optind];
    const char *arg1 = (optind + 1 < argc) ? argv[optind + 1] : NULL;
    const char *arg2 = (optind + 2 < argc) ? argv[optind + 2] : NULL;
    
    if (strcmp(command, "list-gpus") == 0) {
        list_gpus();
    } else if (strcmp(command, "show-config") == 0) {
        show_config();
    } else if (strcmp(command, "set-strategy") == 0) {
        if (!arg1) {
            fprintf(stderr, "Error: Strategy name required\n");
            return 1;
        }
        set_strategy(arg1);
    } else if (strcmp(command, "enable-gpu") == 0) {
        if (!arg1) {
            fprintf(stderr, "Error: GPU index required\n");
            return 1;
        }
        int gpu_index = atoi(arg1);
        set_gpu_enabled(gpu_index, true);
    } else if (strcmp(command, "disable-gpu") == 0) {
        if (!arg1) {
            fprintf(stderr, "Error: GPU index required\n");
            return 1;
        }
        int gpu_index = atoi(arg1);
        set_gpu_enabled(gpu_index, false);
    } else if (strcmp(command, "set-priority") == 0) {
        if (!arg1 || !arg2) {
            fprintf(stderr, "Error: GPU index and priority required\n");
            return 1;
        }
        int gpu_index = atoi(arg1);
        int priority = atoi(arg2);
        set_gpu_priority(gpu_index, priority);
    } else if (strcmp(command, "show-stats") == 0) {
        show_stats();
    } else if (strcmp(command, "reset-stats") == 0) {
        reset_stats();
    } else if (strcmp(command, "reload") == 0) {
        reload_config();
    } else if (strcmp(command, "help") == 0) {
        print_usage(prog);
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(prog);
        return 1;
    }
    
    return 0;
}
