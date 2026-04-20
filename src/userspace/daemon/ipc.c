/**
 * @file ipc.c
 * @brief Inter-process communication for MVGAL daemon
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements IPC between the MVGAL daemon and client applications.
 * Uses Unix domain sockets for local communication.
 */

#include "mvgal_config.h"
#include "mvgal_log.h"
#include "mvgal_ipc.h"
#include "mvgal_gpu.h"
#include "mvgal_scheduler.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

/**
 * @brief IPC connection state
 */
typedef struct {
    int fd;
    struct sockaddr_un addr;
    bool connected;
} ipc_connection_t;

/**
 * @brief IPC server state
 */
typedef struct {
    int server_fd;
    struct sockaddr_un server_addr;
    bool running;
    pthread_t thread;
} ipc_server_t;

/**
 * @brief Global IPC server
 */
static ipc_server_t g_ipc_server = {0};

/**
 * @brief IPC message header structure
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          ///< Magic number: MVGAL_IPC_MAGIC
    uint32_t version;        ///< Protocol version
    uint32_t message_type;    ///< mvgal_ipc_message_type_t
    uint32_t payload_size;    ///< Size of payload data
    uint64_t request_id;      ///< Unique request identifier
} ipc_message_header_t;
#pragma pack(pop)

#define MVGAL_IPC_MAGIC 0x4D564741  ///< "MVGA" in hex
#define MVGAL_IPC_VERSION 1

/**
 * @brief Initialize IPC server
 */
mvgal_error_t mvgal_ipc_server_init(const char *socket_path) {
    if (g_ipc_server.running) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }
    
    if (socket_path == NULL) {
        // Use default socket path
        socket_path = "/var/run/mvgal/mvgal.sock";
    }
    
    // Create server socket
    g_ipc_server.server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_ipc_server.server_fd < 0) {
        MVGAL_LOG_ERROR("Failed to create IPC socket: %s", strerror(errno));
        return MVGAL_ERROR_DRIVER;
    }
    
    // Setup server address
    memset(&g_ipc_server.server_addr, 0, sizeof(g_ipc_server.server_addr));
    g_ipc_server.server_addr.sun_family = AF_UNIX;
    strncpy(g_ipc_server.server_addr.sun_path, socket_path, 
            sizeof(g_ipc_server.server_addr.sun_path) - 1);
    
    // Remove existing socket file if present
    unlink(socket_path);
    
    // Bind socket
    if (bind(g_ipc_server.server_fd, (struct sockaddr *)&g_ipc_server.server_addr, 
             sizeof(g_ipc_server.server_addr)) < 0) {
        MVGAL_LOG_ERROR("Failed to bind IPC socket: %s", strerror(errno));
        close(g_ipc_server.server_fd);
        g_ipc_server.server_fd = -1;
        return MVGAL_ERROR_DRIVER;
    }
    
    // Listen for connections
    if (listen(g_ipc_server.server_fd, 16) < 0) {
        MVGAL_LOG_ERROR("Failed to listen on IPC socket: %s", strerror(errno));
        close(g_ipc_server.server_fd);
        g_ipc_server.server_fd = -1;
        unlink(socket_path);
        return MVGAL_ERROR_DRIVER;
    }
    
    // Set socket permissions
    chmod(socket_path, 0666);
    
    g_ipc_server.running = true;
    
    MVGAL_LOG_INFO("IPC server initialized on %s", socket_path);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Cleanup IPC server
 */
void mvgal_ipc_server_cleanup(void) {
    if (!g_ipc_server.running) {
        return;
    }
    
    g_ipc_server.running = false;
    
    // Close server socket
    if (g_ipc_server.server_fd >= 0) {
        close(g_ipc_server.server_fd);
        g_ipc_server.server_fd = -1;
    }
    
    // Remove socket file
    unlink(g_ipc_server.server_addr.sun_path);
    
    MVGAL_LOG_INFO("IPC server cleaned up");
}

/**
 * @brief Handle a single client connection
 */
static void handle_client(int client_fd) {
    ipc_message_header_t header;
    ssize_t n;
    
    MVGAL_LOG_DEBUG("Handling IPC client connection");
    
    while (g_ipc_server.running) {
        // Read message header
        n = read(client_fd, &header, sizeof(header));
        if (n <= 0) {
            // Connection closed or error
            if (n == 0) {
                MVGAL_LOG_DEBUG("IPC client disconnected");
            } else if (errno != EINTR) {
                MVGAL_LOG_ERROR("IPC read error: %s", strerror(errno));
            }
            break;
        }
        
        if (n != sizeof(header)) {
            MVGAL_LOG_ERROR("IPC: short read on header");
            break;
        }
        
        // Validate magic
        if (header.magic != MVGAL_IPC_MAGIC) {
            MVGAL_LOG_ERROR("IPC: invalid magic number 0x%08X, expected 0x%08X",
                           header.magic, MVGAL_IPC_MAGIC);
            break;
        }
        
        // Validate version
        if (header.version != MVGAL_IPC_VERSION) {
            MVGAL_LOG_ERROR("IPC: unsupported version %u, expected %u",
                           header.version, MVGAL_IPC_VERSION);
            break;
        }
        
        MVGAL_LOG_DEBUG("IPC: received message type=%u, payload_size=%u, request_id=%llu",
                       header.message_type, header.payload_size, (unsigned long long)header.request_id);
        
        // Read payload if any
        void *payload = NULL;
        if (header.payload_size > 0) {
            payload = malloc(header.payload_size);
            if (payload == NULL) {
                MVGAL_LOG_ERROR("IPC: failed to allocate payload buffer");
                break;
            }
            n = read(client_fd, payload, header.payload_size);
            if (n != (ssize_t)header.payload_size) {
                MVGAL_LOG_ERROR("IPC: short read on payload");
                free(payload);
                break;
            }
        }
        
        // Process message
        ipc_message_header_t response_header = {
            .magic = MVGAL_IPC_MAGIC,
            .version = MVGAL_IPC_VERSION,
            .message_type = MVGAL_IPC_MSG_PONG,
            .payload_size = 0,
            .request_id = header.request_id
        };
        
        switch (header.message_type) {
            case MVGAL_IPC_MSG_PING: {
                response_header.message_type = MVGAL_IPC_MSG_PONG;
                break;
            }
            
            case MVGAL_IPC_MSG_GPU_ENUMERATE: {
                // Get GPU list
                mvgal_gpu_descriptor_t gpus[16];
                int32_t count = mvgal_gpu_enumerate(gpus, 16);
                
                if (count > 0) {
                    response_header.message_type = MVGAL_IPC_MSG_GPU_LIST;
                    response_header.payload_size = (uint32_t)(sizeof(mvgal_gpu_descriptor_t) * (size_t)count);
                    
                    // Send response header first
                    if (write(client_fd, &response_header, sizeof(response_header)) != sizeof(response_header)) {
                        MVGAL_LOG_ERROR("IPC: failed to send response header");
                        free(payload);
                        break;
                    }
                    
                    // Send GPU list
                    if (write(client_fd, gpus, response_header.payload_size) != (ssize_t)response_header.payload_size) {
                        MVGAL_LOG_ERROR("IPC: failed to send GPU list");
                        free(payload);
                        break;
                    }
                    free(payload);
                    continue; // Already sent response
                } else {
                    // No GPUs found
                    response_header.message_type = MVGAL_IPC_MSG_ERROR;
                    break;
                }
            }
            
            case MVGAL_IPC_MSG_CONFIG_GET: {
                // Get current configuration
                mvgal_config_t config;
                mvgal_config_get(&config);
                response_header.message_type = MVGAL_IPC_MSG_CONFIG_GET;
                response_header.payload_size = sizeof(mvgal_config_t);
                
                // Send response header first
                if (write(client_fd, &response_header, sizeof(response_header)) != sizeof(response_header)) {
                    MVGAL_LOG_ERROR("IPC: failed to send response header");
                    free(payload);
                    break;
                }
                
                // Send config
                if (write(client_fd, &config, sizeof(config)) != (ssize_t)sizeof(config)) {
                    MVGAL_LOG_ERROR("IPC: failed to send config");
                    free(payload);
                    break;
                }
                free(payload);
                continue; // Already sent response
            }
            
            case MVGAL_IPC_MSG_WORKLOAD_SUBMIT: {
                // Submit workload to scheduler
                if (payload == NULL || header.payload_size < sizeof(mvgal_workload_submit_info_t)) {
                    response_header.message_type = MVGAL_IPC_MSG_ERROR;
                    MVGAL_LOG_ERROR("IPC: invalid workload payload size: %u, expected >= %zu",
                                   header.payload_size, sizeof(mvgal_workload_submit_info_t));
                    break;
                }
                
                mvgal_workload_submit_info_t *submit_info = (mvgal_workload_submit_info_t *)payload;
                mvgal_workload_t workload = NULL;
                
                // Submit with NULL context (default)
                mvgal_error_t err = mvgal_workload_submit(NULL, submit_info, &workload);
                
                if (err == MVGAL_SUCCESS && workload != NULL) {
                    MVGAL_LOG_DEBUG("IPC: workload submitted successfully");
                    // For now, just send success response
                    // In a real implementation, we'd track the workload ID
                    response_header.message_type = MVGAL_IPC_MSG_WORKLOAD_RESULT;
                    response_header.payload_size = 0;
                    
                    // Send response header first
                    if (write(client_fd, &response_header, sizeof(response_header)) != sizeof(response_header)) {
                        MVGAL_LOG_ERROR("IPC: failed to send response header");
                        free(payload);
                        break;
                    }
                    free(payload);
                    continue; // Already sent response
                } else {
                    response_header.message_type = MVGAL_IPC_MSG_ERROR;
                    MVGAL_LOG_ERROR("IPC: failed to submit workload: %d", err);
                }
                break;
            }
            
            default: {
                MVGAL_LOG_WARN("IPC: unhandled message type %u", header.message_type);
                response_header.message_type = MVGAL_IPC_MSG_ERROR;
                break;
            }
        }
        
        // Send response for non-continuation cases
        if (write(client_fd, &response_header, sizeof(response_header)) != sizeof(response_header)) {
            MVGAL_LOG_ERROR("IPC: failed to send response header");
        }
        
        free(payload);
    }
    
    close(client_fd);
    MVGAL_LOG_DEBUG("IPC client handler exited");
}

/**
 * @brief IPC server main loop (runs in separate thread)
 */
static void *ipc_server_thread(void *arg) {
    (void)arg;
    
    MVGAL_LOG_DEBUG("IPC server thread started");
    
    while (g_ipc_server.running) {
        // Accept connection
        int client_fd = accept(g_ipc_server.server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            MVGAL_LOG_ERROR("IPC accept failed: %s", strerror(errno));
            break;
        }
        
        MVGAL_LOG_DEBUG("IPC client connected");
        
        // Handle client connection
        handle_client(client_fd);
    }
    
    MVGAL_LOG_DEBUG("IPC server thread exiting");
    return NULL;
}

/**
 * @brief Start IPC server thread
 */
mvgal_error_t mvgal_ipc_server_start(void) {
    if (!g_ipc_server.running) {
        return MVGAL_ERROR_NOT_INITIALIZED;
    }
    
    if (g_ipc_server.thread != 0) {
        return MVGAL_ERROR_ALREADY_INITIALIZED;
    }
    
    if (pthread_create(&g_ipc_server.thread, NULL, ipc_server_thread, NULL) != 0) {
        MVGAL_LOG_ERROR("Failed to create IPC server thread");
        return MVGAL_ERROR_INITIALIZATION;
    }
    
    MVGAL_LOG_INFO("IPC server thread started");
    return MVGAL_SUCCESS;
}

/**
 * @brief Stop IPC server thread
 */
void mvgal_ipc_server_stop(void) {
    g_ipc_server.running = false;
    
    // Wake up the accept call
    if (g_ipc_server.server_fd >= 0) {
        shutdown(g_ipc_server.server_fd, SHUT_RDWR);
    }
    
    // Wait for thread to finish
    if (g_ipc_server.thread != 0) {
        pthread_join(g_ipc_server.thread, NULL);
        g_ipc_server.thread = 0;
    }
}

/**
 * @brief Connect to IPC server
 */
mvgal_error_t mvgal_ipc_client_connect(const char *socket_path, int *fd_out) {
    if (fd_out == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    if (socket_path == NULL) {
        // Use default socket path
        socket_path = "/var/run/mvgal/mvgal.sock";
    }
    
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        MVGAL_LOG_ERROR("Failed to create IPC client socket: %s", strerror(errno));
        return MVGAL_ERROR_DRIVER;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        MVGAL_LOG_ERROR("Failed to connect to IPC socket: %s", strerror(errno));
        close(fd);
        return MVGAL_ERROR_IPC;
    }
    
    *fd_out = fd;
    MVGAL_LOG_DEBUG("IPC client connected to %s", socket_path);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Disconnect from IPC server
 */
void mvgal_ipc_client_disconnect(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

/**
 * @brief Send an IPC message
 */
mvgal_error_t mvgal_ipc_send(int fd, mvgal_ipc_message_type_t type, 
                              const void *payload, size_t payload_size,
                              uint64_t request_id) {
    if (fd < 0) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    ipc_message_header_t header = {
        .magic = MVGAL_IPC_MAGIC,
        .version = MVGAL_IPC_VERSION,
        .message_type = type,
        .payload_size = (uint32_t)payload_size,
        .request_id = request_id
    };
    
    // Send header
    if (write(fd, &header, sizeof(header)) != sizeof(header)) {
        MVGAL_LOG_ERROR("Failed to send IPC message header");
        return MVGAL_ERROR_IPC;
    }
    
    // Send payload
    if (payload_size > 0 && payload != NULL) {
        if (write(fd, payload, payload_size) != (ssize_t)payload_size) {
            MVGAL_LOG_ERROR("Failed to send IPC message payload");
            return MVGAL_ERROR_IPC;
        }
    }
    
    MVGAL_LOG_DEBUG("IPC message sent: type=%u, size=%zu", type, payload_size);
    
    return MVGAL_SUCCESS;
}

/**
 * @brief Receive an IPC message
 */
mvgal_error_t mvgal_ipc_receive(int fd, mvgal_ipc_message_type_t *type_out,
                                void *payload_buf, size_t payload_buf_size,
                                size_t *payload_size_out, uint64_t *request_id_out) {
    if (fd < 0 || type_out == NULL) {
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    ipc_message_header_t header;
    
    // Receive header
    if (read(fd, &header, sizeof(header)) != sizeof(header)) {
        if (errno == 0) {
            // Connection closed
            return MVGAL_ERROR_IPC;
        }
        MVGAL_LOG_ERROR("Failed to receive IPC message header");
        return MVGAL_ERROR_IPC;
    }
    
    // Validate magic
    if (header.magic != MVGAL_IPC_MAGIC) {
        MVGAL_LOG_ERROR("Invalid IPC message magic number");
        return MVGAL_ERROR_INVALID_ARGUMENT;
    }
    
    // Validate version
    if (header.version != MVGAL_IPC_VERSION) {
        MVGAL_LOG_ERROR("Unsupported IPC message version: %u", header.version);
        return MVGAL_ERROR_NOT_SUPPORTED;
    }
    
    *type_out = (mvgal_ipc_message_type_t)header.message_type;
    
    if (request_id_out) {
        *request_id_out = header.request_id;
    }
    
    if (payload_size_out) {
        *payload_size_out = header.payload_size;
    }
    
    // Receive payload
    if (header.payload_size > 0 && payload_buf != NULL) {
        size_t to_read = header.payload_size < payload_buf_size ? 
                        header.payload_size : payload_buf_size;
        if (read(fd, payload_buf, to_read) != (ssize_t)to_read) {
            MVGAL_LOG_ERROR("Failed to receive IPC message payload");
            return MVGAL_ERROR_IPC;
        }
    }
    
    MVGAL_LOG_DEBUG("IPC message received: type=%u, size=%u", 
                   header.message_type, header.payload_size);
    
    return MVGAL_SUCCESS;
}
