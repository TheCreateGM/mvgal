/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * User-Space Runtime Daemon - Entry Point
 * 
 * SPDX-License-Identifier: MIT
 */

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <signal.h>
#include <unistd.h>

#include "daemon.hpp"

/* Version information */
#define MVGAL_DAEMON_VERSION "0.2.2"
#define MVGAL_DAEMON_NAME "mvgald"

/* Socket path */
#define MVGAL_SOCKET_PATH "/run/mvgal/mvgal.sock"

/* Configuration directory */
#define MVGAL_CONFIG_DIR "/etc/mvgal"

/* PID file */
#define MVGAL_PID_FILE MVGAL_CONFIG_DIR "/" MVGAL_DAEMON_NAME ".pid"

/* Global daemon instance */
static std::unique_ptr<mvgal::Daemon> g_daemon = nullptr;

/* Signal handling */
volatile sig_atomic_t g_running = 1;

/* Signal handler */
static void signalHandler(int signal)
{
    switch (signal) {
    case SIGINT:
    case SIGTERM:
    case SIGQUIT:
        g_running = 0;
        /* Force exit after a brief grace period — nanosleep isn't SA_RESTART-respected */
        {
            static struct sigaction sa_exit;
            memset(&sa_exit, 0, sizeof(sa_exit));
            sa_exit.sa_handler = SIG_DFL;
            sigaction(SIGALRM, &sa_exit, nullptr);
            alarm(2);  /* If main loop hasn't exited in 2s, SIGALRM kills us */
        }
        break;
    case SIGALRM:
        /* Emergency exit — main loop didn't respond */
        _exit(1);
        break;
    default:
        break;
    }
}

static void setupSignalHandlers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    /* SIGALRM: emergency kill after shutdown grace period — no SA_RESTART */
    struct sigaction sa_alarm;
    memset(&sa_alarm, 0, sizeof(sa_alarm));
    sa_alarm.sa_handler = signalHandler;
    sigaction(SIGALRM, &sa_alarm, nullptr);

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);
}

/* Check if another instance is running */
static bool checkAnotherInstance()
{
    FILE* f = fopen(MVGAL_PID_FILE, "r");
    if (f) {
        pid_t pid;
        if (fscanf(f, "%d", &pid) == 1) {
            if (kill(pid, 0) == 0) {
                std::cerr << "Another instance is already running (PID: " << pid << ")" << std::endl;
                fclose(f);
                return true;
            }
        }
        fclose(f);
    }
    return false;
}

/* Main entry point */
int main(int argc, char* argv[])
{
    /* Parse command line arguments */
    bool daemonize = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-daemon") == 0 || strcmp(argv[i], "-n") == 0) {
            daemonize = false;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: " << MVGAL_DAEMON_NAME << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -n, --no-daemon    Don't daemonize" << std::endl;
            std::cout << "  -h, --help         Show this help" << std::endl;
            std::cout << "  --version          Show version" << std::endl;
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            std::cout << MVGAL_DAEMON_NAME << " version " << MVGAL_DAEMON_VERSION << std::endl;
            return 0;
        }
    }

    /* Check for another instance */
    if (checkAnotherInstance()) {
        return 1;
    }

    /* Setup signal handlers */
    setupSignalHandlers();

    /* Create daemon instance */
    g_daemon = std::make_unique<mvgal::Daemon>();
    if (!g_daemon) {
        std::cerr << "Failed to create daemon instance" << std::endl;
        return 1;
    }

    /* Daemonize before init so PID file has correct PID */
    if (daemonize) {
        int ret = daemon(1, 0);
        if (ret < 0) {
            std::cerr << "Failed to daemonize: " << strerror(errno) << std::endl;
            return 1;
        }
    }

    /* Initialize subsystems (PID written here has correct PID after daemonize) */
    if (!g_daemon->init()) {
        std::cerr << "Failed to initialize daemon" << std::endl;
        return 1;
    }

    /* Run main loop */
    g_daemon->run();

    /* Cleanup and exit */
    g_daemon.reset();
    std::cout << "Daemon stopped" << std::endl;

    return 0;
}
