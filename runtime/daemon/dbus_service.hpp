/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 *
 * D-Bus Service Header - Provides D-Bus interface for MVGAL daemon control
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_DBUS_SERVICE_HPP
#define MVGAL_RUNTIME_DBUS_SERVICE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace mvgal {

/* Forward declarations */
class Daemon;

/**
 * D-Bus Service - Provides D-Bus interface for MVGAL daemon
 *
 * Bus name: org.mvgal.MVGAL
 * Object path: /org/mvgal/daemon
 * Interface: org.mvgal.MVGAL
 */
class DBusService {
public:
    explicit DBusService(Daemon* daemon);
    ~DBusService();

    /* Initialize D-Bus service */
    bool init();

    /* Shutdown D-Bus service */
    void fini();

    /* Process D-Bus events */
    void processEvents();

    /* Emit D-Bus signals */
    void emitGPUHotplug(uint32_t gpuIndex, bool added);
    void emitTemperatureWarning(uint32_t gpuIndex, int32_t temperature);
    void emitPowerLimitReached(uint32_t gpuIndex);

    /* D-Bus constants */
    static constexpr const char* BUS_NAME = "org.mvgal.MVGAL";
    static constexpr const char* OBJECT_PATH = "/org/mvgal/daemon";
    static constexpr const char* INTERFACE_NAME = "org.mvgal.MVGAL";

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_DBUS_SERVICE_HPP