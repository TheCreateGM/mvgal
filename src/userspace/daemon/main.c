/**
 * @file main.c
 * @brief MVGAL Daemon main entry point
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This is the main entry point for the MVGAL background daemon.
 * It initializes all subsystems and manages the main event loop.
 */

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_config.h"
#include "mvgal/mvgal_gpu.h"
#include "mvgal/mvgal_log.h"
#include "mvgal/mvgal_ipc.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>

/**
 * @brief Global shutdown flag
 */
static volatile bool g_shutdown = false;

/**
 * @brief Signal handler for clean shutdown
 */
static void signal_handler(int sig) {
    (void)sig;
    g_shutdown = true;
}

/**
 * @brief Setup signal handlers
 */
static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_RESTART;
    
    // Handle termination signals
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    
    // Handle HUP for config reload
    sigaction(SIGHUP, &sa, NULL);
}

/**
 * @brief Create runtime directory
 */
static void create_runtime_dir(void) {
    const char *runtime_dir = "/var/run/mvgal";
    
    if (mkdir(runtime_dir, 0755) < 0 && errno != EEXIST) {
        MVGAL_LOG_WARN("Failed to create runtime directory %s: %s", 
                       runtime_dir, strerror(errno));
    }
}

/**
 * @brief Cleanup runtime directory
 */
static void cleanup_runtime_dir(void) {
    const char *runtime_dir = "/var/run/mvgal";
    const char *socket_path = getenv("MVGAL_SOCKET_PATH");
    if (socket_path == NULL) {
        socket_path = "/var/run/mvgal/mvgal.sock";
    }
    
    // Remove socket file
    unlink(socket_path);
    
    // Only clean up /var/run/mvgal if using default socket
    if (strstr(socket_path, "/var/run/mvgal") == socket_path) {
        // Remove directory (will fail if not empty, that's OK)
        rmdir(runtime_dir);
    }
}

/**
 * @brief Initialize all subsystems
 */
static mvgal_error_t init_subsystems(void) {
    mvgal_error_t err;
    
    // Initialize configuration
    err = mvgal_config_init();
    if (err != MVGAL_SUCCESS) {
        MVGAL_LOG_ERROR("Failed to initialize configuration: %d", err);
        return err;
    }
    
    // Initialize logging (use config)
    // For now, just use stderr MVGAL_LOG_INFO directly
    // In a real implementation, this would use the config
    mvgal_config_t config;
    mvgal_config_get(&config);
    
    MVGAL_LOG_INFO("MVGAL Daemon starting...");
    MVGAL_LOG_INFO("Log level: %d", config.log_level);
    
    // Initialize GPU manager
    MVGAL_LOG_INFO("Initializing GPU manager...");
    // Note: GPU detection happens on demand in gpu_manager.c
    // We just ensure it's ready to be used
    
    // Initialize IPC server
    MVGAL_LOG_INFO("Initializing IPC server...");
    const char *socket_path = getenv("MVGAL_SOCKET_PATH");
    err = mvgal_ipc_server_init(socket_path);
    if (err != MVGAL_SUCCESS) {
        MVGAL_LOG_ERROR("Failed to initialize IPC server: %d", err);
        return err;
    }
    
    // Start IPC server thread
    err = mvgal_ipc_server_start();
    if (err != MVGAL_SUCCESS) {
        MVGAL_LOG_ERROR("Failed to start IPC server thread: %d", err);
        mvgal_ipc_server_cleanup();
        return err;
    }
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Cleanup all subsystems
 */
static void cleanup_subsystems(void) {
    mvgal_config_t config;
    mvgal_config_get(&config);
    
    MVGAL_LOG_INFO("Cleaning up subsystems...");
    
    // Stop and cleanup IPC server
    mvgal_ipc_server_stop();
    mvgal_ipc_server_cleanup();
    
    // Cleanup configuration
    mvgal_config_shutdown();
    
    MVGAL_LOG_INFO("All subsystems cleaned up");
}

/**
 * @brief Daemonize the process
 */
static void daemonize(void) {
    
    // Fork to run in background
    pid_t pid = fork();
    if (pid < 0) {
        MVGAL_LOG_ERROR("Failed to fork: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    if (pid > 0) {
        // Parent exits
        exit(EXIT_SUCCESS);
    }
    
    // Child continues
    
    // Create new session
    if (setsid() < 0) {
        MVGAL_LOG_ERROR("Failed to create new session: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // Set working directory to root
    chdir("/");
    
    // Set file mode mask
    umask(0);
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Reopen stderr for logging
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDIN_FILENO) {
        dup2(fd, STDIN_FILENO);
    }
    if (fd != STDOUT_FILENO) {
        dup2(fd, STDOUT_FILENO);
    }
    if (fd != STDERR_FILENO) {
        dup2(fd, STDERR_FILENO);
    }
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}

/**
 * @brief Write PID file
 */
static void write_pid_file(void) {
    const char *pid_file = "/var/run/mvgal/mvgal.pid";
    FILE *f = fopen(pid_file, "w");
    if (f != NULL) {
        fprintf(f, "%ld\n", (long)getpid());
        fclose(f);
    } else {
        MVGAL_LOG_WARN("Failed to write PID file: %s", strerror(errno));
    }
}

/**
 * @brief Main event loop
 */
static void main_loop(void) {
    MVGAL_LOG_INFO("MVGAL Daemon running (PID: %ld)", (long)getpid());
    
    while (!g_shutdown) {
        // Sleep in 1-second intervals
        // In a real implementation, this would use epoll/poll
        // to wait for IPC events
        sleep(1);
        
        // Periodic tasks could go here
        // For example: load balancing updates
    }
    
    MVGAL_LOG_INFO("Shutdown signal received");
}

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    // Create runtime directory
    create_runtime_dir();
    
    // Setup signal handlers
    setup_signals();
    
    // Initialize logging (basic, before config)
    // Note: Full logging setup happens after config init
    
    // Daemonize if configured
    // Note: daemon mode is controlled by config, but we check env var for override
    const char *no_daemon = getenv("MVGAL_NO_DAEMON");
    const char *force_daemon = getenv("MVGAL_DAEMON");
    
    bool daemonize_flag = true;
    if (no_daemon != NULL && strcmp(no_daemon, "1") == 0) {
        daemonize_flag = false;
    } else if (force_daemon != NULL && strcmp(force_daemon, "0") == 0) {
        daemonize_flag = false;
    }
    
    if (daemonize_flag) {
        daemonize();
    }
    
    // Write PID file
    write_pid_file();
    
    // Initialize all subsystems
    if (init_subsystems() != MVGAL_SUCCESS) {
        cleanup_runtime_dir();
        return EXIT_FAILURE;
    }
    
    // Run main loop
    main_loop();
    
    // Cleanup
    cleanup_subsystems();
    
    // Remove PID file
    unlink("/var/run/mvgal/mvgal.pid");
    
    // Cleanup runtime directory
    cleanup_runtime_dir();
    
    MVGAL_LOG_INFO("MVGAL Daemon stopped");
    
    return EXIT_SUCCESS;
}
