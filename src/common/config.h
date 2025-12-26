//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_CONFIG_H
#define SEMPRACA_CONFIG_H

/**
 * @file config.h
 * @brief Project-wide compile-time configuration constants.
 */

/** @brief Maximum number of simultaneously connected clients the server supports. */
#define MAX_CLIENTS 32

/** @brief Maximum number of worker threads for simulation (if used by worker pool). */
#define MAX_WORKERS 8

/** @brief Maximum size of a Unix domain socket path (sun_path), including NUL terminator. */
#define SOCKET_PATH_LEN 108

#endif //SEMPRACA_CONFIG_H