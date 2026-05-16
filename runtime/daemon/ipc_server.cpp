/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * IPC Server Implementation - Unix socket communication with clients
 * 
 * SPDX-License-Identifier: MIT
 */

#include "ipc_server.hpp"
#include "daemon.hpp"
#include "device_registry.hpp"
#include "scheduler.hpp"
#include "memory_manager.hpp"
#include <mvgal/mvgal_gpu.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <system_error>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace mvgal {

/* =========================================================================
 * IpcClientConnection Implementation
 * =========================================================================
 */

IpcClientConnection::IpcClientConnection(int socketFd, const struct sockaddr_un& remoteAddr, uint32_t clientId, Daemon* daemon)
    : m_socketFd(socketFd),
      m_remoteAddr(remoteAddr),
      m_clientId(clientId),
      m_daemon(daemon),
      m_receiveBuffer(4096)
{
    /* Set non-blocking mode */
    int flags = fcntl(m_socketFd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK);
    }
}

IpcClientConnection::~IpcClientConnection()
{
    if (m_socketFd >= 0) {
        close(m_socketFd);
        m_socketFd = -1;
    }
}

bool IpcClientConnection::readMessageHeader(IpcMessageHeader& header)
{
    size_t received = 0;
    size_t headerSize = sizeof(IpcMessageHeader);
    uint8_t* headerBytes = reinterpret_cast<uint8_t*>(&header);

    while (received < headerSize) {
        ssize_t n = recv(m_socketFd, headerBytes + received, headerSize - received, 0);
        if (n <= 0) {
            if (n == 0) {
                /* Connection closed */
                return false;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Would block - wait for next poll */
                return false;
            }
            /* Error */
            return false;
        }
        received += static_cast<size_t>(n);
    }

    /* Convert from network byte order BEFORE validation */
    header.magic = ntohl(header.magic);
    header.version = ntohl(header.version);
    header.messageType = ntohl(header.messageType);
    header.requestId = ntohl(header.requestId);
    header.payloadSize = ntohl(header.payloadSize);
    header.flags = ntohl(header.flags);
    header.reserved = ntohl(header.reserved);

    /* Validate magic */
    if (header.magic != IPC_MAGIC) {
        std::cerr << "IPC: Invalid magic number from client " << m_clientId << std::endl;
        return false;
    }

    /* Validate version */
    if (header.version != IPC_VERSION) {
        std::cerr << "IPC: Unsupported protocol version " << header.version
                  << " from client " << m_clientId << std::endl;
        return false;
    }

    return true;
}

bool IpcClientConnection::readPayload(void* buffer, size_t size)
{
    size_t received = 0;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(buffer);

    while (received < size) {
        ssize_t n = recv(m_socketFd, bytes + received, size - received, 0);
        if (n <= 0) {
            if (n == 0) {
                return false;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;
            }
            return false;
        }
        received += static_cast<size_t>(n);
    }

    return true;
}

bool IpcClientConnection::authenticate() const
{
    /* Use SCM_CREDENTIALS to get peer credentials */
    struct ucred cred;
    socklen_t credLen = sizeof(cred);

    if (getsockopt(m_socketFd, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) < 0) {
        std::cerr << "IPC: Failed to get peer credentials for client " << m_clientId
                  << ": " << strerror(errno) << std::endl;
        return false;
    }

    /* Check if the client is root or same user as daemon */
    uid_t daemonUid = getuid();
    if (cred.uid != 0 && cred.uid != daemonUid) {
        std::cerr << "IPC: Client " << m_clientId << " authentication failed: "
                  << "uid=" << cred.uid << " (expected 0 or " << daemonUid << ")" << std::endl;
        return false;
    }

    /* Optionally check if client is in the mvgal group */
    gid_t daemonGid = getgid();
    bool inMvgalGroup = false;
    for (socklen_t i = 0; i < credLen; i++) {
        if (cred.gid == daemonGid) {
            inMvgalGroup = true;
            break;
        }
    }

    if (!inMvgalGroup) {
        /* Check supplementary groups */
        gid_t groups[32];
        int ngroups = getgroups(32, groups);
        for (int i = 0; i < ngroups; i++) {
            if (groups[i] == daemonGid) {
                inMvgalGroup = true;
                break;
            }
        }
    }

    return true; /* Allow for now - stricter auth can be added later */
}

bool IpcClientConnection::processMessages()
{
    IpcMessageHeader header;

    if (!readMessageHeader(header)) {
        return false;
    }

    /* Read payload if present */
    if (header.payloadSize > 0) {
        m_receiveBuffer.resize(header.payloadSize);
        if (!readPayload(m_receiveBuffer.data(), header.payloadSize)) {
            return false;
        }
    }

    /* Authenticate on first message (unless it's HELLO) */
    IpcMessageType msgType = static_cast<IpcMessageType>(header.messageType);
    if (msgType != IpcMessageType::HELLO) {
        if (!authenticate()) {
            sendError(msgType, IpcErrorCode::ACCESS_DENIED, header.requestId);
            return false;
        }
    }

    /* Handle message based on type */
    switch (msgType) {
        case IpcMessageType::HELLO: {
            /* Respond to HELLO */
            IpcMessageHeader response;
            response.magic = IPC_MAGIC;
            response.version = IPC_VERSION;
            response.messageType = static_cast<uint32_t>(IpcMessageType::HELLO);
            response.requestId = header.requestId;
            response.payloadSize = 0;
            response.flags = IpcFlags::RESPONSE;
            response.reserved = 0;

            /* Convert to network byte order */
            response.magic = htonl(response.magic);
            response.version = htonl(response.version);
            response.messageType = htonl(response.messageType);
            response.requestId = htonl(response.requestId);
            response.payloadSize = htonl(response.payloadSize);
            response.flags = htonl(response.flags);
            response.reserved = htonl(response.reserved);

            ssize_t n = send(m_socketFd, &response, sizeof(response), 0);
            if (n != static_cast<ssize_t>(sizeof(response))) {
                return false;
            }
            break;
        }

        case IpcMessageType::GOODBYE: {
            /* Client wants to close connection */
            return false;
        }

        case IpcMessageType::QUERY_DEVICES: {
            auto& registry = m_daemon->deviceRegistry();
            uint32_t count = registry.gpuCount();
            
            std::vector<mvgal_gpu_descriptor_t> descriptors(count);
            for (uint32_t i = 0; i < count; ++i) {
                const auto* gpu = registry.getGpu(i);
                auto& desc = descriptors[i];
                memset(&desc, 0, sizeof(desc));
                
                desc.id = gpu->index();
                strncpy(desc.name, gpu->name().c_str(), sizeof(desc.name) - 1);
                desc.vendor = static_cast<mvgal_vendor_t>(gpu->vendor());
                desc.vram_total = gpu->capabilities().vramSize;
                desc.vram_free = gpu->capabilities().vramFree;
                desc.vram_used = desc.vram_total - desc.vram_free;
                desc.temperature_celsius = static_cast<float>(gpu->state().temperature);
                desc.utilization_percent = static_cast<float>(gpu->state().utilization);
                desc.enabled = gpu->isEnabled();
                desc.available = gpu->isAvailable();
            }
            
            sendMessage(msgType, descriptors.data(), descriptors.size() * sizeof(mvgal_gpu_descriptor_t), header.requestId);
            break;
        }

        case IpcMessageType::QUERY_DEVICE_CAPABILITIES: {
            if (m_receiveBuffer.size() < sizeof(uint32_t)) {
                sendError(msgType, IpcErrorCode::INVALID_ARGUMENT, header.requestId);
                break;
            }
            
            uint32_t index = *reinterpret_cast<uint32_t*>(m_receiveBuffer.data());
            /* Assume host byte order for now, or convert if needed: index = ntohl(index); */
            
            auto* gpu = m_daemon->deviceRegistry().getGpu(index);
            if (!gpu) {
                sendError(msgType, IpcErrorCode::NOT_FOUND, header.requestId);
                break;
            }
            
            mvgal_gpu_descriptor_t desc;
            memset(&desc, 0, sizeof(desc));
            desc.id = gpu->index();
            strncpy(desc.name, gpu->name().c_str(), sizeof(desc.name) - 1);
            desc.vendor = static_cast<mvgal_vendor_t>(gpu->vendor());
            desc.vram_total = gpu->capabilities().vramSize;
            desc.vram_free = gpu->capabilities().vramFree;
            desc.vram_used = desc.vram_total - desc.vram_free;
            desc.temperature_celsius = static_cast<float>(gpu->state().temperature);
            desc.utilization_percent = static_cast<float>(gpu->state().utilization);
            desc.enabled = gpu->isEnabled();
            desc.available = gpu->isAvailable();
            
            sendMessage(msgType, &desc, sizeof(desc), header.requestId);
            break;
        }

        case IpcMessageType::QUERY_UNIFIED_CAPABILITIES: {
            auto& registry = m_daemon->deviceRegistry();
            mvgal_logical_device_descriptor_t desc;
            memset(&desc, 0, sizeof(desc));
            
            desc.gpu_count = registry.gpuCount();
            desc.descriptor.vram_total = registry.totalVRAM();
            desc.descriptor.vram_free = registry.freeVRAM();
            desc.heterogeneous = (registry.capabilityTier() == 2); // TIER_MIXED
            
            sendMessage(msgType, &desc, sizeof(desc), header.requestId);
            break;
        }

        case IpcMessageType::ALLOC_MEMORY: {
            if (m_receiveBuffer.size() < sizeof(size_t)) {
                sendError(msgType, IpcErrorCode::INVALID_ARGUMENT, header.requestId);
                break;
            }
            
            size_t size = *reinterpret_cast<size_t*>(m_receiveBuffer.data());
            uint64_t id = m_daemon->memoryManager().allocate(size);
            
            if (id == 0) {
                sendError(msgType, IpcErrorCode::OUT_OF_MEMORY, header.requestId);
            } else {
                sendMessage(msgType, &id, sizeof(id), header.requestId);
            }
            break;
        }

        case IpcMessageType::FREE_MEMORY: {
            if (m_receiveBuffer.size() < sizeof(uint64_t)) {
                sendError(msgType, IpcErrorCode::INVALID_ARGUMENT, header.requestId);
                break;
            }
            
            uint64_t id = *reinterpret_cast<uint64_t*>(m_receiveBuffer.data());
            if (m_daemon->memoryManager().free(id)) {
                sendMessage(msgType, nullptr, 0, header.requestId);
            } else {
                sendError(msgType, IpcErrorCode::NOT_FOUND, header.requestId);
            }
            break;
        }

        case IpcMessageType::IMPORT_DMABUF: {
            /* Complex due to fd passing, needs SCM_RIGHTS */
            std::cerr << "IPC: IMPORT_DMABUF not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::EXPORT_DMABUF: {
            std::cerr << "IPC: EXPORT_DMABUF not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::SUBMIT_WORKLOAD: {
            /* Placeholder for workload submission */
            uint32_t workloadId = m_daemon->scheduler().submit(WorkloadType::GRAPHICS);
            sendMessage(msgType, &workloadId, sizeof(workloadId), header.requestId);
            break;
        }

        case IpcMessageType::WAIT_WORKLOAD: {
            if (m_receiveBuffer.size() < sizeof(uint32_t)) {
                sendError(msgType, IpcErrorCode::INVALID_ARGUMENT, header.requestId);
                break;
            }
            
            uint32_t id = *reinterpret_cast<uint32_t*>(m_receiveBuffer.data());
            bool completed = m_daemon->scheduler().wait(id);
            sendMessage(msgType, &completed, sizeof(completed), header.requestId);
            break;
        }

        case IpcMessageType::SET_SCHEDULING_MODE: {
            if (m_receiveBuffer.size() < sizeof(uint32_t)) {
                sendError(msgType, IpcErrorCode::INVALID_ARGUMENT, header.requestId);
                break;
            }
            
            uint32_t mode = *reinterpret_cast<uint32_t*>(m_receiveBuffer.data());
            m_daemon->scheduler().setMode(static_cast<SchedulingMode>(mode));
            sendMessage(msgType, nullptr, 0, header.requestId);
            break;
        }

        case IpcMessageType::SET_GPU_PRIORITY: {
            if (m_receiveBuffer.size() < sizeof(uint32_t) + sizeof(int)) {
                sendError(msgType, IpcErrorCode::INVALID_ARGUMENT, header.requestId);
                break;
            }
            
            struct { uint32_t index; int priority; } *args = reinterpret_cast<decltype(args)>(m_receiveBuffer.data());
            m_daemon->deviceRegistry().setGpuPriority(args->index, args->priority);
            sendMessage(msgType, nullptr, 0, header.requestId);
            break;
        }

        case IpcMessageType::SET_GPU_ENABLED: {
            if (m_receiveBuffer.size() < sizeof(uint32_t) + sizeof(bool)) {
                sendError(msgType, IpcErrorCode::INVALID_ARGUMENT, header.requestId);
                break;
            }
            
            struct { uint32_t index; bool enabled; } *args = reinterpret_cast<decltype(args)>(m_receiveBuffer.data());
            m_daemon->deviceRegistry().enableGpu(args->index, args->enabled);
            sendMessage(msgType, nullptr, 0, header.requestId);
            break;
        }

        case IpcMessageType::GET_STATISTICS: {
            std::cerr << "IPC: GET_STATISTICS not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::SUBSCRIBE_TELEMETRY: {
            std::cerr << "IPC: SUBSCRIBE_TELEMETRY not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::UNSUBSCRIBE_TELEMETRY: {
            std::cerr << "IPC: UNSUBSCRIBE_TELEMETRY not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::GET_CONFIG: {
            std::cerr << "IPC: GET_CONFIG not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::SET_CONFIG: {
            std::cerr << "IPC: SET_CONFIG not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::LOAD_CONFIG: {
            std::cerr << "IPC: LOAD_CONFIG not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::SAVE_CONFIG: {
            std::cerr << "IPC: SAVE_CONFIG not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        default: {
            std::cerr << "IPC: Unknown message type " << header.messageType
                      << " from client " << m_clientId << std::endl;
            sendError(msgType, IpcErrorCode::INVALID_REQUEST, header.requestId);
            break;
        }
    }

    return true;
}

bool IpcClientConnection::sendMessage(IpcMessageType type, const void* payload, size_t payloadSize, uint32_t requestId)
{
    IpcMessageHeader header;
    header.magic = IPC_MAGIC;
    header.version = IPC_VERSION;
    header.messageType = static_cast<uint32_t>(type);
    header.requestId = requestId;
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.flags = IpcFlags::RESPONSE;
    header.reserved = 0;

    /* Convert to network byte order */
    header.magic = htonl(header.magic);
    header.version = htonl(header.version);
    header.messageType = htonl(header.messageType);
    header.requestId = htonl(header.requestId);
    header.payloadSize = htonl(header.payloadSize);
    header.flags = htonl(header.flags);
    header.reserved = htonl(header.reserved);

    /* Send header */
    ssize_t n = send(m_socketFd, &header, sizeof(header), 0);
    if (n != static_cast<ssize_t>(sizeof(header))) {
        return false;
    }

    /* Send payload if present */
    if (payloadSize > 0 && payload) {
        n = send(m_socketFd, payload, payloadSize, 0);
        if (n != static_cast<ssize_t>(payloadSize)) {
            return false;
        }
    }

    return true;
}

bool IpcClientConnection::sendError(IpcMessageType /*originalType*/, IpcErrorCode error, uint32_t requestId)
{
    struct {
        IpcMessageHeader header;
        uint32_t errorCode;
    } errorMsg;

    errorMsg.header.magic = IPC_MAGIC;
    errorMsg.header.version = IPC_VERSION;
    errorMsg.header.messageType = static_cast<uint32_t>(IpcMessageType::ERROR);
    errorMsg.header.requestId = requestId;
    errorMsg.header.payloadSize = sizeof(uint32_t);
    errorMsg.header.flags = IpcFlags::RESPONSE | IpcFlags::ERROR;
    errorMsg.header.reserved = 0;
    errorMsg.errorCode = htonl(static_cast<uint32_t>(error));

    /* Convert header to network byte order */
    errorMsg.header.magic = htonl(errorMsg.header.magic);
    errorMsg.header.version = htonl(errorMsg.header.version);
    errorMsg.header.messageType = htonl(errorMsg.header.messageType);
    errorMsg.header.requestId = htonl(errorMsg.header.requestId);
    errorMsg.header.payloadSize = htonl(errorMsg.header.payloadSize);
    errorMsg.header.flags = htonl(errorMsg.header.flags);
    errorMsg.header.reserved = htonl(errorMsg.header.reserved);

    ssize_t n = send(m_socketFd, &errorMsg, sizeof(errorMsg), 0);
    return (n == static_cast<ssize_t>(sizeof(errorMsg)));
}

/* =========================================================================
 * IpcServer Implementation
 * =========================================================================
 */

IpcServer::IpcServer(Daemon* daemon)
    : m_daemon(daemon),
      m_listenFd(-1),
      m_nextClientId(1),
      m_nodeId(0),
      m_tcpListenFd(-1),
      m_udpFd(-1),
      m_discoveryPort(0),
      m_tcpPort(0),
      m_discoveryEnabled(false),
      m_discoveryRunning(false)
{
}

IpcServer::~IpcServer()
{
    fini();
}

bool IpcServer::init(const std::string& socketPath)
{
    m_socketPath = socketPath;

    /* Create Unix domain socket */
    m_listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_listenFd < 0) {
        std::cerr << "IPC: Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    /* Bind to socket path */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    /* Remove existing socket if present */
    if (unlink(m_socketPath.c_str()) < 0 && errno != ENOENT) {
        if (errno == EACCES || errno == EPERM) {
            std::cerr << "IPC: Cannot remove stale socket " << m_socketPath
                      << ": " << strerror(errno) << std::endl;
            std::cerr << "IPC: Remove it manually: sudo rm -f " << m_socketPath << std::endl;
            close(m_listenFd);
            m_listenFd = -1;
            return false;
        }
        std::cerr << "IPC: Warning: Failed to remove existing socket " << m_socketPath
                  << ": " << strerror(errno) << std::endl;
    }

    if (bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "IPC: Failed to bind socket to " << m_socketPath
                  << ": " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    /* Listen for connections */
    if (listen(m_listenFd, 64) < 0) {
        std::cerr << "IPC: Failed to listen on socket: " << strerror(errno) << std::endl;
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    /* Set permissions on socket file */
    chmod(m_socketPath.c_str(), 0660);

    std::cout << "IPC: Server listening on " << m_socketPath << std::endl;

    return true;
}

void IpcServer::fini()
{
    /* Tear down network discovery first */
    finiNetworkDiscovery();

    std::lock_guard<std::mutex> lock(m_mutex);

    /* Close all client connections — shared_ptr destructors handle cleanup */
    m_clients.clear();

    /* Close listen socket */
    if (m_listenFd >= 0) {
        close(m_listenFd);
        m_listenFd = -1;
    }

    /* Remove socket file */
    if (!m_socketPath.empty()) {
        unlink(m_socketPath.c_str());
        m_socketPath.clear();
    }
}

bool IpcServer::acceptConnections()
{
    while (true) {
        struct sockaddr_un remoteAddr;
        socklen_t remoteAddrLen = sizeof(remoteAddr);

        int clientFd = accept4(m_listenFd, reinterpret_cast<struct sockaddr*>(&remoteAddr), &remoteAddrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* No more connections to accept */
                return true;
            }
            if (errno == EMFILE || errno == ENFILE) {
                /* Too many open files - wait and try again */
                std::this_thread::yield();
                continue;
            }
            std::cerr << "IPC: Failed to accept connection: " << strerror(errno) << std::endl;
            return false;
        }

        /* Create client connection */
        uint32_t clientId;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            clientId = m_nextClientId++;
        }

        auto connection = std::make_shared<IpcClientConnection>(clientFd, remoteAddr, clientId, m_daemon);

        /* Add to client map */
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_clients[clientId] = connection;
        }

        std::cout << "IPC: New connection from client " << clientId
                  << " (fd=" << clientFd << ")" << std::endl;
    }

    return true;
}

void IpcServer::processEvents()
{
    /* Accept new connections */
    if (!acceptConnections()) {
        std::cerr << "IPC: Error accepting connections" << std::endl;
        return;
    }

    /* Process client messages */
    std::vector<uint32_t> deadClients;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_clients) {
            IpcClientConnection* client = pair.second.get();
            if (!client->processMessages()) {
                deadClients.push_back(pair.first);
            }
        }
    }

    /* Cleanup dead clients using list computed above */
    cleanupDeadClients(deadClients);

    /* Process any registered handlers */
    /* Note: Custom handlers would be invoked from processMessages() in client connection */
}

void IpcServer::sendNotification(IpcMessageType type, const void* payload, size_t payloadSize)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_clients) {
        sendMessageInternal(pair.second.get(), type, payload, payloadSize, 0);
    }
}

void IpcServer::sendNotification(uint32_t clientId, IpcMessageType type, const void* payload, size_t payloadSize)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_clients.find(clientId);
    if (it != m_clients.end()) {
        sendMessageInternal(it->second.get(), type, payload, payloadSize, 0);
    }
}

void IpcServer::registerHandler(IpcMessageType type, std::function<void(const IpcClientConnection&, const IpcMessageHeader&, const void*)> handler)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers[static_cast<uint32_t>(type)] = handler;
}

void IpcServer::cleanupDeadClients(const std::vector<uint32_t>& deadClients)
{
    if (deadClients.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (uint32_t clientId : deadClients) {
        auto it = m_clients.find(clientId);
        if (it != m_clients.end()) {
            m_clients.erase(it);
            std::cout << "IPC: Client " << clientId << " disconnected" << std::endl;
        }
    }
}



bool IpcServer::sendMessageInternal(IpcClientConnection* client, IpcMessageType type, const void* payload, size_t payloadSize, uint32_t requestId)
{
    IpcMessageHeader header;
    header.magic = IPC_MAGIC;
    header.version = IPC_VERSION;
    header.messageType = static_cast<uint32_t>(type);
    header.requestId = requestId;
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.flags = IpcFlags::NOTIFICATION;
    header.reserved = 0;

    /* Convert to network byte order */
    header.magic = htonl(header.magic);
    header.version = htonl(header.version);
    header.messageType = htonl(header.messageType);
    header.requestId = htonl(header.requestId);
    header.payloadSize = htonl(header.payloadSize);
    header.flags = htonl(header.flags);
    header.reserved = htonl(header.reserved);

    /* Send header */
    ssize_t n = send(client->fd(), &header, sizeof(header), 0);
    if (n != static_cast<ssize_t>(sizeof(header))) {
        return false;
    }

    /* Send payload if present */
    if (payloadSize > 0 && payload) {
        n = send(client->fd(), payload, payloadSize, 0);
        if (n != static_cast<ssize_t>(payloadSize)) {
            return false;
        }
    }

    return true;
}

/* =========================================================================
 * Network Peer Discovery Implementation
 * =========================================================================
 */

uint64_t IpcServer::generateNodeId()
{
    /* Derive a unique node ID from hostname hash + random seed */
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        std::strncpy(hostname, "unknown", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    std::hash<std::string> hasher;
    uint64_t hostHash = hasher(std::string(hostname));

    /* Add random entropy for uniqueness */
    std::random_device rd;
    std::mt19937_64 gen(rd());
    uint64_t entropy = gen();

    /* Combine: upper 32 bits = hostname hash, lower 32 bits = entropy */
    return (hostHash & 0xFFFFFFFF00000000ULL) | (entropy & 0x00000000FFFFFFFFULL);
}

bool IpcServer::initUdpSocket()
{
    m_udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_udpFd < 0) {
        std::cerr << "IPC: Failed to create UDP discovery socket: "
                  << strerror(errno) << std::endl;
        return false;
    }

    /* Allow broadcast sending */
    int broadcastEnable = 1;
    if (setsockopt(m_udpFd, SOL_SOCKET, SO_BROADCAST,
                   &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        std::cerr << "IPC: Failed to set SO_BROADCAST: "
                  << strerror(errno) << std::endl;
        close(m_udpFd);
        m_udpFd = -1;
        return false;
    }

    /* Allow address reuse */
    int reuseEnable = 1;
    if (setsockopt(m_udpFd, SOL_SOCKET, SO_REUSEADDR,
                   &reuseEnable, sizeof(reuseEnable)) < 0) {
        std::cerr << "IPC: Failed to set SO_REUSEADDR on UDP socket: "
                  << strerror(errno) << std::endl;
        close(m_udpFd);
        m_udpFd = -1;
        return false;
    }

    /* Bind to discovery port to receive broadcasts */
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(m_discoveryPort);
    bindAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_udpFd, reinterpret_cast<struct sockaddr*>(&bindAddr),
             sizeof(bindAddr)) < 0) {
        std::cerr << "IPC: Failed to bind UDP socket to port "
                  << m_discoveryPort << ": " << strerror(errno) << std::endl;
        close(m_udpFd);
        m_udpFd = -1;
        return false;
    }

    std::cout << "IPC: UDP discovery socket bound to port "
              << m_discoveryPort << " (fd=" << m_udpFd << ")" << std::endl;
    return true;
}

bool IpcServer::initTcpListener()
{
    m_tcpListenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_tcpListenFd < 0) {
        std::cerr << "IPC: Failed to create TCP listener socket: "
                  << strerror(errno) << std::endl;
        return false;
    }

    /* Allow address reuse */
    int reuseEnable = 1;
    if (setsockopt(m_tcpListenFd, SOL_SOCKET, SO_REUSEADDR,
                   &reuseEnable, sizeof(reuseEnable)) < 0) {
        std::cerr << "IPC: Failed to set SO_REUSEADDR on TCP socket: "
                  << strerror(errno) << std::endl;
        close(m_tcpListenFd);
        m_tcpListenFd = -1;
        return false;
    }

    /* Bind to listen address */
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(m_tcpPort);
    if (inet_pton(AF_INET, m_listenAddr.c_str(), &bindAddr.sin_addr) <= 0) {
        std::cerr << "IPC: Invalid listen address: " << m_listenAddr << std::endl;
        close(m_tcpListenFd);
        m_tcpListenFd = -1;
        return false;
    }

    if (bind(m_tcpListenFd, reinterpret_cast<struct sockaddr*>(&bindAddr),
             sizeof(bindAddr)) < 0) {
        std::cerr << "IPC: Failed to bind TCP listener to "
                  << m_listenAddr << ":" << m_tcpPort
                  << ": " << strerror(errno) << std::endl;
        close(m_tcpListenFd);
        m_tcpListenFd = -1;
        return false;
    }

    if (listen(m_tcpListenFd, 16) < 0) {
        std::cerr << "IPC: Failed to listen on TCP socket: "
                  << strerror(errno) << std::endl;
        close(m_tcpListenFd);
        m_tcpListenFd = -1;
        return false;
    }

    /* Get the actual port if auto-assigned */
    if (m_tcpPort == 0) {
        struct sockaddr_in actualAddr;
        socklen_t addrLen = sizeof(actualAddr);
        if (getsockname(m_tcpListenFd,
                        reinterpret_cast<struct sockaddr*>(&actualAddr),
                        &addrLen) == 0) {
            m_tcpPort = ntohs(actualAddr.sin_port);
        }
    }

    std::cout << "IPC: TCP peer listener on "
              << m_listenAddr << ":" << m_tcpPort
              << " (fd=" << m_tcpListenFd << ")" << std::endl;
    return true;
}

bool IpcServer::initNetworkDiscovery(uint16_t discoveryPort,
                                      uint16_t listenPort,
                                      const std::string& listenAddr)
{
    if (m_discoveryEnabled) {
        return true; /* Already initialized */
    }

    m_discoveryPort = discoveryPort;
    m_tcpPort = listenPort;
    m_listenAddr = listenAddr;

    /* Generate unique node ID */
    m_nodeId = generateNodeId();

    /* Initialize UDP discovery socket */
    if (!initUdpSocket()) {
        return false;
    }

    /* Initialize TCP listener for peer connections */
    if (!initTcpListener()) {
        close(m_udpFd);
        m_udpFd = -1;
        return false;
    }

    /* Mark as enabled and start the discovery thread */
    m_discoveryEnabled = true;
    m_discoveryRunning = true;
    m_discoveryThread = std::thread(&IpcServer::discoveryThreadLoop, this);

    std::cout << "IPC: Network discovery started (node=" << m_nodeId
              << ", discoveryPort=" << m_discoveryPort
              << ", tcpPort=" << m_tcpPort << ")" << std::endl;
    return true;
}

void IpcServer::finiNetworkDiscovery()
{
    if (!m_discoveryEnabled) {
        return;
    }

    /* Signal thread to stop */
    m_discoveryEnabled = false;
    m_discoveryRunning = false;

    /* Join discovery thread */
    if (m_discoveryThread.joinable()) {
        m_discoveryThread.join();
    }

    /* Close UDP socket */
    if (m_udpFd >= 0) {
        close(m_udpFd);
        m_udpFd = -1;
    }

    /* Close TCP listener */
    if (m_tcpListenFd >= 0) {
        close(m_tcpListenFd);
        m_tcpListenFd = -1;
    }

    /* Clear peer list */
    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        m_peers.clear();
    }

    std::cout << "IPC: Network discovery stopped" << std::endl;
}

bool IpcServer::sendDiscoveryAnnounce()
{
    if (m_udpFd < 0) {
        return false;
    }

    /* Build announcement message */
    DiscoveryMessageHeader msg;
    msg.magic = DISCOVERY_MAGIC;
    msg.version = DISCOVERY_VERSION;
    msg.messageType = static_cast<uint32_t>(DiscoveryMessageType::ANNOUNCE);
    msg.senderNodeId = m_nodeId;
    msg.gpuCount = 0; /* Will be filled from device registry */
    msg.tcpPort = m_tcpPort;
    msg.timestampNs = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    msg.reserved = 0;

    /* Convert to network byte order */
    msg.magic = htonl(msg.magic);
    msg.version = htonl(msg.version);
    msg.messageType = htonl(msg.messageType);
    msg.senderNodeId = htobe64(msg.senderNodeId);
    msg.gpuCount = htonl(msg.gpuCount);
    msg.tcpPort = htons(msg.tcpPort);
    msg.timestampNs = htobe64(msg.timestampNs);
    msg.reserved = htonl(msg.reserved);

    /* Broadcast to discovery port */
    struct sockaddr_in broadcastAddr;
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(m_discoveryPort);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    ssize_t sent = sendto(m_udpFd, &msg, sizeof(msg), 0,
                          reinterpret_cast<struct sockaddr*>(&broadcastAddr),
                          sizeof(broadcastAddr));

    return (sent == static_cast<ssize_t>(sizeof(msg)));
}

bool IpcServer::sendHeartbeat(PeerInfo& peer)
{
    if (m_udpFd < 0) {
        return false;
    }

    DiscoveryMessageHeader msg;
    msg.magic = DISCOVERY_MAGIC;
    msg.version = DISCOVERY_VERSION;
    msg.messageType = static_cast<uint32_t>(DiscoveryMessageType::HEARTBEAT);
    msg.senderNodeId = m_nodeId;
    msg.gpuCount = 0;
    msg.tcpPort = m_tcpPort;
    msg.timestampNs = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    msg.reserved = 0;

    /* Convert to network byte order */
    msg.magic = htonl(msg.magic);
    msg.version = htonl(msg.version);
    msg.messageType = htonl(msg.messageType);
    msg.senderNodeId = htobe64(msg.senderNodeId);
    msg.gpuCount = htonl(msg.gpuCount);
    msg.tcpPort = htons(msg.tcpPort);
    msg.timestampNs = htobe64(msg.timestampNs);
    msg.reserved = htonl(msg.reserved);

    /* Send to peer's discovery listener */
    struct sockaddr_in peerAddr;
    memset(&peerAddr, 0, sizeof(peerAddr));
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_port = htons(m_discoveryPort);
    if (inet_pton(AF_INET, peer.address.c_str(), &peerAddr.sin_addr) <= 0) {
        return false;
    }

    ssize_t sent = sendto(m_udpFd, &msg, sizeof(msg), 0,
                          reinterpret_cast<struct sockaddr*>(&peerAddr),
                          sizeof(peerAddr));

    return (sent == static_cast<ssize_t>(sizeof(msg)));
}

void IpcServer::handlePeerAnnouncement(uint64_t nodeId, const std::string& address,
                                        uint16_t tcpPort, uint32_t gpuCount)
{
    /* Ignore self-announcements */
    if (nodeId == m_nodeId) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_peerMutex);

    auto it = m_peers.find(nodeId);
    if (it != m_peers.end()) {
        /* Update existing peer */
        PeerInfo& peer = it->second;
        peer.address = address;
        peer.tcpPort = tcpPort;
        peer.gpuCount = gpuCount;
        peer.lastSeenNs = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());

        /* Transition state machine */
        if (peer.state == PeerState::DISCOVERED) {
            peer.state = PeerState::CONNECTED;
        }
        peer.missedHeartbeats = 0;
    } else {
        /* New peer discovered */
        PeerInfo newPeer;
        newPeer.nodeId = nodeId;
        newPeer.address = address;
        newPeer.tcpPort = tcpPort;
        newPeer.gpuCount = gpuCount;
        newPeer.state = PeerState::DISCOVERED;
        newPeer.lastSeenNs = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        newPeer.lastHeartbeatNs = newPeer.lastSeenNs;
        newPeer.missedHeartbeats = 0;
        newPeer.tcpSocketFd = -1;

        m_peers[nodeId] = newPeer;

        std::cout << "IPC: New peer discovered: node=" << nodeId
                  << " at " << address << ":" << tcpPort
                  << " (" << gpuCount << " GPUs)" << std::endl;

        /* Notify device registry about remote GPUs */
        PeerInfo peerCopy = m_peers[nodeId];
        notifyRemoteGpuDiscovered(peerCopy);
    }
}

void IpcServer::processDiscoveryMessage(const struct sockaddr_in& fromAddr,
                                         const DiscoveryMessageHeader& msg)
{
    /* Convert sender address to string */
    char addrStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, sizeof(addrStr));
    std::string address(addrStr);

    DiscoveryMessageType msgType = static_cast<DiscoveryMessageType>(ntohl(msg.messageType));

    switch (msgType) {
    case DiscoveryMessageType::ANNOUNCE:
        handlePeerAnnouncement(be64toh(msg.senderNodeId), address,
                                ntohs(msg.tcpPort), ntohl(msg.gpuCount));

        /* Reply with our own announce */
        sendDiscoveryAnnounce();
        break;

    case DiscoveryMessageType::ANNOUNCE_REPLY:
        handlePeerAnnouncement(be64toh(msg.senderNodeId), address,
                                ntohs(msg.tcpPort), ntohl(msg.gpuCount));
        break;

    case DiscoveryMessageType::HEARTBEAT:
        /* Update peer's last seen timestamp */
        {
            uint64_t nodeId = be64toh(msg.senderNodeId);
            if (nodeId == m_nodeId) break;

            std::lock_guard<std::mutex> lock(m_peerMutex);
            auto it = m_peers.find(nodeId);
            if (it != m_peers.end()) {
                it->second.lastSeenNs = static_cast<uint64_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count());
                it->second.missedHeartbeats = 0;

                /* Transition to ACTIVE if connected */
                if (it->second.state == PeerState::CONNECTED) {
                    it->second.state = PeerState::ACTIVE;
                }
            } else {
                /* Unknown peer sending heartbeat — treat as discovery */
                handlePeerAnnouncement(nodeId, address,
                                        ntohs(msg.tcpPort), ntohl(msg.gpuCount));
            }
        }
        break;

    case DiscoveryMessageType::HEARTBEAT_ACK:
        /* Peer acknowledged our heartbeat — confirm active */
        {
            uint64_t nodeId = be64toh(msg.senderNodeId);
            if (nodeId == m_nodeId) break;

            std::lock_guard<std::mutex> lock(m_peerMutex);
            auto it = m_peers.find(nodeId);
            if (it != m_peers.end()) {
                it->second.lastSeenNs = static_cast<uint64_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count());
                it->second.missedHeartbeats = 0;
                if (it->second.state == PeerState::CONNECTED) {
                    it->second.state = PeerState::ACTIVE;
                }
            }
        }
        break;

    case DiscoveryMessageType::GOODBYE:
        /* Peer is leaving gracefully */
        {
            uint64_t nodeId = be64toh(msg.senderNodeId);
            std::lock_guard<std::mutex> lock(m_peerMutex);
            auto it = m_peers.find(nodeId);
            if (it != m_peers.end()) {
                it->second.state = PeerState::DISCONNECTED;
                std::cout << "IPC: Peer " << nodeId
                          << " departed gracefully" << std::endl;
            }
        }
        break;

    case DiscoveryMessageType::GPU_ANNOUNCE:
        /* Peer updated GPU capabilities */
        handlePeerAnnouncement(be64toh(msg.senderNodeId), address,
                                ntohs(msg.tcpPort), ntohl(msg.gpuCount));
        break;

    default:
        break;
    }
}

bool IpcServer::acceptPeerTcpConnection()
{
    if (m_tcpListenFd < 0) {
        return false;
    }

    struct sockaddr_in peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);

    int peerFd = accept4(m_tcpListenFd,
                         reinterpret_cast<struct sockaddr*>(&peerAddr),
                         &peerAddrLen,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (peerFd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true; /* No pending connections */
        }
        return false;
    }

    /* Convert peer address to string */
    char addrStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peerAddr.sin_addr, addrStr, sizeof(addrStr));

    std::cout << "IPC: Incoming peer TCP connection from "
              << addrStr << ":" << ntohs(peerAddr.sin_port)
              << " (fd=" << peerFd << ")" << std::endl;

    /* Find peer by address and associate TCP socket */
    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        for (auto& pair : m_peers) {
            PeerInfo& peer = pair.second;
            if (peer.address == addrStr && peer.tcpSocketFd < 0) {
                peer.tcpSocketFd = peerFd;
                if (peer.state == PeerState::DISCOVERED) {
                    peer.state = PeerState::CONNECTED;
                }
                return true;
            }
        }
    }

    /* No matching peer found — close connection */
    close(peerFd);
    return true;
}

void IpcServer::notifyRemoteGpuDiscovered(const PeerInfo& peer)
{
    if (!m_daemon) {
        return;
    }

    auto& registry = m_daemon->deviceRegistry();

    /* For each GPU the peer advertises, add a remote GPU entry */
    for (uint32_t i = 0; i < peer.gpuCount; i++) {
        registry.addRemoteGpu(
            peer.nodeId,
            i,                        /* localGpuIndex on peer */
            0,                        /* vendorId (unknown — would come from detailed handshake) */
            0,                        /* deviceId (unknown) */
            0,                        /* vramBytes (unknown at discovery time) */
            0,                        /* vramFree */
            0,                        /* computeUnits */
            0,                        /* apiFlags */
            true,                     /* supportsGraphics (assumed) */
            true,                     /* supportsCompute (assumed) */
            peer.address
        );
    }
}

void IpcServer::discoveryThreadLoop()
{
    /* Constants for timing */
    constexpr auto HEARTBEAT_INTERVAL = std::chrono::seconds(5);
    constexpr auto ANNOUNCE_INTERVAL = std::chrono::seconds(30);
    constexpr auto PRUNE_INTERVAL = std::chrono::seconds(60);
    constexpr uint64_t PEER_TIMEOUT_NS = std::chrono::seconds(120).count() * 1000000000ULL;

    auto lastAnnounce = std::chrono::steady_clock::now();
    auto lastHeartbeat = std::chrono::steady_clock::now();
    auto lastPrune = std::chrono::steady_clock::now();

    /* Set up pollfd for UDP and TCP */
    struct pollfd fds[2];
    fds[0].fd = m_udpFd;
    fds[0].events = POLLIN;
    fds[1].fd = m_tcpListenFd;
    fds[1].events = POLLIN;

    while (m_discoveryRunning) {
        auto now = std::chrono::steady_clock::now();

        /* 1. Send periodic broadcast announcement */
        if (now - lastAnnounce >= ANNOUNCE_INTERVAL) {
            sendDiscoveryAnnounce();
            lastAnnounce = now;
        }

        /* 2. Send heartbeats to all connected peers */
        if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            for (auto& pair : m_peers) {
                PeerInfo& peer = pair.second;
                if (peer.state == PeerState::ACTIVE ||
                    peer.state == PeerState::CONNECTED) {
                    sendHeartbeat(peer);
                    peer.lastHeartbeatNs = static_cast<uint64_t>(
                        std::chrono::steady_clock::now().time_since_epoch().count());
                    peer.missedHeartbeats++;
                }
            }
            lastHeartbeat = std::chrono::steady_clock::now();
        }

        /* 3. Poll for incoming messages (100ms timeout) */
        int pollResult = poll(fds, 2, 100);

        if (pollResult > 0) {
            /* Check UDP socket for discovery messages */
            if (fds[0].revents & POLLIN) {
                uint8_t buffer[sizeof(DiscoveryMessageHeader) + 256];
                struct sockaddr_in fromAddr;
                socklen_t fromAddrLen = sizeof(fromAddr);

                ssize_t received = recvfrom(m_udpFd, buffer, sizeof(buffer), 0,
                                            reinterpret_cast<struct sockaddr*>(&fromAddr),
                                            &fromAddrLen);

                if (received >= static_cast<ssize_t>(sizeof(DiscoveryMessageHeader))) {
                    DiscoveryMessageHeader msg;
                    memcpy(&msg, buffer, sizeof(msg));

                    /* Validate magic */
                    if (ntohl(msg.magic) == DISCOVERY_MAGIC) {
                        processDiscoveryMessage(fromAddr, msg);
                    }
                }
            }

            /* Check TCP socket for peer connections */
            if (fds[1].revents & POLLIN) {
                acceptPeerTcpConnection();
            }
        }

        /* 4. Prune stale peers periodically */
        if (now - lastPrune >= PRUNE_INTERVAL) {
            pruneStalePeers(PEER_TIMEOUT_NS);
            lastPrune = now;
        }

        /* Brief sleep to prevent busy-waiting */
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void IpcServer::pruneStalePeers(uint64_t timeoutNs)
{
    std::lock_guard<std::mutex> lock(m_peerMutex);

    auto now = std::chrono::steady_clock::now();
    uint64_t nowNs = static_cast<uint64_t>(now.time_since_epoch().count());

    std::vector<uint64_t> stalePeers;

    for (const auto& pair : m_peers) {
        uint64_t elapsed = (nowNs > pair.second.lastSeenNs)
            ? (nowNs - pair.second.lastSeenNs)
            : 0;

        if (elapsed > timeoutNs || pair.second.missedHeartbeats > 24) {
            stalePeers.push_back(pair.first);
        }
    }

    for (uint64_t nodeId : stalePeers) {
        auto it = m_peers.find(nodeId);
        if (it != m_peers.end()) {
            /* Close TCP socket if open */
            if (it->second.tcpSocketFd >= 0) {
                close(it->second.tcpSocketFd);
            }
            std::cout << "IPC: Pruned stale peer " << nodeId << std::endl;
            m_peers.erase(it);
        }
    }
}

size_t IpcServer::peerCount() const
{
    std::lock_guard<std::mutex> lock(m_peerMutex);
    return m_peers.size();
}

std::vector<PeerInfo> IpcServer::getPeers() const
{
    std::lock_guard<std::mutex> lock(m_peerMutex);
    std::vector<PeerInfo> result;
    result.reserve(m_peers.size());
    for (const auto& pair : m_peers) {
        result.push_back(pair.second);
    }
    return result;
}

PeerState IpcServer::getPeerState(uint64_t nodeId) const
{
    std::lock_guard<std::mutex> lock(m_peerMutex);
    auto it = m_peers.find(nodeId);
    if (it == m_peers.end()) {
        return PeerState::DISCONNECTED;
    }
    return it->second.state;
}

void IpcServer::updatePeerState(uint64_t nodeId, PeerState newState)
{
    std::lock_guard<std::mutex> lock(m_peerMutex);
    auto it = m_peers.find(nodeId);
    if (it != m_peers.end()) {
        it->second.state = newState;
    }
}

} // namespace mvgal
