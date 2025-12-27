//
// Created by Jozef Jelšík on 27/12/2025.
//

#ifndef SEMPRACA_CLIENT_DISPATCHER_H
#define SEMPRACA_CLIENT_DISPATCHER_H

#include "../common/protocol.h"

#include <stdint.h>
#include <stddef.h>

/**
 * @file client_dispatcher.h
 * @brief Single-reader dispatcher for client socket.
 *
 * Exactly one thread is allowed to call rw_recv_hdr()/rw_recv_payload() on the
 * client socket FD. This module owns that reader thread.
 *
 * It provides a minimal v1 API:
 * - start/stop the reader thread
 * - serialize exactly one in-flight synchronous request (ACK/STATUS/ERROR)
 *
 * Async messages (PROGRESS/END/GLOBAL_MODE_CHANGED):
 * - They are always consumed to prevent socket buffer buildup.
 * - They are intentionally NOT printed in the interactive client, because
 *   printing would corrupt the menu prompt.
 *
 * Snapshot stream (BEGIN/CHUNK/END):
 * - Is fed into snapshot_reciever.* and rendered when SNAPSHOT_END arrives.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Start reader thread for @p fd. Returns 0 on success. */
int dispatcher_start(int fd);

/** Stop reader thread and join it. Safe to call multiple times. */
void dispatcher_stop(void);

/**
 * @brief Send a request and wait for a matching response.
 *
 * Only one caller may wait at a time. The function serializes callers with an
 * internal mutex.
 *
 * @param fd Connected socket.
 * @param req_type Request type.
 * @param payload Request payload (may be NULL if payload_len==0).
 * @param payload_len Request payload length.
 * @param expected Array of expected response types (e.g. {RW_MSG_ACK, RW_MSG_ERROR}).
 * @param expected_count Number of expected response types.
 * @param timeout_ms Timeout in milliseconds. 0 means wait forever.
 * @param out_hdr Output response header.
 * @param out_payload Output buffer that will be malloc'ed and filled with payload (or NULL if payload_len==0).
 *                    Caller must free() when non-NULL.
 * @return 0 on success, -1 on error/timeout.
 */
int dispatcher_send_and_wait(
    int fd,
    rw_msg_type_t req_type,
    const void *payload,
    uint32_t payload_len,
    const rw_msg_type_t *expected,
    size_t expected_count,
    uint32_t timeout_ms,
    rw_msg_hdr_t *out_hdr,
    void **out_payload);

#ifdef __cplusplus
}
#endif

#endif //SEMPRACA_CLIENT_DISPATCHER_H

