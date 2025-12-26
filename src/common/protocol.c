//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "protocol.h"

#include <errno.h>
#include <unistd.h>

/**
 * @file protocol.c
 * @brief Implementation of wire-level message send/receive helpers.
 */

/**
 * @brief Write exactly @p len bytes to a file descriptor.
 *
 * Retries short writes and handles `EINTR`. Returns -1 on any permanent error.
 *
 * @param fd File descriptor.
 * @param buf Bytes to write.
 * @param len Number of bytes to write.
 * @return 0 on success, -1 on error.
 */
static int rw_write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(fd, p + sent, len - sent);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n == 0) {
            /*Should not happen for write().*/
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

/**
 * @brief Read exactly @p len bytes from a file descriptor.
 *
 * Retries short reads and handles `EINTR`. Returns -1 on EOF or any permanent error.
 *
 * @param fd File descriptor.
 * @param buf Output buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 on EOF/error.
 */
static int rw_read_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t recvd = 0;

    while (recvd < len) {
        ssize_t n = read(fd, p + recvd, len - recvd);
        if (n > 0) {
            recvd += (size_t)n;
            continue;
        }
        if (n == 0) {
            /*EOF*/
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

/**
 * @brief Send a framed message (header + optional payload).
 *
 * @param fd Connected socket.
 * @param type Message type.
 * @param payload Pointer to payload bytes (may be NULL only if @p payload_len is 0).
 * @param payload_len Payload size in bytes.
 * @return 0 on success, -1 on error.
 */
int rw_send_msg(int fd, rw_msg_type_t type, const void *payload, uint32_t payload_len) {
    rw_msg_hdr_t hdr;
    hdr.type = (uint8_t)type;
    hdr.reserved = 0;
    hdr.payload_len = payload_len;

    if (rw_write_all(fd, &hdr, sizeof(hdr)) != 0) {
        return -1;
    }
    if (payload_len > 0) {
        if (payload == NULL) {
            return -1;
        }
        if (rw_write_all(fd, payload, (size_t)payload_len) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Receive a message header.
 *
 * @param fd Connected socket.
 * @param out_hdr Output header.
 * @return 0 on success, -1 on EOF/error.
 */
int rw_recv_hdr(int fd, rw_msg_hdr_t *out_hdr) {
    if (!out_hdr) {
        return -1;
    }
    if (rw_read_all(fd, out_hdr, sizeof(*out_hdr)) != 0) {
        return -1;
    }
    return 0;
}

/**
 * @brief Receive exactly @p len payload bytes.
 *
 * @param fd Connected socket.
 * @param buf Output buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 on EOF/error.
 */
int rw_recv_payload(int fd, void *buf, uint32_t len) {
    if (len == 0) {
        return 0;
    }
    if (!buf) {
        return -1;
    }
    if (rw_read_all(fd, buf, (size_t)len) != 0) {
        return -1;
    }
    return 0;
}
