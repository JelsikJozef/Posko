// Snapshot assembly/renderer for client-side snapshot streaming.
#ifndef SEMPRACA_CLIENT_SNAPSHOT_H
#define SEMPRACA_CLIENT_SNAPSHOT_H

#include "../common/protocol.h"

/**
 * @file snapshot_reciever.h
 * @brief Client-side snapshot reassembly and rendering.
 *
 * The server may stream large datasets (world + results) using the chunked snapshot
 * protocol:
 *  - @ref RW_MSG_SNAPSHOT_BEGIN
 *  - 0..N x @ref RW_MSG_SNAPSHOT_CHUNK
 *  - @ref RW_MSG_SNAPSHOT_END
 *
 * This module reassembles the chunks into per-field buffers and can render the
 * completed snapshot to stdout.
 *
 * Call order
 * ----------
 * 1) @ref client_snapshot_begin()
 * 2) 0..N x @ref client_snapshot_chunk()
 * 3) @ref client_snapshot_end()
 *
 * Memory ownership
 * --------------
 * The module owns internal buffers for the current snapshot. Starting a new
 * snapshot frees any previous buffers.
 *
 * Threading
 * ---------
 * Not thread-safe. Call from a single thread (typically the client IPC/dispatch
 * thread).
 */

/**
 * @brief Begin assembling a new snapshot.
 *
 * Frees any previously assembled snapshot buffers and allocates buffers for the
 * fields indicated by @p begin->included_fields.
 *
 * @param begin Snapshot metadata received from the server.
 * @retval 0  Success.
 * @retval -1 Invalid arguments or allocation failure.
 */
int client_snapshot_begin(const rw_snapshot_begin_t *begin);

/**
 * @brief Apply one received snapshot chunk.
 *
 * The chunk is copied into the appropriate internal buffer at
 * @p chunk->offset_bytes.
 *
 * Snapshot ID handling:
 * - If @p chunk->snapshot_id does not match the most recent
 *   @ref client_snapshot_begin() call, the function ignores the chunk and
 *   returns success. This allows the client to tolerate late/stale chunks.
 *
 * @param chunk Chunk payload received from the server.
 * @retval 0  Success (including ignored stale chunks).
 * @retval -1 Invalid arguments, unknown field, buffer missing (field not included),
 *            or bounds error.
 */
int client_snapshot_chunk(const rw_snapshot_chunk_t *chunk);

/**
 * @brief Finish snapshot assembly and render it.
 *
 * Renders a radial summary table and heuristic summary bullets.
 *
 * @retval 0 Success.
 * @retval -1 Reserved for future use.
 */
int client_snapshot_end(void);

/**
 * @brief Render the last assembled snapshot again (radial summary + small grid).
 */
int client_snapshot_render_last(void);

/**
 * @brief Dump one cell from the last snapshot to stdout.
 */
int client_snapshot_dump_cell(uint32_t x, uint32_t y);

/**
 * @brief Cache K (k_max_steps) from WELCOME/status for snapshot summaries.
 */
void client_snapshot_set_k_max(uint32_t k_max_steps);

/**
 * @brief Free any allocated snapshot buffers.
 *
 * Optional cleanup helper to release memory when the client exits.
 */
void client_snapshot_free(void);

#endif /* SEMPRACA_CLIENT_SNAPSHOT_H */
