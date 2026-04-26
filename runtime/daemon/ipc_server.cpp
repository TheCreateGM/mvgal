/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * IPC Server Implementation - Unix socket communication with clients
 * 
 * SPDX-License-Identifier: MIT
 */

#include "ipc_server.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <system_error>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
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

IpcClientConnection::IpcClientConnection(int socketFd, const struct sockaddr_un& remoteAddr, uint32_t clientId)
    : m_socketFd(socketFd),
      m_remoteAddr(remoteAddr),
      m_clientId(clientId),
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

    /* Convert from network byte order */
    header.magic = ntohl(header.magic);
    header.version = ntohl(header.version);
    header.messageType = ntohl(header.messageType);
    header.requestId = ntohl(header.requestId);
    header.payloadSize = ntohl(header.payloadSize);
    header.flags = ntohl(header.flags);
    header.reserved = ntohl(header.reserved);

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
            /* Handle device query - placeholder */
            std::cerr << "IPC: QUERY_DEVICES not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::QUERY_DEVICE_CAPABILITIES: {
            std::cerr << "IPC: QUERY_DEVICE_CAPABILITIES not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::QUERY_UNIFIED_CAPABILITIES: {
            std::cerr << "IPC: QUERY_UNIFIED_CAPABILITIES not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::ALLOC_MEMORY: {
            std::cerr << "IPC: ALLOC_MEMORY not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::FREE_MEMORY: {
            std::cerr << "IPC: FREE_MEMORY not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::IMPORT_DMABUF: {
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
            std::cerr << "IPC: SUBMIT_WORKLOAD not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::WAIT_WORKLOAD: {
            std::cerr << "IPC: WAIT_WORKLOAD not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::SET_SCHEDULING_MODE: {
            std::cerr << "IPC: SET_SCHEDULING_MODE not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::SET_GPU_PRIORITY: {
            std::cerr << "IPC: SET_GPU_PRIORITY not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
            break;
        }

        case IpcMessageType::SET_GPU_ENABLED: {
            std::cerr << "IPC: SET_GPU_ENABLED not yet implemented" << std::endl;
            sendError(msgType, IpcErrorCode::NOT_IMPLEMENTED, header.requestId);
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
      m_nextClientId(1)
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
    unlink(m_socketPath.c_str());

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
    std::lock_guard<std::mutex> lock(m_mutex);

    /* Close all client connections */
    for (auto& pair : m_clients) {
        pair.second->~IpcClientConnection();
    }
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

        auto connection = std::make_shared<IpcClientConnection>(clientFd, remoteAddr, clientId);

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

    /* Cleanup dead clients */
    cleanupDeadClients();

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

void IpcServer::cleanupDeadClients()
{
    std::vector<uint32_t> deadClients;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_clients) {
            if (pair.second.use_count() == 1) {
                /* Only we hold the reference, client is dead */
                deadClients.push_back(pair.first);
            }
        }
    }

    for (uint32_t clientId : deadClients) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clients.erase(clientId);
        std::cout << "IPC: Client " << clientId << " disconnected" << std::endl;
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

} // namespace mvgal
