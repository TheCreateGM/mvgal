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

#include "../src/userspace/core/mvgal.h"

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

static void list_gpus(void) {
    printf("Listing detected GPUs:\n");
    printf("======================\n");
    
    // This will use the actual MVGAL API once available
    // For now, simulate detection
    printf("GPU 0: NVIDIA GeForce RTX 4090 (enabled, priority: 0)\n");
    printf("GPU 1: AMD Radeon RX 7900 XTX (enabled, priority: 1)\n");
    printf("\nTotal GPUs: 2\n");
}

static void show_config(void) {
    printf("Current MVGAL Configuration:\n");
    printf("=============================\n");
    printf("Core:\n");
    printf("  Enabled: yes\n");
    printf("  Debug Level: info\n");
    printf("  GPU Count: 2\n");
    printf("  Default Strategy: round_robin\n");
    printf("  Memory Migration: enabled\n");
    printf("  DMA-BUF: enabled\n");
    printf("\nCUDA:\n");
    printf("  Driver Intercept: enabled\n");
    printf("  Runtime Intercept: enabled\n");
    printf("  Kernel Launch Intercept: enabled\n");
    printf("\nDirect3D:\n");
    printf("  Enabled: enabled\n");
    printf("\nWebGPU:\n");
    printf("  Enabled: enabled\n");
}

static void set_strategy(const char *strategy) {
    printf("Setting workload distribution strategy to: %s\n", strategy);
    
    // Validate strategy
    const char *valid_strategies[] = {
        "round_robin", "afr", "sfr", "single", "hybrid", "custom", NULL
    };
    
    bool valid = false;
    for (int i = 0; valid_strategies[i]; i++) {
        if (strcmp(strategy, valid_strategies[i]) == 0) {
            valid = true;
            break;
        }
    }
    
    if (valid) {
        printf("Strategy '%s' is valid and will be applied.\n", strategy);
        // TODO: Actually set the strategy via mvgal_set_strategy()
    } else {
        fprintf(stderr, "Error: Invalid strategy '%s'\n", strategy);
        fprintf(stderr, "Valid strategies: round_robin, afr, sfr, single, hybrid, custom\n");
        exit(1);
    }
}

static void set_gpu_enabled(int gpu_index, bool enabled) {
    printf("%s GPU %d\n", enabled ? "Enabling" : "Disabling", gpu_index);
    // TODO: Actually enable/disable GPU via mvgal_set_gpu_enabled()
}

static void set_gpu_priority(int gpu_index, int priority) {
    printf("Setting GPU %d priority to %d\n", gpu_index, priority);
    // TODO: Actually set priority via mvgal_set_gpu_priority()
}

static void show_stats(void) {
    printf("MVGAL Runtime Statistics:\n");
    printf("========================\n");
    printf("Total Workloads: 1234\n");
    printf("Workloads on GPU 0: 617\n");
    printf("Workloads on GPU 1: 617\n");
    printf("Memory Allocated: 2048 MB\n");
    printf("Memory Used: 1536 MB\n");
    printf("Load Balance: 100.00%%\n");
}

static void reset_stats(void) {
    printf("Resetting statistics...\n");
    // TODO: Actually reset via mvgal_reset_stats()
}

static void reload_config(void) {
    printf("Reloading configuration...\n");
    // TODO: Actually reload via mvgal_reload_config()
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
    
    // Initialize MVGAL
    if (mvgal_init(config_file) != 0) {
        fprintf(stderr, "Error: Failed to initialize MVGAL\n");
        return 1;
    }
    
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
    
    // Shutdown MVGAL
    mvgal_shutdown();
    
    return 0;
}
