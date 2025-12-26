//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/**
 * @file util.c
 * @brief Implementation of logging and fatal-error helpers.
 */

/**
 * @brief Print a `[HH:MM:SS]` prefix to the given output stream.
 *
 * @param out Output stream.
 */
static void print_time_prefix(FILE *out) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    fprintf(out, "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

/**
 * @brief Internal printf-style logger.
 *
 * Prepends a timestamp and log level, prints the formatted message and a trailing newline,
 * and flushes the stream.
 *
 * @param out Output stream.
 * @param level Log level label (e.g., "INFO").
 * @param fmt printf-style format string.
 * @param ap Variadic argument list.
 */
static void vlog(FILE *out, const char *level, const char *fmt, va_list ap) {
    print_time_prefix(out);
    fprintf(out, "[%s] ", level);
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    fflush(out);
}

/**
 * @brief Log an informational message to stdout.
 *
 * @param fmt printf-style format string.
 * @param ... Format arguments.
 */
void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stdout, "INFO", fmt, ap);
    va_end(ap);
}

/**
 * @brief Log an error message to stderr.
 *
 * @param fmt printf-style format string.
 * @param ... Format arguments.
 */
void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "ERROR", fmt, ap);
    va_end(ap);
}

/**
 * @brief Log a fatal error message and terminate the process.
 *
 * Additionally prints `errno` if it was non-zero on entry.
 *
 * @param fmt printf-style format string.
 * @param ... Format arguments.
 * @return This function does not return.
 */
void die(const char *fmt,...) {
    int saved_errno = errno;

    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, "FATAL", fmt, ap);
    va_end(ap);

    if (saved_errno != 0) {
        print_time_prefix(stderr);
        fprintf(stderr, "[FATAL] errno: %d (%s)\n", saved_errno, strerror(saved_errno));
        fflush(stderr);
    }
    exit(EXIT_FAILURE);
}