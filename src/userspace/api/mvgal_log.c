/**
 * @file mvgal_log.c
 * @brief Logging implementation for MVGAL
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 *
 * This module implements the logging system with support for:
 * - Console output (stdout/stderr)
 * - File output
 * - Syslog output
 * - Custom callbacks
 * - Log level filtering
 * - Color output
 */

#include "mvgal/mvgal_log.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h>

/**
 * @brief Maximum number of registered callbacks
 */
#define MVGAL_MAX_LOG_CALLBACKS 8

/**
 * @brief Default log prefix
 */
#define MVGAL_DEFAULT_LOG_PREFIX "[MVGAL] "

/**
 * @brief Maximum log message length
 */
#define MVGAL_MAX_LOG_MESSAGE_LEN 2048

/**
 * @brief Log level names
 */
static const char *g_log_level_names[] = {
    "ERROR",
    "WARN",
    "INFO",
    "DEBUG",
    "TRACE",
    "MAX"
};

/**
 * @brief Log level colors (ANSI escape codes)
 */
static const char *g_log_colors[] = {
    "\033[0;31m",  // Red for ERROR
    "\033[0;33m",  // Yellow for WARN
    "\033[0;32m",  // Green for INFO
    "\033[0;36m",  // Cyan for DEBUG
    "\033[0;35m",  // Magenta for TRACE
    "\033[0m"      // Reset
};

/**
 * @brief Global log state
 */
typedef struct {
    mvgal_log_level_t level;              ///< Current log level
    FILE *output;                         ///< Output file (stdout/stderr by default)
    bool use_colors;                      ///< Enable ANSI color output
    bool file_enabled;                    ///< File logging enabled
    FILE *file_handle;                    ///< File handle for file logging
    bool syslog_enabled;                  ///< Syslog logging enabled
    char *prefix;                         ///< Log message prefix
    pthread_mutex_t lock;                 ///< Mutex for thread safety
    mvgal_log_callback_t callbacks[MVGAL_MAX_LOG_CALLBACKS];
    void *callback_data[MVGAL_MAX_LOG_CALLBACKS];
    int callback_count;
} log_state_t;

/**
 * @brief Global logging state
 */
static log_state_t g_log_state = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

/**
 * @brief Check if logging is initialized
 */
static bool g_log_initialized = false;

/**
 * @brief Default output
 */
static FILE *get_default_output(mvgal_log_level_t level) {
    // Error and warning messages go to stderr
    if (level <= MVGAL_LOG_LEVEL_WARN) {
        return stderr;
    }
    // Info and above go to stdout
    return stdout;
}

/**
 * @brief Get current timestamp string
 */
static const char *get_timestamp(void) {
    static char buf[64];
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

/**
 * @brief Write a formatted log message to a FILE*
 */
static void write_to_file(FILE *file, const char *message, bool use_color, const char *color) {
    if (file == NULL) {
        return;
    }
    
    if (use_color && g_log_state.use_colors && color != NULL) {
        fprintf(file, "%s%s\033[0m\n", color, message);
    } else {
        fprintf(file, "%s\n", message);
    }
    fflush(file);
}

/**
 * @brief Write a formatted log message to syslog
 */
static void write_to_syslog(mvgal_log_level_t level, const char *message) {
    if (!g_log_state.syslog_enabled) {
        return;
    }
    
    int syslog_level = LOG_INFO;
    switch (level) {
        case MVGAL_LOG_LEVEL_ERROR:
            syslog_level = LOG_ERR;
            break;
        case MVGAL_LOG_LEVEL_WARN:
            syslog_level = LOG_WARNING;
            break;
        case MVGAL_LOG_LEVEL_DEBUG:
        case MVGAL_LOG_LEVEL_TRACE:
            syslog_level = LOG_DEBUG;
            break;
        default:
            syslog_level = LOG_INFO;
            break;
    }
    
    syslog(syslog_level, "%s", message);
}

/**
 * @brief Log a message (internal)
 */
static void log_internal(mvgal_log_level_t level, const char *format, va_list args) {
    if (!g_log_initialized) {
        // Fallback to vfprintf if not initialized
        if (level <= MVGAL_LOG_LEVEL_WARN) {
            vfprintf(stderr, format, args);
            fprintf(stderr, "\n");
        } else {
            vfprintf(stdout, format, args);
            fprintf(stdout, "\n");
        }
        return;
    }
    
    // Check if this level is enabled
    if (level > g_log_state.level) {
        return;
    }
    
    pthread_mutex_lock(&g_log_state.lock);
    
    // Format the message
    char message[MVGAL_MAX_LOG_MESSAGE_LEN];
    
    // Build full message with timestamp, level, and prefix
    char full_message[MVGAL_MAX_LOG_MESSAGE_LEN];
    const char *level_str = g_log_level_names[(int)level];
    const char *prefix = g_log_state.prefix ? g_log_state.prefix : MVGAL_DEFAULT_LOG_PREFIX;
    const char *timestamp = get_timestamp();
    
    // Format the variable part first
    vsnprintf(message, sizeof(message), format, args);
    
    // Build full message: [timestamp] [LEVEL] prefix message
    // Use a safe approach to avoid buffer overflow
    int written = snprintf(full_message, sizeof(full_message), "[%s] [%s] %s", 
                           timestamp, level_str, prefix);
    if (written < (int)sizeof(full_message)) {
        size_t remaining = sizeof(full_message) - (size_t)written;
        snprintf(full_message + written, remaining, "%s", message);
    } else {
        // Buffer is full, just use what we have
        full_message[sizeof(full_message) - 1] = '\0';
    }
    
    // Write to default output
    if (g_log_state.output != NULL) {
        const char *color = g_log_state.use_colors ? g_log_colors[(int)level] : NULL;
        write_to_file(g_log_state.output, full_message, color != NULL, color);
    } else {
        // Use default based on level
        FILE *default_out = get_default_output(level);
        const char *color = g_log_state.use_colors ? g_log_colors[(int)level] : NULL;
        write_to_file(default_out, full_message, color != NULL, color);
    }
    
    // Write to file if enabled
    if (g_log_state.file_enabled && g_log_state.file_handle != NULL) {
        write_to_file(g_log_state.file_handle, full_message, false, NULL);
    }
    
    // Write to syslog if enabled
    if (g_log_state.syslog_enabled) {
        write_to_syslog(level, full_message);
    }
    
    // Call registered callbacks
    for (int i = 0; i < g_log_state.callback_count; i++) {
        if (g_log_state.callbacks[i] != NULL) {
            g_log_state.callbacks[i](level, full_message, g_log_state.callback_data[i]);
        }
    }
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Initialize logging system
 */
void mvgal_log_init(mvgal_log_level_t level, mvgal_log_callback_t callback, void *user_data) {
    pthread_mutex_lock(&g_log_state.lock);
    
    if (g_log_initialized) {
        pthread_mutex_unlock(&g_log_state.lock);
        return;
    }
    
    // Initialize state
    g_log_state.level = level;
    g_log_state.output = NULL;
    g_log_state.use_colors = true;
    g_log_state.file_enabled = false;
    g_log_state.file_handle = NULL;
    g_log_state.syslog_enabled = false;
    g_log_state.prefix = NULL;
    g_log_state.callback_count = 0;
    
    // Check if we should use colors (is stdout a terminal?)
    if (isatty(fileno(stdout)) || isatty(fileno(stderr))) {
        g_log_state.use_colors = true;
    } else {
        g_log_state.use_colors = false;
    }
    
    // Register initial callback if provided
    if (callback != NULL) {
        mvgal_log_register_callback(callback, user_data);
    }
    
    g_log_initialized = true;
    
    pthread_mutex_unlock(&g_log_state.lock);
    
    // Log initialization
    if (g_log_state.use_colors) {
        fprintf(stderr, "\033[0;32m[MVGAL] Logging initialized (level=%s)\033[0m\n", 
                g_log_level_names[(int)level]);
    } else {
        fprintf(stderr, "[MVGAL] Logging initialized (level=%s)\n", 
                g_log_level_names[(int)level]);
    }
}

/**
 * @brief Shutdown logging system
 */
void mvgal_log_shutdown(void) {
    if (!g_log_initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_log_state.lock);
    
    // Disable file logging
    if (g_log_state.file_enabled) {
        mvgal_log_disable_file();
    }
    
    // Disable syslog
    if (g_log_state.syslog_enabled) {
        mvgal_log_disable_syslog();
    }
    
    // Free prefix
    free(g_log_state.prefix);
    g_log_state.prefix = NULL;
    
    g_log_initialized = false;
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Set log level
 */
void mvgal_log_set_level(mvgal_log_level_t level) {
    pthread_mutex_lock(&g_log_state.lock);
    g_log_state.level = level;
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Get current log level
 */
mvgal_log_level_t mvgal_log_get_level(void) {
    pthread_mutex_lock(&g_log_state.lock);
    mvgal_log_level_t level = g_log_state.level;
    pthread_mutex_unlock(&g_log_state.lock);
    return level;
}

/**
 * @brief Register a log callback
 */
void mvgal_log_register_callback(mvgal_log_callback_t callback, void *user_data) {
    if (callback == NULL) {
        return;
    }
    
    pthread_mutex_lock(&g_log_state.lock);
    
    if (g_log_state.callback_count < MVGAL_MAX_LOG_CALLBACKS) {
        g_log_state.callbacks[g_log_state.callback_count] = callback;
        g_log_state.callback_data[g_log_state.callback_count] = user_data;
        g_log_state.callback_count++;
    }
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Unregister a log callback
 */
void mvgal_log_unregister_callback(mvgal_log_callback_t callback) {
    if (callback == NULL) {
        return;
    }
    
    pthread_mutex_lock(&g_log_state.lock);
    
    for (int i = 0; i < g_log_state.callback_count; i++) {
        if (g_log_state.callbacks[i] == callback) {
            // Remove callback by shifting
            for (int j = i; j < g_log_state.callback_count - 1; j++) {
                g_log_state.callbacks[j] = g_log_state.callbacks[j + 1];
                g_log_state.callback_data[j] = g_log_state.callback_data[j + 1];
            }
            g_log_state.callback_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Check if a log level is enabled
 */
bool mvgal_log_enabled(mvgal_log_level_t level) {
    pthread_mutex_lock(&g_log_state.lock);
    bool enabled = (level <= g_log_state.level);
    pthread_mutex_unlock(&g_log_state.lock);
    return enabled;
}

/**
 * @brief Enable file logging
 */
bool mvgal_log_enable_file(const char *filepath) {
    if (filepath == NULL) {
        return false;
    }
    
    pthread_mutex_lock(&g_log_state.lock);
    
    if (g_log_state.file_enabled) {
        // Already enabled, close existing file
        mvgal_log_disable_file();
    }
    
    FILE *file = fopen(filepath, "a");
    if (file == NULL) {
        pthread_mutex_unlock(&g_log_state.lock);
        return false;
    }
    
    g_log_state.file_handle = file;
    g_log_state.file_enabled = true;
    
    pthread_mutex_unlock(&g_log_state.lock);
    return true;
}

/**
 * @brief Disable file logging
 */
void mvgal_log_disable_file(void) {
    pthread_mutex_lock(&g_log_state.lock);
    
    if (g_log_state.file_handle != NULL) {
        fclose(g_log_state.file_handle);
        g_log_state.file_handle = NULL;
    }
    g_log_state.file_enabled = false;
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Enable syslog logging
 */
bool mvgal_log_enable_syslog(const char *ident) {
    if (ident == NULL) {
        return false;
    }
    
    pthread_mutex_lock(&g_log_state.lock);
    
    if (!g_log_state.syslog_enabled) {
        openlog(ident, LOG_PID | LOG_NDELAY, LOG_USER);
        g_log_state.syslog_enabled = true;
    }
    
    pthread_mutex_unlock(&g_log_state.lock);
    return true;
}

/**
 * @brief Disable syslog logging
 */
void mvgal_log_disable_syslog(void) {
    pthread_mutex_lock(&g_log_state.lock);
    
    if (g_log_state.syslog_enabled) {
        closelog();
        g_log_state.syslog_enabled = false;
    }
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Enable colors in console output
 */
void mvgal_log_enable_colors(bool enabled) {
    pthread_mutex_lock(&g_log_state.lock);
    g_log_state.use_colors = enabled;
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Set log output file
 */
void mvgal_log_set_output(FILE *file) {
    pthread_mutex_lock(&g_log_state.lock);
    g_log_state.output = file;
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Get default log output file
 */
FILE *mvgal_log_get_output(void) {
    pthread_mutex_lock(&g_log_state.lock);
    FILE *output = g_log_state.output;
    pthread_mutex_unlock(&g_log_state.lock);
    return output != NULL ? output : get_default_output(MVGAL_LOG_LEVEL_INFO);
}

/**
 * @brief Flush log buffers
 */
void mvgal_log_flush(void) {
    pthread_mutex_lock(&g_log_state.lock);
    
    if (g_log_state.output != NULL) {
        fflush(g_log_state.output);
    }
    if (g_log_state.file_enabled && g_log_state.file_handle != NULL) {
        fflush(g_log_state.file_handle);
    }
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Set prefix for log messages
 */
void mvgal_log_set_prefix(const char *prefix) {
    pthread_mutex_lock(&g_log_state.lock);
    
    free(g_log_state.prefix);
    g_log_state.prefix = prefix ? strdup(prefix) : NULL;
    
    pthread_mutex_unlock(&g_log_state.lock);
}

/**
 * @brief Get current log prefix
 */
const char *mvgal_log_get_prefix(void) {
    pthread_mutex_lock(&g_log_state.lock);
    const char *prefix = g_log_state.prefix;
    pthread_mutex_unlock(&g_log_state.lock);
    return prefix ? prefix : MVGAL_DEFAULT_LOG_PREFIX;
}

// Public logging functions

void mvgal_log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(MVGAL_LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

void mvgal_log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(MVGAL_LOG_LEVEL_WARN, format, args);
    va_end(args);
}

void mvgal_log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(MVGAL_LOG_LEVEL_INFO, format, args);
    va_end(args);
}

void mvgal_log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(MVGAL_LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

void mvgal_log_trace(const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(MVGAL_LOG_LEVEL_TRACE, format, args);
    va_end(args);
}

void mvgal_log(mvgal_log_level_t level, const char *format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(level, format, args);
    va_end(args);
}
