/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * Daemon Class Header - Runtime daemon core
 * 
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_DAEMON_HPP
#define MVGAL_RUNTIME_DAEMON_HPP

#include <memory>
#include <string>
#include <csignal>

namespace mvgal {

/* Forward declarations */
class Scheduler;
class MemoryManager;
class DeviceRegistry;
class IpcServer;
class PowerManager;
class MetricsCollector;

/**
 * Main Daemon class - orchestrates all subsystems
 */
class Daemon {
public:
    Daemon();
    ~Daemon();

    bool init();
    void cleanup();
    void run();

    /* Subsystem accessors */
    Scheduler& scheduler() { return *m_scheduler; }
    MemoryManager& memoryManager() { return *m_memory_manager; }
    DeviceRegistry& deviceRegistry() { return *m_device_registry; }
    IpcServer& ipcServer() { return *m_ipc_server; }
    PowerManager& powerManager() { return *m_power_manager; }
    MetricsCollector& metricsCollector() { return *m_metrics_collector; }

private:
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<MemoryManager> m_memory_manager;
    std::unique_ptr<DeviceRegistry> m_device_registry;
    std::unique_ptr<IpcServer> m_ipc_server;
    std::unique_ptr<PowerManager> m_power_manager;
    std::unique_ptr<MetricsCollector> m_metrics_collector;

    /* Helper functions */
    static bool ensureDirectory(const std::string& path);
    static void writePidFile(const std::string& path);

    /* Prevent copying */
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_DAEMON_HPP
