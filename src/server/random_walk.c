//
// Created by Jozef Jelšík on 27/12/2025.
//

#include "random_walk.h"

#include "../common/util.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
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

void rw_rng_init_time_seed(rw_rng_t *rng) {
    if (!rng) return;
    memset(rng,0, sizeof(*rng));

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    uint32_t t = (uint32_t)ts.tv_sec ^ (uint32_t)ts.tv_nsec;
    uint32_t p = (uint32_t)getpid();

    uintptr_t tid = (uintptr_t)pthread_self();
    uint32_t th = (uint32_t)(tid ^ (tid >> 32));

    uint32_t seed = mix_u32(t ^ (p * 2654435761u) ^ (th * 1013904223u));

    int rc = initstate_r((unsigned int)seed,
                          rng->state_buf,
                          sizeof(rng->state_buf),
                          &rng->rd);
    if (rc != 0) {
        errno = rc;
        die("rw_rng_init_time_seed: initstate() failed: %s", strerror(errno));
    }

    rng->initialized = 1;
}

double rw_rng_next01(rw_rng_t *rng) {
    if (!rng || !rng->initialized) {
        die("rw_rng_next01: RNG not initialized");
    }

    int32_t val = 0;

    if (random_r(&rng->rd, &val) != 0) {
        die("rw_rng_next01: random_r() failed");
    }

    uint32_t u = (uint32_t)val;
    return (double)(u) / (double)UINT32_MAX + 1.0;
}

static pos_t step_choice(pos_t p, double r) {
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 0.999999999;

    (void)p;
    return p;
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

