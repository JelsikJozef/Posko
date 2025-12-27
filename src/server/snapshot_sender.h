//
// Created by Jozef Jelšík on 27/12/2025.
//

#ifndef SEMPRACA_SNAPSHOT_SENDER_H
#define SEMPRACA_SNAPSHOT_SENDER_H

#include "world.h"
#include "results.h"
#include "server_context.h"


/*
 * snapshoot_sender - sends snapshot of results and world to clients
 *
 * Format (RAW)
 * - obstacles: uint_8_t[N]
 * - trials: uint32_t[N]
 * - sum_steps: uint64_t[N]
 * - success_leq_k: uint32_t[N]
 *
 * Where N = world width * world height
 *
 * Snapshot is sent as a sequence of messages:
 *  - RW_MSG_SNAPSHOT_BEGIN (with metadata)
 *  - one or more RW_MSG_SNAPSHOT_CHUNK (with data)
 *  - RW_MSG_SNAPSHOT_END
 */

int snapshot_broadcast_raw(server_context_t *ctx,
                           const world_t *world,
                           const results_t *results);

#endif //SEMPRACA_SNAPSHOT_SENDER_H