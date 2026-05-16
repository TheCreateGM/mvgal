/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 *
 * D-Bus Service - Provides D-Bus interface for MVGAL daemon control
 *
 * SPDX-License-Identifier: MIT
 */

#include "dbus_service.hpp"

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "daemon.hpp"
#include "scheduler.hpp"
#include "device_registry.hpp"
#include "metrics_collector.hpp"

namespace mvgal {

/* D-Bus constants */
constexpr std::string_view DBUS_SERVICE_NAME = "org.mvgal.MVGAL";
constexpr std::string_view DBUS_OBJECT_PATH = "/org/mvgal/daemon";
constexpr std::string_view DBUS_INTERFACE_NAME = "org.mvgal.MVGAL";

/* Method signatures for D-Bus introspection */
namespace MethodSig {
    constexpr std::string_view SetSchedulingMode = "s";
    constexpr std::string_view GetSchedulingMode = "";
    constexpr std::string_view SetGPUEnabled = "ub";
    constexpr std::string_view GetGPUEnabled = "u";
    constexpr std::string_view TriggerRescan = "";
    constexpr std::string_view GetStatistics = "";
}

/* Signal signatures for D-Bus introspection */
namespace SignalSig {
    constexpr std::string_view GPUHotplug = "ub";
    constexpr std::string_view TemperatureWarning = "ui";
    constexpr std::string_view PowerLimitReached = "u";
}

/**
 * D-Bus service implementation
 */
class DBusService::Impl {
public:
    explicit Impl(Daemon* daemon);
    ~Impl();

    bool init();
    void fini();

    /* Process D-Bus events */
    void processEvents();

    /* Emit signals */
    void emitGPUHotplug(uint32_t gpuIndex, bool added);
    void emitTemperatureWarning(uint32_t gpuIndex, int32_t temperature);
    void emitPowerLimitReached(uint32_t gpuIndex);

private:
    Daemon* m_daemon;
    sd_bus* m_bus = nullptr;
    sd_event* m_event = nullptr;

    /* D-Bus connection status */
    bool m_connected = false;

    /* Method handlers */
    static int handleSetSchedulingMode(sd_bus_message* msg, void* userdata, sd_bus_error* err);
    static int handleGetSchedulingMode(sd_bus_message* msg, void* userdata, sd_bus_error* err);
    static int handleSetGPUEnabled(sd_bus_message* msg, void* userdata, sd_bus_error* err);
    static int handleGetGPUEnabled(sd_bus_message* msg, void* userdata, sd_bus_error* err);
    static int handleTriggerRescan(sd_bus_message* msg, void* userdata, sd_bus_error* err);
    static int handleGetStatistics(sd_bus_message* msg, void* userdata, sd_bus_error* err);

    /* Object vtable */
    static const sd_bus_vtable s_vtable[];
    static const char* s_introspection;

    /* Helper to convert scheduling mode to string */
    static std::string modeToString(SchedulingMode mode);

    /* Helper to convert string to scheduling mode */
    static SchedulingMode stringToMode(const std::string& str);
};

/* D-Bus introspection XML */
const char* DBusService::Impl::s_introspection =
    "<node>"
    "  <interface name=\"org.mvgal.MVGAL\">"
    "    <method name=\"SetSchedulingMode\">"
    "      <arg type=\"s\" direction=\"in\" name=\"mode\"/>"
    "    </method>"
    "    <method name=\"GetSchedulingMode\">"
    "      <arg type=\"s\" direction=\"out\" name=\"mode\"/>"
    "    </method>"
    "    <method name=\"SetGPUEnabled\">"
    "      <arg type=\"u\" direction=\"in\" name=\"gpu_index\"/>"
    "      <arg type=\"b\" direction=\"in\" name=\"enabled\"/>"
    "    </method>"
    "    <method name=\"GetGPUEnabled\">"
    "      <arg type=\"u\" direction=\"in\" name=\"gpu_index\"/>"
    "      <arg type=\"b\" direction=\"out\" name=\"enabled\"/>"
    "    </method>"
    "    <method name=\"TriggerRescan\"/>"
    "    <method name=\"GetStatistics\">"
    "      <arg type=\"a{sv}\" direction=\"out\" name=\"statistics\"/>"
    "    </method>"
    "    <signal name=\"GPUHotplug\">"
    "      <arg type=\"u\" name=\"gpu_index\"/>"
    "      <arg type=\"b\" name=\"added\"/>"
    "    </signal>"
    "    <signal name=\"TemperatureWarning\">"
    "      <arg type=\"u\" name=\"gpu_index\"/>"
    "      <arg type=\"i\" name=\"temperature\"/>"
    "    </signal>"
    "    <signal name=\"PowerLimitReached\">"
    "      <arg type=\"u\" name=\"gpu_index\"/>"
    "    </signal>"
    "  </interface>"
    "  <node name=\"org/freedesktop/DBus/Introspectable\"/>"
    "</node>";

/* D-Bus vtable */
const sd_bus_vtable DBusService::Impl::s_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("SetSchedulingMode", "s", "", handleSetSchedulingMode, 0),
    SD_BUS_METHOD("GetSchedulingMode", "", "s", handleGetSchedulingMode, 0),
    SD_BUS_METHOD("SetGPUEnabled", "ub", "b", handleSetGPUEnabled, 0),
    SD_BUS_METHOD("GetGPUEnabled", "u", "b", handleGetGPUEnabled, 0),
    SD_BUS_METHOD("TriggerRescan", "", "", handleTriggerRescan, 0),
    SD_BUS_METHOD("GetStatistics", "", "a{sv}", handleGetStatistics, 0),
    SD_BUS_SIGNAL("GPUHotplug", "ub", 0),
    SD_BUS_SIGNAL("TemperatureWarning", "ui", 0),
    SD_BUS_SIGNAL("PowerLimitReached", "u", 0),
    SD_BUS_VTABLE_END
};

DBusService::Impl::Impl(Daemon* daemon)
    : m_daemon(daemon)
{
}

DBusService::Impl::~Impl() {
    fini();
}

bool DBusService::Impl::init() {
    int r;

    /* Open system D-Bus connection */
    r = sd_bus_open_system(&m_bus);
    if (r < 0) {
        fprintf(stderr, "Failed to open D-Bus system connection: %s\n", strerror(-r));
        m_bus = nullptr;
        // D-Bus is optional - continue without it
        m_connected = false;
        printf("D-Bus service disabled (optional feature)\n");
        return true;
    }

    /* Add our vtable - use new API signature */
    r = sd_bus_add_object_vtable(m_bus, nullptr, DBUS_OBJECT_PATH.data(), "org.mvgal.MVGAL", s_vtable, this);
    if (r < 0) {
        fprintf(stderr, "Failed to add D-Bus vtable: %s\n", strerror(-r));
        sd_bus_unref(m_bus);
        m_bus = nullptr;
        m_connected = false;
        printf("D-Bus service disabled (vtable failed)\n");
        return true;
    }

    /* Request our bus name - new API signature */
    r = sd_bus_request_name(m_bus, DBUS_SERVICE_NAME.data(), 0);
    if (r < 0) {
        fprintf(stderr, "Failed to request D-Bus name: %s\n", strerror(-r));
        sd_bus_unref(m_bus);
        m_bus = nullptr;
        m_connected = false;
        printf("D-Bus service disabled (name request failed)\n");
        return true;
    }

    m_connected = true;
    printf("D-Bus service initialized: %s at %s\n",
           DBUS_SERVICE_NAME.data(), DBUS_OBJECT_PATH.data());
    return true;
}

void DBusService::Impl::fini() {
    if (m_bus) {
        sd_bus_release_name(m_bus, DBUS_SERVICE_NAME.data());
        sd_bus_unref(m_bus);
        m_bus = nullptr;
    }
    m_connected = false;
}

void DBusService::Impl::processEvents() {
    if (m_bus && m_event) {
        sd_event_run(m_event, 0);
    }
}

void DBusService::Impl::emitGPUHotplug(uint32_t gpuIndex, bool added) {
    if (!m_bus) return;

    sd_bus_emit_signal(m_bus, DBUS_OBJECT_PATH.data(), DBUS_INTERFACE_NAME.data(),
                      "GPUHotplug", "ub", gpuIndex, added);
}

void DBusService::Impl::emitTemperatureWarning(uint32_t gpuIndex, int32_t temperature) {
    if (!m_bus) return;

    sd_bus_emit_signal(m_bus, DBUS_OBJECT_PATH.data(), DBUS_INTERFACE_NAME.data(),
                      "TemperatureWarning", "ui", gpuIndex, temperature);
}

void DBusService::Impl::emitPowerLimitReached(uint32_t gpuIndex) {
    if (!m_bus) return;

    sd_bus_emit_signal(m_bus, DBUS_OBJECT_PATH.data(), DBUS_INTERFACE_NAME.data(),
                      "PowerLimitReached", "u", gpuIndex);
}

/* Method handlers */
int DBusService::Impl::handleSetSchedulingMode(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
    auto* impl = static_cast<Impl*>(userdata);
    const char* modeStr = nullptr;

    int r = sd_bus_message_read(msg, "s", &modeStr);
    if (r < 0) {
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    SchedulingMode mode = stringToMode(modeStr);
    impl->m_daemon->scheduler().setMode(mode);

    return sd_bus_reply_method_return(msg, nullptr);
}

int DBusService::Impl::handleGetSchedulingMode(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
    auto* impl = static_cast<Impl*>(userdata);

    SchedulingMode mode = impl->m_daemon->scheduler().mode();
    std::string modeStr = modeToString(mode);

    return sd_bus_reply_method_return(msg, "s", modeStr.c_str());
}

int DBusService::Impl::handleSetGPUEnabled(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
    auto* impl = static_cast<Impl*>(userdata);
    uint32_t gpuIndex = 0;
    int enabled = 0;

    int r = sd_bus_message_read(msg, "ub", &gpuIndex, &enabled);
    if (r < 0) {
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    impl->m_daemon->deviceRegistry().enableGpu(gpuIndex, enabled != 0);

    return sd_bus_reply_method_return(msg, "b", enabled);
}

int DBusService::Impl::handleGetGPUEnabled(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
    auto* impl = static_cast<Impl*>(userdata);
    uint32_t gpuIndex = 0;

    int r = sd_bus_message_read(msg, "u", &gpuIndex);
    if (r < 0) {
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    auto* gpu = impl->m_daemon->deviceRegistry().getGpu(gpuIndex);
    bool enabled = gpu ? gpu->isEnabled() : false;

    return sd_bus_reply_method_return(msg, "b", enabled);
}

int DBusService::Impl::handleTriggerRescan(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
    auto* impl = static_cast<Impl*>(userdata);

    impl->m_daemon->deviceRegistry().reenum();

    return sd_bus_reply_method_return(msg, nullptr);
}

int DBusService::Impl::handleGetStatistics(sd_bus_message* msg, void* userdata, sd_bus_error* err) {
    auto* impl = static_cast<Impl*>(userdata);

    /* Build statistics dictionary */
    sd_bus_message* reply = nullptr;
    int r = sd_bus_message_new_method_return(msg, &reply);
    if (r < 0) {
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    /* GPU count */
    uint32_t gpuCount = impl->m_daemon->deviceRegistry().gpuCount();
    r = sd_bus_message_append(reply, "{sv}", "gpu_count", "u", gpuCount);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    /* Total VRAM */
    uint64_t totalVram = impl->m_daemon->deviceRegistry().totalVRAM();
    r = sd_bus_message_append(reply, "{sv}", "total_vram_bytes", "t", totalVram);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    /* Free VRAM */
    uint64_t freeVram = impl->m_daemon->deviceRegistry().freeVRAM();
    r = sd_bus_message_append(reply, "{sv}", "free_vram_bytes", "t", freeVram);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    /* Scheduler statistics */
    uint64_t totalWorkloads = impl->m_daemon->scheduler().totalWorkloads();
    r = sd_bus_message_append(reply, "{sv}", "total_workloads", "t", totalWorkloads);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    uint64_t completedWorkloads = impl->m_daemon->scheduler().completedWorkloads();
    r = sd_bus_message_append(reply, "{sv}", "completed_workloads", "t", completedWorkloads);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    /* Scheduling mode */
    std::string modeStr = modeToString(impl->m_daemon->scheduler().mode());
    r = sd_bus_message_append(reply, "{sv}", "scheduling_mode", "s", modeStr.c_str());
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    /* Per-GPU statistics */
    for (uint32_t i = 0; i < gpuCount; ++i) {
        auto* gpu = impl->m_daemon->deviceRegistry().getGpu(i);
        if (!gpu) continue;

        char key[64];
        snprintf(key, sizeof(key), "gpu_%u_utilization", i);
        r = sd_bus_message_append(reply, "{sv}", key, "u", gpu->utilization());
        if (r < 0) {
            sd_bus_message_unref(reply);
            sd_bus_error_set_errno(err, -r);
            return r;
        }

        snprintf(key, sizeof(key), "gpu_%u_temperature", i);
        r = sd_bus_message_append(reply, "{sv}", key, "i", gpu->state().temperature);
        if (r < 0) {
            sd_bus_message_unref(reply);
            sd_bus_error_set_errno(err, -r);
            return r;
        }

        snprintf(key, sizeof(key), "gpu_%u_power_watts", i);
        r = sd_bus_message_append(reply, "{sv}", key, "u", gpu->state().powerDrawWatts);
        if (r < 0) {
            sd_bus_message_unref(reply);
            sd_bus_error_set_errno(err, -r);
            return r;
        }
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) {
        sd_bus_message_unref(reply);
        sd_bus_error_set_errno(err, -r);
        return r;
    }

    r = sd_bus_send(NULL, reply, nullptr);
    sd_bus_message_unref(reply);
    return r;
}

std::string DBusService::Impl::modeToString(SchedulingMode mode) {
    switch (mode) {
        case SchedulingMode::STATIC_PARTITIONING:
            return "static_partitioning";
        case SchedulingMode::DYNAMIC_LOAD_BALANCING:
            return "dynamic_load_balancing";
        case SchedulingMode::APPLICATION_PROFILE:
            return "application_profile";
        default:
            return "unknown";
    }
}

SchedulingMode DBusService::Impl::stringToMode(const std::string& str) {
    if (str == "static_partitioning") return SchedulingMode::STATIC_PARTITIONING;
    if (str == "dynamic_load_balancing") return SchedulingMode::DYNAMIC_LOAD_BALANCING;
    if (str == "application_profile") return SchedulingMode::APPLICATION_PROFILE;
    return SchedulingMode::STATIC_PARTITIONING;  /* Default */
}

/* Public API */
DBusService::DBusService(Daemon* daemon)
    : m_impl(std::make_unique<Impl>(daemon))
{
}

DBusService::~DBusService() = default;

bool DBusService::init() {
    return m_impl->init();
}

void DBusService::fini() {
    m_impl->fini();
}

void DBusService::processEvents() {
    m_impl->processEvents();
}

void DBusService::emitGPUHotplug(uint32_t gpuIndex, bool added) {
    m_impl->emitGPUHotplug(gpuIndex, added);
}

void DBusService::emitTemperatureWarning(uint32_t gpuIndex, int32_t temperature) {
    m_impl->emitTemperatureWarning(gpuIndex, temperature);
}

void DBusService::emitPowerLimitReached(uint32_t gpuIndex) {
    m_impl->emitPowerLimitReached(gpuIndex);
}

} // namespace mvgal