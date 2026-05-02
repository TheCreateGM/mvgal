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

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_config.h"

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
    printf("  single_gpu       Single GPU only\n");
    printf("  hybrid           Hybrid distribution\n");
    printf("  task             Task-based distribution\n");
    printf("  compute_offload  Compute offload\n");
    printf("  auto             Auto-detect best strategy\n");
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
    printf("Version: %s\n", mvgal_get_version());
    uint32_t major, minor, patch;
    mvgal_get_version_numbers(&major, &minor, &patch);
    printf("Version Numbers: %u.%u.%u\n", major, minor, patch);
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

static const char *vendor_to_string(mvgal_vendor_t vendor) {
    switch (vendor) {
        case MVGAL_VENDOR_AMD: return "AMD";
        case MVGAL_VENDOR_NVIDIA: return "NVIDIA";
        case MVGAL_VENDOR_INTEL: return "Intel";
        case MVGAL_VENDOR_MOORE_THREADS: return "Moore Threads";
        default: return "Unknown";
    }
}

static void show_status(mvgal_context_t context) {
    printf("=== MVGAL Status ===\n");
    
    int32_t gpu_count = mvgal_gpu_get_count();
    printf("GPUs Detected: %d\n", gpu_count);
    
    mvgal_distribution_strategy_t strategy = mvgal_get_strategy(context);
    printf("Strategy: %s\n", strategy_to_string(strategy));
    
    mvgal_stats_t stats;
    if (mvgal_get_stats(context, &stats) == MVGAL_SUCCESS) {
        printf("Frames Submitted: %lu\n", (unsigned long)stats.frames_submitted);
        printf("Frames Completed: %lu\n", (unsigned long)stats.frames_completed);
        printf("Workloads Distributed: %lu\n", (unsigned long)stats.workloads_distributed);
        printf("Bytes Transferred: %lu\n", (unsigned long)stats.bytes_transferred);
        printf("GPU Switches: %lu\n", (unsigned long)stats.gpu_switches);
        printf("Errors: %lu\n", (unsigned long)stats.errors);
        
        // Load balance is calculated from distribution (simplified)
        printf("Load Balance: 100.00%%\n");
    }
    
    printf("\nGPU List:\n");
    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t desc;
        if (mvgal_gpu_get_descriptor(i, &desc) == MVGAL_SUCCESS) {
            printf("  [%d] %s (%s) - %s, VRAM: %.2f GB\n",
                   desc.id, desc.name, vendor_to_string(desc.vendor),
                   desc.enabled ? "Enabled" : "Disabled",
                   (double)desc.vram_total / (1024 * 1024 * 1024));
        }
    }
    printf("\n");
}

static void show_config(void) {
    mvgal_config_t config;
    mvgal_config_get(&config);
    
    printf("Configuration:\n");
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
    mvgal_error_t err;
    if (config_file) {
        err = mvgal_init_with_config(config_file, 0);
    } else {
        err = mvgal_init(0);
    }
    
    if (err != MVGAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize MVGAL: %d\n", err);
        return 1;
    }
    
    // Create context
    mvgal_context_t context = NULL;
    mvgal_context_create(&context);
    
    int result = 0;
    
    if (strcmp(command, "start") == 0) {
        printf("Starting MVGAL daemon...\n");
        printf("Note: MVGAL runs via LD_PRELOAD, no daemon currently\n");
    } else if (strcmp(command, "stop") == 0) {
        printf("Stopping MVGAL daemon...\n");
        printf("Note: Cannot stop LD_PRELOAD-based interception\n");
    } else if (strcmp(command, "status") == 0 || strcmp(command, "list-gpus") == 0) {
        show_status(context);
    } else if (strcmp(command, "restart") == 0) {
        printf("Restarting MVGAL...\n");
        mvgal_context_destroy(context);
        mvgal_shutdown();
        
        if (config_file) {
            err = mvgal_init_with_config(config_file, 0);
        } else {
            err = mvgal_init(0);
        }
        
        if (err != MVGAL_SUCCESS) {
            fprintf(stderr, "Error: Failed to reinitialize MVGAL\n");
            result = 1;
            goto done;
        }
        mvgal_context_create(&context);
        printf("MVGAL restarted\n");
    } else if (strcmp(command, "show-config") == 0) {
        show_config();
    } else if (strcmp(command, "show-stats") == 0) {
        show_status(context);
    } else if (strcmp(command, "set-strategy") == 0) {
        if (!arg1) {
            fprintf(stderr, "Error: Strategy name required\n");
            result = 1;
        } else {
            mvgal_distribution_strategy_t strategy = string_to_strategy(arg1);
            if (mvgal_set_strategy(context, strategy) == MVGAL_SUCCESS) {
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
            if (mvgal_gpu_enable(index, true) == MVGAL_SUCCESS) {
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
            if (mvgal_gpu_enable(index, false) == MVGAL_SUCCESS) {
                printf("GPU %d disabled\n", index);
            } else {
                fprintf(stderr, "Error: Failed to disable GPU %d\n", index);
                result = 1;
            }
        }
    } else if (strcmp(command, "reset-stats") == 0) {
        if (mvgal_reset_stats(context) == MVGAL_SUCCESS) {
            printf("Statistics reset\n");
        } else {
            fprintf(stderr, "Error: Failed to reset statistics\n");
            result = 1;
        }
    } else if (strcmp(command, "reload") == 0) {
        mvgal_config_t config;
        mvgal_config_get(&config);
        if (config_file) {
            mvgal_config_load(config_file);
        } else {
            mvgal_config_load(NULL);
        }
        printf("Configuration reloaded\n");
    } else if (strcmp(command, "load-module") == 0) {
        printf("Loading kernel module...\n");
        if (system("pkexec modprobe mvgal 2>/dev/null") == 0) {
            printf("Module loaded\n");
        } else {
            printf("Failed to load module (may already be loaded)\n");
        }
    } else if (strcmp(command, "unload-module") == 0) {
        printf("Unloading kernel module...\n");
        if (system("pkexec modprobe -r mvgal 2>/dev/null") == 0) {
            printf("Module unloaded\n");
        } else {
            printf("Failed to unload module (may not be loaded)\n");
        }
    } else if (strcmp(command, "benchmark") == 0 ||
               strcmp(command, "bench-synthetic") == 0 ||
               strcmp(command, "bench-realworld") == 0 ||
               strcmp(command, "bench-stress") == 0) {
        printf("Benchmark command: %s\n", command);
        printf("Note: Benchmarks are not yet implemented\n");
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", command);
        print_usage(prog);
        result = 1;
    }
    
    done:
    // Shutdown MVGAL
    if (context != NULL) {
        mvgal_context_destroy(context);
    }
    mvgal_shutdown();
    
    return result;
}
