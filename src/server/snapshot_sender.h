//
// Created by Jozef Jelšík on 27/12/2025.
//

#ifndef SEMPRACA_SNAPSHOT_SENDER_H
#define SEMPRACA_SNAPSHOT_SENDER_H

#include "world.h"
#include "results.h"
#include "server_context.h"

/**
 * @file snapshot_sender.h
 * @brief Server-side snapshot serialization and broadcast to clients.
 *
 * A "snapshot" is a point-in-time (best-effort) export of the current world and
 * aggregated results, sent to clients using the chunked snapshot protocol defined
 * in @ref protocol.h.
 *
 * Wire format
 * ----------
 * Data is transferred as per-field, byte-addressed arrays in row-major order:
 *   idx = y * width + x
 *
 * Fields (when included):
 * - obstacles      : uint8_t[cell_count]  (1=obstacle, 0=free)
 * - trials         : uint32_t[cell_count]
 * - sum_steps      : uint64_t[cell_count]
 * - success_leq_k  : uint32_t[cell_count]
 *
 * Chunking and ordering
 * ---------------------
 * The server sends:
 *  1) RW_MSG_SNAPSHOT_BEGIN (metadata; also contains included_fields bitmask)
 *  2) 0..N x RW_MSG_SNAPSHOT_CHUNK (slices of a single field)
 *  3) RW_MSG_SNAPSHOT_END
 *
 * Chunks may arrive in any grouping, but the receiver should copy each field's
 * bytes into a buffer at @c offset_bytes.
 *
 * Consistency
 * -----------
 * The snapshot is intended for visualization. The results arrays exposed by
 * @ref results_t are not an atomic snapshot; this function may observe updates
 * while sending unless the caller arranges external synchronization.
 */

/**
 * @brief Broadcast a snapshot to all currently connected clients.
 *
 * The function iterates over the current client list in @p ctx and attempts to
 * stream the snapshot to each client.
 *
 * @param ctx     Shared server context (used for client list and metadata).
 * @param world   World to snapshot.
 * @param results Results to snapshot.
 *
 * @retval 0  Success (best-effort).
 * @retval -1 Invalid arguments.
 *
 * @warning This function may perform blocking socket writes.
 */
int snapshot_broadcast(server_context_t *ctx,
                       const world_t *world,
                       const results_t *results);

/**
 * @brief Send a snapshot to a specific client.
 *
 * This function serializes and sends a snapshot to a single client identified
 * by the file descriptor @p fd. It's useful for targeted responses, such as
 * sending updated information to a specific client without broadcasting to
 * all connected clients.
 *
 * @param fd      File descriptor of the client socket.
 * @param world   World to snapshot.
 * @param results Results to snapshot.
 * @param snapshot_id Identifier for the snapshot, used to match requests and
 *                    responses.
 *
 * @retval 0  Success (best-effort).
 * @retval -1 Invalid arguments.
 *
 * @warning This function may perform blocking socket writes.
 */
int snapshot_send_to_client(int fd,
                            const world_t *world,
                            const results_t *results,
                            uint32_t snapshot_id);

uint32_t snapshot_next_id(void);

#endif //SEMPRACA_SNAPSHOT_SENDER_H
