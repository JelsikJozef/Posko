//
// Created by Jozef Jelšík on 27/12/2025.
//

#include "random_walk.h"

#include "../common/util.h"

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/*simple mix for seeding*/
static uint32_t mix_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* splitmix64: simple, fast 64-bit generator suitable for per-thread simulation RNG */
static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void rw_rng_init_time_seed(rw_rng_t *rng) {
    if (!rng) return;
    memset(rng, 0, sizeof(*rng));

    struct timespec ts;
#if defined(CLOCK_REALTIME)
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = time(NULL);
        ts.tv_nsec = 0;
    }
#else
    ts.tv_sec = time(NULL);
    ts.tv_nsec = 0;
#endif

    uint64_t t = ((uint64_t)(uint32_t)ts.tv_sec << 32) ^ (uint64_t)(uint32_t)ts.tv_nsec;
    uint64_t p = (uint64_t)(uint32_t)getpid();

    uintptr_t tid = (uintptr_t)pthread_self();
    uint64_t th = (uint64_t)tid;

    uint64_t seed = (uint64_t)mix_u32((uint32_t)(t ^ (t >> 32) ^ p ^ th ^ (th >> 32)));
    seed = (seed << 32) ^ (uint64_t)mix_u32((uint32_t)(seed ^ 0xA5A5A5A5u));

    /* splitmix64 has a fixed-point at 0; avoid it */
    if (seed == 0) {
        seed = 0xD1B54A32D192ED03ULL;
    }

    rng->state = seed;
    rng->initialized = 1;
}

double rw_rng_next01(rw_rng_t *rng) {
    if (!rng || !rng->initialized) {
        die("rw_rng_next01: RNG not initialized");
    }

    uint64_t x = splitmix64_next(&rng->state);

    /* Convert to [0,1) using the top 53 bits for a uniform double. */
    const uint64_t top53 = x >> 11;
    return (double)top53 * (1.0 / 9007199254740992.0); /* 2^53 */
}

void random_walk_run(const world_t *w,
                     pos_t start,
                     move_probs_t probs,
                     uint32_t max_steps,
                     rw_rng_t *rng,
                     uint32_t *out_steps,
                     int *out_reached_origin,
                     int *out_success_leq_k) {

    if (!w || !rng || !out_steps || !out_reached_origin || !out_success_leq_k) {
        return;
    }

    pos_t p = start;

    if (!world_in_bounds(w, p.x, p.y)) {
        *out_steps = 0;
        *out_reached_origin = 0;
        *out_success_leq_k = 0;
        return;
    }

    //if start is obstacle
    if (world_is_obstacle_xy(w, p.x, p.y)) {
        *out_steps = 0;
        *out_reached_origin = 0;
        *out_success_leq_k = 0;
        return;
    }

    if (p.x == 0 && p.y == 0) {
        *out_steps = 0;
        *out_reached_origin = 1;
        *out_success_leq_k = 1;
        return;
    }

    double c1 = probs.p_up;
    double c2 = c1 + probs.p_down;
    double c3 = c2 + probs.p_left;
    double c4 = c3 + probs.p_right;

    //basic check: if it is not approx 1.0, normalize
    if (c4 <= 0.0) {
        *out_steps = max_steps;
        *out_reached_origin = 0;
        *out_success_leq_k = 0;
        return;
    }

    for (uint32_t step = 1 ; step <= max_steps ; step++) {
        double r = rw_rng_next01(rng);

        r *= c4;

        pos_t next = p;

        if (r < c1) {
            //up
            next.y -= 1;
        } else if (r < c2) {
            //down
            next.y += 1;
        } else if (r < c3) {
            //left
            next.x -= 1;
        } else {
            //right
            next.x += 1;
        }

        //wrap
        if (w->kind == WORLD_WRAP) {
            next = world_wrap_pos(w, next);
        }

        //if out of bouds (WORLD_OBSTACLES without wrap)  -> stay in place
        if (!world_in_bounds(w, next.x, next.y)) {
            next = p;
        } else {
            if (world_is_obstacle_xy(w, next.x, next.y)) {
                next = p;
            }
        }

        p = next;

        if (p.x == 0 && p.y == 0) {
            *out_steps = step;
            *out_reached_origin = 1;
            *out_success_leq_k = 1;
            return;
        }
    }

    *out_steps = max_steps;
    *out_reached_origin = 0;
    *out_success_leq_k = 0;
}
