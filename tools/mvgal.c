/**
 * @file mvgal.c
 * @brief MVGAL Main Command-Line Interface
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Main entry point for MVGAL operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/userspace/core/mvgal.h"

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS] COMMAND\n\n", prog);
    printf("MVGAL - Multi-Vendor GPU Aggregation Layer\n\n");
    printf("Main Commands:\n");
    printf("  start              Start MVGAL daemon\n");
    printf("  stop               Stop MVGAL daemon\n");
    printf("  status             Show MVGAL status\n");
    printf("  restart            Restart MVGAL daemon\n");
    printf("\n");
    printf("Information Commands:\n");
    printf("  list-gpus          List detected GPUs\n");
    printf("  show-config        Show current configuration\n");
    printf("  show-stats         Show runtime statistics\n");
    printf("\n");
    printf("Configuration Commands:\n");
    printf("  set-strategy ST    Set workload distribution strategy\n");
    printf("  enable-gpu N       Enable GPU index N\n");
    printf("  disable-gpu N      Disable GPU index N\n");
    printf("  set-priority N P   Set GPU N priority to P\n");
    printf("\n");
    printf("Management Commands:\n");
    printf("  reset-stats        Reset all statistics\n");
    printf("  reload            Reload configuration\n");
    printf("  load-module       Load kernel module\n");
    printf("  unload-module     Unload kernel module\n");
    printf("\n");
    printf("Benchmark Commands:\n");
    printf("  benchmark         Run all benchmarks\n");
    printf("  bench-synthetic   Run synthetic benchmarks\n");
    printf("  bench-realworld   Run real-world benchmarks\n");
    printf("  bench-stress      Run stress benchmarks\n");
    printf("\n");
    printf("Options:\n");
    printf("  -c, --config FILE Use alternative config file\n");
    printf("  -h, --help        Show this help message\n");
    printf("  --version         Show version information\n");
    printf("\n");
    printf("Strategies:\n");
    printf("  round_robin       Round-robin distribution\n");
    printf("  afr              Alternate Frame Rendering\n");
    printf("  sfr              Split Frame Rendering\n");
    printf("  single           Single GPU only\n");
    printf("  hybrid           Hybrid distribution\n");
    printf("  custom           Custom strategy\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s list-gpus\n", prog);
    printf("  %s set-strategy round_robin\n", prog);
    printf("  %s load-module\n", prog);
    printf("  %s benchmark\n", prog);
}

static void print_version(void) {
    printf("MVGAL - Multi-Vendor GPU Aggregation Layer\n");
    printf("Version: 0.1.0\n");
    printf("Build Date: %s %s\n", __DATE__, __TIME__);
    printf("License: GPL-3.0-or-later\n");
    printf("\n");
    printf("Supported APIs:\n");
    printf("  CUDA Driver API (cu*)\n");
    printf("  CUDA Runtime API (cuda*)\n");
    printf("  Direct3D (D3D11, D3D12, DXGI)\n");
    printf("  Metal API\n");
    printf("  WebGPU\n");
    printf("  OpenCL\n");
}

static void show_status(void) {
    printf("=== MVGAL Status ===\n");
    
    int gpu_count = mvgal_get_gpu_count();
    printf("GPUs Detected: %d\n", gpu_count);
    
    mvgal_strategy_t strategy = mvgal_get_strategy();
    const char *strategy_name = "Unknown";
    switch (strategy) {
        case MVGAL_STRATEGY_ROUND_ROBIN: strategy_name = "Round Robin"; break;
        case MVGAL_STRATEGY_AFR: strategy_name = "AFR"; break;
        case MVGAL_STRATEGY_SFR: strategy_name = "SFR"; break;
        case MVGAL_STRATEGY_SINGLE: strategy_name = "Single GPU"; break;
        case MVGAL_STRATEGY_HYBRID: strategy_name = "Hybrid"; break;
        case MVGAL_STRATEGY_CUSTOM: strategy_name = "Custom"; break;
    }
    printf("Strategy: %s\n", strategy_name);
    
    mvgal_stats_t stats;
    if (mvgal_get_stats(&stats) == 0) {
        printf("Total Workloads: %lu\n", (unsigned long)stats.total_workloads);
        printf("Load Balance: %.2f%%\n", stats.load_balance);
    }
    
    printf("\nGPU List:\n");
    for (int i = 0; i < gpu_count; i++) {
        mvgal_gpu_info_t info;
        if (mvgal_get_gpu_info(i, &info) == 0) {
            printf("  [%d] %s (%s) - %s, Priority: %d\n",
                   info.id, info.name, info.vendor,
                   info.enabled ? "Enabled" : "Disabled", info.priority);
        }
    }
}

int main(int argc, char *argv[]) {
    const char *prog = argv[0];
    const char *config_file = NULL;
    
    // Parse command line
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -c/--config requires an argument\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(prog);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
    }
    
    // Check for command
    if (argc < 2) {
        print_usage(prog);
        return 1;
    }
    
    const char *command = argv[1];
    const char *arg1 = (argc > 2) ? argv[2] : NULL;
    const char *arg2 = (argc > 3) ? argv[3] : NULL;
    
    // Initialize MVGAL
    if (mvgal_init(config_file) != 0) {
        fprintf(stderr, "Error: Failed to initialize MVGAL\n");
        return 1;
    }
    
    int result = 0;
    
    if (strcmp(command, "start") == 0) {
        printf("Starting MVGAL daemon...\n");
        printf("Note: MVGAL runs via LD_PRELOAD, no daemon currently\n");
    } else if (strcmp(command, "stop") == 0) {
        printf("Stopping MVGAL daemon...\n");
        printf("Note: Cannot stop LD_PRELOAD-based interception\n");
    } else if (strcmp(command, "status") == 0) {
        show_status();
    } else if (strcmp(command, "restart") == 0) {
        printf("Restarting MVGAL...\n");
        mvgal_shutdown();
        mvgal_init(config_file);
        printf("MVGAL restarted\n");
    } else if (strcmp(command, "list-gpus") == 0) {
        show_status();
    } else if (strcmp(command, "show-config") == 0) {
        printf("Configuration:\n");
        mvgal_config_t config;
        if (mvgal_get_config(&config) == 0) {
            printf("  Enabled: %s\n", config.enabled ? "yes" : "no");
            printf("  Debug Level: %s\n", config.debug_level);
            printf("  GPU Count: %d\n", config.gpu_count);
            printf("  Memory Migration: %s\n", config.enable_memory_migration ? "enabled" : "disabled");
            printf("  DMA-BUF: %s\n", config.enable_dmabuf ? "enabled" : "disabled");
        }
    } else if (strcmp(command, "show-stats") == 0) {
        show_status();
    } else if (strcmp(command, "set-strategy") == 0) {
        if (!arg1) {
            fprintf(stderr, "Error: Strategy name required\n");
            result = 1;
        } else {
            mvgal_strategy_t strategy;
            if (strcmp(arg1, "round_robin") == 0) strategy = MVGAL_STRATEGY_ROUND_ROBIN;
            else if (strcmp(arg1, "afr") == 0) strategy = MVGAL_STRATEGY_AFR;
            else if (strcmp(arg1, "sfr") == 0) strategy = MVGAL_STRATEGY_SFR;
            else if (strcmp(arg1, "single") == 0) strategy = MVGAL_STRATEGY_SINGLE;
            else if (strcmp(arg1, "hybrid") == 0) strategy = MVGAL_STRATEGY_HYBRID;
            else if (strcmp(arg1, "custom") == 0) strategy = MVGAL_STRATEGY_CUSTOM;
            else {
                fprintf(stderr, "Error: Unknown strategy '%s'\n", arg1);
                result = 1;
                goto done;
            }
            if (mvgal_set_strategy(strategy) == 0) {
                printf("Strategy set to: %s\n", arg1);
            } else {
                fprintf(stderr, "Error: Failed to set strategy\n");
                result = 1;
            }
        }
    } else if (strcmp(command, "enable-gpu") == 0) {
        if (!arg1) {
            fprintf(stderr, "Error: GPU index required\n");
            result = 1;
        } else {
            int index = atoi(arg1);
            if (mvgal_set_gpu_enabled(index, true) == 0) {
                printf("GPU %d enabled\n", index);
            } else {
                fprintf(stderr, "Error: Failed to enable GPU %d\n", index);
                result = 1;
            }
        }
    } else if (strcmp(command, "disable-gpu") == 0) {
        if (!arg1) {
            fprintf(stderr, "Error: GPU index required\n");
            result = 1;
        } else {
            int index = atoi(arg1);
            if (mvgal_set_gpu_enabled(index, false) == 0) {
                printf("GPU %d disabled\n", index);
            } else {
                fprintf(stderr, "Error: Failed to disable GPU %d\n", index);
                result = 1;
            }
        }
    } else if (strcmp(command, "set-priority") == 0) {
        if (!arg1 || !arg2) {
            fprintf(stderr, "Error: GPU index and priority required\n");
            result = 1;
        } else {
            int index = atoi(arg1);
            int priority = atoi(arg2);
            if (mvgal_set_gpu_priority(index, priority) == 0) {
                printf("GPU %d priority set to %d\n", index, priority);
            } else {
                fprintf(stderr, "Error: Failed to set priority\n");
                result = 1;
            }
        }
    } else if (strcmp(command, "reset-stats") == 0) {
        if (mvgal_reset_stats() == 0) {
            printf("Statistics reset\n");
        } else {
            fprintf(stderr, "Error: Failed to reset statistics\n");
            result = 1;
        }
    } else if (strcmp(command, "reload") == 0) {
        if (mvgal_reload_config() == 0) {
            printf("Configuration reloaded\n");
        } else {
            fprintf(stderr, "Error: Failed to reload configuration\n");
            result = 1;
        }
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(prog);
        result = 1;
    }
    
    done:
    // Shutdown MVGAL
    mvgal_shutdown();
    
    return result;
}
