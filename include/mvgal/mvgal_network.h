/**
 * @file mvgal_network.h
 * @brief MVGAL Network-Distributed GPU Pooling Types
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Network transport layer, peer discovery, and remote GPU management.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_NETWORK_H
#define MVGAL_NETWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * Network Protocol Constants
 ******************************************************************************/

/** Magic number for network messages: 'MVGN' */
#define MVGAL_NET_MAGIC         0x4D56474E

/** Current protocol version */
#define MVGAL_NET_VERSION       1

/** Max payload size for a single message (1 MiB) */
#define MVGAL_NET_MAX_PAYLOAD   (1024 * 1024)

/** Max peer instance name length (including null terminator) */
#define MVGAL_NET_MAX_NAME_LEN  64

/** Max GPUs advertised in a single announce message */
#define MVGAL_NET_MAX_GPU_ANNOUNCE 8

/** Default discovery port */
#define MVGAL_NET_DEFAULT_PORT      54765

/** Default heartbeat interval in ms */
#define MVGAL_NET_DEFAULT_HEARTBEAT_MS 1000

/** Default timeout in ms (3 missed heartbeats) */
#define MVGAL_NET_DEFAULT_TIMEOUT_MS 5000

/******************************************************************************
 * Network Message Types
 ******************************************************************************/

/**
 * Network message type identifiers.
 * All messages begin with @ref MvgalNetMessageHeader.
 */
typedef enum {
    MVGAL_NET_MSG_INVALID       = 0,

    /* Discovery */
    MVGAL_NET_MSG_PEER_DISCOVERY  = 1,  /**< Broadcast: find peers */
    MVGAL_NET_MSG_GPU_ANNOUNCE    = 2,  /**< Unicast: advertise GPUs */

    /* Data plane */
    MVGAL_NET_MSG_WORK_SUBMIT    = 10,  /**< Submit workload to remote GPU */
    MVGAL_NET_MSG_WORK_COMPLETE  = 11,  /**< Workload finished on remote GPU */
    MVGAL_NET_MSG_MEM_TRANSFER   = 12,  /**< Transfer memory to/from remote GPU */
    MVGAL_NET_MSG_FENCE_SYNC     = 13,  /**< Cross-node fence synchronization */

    /* Liveness */
    MVGAL_NET_MSG_HEARTBEAT      = 20,  /**< Periodic liveness check */
    MVGAL_NET_MSG_HEARTBEAT_ACK  = 21,  /**< Heartbeat acknowledgement */
    MVGAL_NET_MSG_PEER_GOODBYE   = 22,  /**< Graceful disconnection */

    /* Control */
    MVGAL_NET_MSG_CONFIG_SYNC    = 30,  /**< Synchronize configuration */
    MVGAL_NET_MSG_ERROR          = 31,  /**< Error notification */
} mvgal_net_msg_type_t;

/******************************************************************************
 * Peer Connection States
 ******************************************************************************/

/**
 * State machine for peer connections.
 */
typedef enum {
    MVGAL_NET_PEER_DISCOVERED   = 0,  /**< Heard from peer, no connection yet */
    MVGAL_NET_PEER_CONNECTING   = 1,  /**< TCP connection in progress */
    MVGAL_NET_PEER_CONNECTED    = 2,  /**< TCP established, awaiting handshake */
    MVGAL_NET_PEER_ACTIVE       = 3,  /**< Fully operational */
    MVGAL_NET_PEER_DISCONNECTED = 4,  /**< Graceful disconnect */
    MVGAL_NET_PEER_TIMEOUT      = 5,  /**< Heartbeat timeout */
} mvgal_net_peer_state_t;

/******************************************************************************
 * Network Message Header
 ******************************************************************************/

/**
 * Wire-format message header.
 * All network messages start with this header.
 * Layout: 8 fields × 4 bytes = 32 bytes (packed, no padding).
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;               /**< Magic number: @ref MVGAL_NET_MAGIC */
    uint32_t version;             /**< Protocol version: @ref MVGAL_NET_VERSION */
    uint32_t msg_type;            /**< Message type from @ref mvgal_net_msg_type_t */
    uint32_t payload_size;        /**< Size of payload following header (bytes) */
    uint32_t sequence_num;        /**< Monotonic sequence number for ordering */
    uint32_t checksum;            /**< CRC32 of header + payload (0 = no checksum) */
    uint32_t flags;               /**< Bitfield: @ref MvgalNetMsgFlags */
    uint32_t reserved[5];         /**< Reserved for future use, must be 0 */
} mvgal_net_message_header_t;
#pragma pack(pop)

/** Total header size (should be 32) */
#define MVGAL_NET_HEADER_SIZE (sizeof(mvgal_net_message_header_t))

/******************************************************************************
 * Message Flags
 ******************************************************************************/

/** @name Message flags */
/**@{*/
#define MVGAL_NET_FLAG_REQUEST      (1U << 0)  /**< This is a request */
#define MVGAL_NET_FLAG_RESPONSE     (1U << 1)  /**< This is a response */
#define MVGAL_NET_FLAG_ERROR        (1U << 2)  /**< Error response */
#define MVGAL_NET_FLAG_MORE_FRAG    (1U << 3)  /**< More fragments follow */
#define MVGAL_NET_FLAG_NO_CHECKSUM  (1U << 4)  /**< Checksum field is invalid */

/** Bitmask of all defined flags */
#define MVGAL_NET_FLAG_ALL          (0x1F)
/**@}*/

/******************************************************************************
 * Payload Structures
 ******************************************************************************/

/**
 * Peer discovery payload.
 * Sent as broadcast on discovery port.
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t      node_id;                            /**< Unique node identifier */
    char          instance_name[MVGAL_NET_MAX_NAME_LEN]; /**< Human-readable node name */
    uint32_t      protocol_version;                   /**< Protocol version supported */
    uint32_t      num_gpus;                           /**< Number of local GPUs */
    /** Compact GPU descriptors follow (see @ref mvgal_net_gpu_descriptor_t) */
} mvgal_net_peer_discovery_t;
#pragma pack(pop)

/**
 * GPU descriptor for network announcement.
 * One per GPU in the announcement message.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t      gpu_index;          /**< Local GPU index */
    uint32_t      vendor_id;          /**< PCI vendor ID */
    uint32_t      device_id;          /**< PCI device ID */
    uint64_t      vram_bytes;         /**< Total VRAM in bytes */
    uint32_t      compute_units;      /**< Number of compute units */
    uint32_t      api_flags;          /**< Bitmask of supported APIs */
    uint32_t      pcie_gen;           /**< PCIe generation */
    uint32_t      pcie_lanes;         /**< PCIe lane count */
    uint32_t      supports_graphics;  /**< 1 if graphics capable */
    uint32_t      supports_compute;   /**< 1 if compute capable */
    uint32_t      reserved[5];        /**< Reserved, must be 0 */
} mvgal_net_gpu_descriptor_t;
#pragma pack(pop)

/**
 * GPU announce message header.
 * Followed by @ref num_gpus entries of @ref mvgal_net_gpu_descriptor_t.
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t      peer_node_id;       /**< Sender node ID */
    uint32_t      num_gpus;           /**< Number of GPU descriptors following */
    uint32_t      load_average;       /**< Current scheduler load (0-1000) */
} mvgal_net_gpu_announce_t;
#pragma pack(pop)

/**
 * Remote workload submission payload.
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t      workload_id;        /**< Unique workload identifier */
    uint32_t      source_gpu;         /**< GPU index on source node */
    uint32_t      target_gpu;         /**< GPU index on target node */
    uint32_t      workload_type;      /**< Workload type (mvgal_workload_type_t) */
    uint64_t      size_bytes;         /**< Estimated size for scheduling */
    uint32_t      priority;           /**< 0 (low) - 15 (high) */
    uint32_t      timeout_ms;         /**< Execution timeout (0 = infinite) */
    uint32_t      flags;              /**< Reserved */
    uint32_t      reserved[4];        /**< Reserved, must be 0 */
} mvgal_net_work_submit_t;
#pragma pack(pop)

/**
 * Memory transfer payload.
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t      transfer_id;        /**< Unique transfer identifier */
    uint32_t      direction;          /**< 0 = local→remote, 1 = remote→local */
    uint32_t      source_gpu;         /**< Source GPU index */
    uint32_t      target_gpu;         /**< Target GPU index */
    uint64_t      offset;             /**< Offset in source allocation */
    uint64_t      size_bytes;         /**< Number of bytes to transfer */
    uint32_t      dmabuf_fd;          /**< DMA-BUF file descriptor (-1 if none) */
    uint32_t      reserved[4];        /**< Reserved, must be 0 */
} mvgal_net_mem_transfer_t;
#pragma pack(pop)

/**
 * Fence synchronization payload.
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t      fence_id;           /**< Fence identifier (global) */
    uint32_t      signal_value;       /**< Value to signal */
    uint32_t      wait_value;         /**< Value to wait on */
    uint32_t      flags;              /**< 0 = signal, 1 = wait, 2 = signal+wait */
    uint32_t      reserved[4];        /**< Reserved, must be 0 */
} mvgal_net_fence_sync_t;
#pragma pack(pop)

/******************************************************************************
 * Remote GPU Representation
 ******************************************************************************/

/**
 * Maximum number of tracked peers.
 */
#define MVGAL_NET_MAX_PEERS 64

/**
 * Remote peer descriptor (node-level).
 */
typedef struct {
    uint64_t      node_id;            /**< Unique node identifier */
    char          instance_name[MVGAL_NET_MAX_NAME_LEN]; /**< Node name */
    uint32_t      peer_state;         /**< Current state from @ref mvgal_net_peer_state_t */
    uint32_t      num_gpus;           /**< Number of GPUs on this peer */
    uint32_t      protocol_version;   /**< Negotiated protocol version */
    /** Network address (sockaddr_storage sized, platform-dependent) */
    uint8_t       addr_storage[128];  /**< sockaddr_storage placeholder */
    uint32_t      addr_len;           /**< Length of addr_storage */
    uint32_t      heartbeat_ms;       /**< Heartbeat interval (negotiated) */
    uint64_t      last_seen_ns;       /**< Monotonic timestamp of last contact */
    uint64_t      latency_us;         /**< Estimated round-trip latency */
    uint32_t      load_average;       /**< Last reported load (0-1000) */
    uint32_t      flags;              /**< Peer flags (reserved) */
} mvgal_net_peer_t;

/**
 * Remote GPU descriptor (runtime representation).
 */
typedef struct {
    uint64_t      peer_node_id;       /**< Owning peer node ID */
    uint32_t      local_gpu_index;    /**< GPU index on the peer node */
    uint32_t      vendor_id;          /**< PCI vendor ID */
    uint32_t      device_id;          /**< PCI device ID */
    uint64_t      vram_bytes;         /**< Total VRAM in bytes */
    uint64_t      vram_free;          /**< Estimated free VRAM */
    uint32_t      compute_units;      /**< Compute unit count */
    uint32_t      api_flags;          /**< Supported API flags */
    uint32_t      pcie_gen;           /**< PCIe generation */
    uint32_t      pcie_lanes;         /**< PCIe lane count */
    uint32_t      supports_graphics;  /**< 1 if graphics capable */
    uint32_t      supports_compute;   /**< 1 if compute capable */
    uint32_t      is_online;          /**< 1 if currently reachable */
    uint32_t      last_health_state;  /**< Last health check result (0 = ok) */
    uint32_t      latency_us;         /**< Estimated round-trip latency */
    uint32_t      reserved[4];        /**< Reserved, must be 0 */
} mvgal_net_remote_gpu_t;

/******************************************************************************
 * Network Configuration
 ******************************************************************************/

/**
 * Network subsystem configuration.
 */
typedef struct {
    uint16_t      discovery_port;             /**< UDP discovery port */
    uint16_t      data_port;                  /**< TCP data port */
    char          listen_addr[64];            /**< Bind address (IPv4/IPv6) */
    uint32_t      heartbeat_interval_ms;      /**< Heartbeat interval */
    uint32_t      timeout_ms;                 /**< Connection timeout */
    uint32_t      max_peers;                  /**< Max peer connections */
    uint32_t      enable_discovery;           /**< 1 = enable UDP discovery */
    uint32_t      enable_tcp;                 /**< 1 = enable TCP data plane */
    uint64_t      max_transfer_bytes;         /**< Max per-transfer size */
    uint32_t      reserved[8];                /**< Reserved for future use */
} mvgal_net_config_t;

/******************************************************************************
 * Network Stats
 ******************************************************************************/

/**
 * Network subsystem statistics.
 */
typedef struct {
    uint64_t      peers_discovered;           /**< Total peers discovered */
    uint64_t      peers_lost;                 /**< Peers lost (timeout/disconnect) */
    uint64_t      messages_sent;              /**< Total messages sent */
    uint64_t      messages_received;          /**< Total messages received */
    uint64_t      bytes_sent;                 /**< Total bytes sent */
    uint64_t      bytes_received;             /**< Total bytes received */
    uint64_t      work_submitted;             /**< Remote workloads submitted */
    uint64_t      work_completed;             /**< Remote workloads completed */
    uint64_t      transfers_initiated;        /**< Memory transfers initiated */
    uint64_t      transfers_completed;        /**< Memory transfers completed */
    uint64_t      errors;                     /**< Total errors */
    uint32_t      active_peers;               /**< Currently connected peers */
    uint32_t      active_remote_gpus;         /**< Remote GPUs currently online */
    uint32_t      reserved[4];                /**< Reserved */
} mvgal_net_stats_t;

/******************************************************************************
 * Checksum Helper (inline)
 ******************************************************************************/

/**
 * Simple XOR-based checksum for network messages.
 * Not cryptographically secure — use CRC32 in production.
 *
 * @param data Pointer to data to checksum
 * @param len  Length of data in bytes
 * @return 32-bit XOR checksum
 */
static inline uint32_t mvgal_net_checksum_xor(const void *data, size_t len) {
    uint32_t sum = 0;
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        sum ^= ((uint32_t)bytes[i] << ((i & 3) * 8));
    }
    return sum;
}

#ifdef __cplusplus
}
#endif

#endif /* MVGAL_NETWORK_H */
