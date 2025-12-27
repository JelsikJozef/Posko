//
// Created by Jozef Jelšík on 27/12/2025.
//

#ifndef SEMPRACA_PERSIST_H
#define SEMPRACA_PERSIST_H

#include "world.h"
#include "results.h"
#include "server_context.h"

/**
 * @file persist.h
 * @brief Simple binary persistence for world and results.
 *
 * File format (little-endian, versioned):
 *  - magic[8] = "RWRES\0\0\0"
 *  - uint32_t version (1)
 *  - uint32_t world_kind
 *  - uint32_t width
 *  - uint32_t height
 *  - double probs[4] {up,down,left,right}
 *  - uint32_t k_max_steps
 *  - uint32_t total_reps
 *  - uint8_t obstacles[cell_count]
 *  - uint32_t trials[cell_count]
 *  - uint64_t sum_steps[cell_count]
 *  - uint32_t success_leq_k[cell_count]
 */

int persist_save_results(const char *path,
                         const server_context_t *ctx,
                         const world_t *world,
                         const results_t *results);

int persist_load_results(const char *path,
                         server_context_t *ctx,
                         world_t *world,
                         results_t *results);

int persist_load_world(const char *path,
                       world_t *world,
                       server_context_t *ctx_optional);

#endif //SEMPRACA_PERSIST_H

