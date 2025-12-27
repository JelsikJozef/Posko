//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_UTIL_H
#define SEMPRACA_UTIL_H

#include <stddef.h>

/**
 * @file util.h
 * @brief Small logging and fatal-error helpers.
 *
 * This module provides simple, thread-safe(ish) logging to stdout/stderr with a time
 * prefix, plus a `die()` helper that logs and terminates the process.
 */

/**
 * @brief Log a fatal error message and terminate the process.
 *
 * The function prints a timestamped message to stderr and exits with `EXIT_FAILURE`.
 * If `errno` is set at the time of call, it also prints the numeric errno and
 * corresponding strerror() string.
 *
 * @param fmt printf-style format string.
 * @param ... Format arguments.
 * @return This function does not return.
 */
void die(const char *fmt, ...);

/**
 * @brief Log an informational message to stdout.
 *
 * @param fmt printf-style format string.
 * @param ... Format arguments.
 */
void log_info(const char *fmt, ...);

/**
 * @brief Log an error message to stderr.
 *
 * @param fmt printf-style format string.
 * @param ... Format arguments.
 */
void log_error(const char *fmt, ...);

/**
 * @brief Safely copy a Unix-domain socket path into a fixed-size buffer.
 *
 * Ensures NUL-termination and fails if the source path doesn't fit.
 *
 * @param dst Destination buffer.
 * @param dst_size Size of destination buffer in bytes.
 * @param src Source C-string path.
 * @return 0 on success, -1 if args invalid or src doesn't fit.
 */
int rw_copy_socket_path(char *dst, size_t dst_size, const char *src);

#endif //SEMPRACA_UTIL_H
