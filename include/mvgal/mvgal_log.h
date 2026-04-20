/**
 * @file mvgal_log.h
 * @brief Logging API
 * 
 * Multi-Vendor GPU Aggregation Layer for Linux
 * 
 * This header provides logging functions for MVGAL.
 */

#ifndef MVGAL_LOG_H
#define MVGAL_LOG_H

#include "mvgal_types.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup Logging
 * @{
 */

/**
 * @brief Log callback function
 * @param level Log level
 * @param message Message string
 * @param user_data User data
 */
typedef void (*mvgal_log_callback_t)(
    mvgal_log_level_t level,
    const char *message,
    void *user_data
);

/**
 * @brief Initialize logging system
 * 
 * @param level Default log level
 * @param callback Optional log callback (NULL for default)
 * @param user_data User data for callback
 */
void mvgal_log_init(mvgal_log_level_t level, mvgal_log_callback_t callback, void *user_data);

/**
 * @brief Shutdown logging system
 */
void mvgal_log_shutdown(void);

/**
 * @brief Set log level
 * 
 * @param level New log level
 */
void mvgal_log_set_level(mvgal_log_level_t level);

/**
 * @brief Get current log level
 * 
 * @return Current log level
 */
mvgal_log_level_t mvgal_log_get_level(void);

/**
 * @brief Register a log callback
 * 
 * @param callback Callback function
 * @param user_data User data
 */
void mvgal_log_register_callback(mvgal_log_callback_t callback, void *user_data);

/**
 * @brief Unregister a log callback
 * 
 * @param callback Callback function to remove
 */
void mvgal_log_unregister_callback(mvgal_log_callback_t callback);

/**
 * @brief Log a message at error level
 * 
 * @param format Format string (printf-style)
 * @param ... Variable arguments
 */
void mvgal_log_error(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Log a message at warning level
 * 
 * @param format Format string (printf-style)
 * @param ... Variable arguments
 */
void mvgal_log_warn(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Log a message at info level
 * 
 * @param format Format string (printf-style)
 * @param ... Variable arguments
 */
void mvgal_log_info(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Log a message at debug level
 * 
 * @param format Format string (printf-style)
 * @param ... Variable arguments
 */
void mvgal_log_debug(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Log a message at trace level
 * 
 * @param format Format string (printf-style)
 * @param ... Variable arguments
 */
void mvgal_log_trace(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Log a message at a specific level
 * 
 * @param level Log level
 * @param format Format string (printf-style)
 * @param ... Variable arguments
 */
void mvgal_log(mvgal_log_level_t level, const char *format, ...) __attribute__((format(printf, 2, 3)));

/**
 * @brief Check if a log level is enabled
 * 
 * @param level Log level to check
 * @return true if enabled, false otherwise
 */
bool mvgal_log_enabled(mvgal_log_level_t level);

/**
 * @brief Enable file logging
 * 
 * @param filepath Path to log file
 * @return true on success, false on failure
 */
bool mvgal_log_enable_file(const char *filepath);

/**
 * @brief Disable file logging
 */
void mvgal_log_disable_file(void);

/**
 * @brief Enable syslog logging
 * 
 * @param ident Identifier for syslog
 * @return true on success, false on failure
 */
bool mvgal_log_enable_syslog(const char *ident);

/**
 * @brief Disable syslog logging
 */
void mvgal_log_disable_syslog(void);

/**
 * @brief Enable colors in console output
 * 
 * @param enabled true to enable colors, false to disable
 */
void mvgal_log_enable_colors(bool enabled);

/**
 * @brief Set log output file
 * 
 * @param file FILE pointer to use for output
 */
void mvgal_log_set_output(FILE *file);

/**
 * @brief Get default log output file
 * 
 * @return Current output FILE pointer
 */
FILE *mvgal_log_get_output(void);

/**
 * @brief Flush log buffers
 */
void mvgal_log_flush(void);

/**
 * @brief Set prefix for log messages
 * 
 * @param prefix Prefix string (NULL for default)
 */
void mvgal_log_set_prefix(const char *prefix);

/**
 * @brief Get current log prefix
 * 
 * @return Current prefix string
 */
const char *mvgal_log_get_prefix(void);

/** @} */ // end of Logging

#ifdef __cplusplus
}
#endif

// Convenience macros for logging
#define MVGAL_LOG_ERROR(...) mvgal_log_error(__VA_ARGS__)
#define MVGAL_LOG_WARN(...) mvgal_log_warn(__VA_ARGS__)
#define MVGAL_LOG_INFO(...) mvgal_log_info(__VA_ARGS__)
#define MVGAL_LOG_DEBUG(...) mvgal_log_debug(__VA_ARGS__)
#define MVGAL_LOG_TRACE(...) mvgal_log_trace(__VA_ARGS__)

#endif // MVGAL_LOG_H
