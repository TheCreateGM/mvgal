/**
 * MVGAL - Multi-Vendor GPU Aggregation Layer for Linux
 * IPC Server Header - Unix socket communication with clients
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_RUNTIME_IPC_SERVER_HPP
#define MVGAL_RUNTIME_IPC_SERVER_HPP

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <functional>

#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <poll.h>

namespace mvgal {

class Daemon;



/**
 * IPC message types
 */
enum class IpcMessageType : uint32_t {
    /* Connection */
    HELLO = 0,
    GOODBYE = 1,
    
    /* Device query */
    QUERY_DEVICES = 10,
    QUERY_DEVICE_CAPABILITIES = 11,
    QUERY_UNIFIED_CAPABILITIES = 12,
    
    /* Memory */
    ALLOC_MEMORY = 20,
    FREE_MEMORY = 21,
    IMPORT_DMABUF = 22,
    EXPORT_DMABUF = 23,
    
    /* Workload */
    SUBMIT_WORKLOAD = 30,
    WAIT_WORKLOAD = 31,
    
    /* Scheduling */
    SET_SCHEDULING_MODE = 40,
    SET_GPU_PRIORITY = 41,
    SET_GPU_ENABLED = 42,
    
    /* Statistics */
    GET_STATISTICS = 50,
    SUBSCRIBE_TELEMETRY = 51,
    UNSUBSCRIBE_TELEMETRY = 52,
    
    /* Configuration */
    GET_CONFIG = 60,
    SET_CONFIG = 61,
    LOAD_CONFIG = 62,
    SAVE_CONFIG = 63,
    
    /* Error */
    ERROR = 0xFFFFFFFF,
};

/**
 * IPC message header
 * All messages start with this header
 */
#pragma pack(push, 1)
struct IpcMessageHeader {
    uint32_t magic;           /* Magic number: 'MVGL' */
    uint32_t version;        /* Protocol version */
    uint32_t messageType;    /* IpcMessageType */
    uint32_t requestId;      /* Request ID (echoed in response) */
    uint32_t payloadSize;    /* Size of payload data */
    uint32_t flags;          /* Message flags */
    uint32_t reserved;       /* Reserved for alignment */
};
#pragma pack(pop)

static constexpr uint32_t IPC_MAGIC = 0x4D56474C; /* 'MVGL' */
static constexpr uint32_t IPC_VERSION = 1;

/**
 * IPC message flags
 */
namespace IpcFlags {
    constexpr uint32_t REQUEST = (1 << 0);   /* This is a request */
    constexpr uint32_t RESPONSE = (1 << 1);  /* This is a response */
    constexpr uint32_t ERROR = (1 << 2);     /* This is an error response */
    constexpr uint32_t NOTIFICATION = (1 << 3); /* This is a notification */
}

/**
 * IPC error codes
 */
enum class IpcErrorCode : uint32_t {
    SUCCESS = 0,
    INVALID_REQUEST = 1,
    NOT_IMPLEMENTED = 2,
    OUT_OF_MEMORY = 3,
    INVALID_ARGUMENT = 4,
    NOT_FOUND = 5,
    ACCESS_DENIED = 6,
    TIMEOUT = 7,
    GNU_ERROR, /* Internal error */
};

/**
 * Client connection
 */
class IpcClientConnection {
public:
    IpcClientConnection(int socketFd, const struct sockaddr_un& remoteAddr, uint32_t clientId);
    ~IpcClientConnection();

    int fd() const { return m_socketFd; }
    uint32_t clientId() const { return m_clientId; }
    
    /* Process incoming messages */
    bool processMessages();
    
    /* Send a message */
    bool sendMessage(IpcMessageType type, const void* payload = nullptr, size_t payloadSize = 0, uint32_t requestId = 0);
    
    /* Send an error response */
    bool sendError(IpcMessageType originalType, IpcErrorCode error, uint32_t requestId);
    
    /* Authentication */
    bool authenticate() const;

private:
    int m_socketFd;
    struct sockaddr_un m_remoteAddr;
    uint32_t m_clientId;
    
    /* Receive buffer */
    std::vector<uint8_t> m_receiveBuffer;
    
    /* Message headers */
    bool readMessageHeader(IpcMessageHeader& header);
    bool readPayload(void* buffer, size_t size);
};

/**
 * IPC Server - Unix domain socket for client communication
 */
class IpcServer {
public:
    explicit IpcServer(Daemon* daemon);
    ~IpcServer();

    bool init(const std::string& socketPath);
    void fini();

    /* Process events */
    void processEvents();
    
    /* Send notification to all clients */
    void sendNotification(IpcMessageType type, const void* payload = nullptr, size_t payloadSize = 0);
    
    /* Send notification to specific client */
    void sendNotification(uint32_t clientId, IpcMessageType type, const void* payload = nullptr, size_t payloadSize = 0);

    /* Register message handler */
    void registerHandler(IpcMessageType type, std::function<void(const IpcClientConnection&, const IpcMessageHeader&, const void*)> handler);

private:
    Daemon* m_daemon;
    int m_listenFd;
    std::string m_socketPath;
    mutable std::mutex m_mutex;
    
    /* Connected clients */
    std::unordered_map<uint32_t, std::shared_ptr<IpcClientConnection>> m_clients;
    uint32_t m_nextClientId;
    
    /* Message handlers */
    std::unordered_map<uint32_t, std::function<void(const IpcClientConnection&, const IpcMessageHeader&, const void*)>> m_handlers;
    
    /* Accept new connections */
    bool acceptConnections();
    
    /* Handle client message */
    void handleClientMessage(IpcClientConnection* client);
    
    /* Remove dead clients */
    void cleanupDeadClients();
    
    /* Helper to send message */
    bool sendMessageInternal(IpcClientConnection* client, IpcMessageType type, const void* payload, size_t payloadSize, uint32_t requestId);
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_IPC_SERVER_HPP
