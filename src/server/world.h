//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_WORLD_H
#define SEMPRACA_WORLD_H

/**
 *world.h - representation of the world (grid) and obstacles
 *
 *Responsibility:
 *- Define and manage the world grid, including obstacles and boundaries.
 *-helpers for position validation and movement within the world.
 *-without simulation logic (which is elsewhere).
 */

#include "../common/types.h"

#include <stdint.h>

typedef struct {
    world_kind_t kind;
    world_size_t size;
    uint8_t* obstacles; /* obstacles 0 = free, 1 = obsatcle */
} world_t;

/*lifecycle*/
int world_init(world_t *w, world_kind_t kind, world_size_t size);
void world_destroy(world_t *w);

/* obstacke generation (percentage 0..100).
 * seed = deterministicaly uniform random obstacles */
void world_generate_obstacles(world_t *w, int percent, uint32_t seed);

/* helpers */
uint32_t world_cell_count(const world_t *w);
uint32_t world_index(const world_t *w, int32_t x, int32_t y);

int world_in_bounds(const world_t *w, int32_t x, int32_t y);
pos_t world_wrap_pos(const world_t *w, pos_t p);

int world_is_obstacle_xy(const world_t *w, int32_t x, int32_t y);
int world_is_obstacle_idx(const world_t *w, uint32_t idx);

/* set/remove an obstacle at (x,y) */
void world_set_obstacle(world_t *w, int32_t x, int32_t y, int value);




#endif //SEMPRACA_WORLD_H