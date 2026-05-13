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
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>

#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <poll.h>
#include <netinet/in.h>

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
 * Peer discovery protocol message types
 */
enum class DiscoveryMessageType : uint32_t {
    ANNOUNCE = 0,        /* Broadcast: "I exist" */
    ANNOUNCE_REPLY = 1,  /* Unicast reply to announce */
    HEARTBEAT = 2,       /* Periodic: "I'm still here" */
    HEARTBEAT_ACK = 3,   /* Acknowledge heartbeat */
    GOODBYE = 4,         /* Graceful departure */
    GPU_ANNOUNCE = 5,    /* Remote GPU capabilities update */
};

/**
 * Peer connection state machine
 */
enum class PeerState : uint32_t {
    DISCOVERED = 0,   /* Heard from peer via broadcast */
    CONNECTED = 1,    /* TCP connection established */
    ACTIVE = 2,       /* Bidirectional communication verified */
    DISCONNECTED = 3, /* Connection lost or peer departed */
};

/**
 * Discovery message header (sent over UDP)
 */
#pragma pack(push, 1)
struct DiscoveryMessageHeader {
    uint32_t magic;                 /* 'MVGD' */
    uint32_t version;              /* Protocol version */
    uint32_t messageType;          /* DiscoveryMessageType */
    uint64_t senderNodeId;         /* Unique node identifier */
    uint32_t gpuCount;             /* Number of GPUs on sender */
    uint32_t tcpPort;              /* TCP listener port of sender */
    uint64_t timestampNs;          /* Monotonic timestamp */
    uint32_t reserved;
};
#pragma pack(pop)

static constexpr uint32_t DISCOVERY_MAGIC = 0x4D564744; /* 'MVGD' */
static constexpr uint32_t DISCOVERY_VERSION = 1;
static constexpr uint16_t DEFAULT_DISCOVERY_PORT = 42069;

/**
 * Peer tracking info
 */
struct PeerInfo {
    uint64_t nodeId;                /* Unique node identifier */
    std::string address;            /* IP address string */
    uint16_t tcpPort;               /* TCP port for data connection */
    PeerState state;                /* Current connection state */
    uint32_t gpuCount;              /* Number of GPUs on peer */
    uint64_t lastSeenNs;            /* Last contact timestamp (ns) */
    uint64_t lastHeartbeatNs;       /* Last heartbeat timestamp (ns) */
    uint32_t missedHeartbeats;      /* Consecutive missed heartbeats */
    int tcpSocketFd;                /* Connected TCP socket (-1 if not connected) */
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

    /* =================================================================
     * Network Peer Discovery (TCP/UDP)
     * =================================================================
     */

    /** Initialize TCP listener and start discovery thread */
    bool initNetworkDiscovery(uint16_t discoveryPort = DEFAULT_DISCOVERY_PORT,
                               uint16_t listenPort = 0,
                               const std::string& listenAddr = "0.0.0.0");

    /** Stop network discovery thread */
    void finiNetworkDiscovery();

    /** Query discovered peers */
    size_t peerCount() const;
    std::vector<PeerInfo> getPeers() const;

    /** Get peer state */
    PeerState getPeerState(uint64_t nodeId) const;

    /** Update peer state in state machine */
    void updatePeerState(uint64_t nodeId, PeerState newState);

    /** Remove stale peers */
    void pruneStalePeers(uint64_t timeoutNs);

    /** Get node ID */
    uint64_t nodeId() const { return m_nodeId; }

    /** Check if network discovery is active */
    bool discoveryEnabled() const { return m_discoveryEnabled; }

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

    /* =================================================================
     * Network discovery implementation
     * =================================================================
     */

    /* Unique node identifier (derived from hostname + boot time) */
    uint64_t m_nodeId;

    /* TCP listener for peer data connections */
    int m_tcpListenFd;

    /* UDP socket for discovery broadcasts */
    int m_udpFd;

    /* Network discovery config */
    uint16_t m_discoveryPort;
    uint16_t m_tcpPort;
    std::string m_listenAddr;
    std::atomic<bool> m_discoveryEnabled;
    std::atomic<bool> m_discoveryRunning;

    /* Discovery thread */
    std::thread m_discoveryThread;

    /* Discovered peers */
    std::unordered_map<uint64_t, PeerInfo> m_peers;
    mutable std::mutex m_peerMutex;

    /* Initialize UDP discovery socket */
    bool initUdpSocket();

    /* Initialize TCP listener for peer connections */
    bool initTcpListener();

    /* Discovery thread main loop */
    void discoveryThreadLoop();

    /* Send broadcast announce message */
    bool sendDiscoveryAnnounce();

    /* Send directed heartbeat to a peer */
    bool sendHeartbeat(PeerInfo& peer);

    /* Process incoming discovery messages */
    void processDiscoveryMessage(const struct sockaddr_in& fromAddr, const DiscoveryMessageHeader& msg);

    /* Accept incoming TCP connections from peers */
    bool acceptPeerTcpConnection();

    /* Handle peer announcement from discovery message */
    void handlePeerAnnouncement(uint64_t nodeId, const std::string& address,
                                 uint16_t tcpPort, uint32_t gpuCount);

    /* Notify device registry of remote GPU availability */
    void notifyRemoteGpuDiscovered(const PeerInfo& peer);

    /* Helper to generate node ID */
    static uint64_t generateNodeId();
};

} // namespace mvgal

#endif // MVGAL_RUNTIME_IPC_SERVER_HPP
