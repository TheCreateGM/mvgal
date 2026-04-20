/**
 * @file mvgal_ipc.h
 * @brief Inter-process communication API
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This header provides the IPC API for communication between the MVGAL
 * daemon and client applications.
 */

#ifndef MVGAL_IPC_H
#define MVGAL_IPC_H

#include "mvgal_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IPC message types
 */
typedef enum {
    MVGAL_IPC_MSG_PING = 0,                ///< Ping message
    MVGAL_IPC_MSG_PONG = 1,                ///< Pong response
    MVGAL_IPC_MSG_GPU_ENUMERATE = 2,      ///< Request GPU list
    MVGAL_IPC_MSG_GPU_LIST = 3,            ///< Response with GPU list
    MVGAL_IPC_MSG_WORKLOAD_SUBMIT = 4,    ///< Submit workload
    MVGAL_IPC_MSG_WORKLOAD_RESULT = 5,    ///< Workload result
    MVGAL_IPC_MSG_MEMORY_ALLOCATE = 6,    ///< Allocate memory
    MVGAL_IPC_MSG_MEMORY_FREE = 7,        ///< Free memory
    MVGAL_IPC_MSG_CONFIG_GET = 8,         ///< Get configuration
    MVGAL_IPC_MSG_CONFIG_SET = 9,         ///< Set configuration
    MVGAL_IPC_MSG_ERROR = 10,             ///< Error message
    MVGAL_IPC_MSG_MAX
} mvgal_ipc_message_type_t;

/**
 * @brief Initialize IPC server
 * @param socket_path Path to Unix domain socket (NULL for default)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_ipc_server_init(const char *socket_path);

/**
 * @brief Cleanup IPC server
 */
void mvgal_ipc_server_cleanup(void);

/**
 * @brief Start IPC server thread
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_ipc_server_start(void);

/**
 * @brief Stop IPC server thread
 */
void mvgal_ipc_server_stop(void);

/**
 * @brief Connect to IPC server as a client
 * @param socket_path Path to Unix domain socket (NULL for default)
 * @param fd_out Output file descriptor
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_ipc_client_connect(const char *socket_path, int *fd_out);

/**
 * @brief Disconnect from IPC server
 * @param fd File descriptor to close
 */
void mvgal_ipc_client_disconnect(int fd);

/**
 * @brief Send an IPC message
 * @param fd Socket file descriptor
 * @param type Message type
 * @param payload Pointer to payload data (NULL if no payload)
 * @param payload_size Size of payload in bytes
 * @param request_id Unique request identifier
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_ipc_send(int fd, mvgal_ipc_message_type_t type,
                              const void *payload, size_t payload_size,
                              uint64_t request_id);

/**
 * @brief Receive an IPC message
 * @param fd Socket file descriptor
 * @param type_out Output message type
 * @param payload_buf Buffer for payload data (NULL to skip payload)
 * @param payload_buf_size Size of payload buffer
 * @param payload_size_out Output actual payload size (NULL to skip)
 * @param request_id_out Output request identifier (NULL to skip)
 * @return MVGAL_SUCCESS on success, error code on failure
 */
mvgal_error_t mvgal_ipc_receive(int fd, mvgal_ipc_message_type_t *type_out,
                                void *payload_buf, size_t payload_buf_size,
                                size_t *payload_size_out, uint64_t *request_id_out);

#ifdef __cplusplus
}
#endif

#endif // MVGAL_IPC_H
