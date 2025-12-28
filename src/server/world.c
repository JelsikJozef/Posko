//
// Created by Jozef Jelšík on 26/12/2025.
//

/**
 * @file world.c
 * @brief Implementation of the world grid and obstacle helpers.
 */

#include "world.h"

#include "../common/util.h"

#include <stdlib.h>
#include <string.h>

static const int kNeighborDirs[4][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}
};

static void world_mark_reachable(const world_t *w,
                                 uint8_t *reachable,
                                 uint32_t *queue) {
    if (!w || !reachable || !queue) return;

    uint32_t n = world_cell_count(w);
    if (n == 0) return;

    memset(reachable, 0, n);

    if (w->obstacles[0]) {
        return; /* origin blocked – nothing reachable */
    }

    uint32_t width = (uint32_t)w->size.width;
    uint32_t head = 0;
    uint32_t tail = 0;
    queue[tail++] = 0;
    reachable[0] = 1;

    while (head < tail) {
        uint32_t idx = queue[head++];
        int32_t x = (int32_t)(idx % width);
        int32_t y = (int32_t)(idx / width);

        for (int i = 0; i < 4; i++) {
            int32_t nx = x + kNeighborDirs[i][0];
            int32_t ny = y + kNeighborDirs[i][1];
            if (!world_in_bounds(w, nx, ny)) continue;
            uint32_t nidx = world_index(w, nx, ny);
            if (w->obstacles[nidx]) continue;
            if (reachable[nidx]) continue;
            reachable[nidx] = 1;
            queue[tail++] = nidx;
        }
    }
}

static void world_carve_path_to_origin(world_t *w, uint32_t idx) {
    if (!w || !w->obstacles) return;

    uint32_t width = (uint32_t)w->size.width;
    if (width == 0) return;

    uint32_t height = (uint32_t)w->size.height;
    if (height == 0) return;

    if (idx >= width * height) return;

    int32_t x = (int32_t)(idx % width);
    int32_t y = (int32_t)(idx / width);

    /* Clear the starting cell and carve an axis-aligned corridor back to origin. */
    w->obstacles[idx] = 0;
    while (x > 0) {
        x--;
        w->obstacles[world_index(w, x, y)] = 0;
    }
    while (y > 0) {
        y--;
        w->obstacles[world_index(w, x, y)] = 0;
    }
}

static void world_enforce_origin_reachability(world_t *w) {
    if (!w || !w->obstacles) return;

    uint32_t n = world_cell_count(w);
    if (n == 0) return;

    uint8_t *reachable = (uint8_t*)malloc(n);
    uint32_t *queue = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)n);
    if (!reachable || !queue) {
        free(reachable);
        free(queue);
        return;
    }

    int fixed_any;
    do {
        fixed_any = 0;
        world_mark_reachable(w, reachable, queue);
        for (uint32_t i = 0; i < n; i++) {
            if (w->obstacles[i] == 0 && reachable[i] == 0) {
                world_carve_path_to_origin(w, i);
                fixed_any = 1;
                break; /* re-run flood fill after each carve */
            }
        }
    } while (fixed_any);

    free(queue);
    free(reachable);
}

/* Simple deterministic RNG (LCG) to generate obstacles.
 * Note: this is not cryptographically secure; it's only for reproducible maps.
 */

static uint32_t lcg_next(uint32_t *state) {
    *state = (*state * 1103515245u + 12345u) + 1013904223u;
    return *state;
}

uint32_t world_cell_count(const world_t *w) {
    return w->size.width * w->size.height;
}

int world_init(world_t *w, world_kind_t kind, world_size_t size) {
    if (!w) return -1;
    if (size.width == 0 || size.height == 0) return -1;

    memset(w,0, sizeof(*w));
    w->kind = kind;
    w->size = size;

    uint32_t n = world_cell_count(w);
    w->obstacles = (uint8_t*)malloc((size_t)n);
    if (!w->obstacles) {
        return -1;
    }
    memset(w->obstacles, 0, (size_t)n);
    return 0;
}

void world_destroy(world_t *w) {
    if (!w) return;
    free(w->obstacles);
    w->obstacles = NULL;
}

int world_in_bounds(const world_t *w, int32_t x, int32_t y) {
    return (x >= 0 &&  y >= 0 &&
            x < (int32_t)w->size.width &&
            y < (int32_t)w->size.height);
}

pos_t world_wrap_pos(const world_t *w, pos_t p) {
    pos_t out = p;
    //wrap x
    if (w->size.width > 0) {
        int32_t W = (int32_t)w->size.width;
        out.x = out.x % W;
        if (out.x < 0) out.x += W;
    }

    //wrap y
    if (w->size.height > 0) {
        int32_t H = (int32_t)w->size.height;
        out.y = out.y % H;
        if (out.y < 0) out.y += H;
    }
    return out;
}

uint32_t world_index(const world_t *w, int32_t x, int32_t y) {
    return (uint32_t)y * w->size.width + (uint32_t)x;
}

int world_is_obstacle_idx(const world_t *w, uint32_t idx) {
    uint32_t n = world_cell_count(w);
    if (idx >= n) return 1;
    return w->obstacles[idx] ? 1 : 0;
}

int world_is_obstacle_xy(const world_t *w, int32_t x, int32_t y) {
    if (!world_in_bounds(w, x, y)) return 1;
    uint32_t idx = world_index(w, x, y);
    return world_is_obstacle_idx(w, idx);
}

void world_set_obstacle(world_t *w, int32_t x, int32_t y, int value) {
    if (!world_in_bounds(w, x, y)) return;
    uint32_t idx = world_index(w, x, y);
    w->obstacles[idx] = (uint8_t)value ? 1 : 0;
}

void world_generate_obstacles(world_t *w, int percent, uint32_t seed) {
    if (!w || !w->obstacles) return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    uint32_t n = world_cell_count(w);
    uint32_t state = seed;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = lcg_next(&state) % 100u;
        w->obstacles[i] = (r < (uint32_t)percent) ? 1 : 0;
    }

    if (n > 0) {
        w->obstacles[0] = 0; /* Ensure origin (0,0) is always free. */
    }

    world_enforce_origin_reachability(w);
}
