#include "snapshot_sender.h"

#include "server_context.h"
#include "world.h"
#include "results.h"
#include "../common/protocol.h"
#include "../common/util.h"

#include <string.h>

static uint32_t next_snapshot_id(void) {
    static uint32_t counter = 1;
    return counter++;
}

/* Send one field sliced into RW_SNAPSHOT_CHUNK_MAX-sized pieces. */
static int send_field_chunks(int fd,
                             uint32_t snapshot_id,
                             rw_snapshot_field_t field,
                             const uint8_t *data,
                             uint32_t total_bytes) {
    rw_snapshot_chunk_t chunk;
    chunk.snapshot_id = snapshot_id;
    chunk.field = (uint16_t)field;
    chunk.reserved = 0;

    uint32_t offset = 0;
    while (offset < total_bytes) {
        uint32_t remaining = total_bytes - offset;
        uint32_t to_copy = remaining < RW_SNAPSHOT_CHUNK_MAX ? remaining : RW_SNAPSHOT_CHUNK_MAX;

        chunk.offset_bytes = offset;
        chunk.data_len = to_copy;
        memcpy(chunk.data, data + offset, to_copy);

        if (rw_send_msg(fd, RW_MSG_SNAPSHOT_CHUNK, &chunk, sizeof(chunk.snapshot_id) + sizeof(chunk.field) + sizeof(chunk.reserved) + sizeof(chunk.offset_bytes) + sizeof(chunk.data_len) + to_copy) != 0) {
            return -1;
        }
        offset += to_copy;
    }
    return 0;
}

struct broadcast_ctx {
    uint32_t snapshot_id;
    const world_t *world;
    const results_t *results;
};

static uint32_t field_bit(rw_snapshot_field_t field) {
    /* Protocol enum starts at 1, so shift by (field-1) to build a bitmask. */
    if (field == 0) {
        return 0;
    }
    return 1u << (field - 1);
}

static int send_snapshot_to_client(int fd, const struct broadcast_ctx *bctx) {
    const world_t *world = bctx->world;
    const results_t *results = bctx->results;
    uint32_t cell_count = (uint32_t)(world->size.width * world->size.height);

    rw_snapshot_begin_t begin;
    begin.snapshot_id = bctx->snapshot_id;
    begin.size.width = (uint32_t)world->size.width;
    begin.size.height = (uint32_t)world->size.height;
    begin.world_kind = (world->kind == WORLD_OBSTACLES) ? RW_WIRE_WORLD_OBSTACLES : RW_WIRE_WORLD_WRAP;
    begin.cell_count = cell_count;
    begin.included_fields = field_bit(RW_SNAP_FIELD_OBSTACLES) |
                            field_bit(RW_SNAP_FIELD_TRIALS) |
                            field_bit(RW_SNAP_FIELD_SUM_STEPS) |
                            field_bit(RW_SNAP_FIELD_SUCC_LEQ_K);

    if (rw_send_msg(fd, RW_MSG_SNAPSHOT_BEGIN, &begin, sizeof(begin)) != 0) {
        return -1;
    }

    /* Obstacles */
    if (send_field_chunks(fd, bctx->snapshot_id, RW_SNAP_FIELD_OBSTACLES,
                          world->obstacles, cell_count * sizeof(uint8_t)) != 0) {
        return -1;
    }
    /* Trials */
    if (send_field_chunks(fd, bctx->snapshot_id, RW_SNAP_FIELD_TRIALS,
                          (const uint8_t *)results_trials(results), cell_count * sizeof(uint32_t)) != 0) {
        return -1;
    }
    /* Sum steps */
    if (send_field_chunks(fd, bctx->snapshot_id, RW_SNAP_FIELD_SUM_STEPS,
                          (const uint8_t *)results_sum_steps(results), cell_count * sizeof(uint64_t)) != 0) {
        return -1;
    }
    /* Success <= k */
    if (send_field_chunks(fd, bctx->snapshot_id, RW_SNAP_FIELD_SUCC_LEQ_K,
                          (const uint8_t *)results_success_leq_k(results), cell_count * sizeof(uint32_t)) != 0) {
        return -1;
    }

    if (rw_send_msg(fd, RW_MSG_SNAPSHOT_END, NULL, 0) != 0) {
        return -1;
    }

    return 0;
}

static void broadcast_cb(int fd, void *user) {
    const struct broadcast_ctx *bctx = (const struct broadcast_ctx *)user;
    if (send_snapshot_to_client(fd, bctx) != 0) {
        log_error("Failed to send snapshot to client fd=%d", fd);
    }
}

int snapshot_broadcast(server_context_t *ctx,
                       const world_t *world,
                       const results_t *results) {
    if (!ctx || !world || !results) {
        return -1;
    }
    struct broadcast_ctx bctx;
    bctx.snapshot_id = next_snapshot_id();
    bctx.world = world;
    bctx.results = results;

    server_context_for_each_client(ctx, broadcast_cb, &bctx);
    return 0;
}

