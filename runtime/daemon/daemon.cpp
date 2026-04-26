/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Daemon Class Implementation - Runtime daemon core
 * 
 * SPDX-License-Identifier: MIT
 */

#include "daemon.hpp"

/* External declaration for global running flag from main.cpp */
extern volatile sig_atomic_t g_running;
#include "scheduler.hpp"
#include "memory_manager.hpp"
#include "device_registry.hpp"
#include "ipc_server.hpp"
#include "power_manager.hpp"
#include "metrics_collector.hpp"

#include <iostream>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace mvgal {

Daemon::Daemon()
{
}

Daemon::~Daemon()
{
    cleanup();
}

bool Daemon::init()
{
    /* Create directories if they don't exist */
    if (!ensureDirectory("/etc/mvgal") ||
        !ensureDirectory("/var/log/mvgal") ||
        !ensureDirectory("/run/mvgal")) {
        std::cerr << "Failed to create directories" << std::endl;
        return false;
    }

    /* Initialize subsystems in dependency order */
    m_device_registry = std::make_unique<DeviceRegistry>(this);
    m_memory_manager = std::make_unique<MemoryManager>(this);
    m_scheduler = std::make_unique<Scheduler>(this);
    m_power_manager = std::make_unique<PowerManager>(this, m_device_registry.get());
    m_metrics_collector = std::make_unique<MetricsCollector>(this, m_device_registry.get());
    m_ipc_server = std::make_unique<IpcServer>(this);

    if (!m_device_registry || !m_memory_manager || !m_scheduler || 
        !m_power_manager || !m_metrics_collector || !m_ipc_server) {
        std::cerr << "Failed to allocate subsystems" << std::endl;
        return false;
    }

    if (!m_device_registry->init()) {
        std::cerr << "Failed to initialize device registry" << std::endl;
        return false;
    }

    if (!m_memory_manager->init()) {
        std::cerr << "Failed to initialize memory manager" << std::endl;
        return false;
    }

    if (!m_scheduler->init()) {
        std::cerr << "Failed to initialize scheduler" << std::endl;
        return false;
    }

    if (!m_power_manager->init()) {
        std::cerr << "Failed to initialize power manager" << std::endl;
        return false;
    }

    if (!m_metrics_collector->init()) {
        std::cerr << "Failed to initialize metrics collector" << std::endl;
        return false;
    }

    /* Initialize IPC server */
    if (!m_ipc_server->init("/run/mvgal/mvgal.sock")) {
        std::cerr << "Failed to initialize IPC server" << std::endl;
        return false;
    }

    /* Write PID file */
    writePidFile("/etc/mvgal/mvgald.pid");

    return true;
}

void Daemon::cleanup()
{
    /* Cleanup in reverse order */
    if (m_ipc_server) m_ipc_server->fini();
    if (m_metrics_collector) m_metrics_collector->fini();
    if (m_power_manager) m_power_manager->fini();
    if (m_scheduler) m_scheduler->fini();
    if (m_memory_manager) m_memory_manager->fini();
    if (m_device_registry) m_device_registry->fini();

    /* Remove PID file */
    std::remove("/etc/mvgal/mvgald.pid");

    /* Reset unique_ptrs */
    m_ipc_server.reset();
    m_metrics_collector.reset();
    m_power_manager.reset();
    m_scheduler.reset();
    m_memory_manager.reset();
    m_device_registry.reset();

    std::cout << "Daemon cleanup complete" << std::endl;
}

void Daemon::run()
{
    std::cout << "MVGAL daemon version 0.2.0 started" << std::endl;
    std::cout << "Socket: /run/mvgal/mvgal.sock" << std::endl;

    /* Main loop */
    while (g_running) {
        /* Process IPC events */
        m_ipc_server->processEvents();

        /* Process device events */
        m_device_registry->processEvents();

        /* Process power management */
        m_power_manager->processEvents();

        /* Process metrics collection */
        m_metrics_collector->processEvents();

        /* Sleep briefly to avoid busy loop */
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

/* static */
bool Daemon::ensureDirectory(const std::string& path)
{
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        return true; /* Already exists */
    }
    return false;
}

/* static */
void Daemon::writePidFile(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

} // namespace mvgal
