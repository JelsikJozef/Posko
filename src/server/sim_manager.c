//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "sim_manager.h"

#include  "../common/protocol.h"
#include  "../common/util.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*======== broadcast PROGRESS =====*/

typedef struct {
    rw_progress_t msg;
}broadcast_progress_ctx_t;

static void send_progress_fn(int fd, void *user) {
    broadcast_progress_ctx_t *b = (broadcast_progress_ctx_t*)user;
    rw_send_msg(fd, RW_MSG_PROGRESS, &b->msg, sizeof(b->msg));
}

static void broadcast_progress(server_context_t *ctx,
                                uint32_t current,
                                uint32_t total) {
    broadcast_progress_ctx_t b;
    b.msg.current_rep = current;
    b.msg.total_reps = total;

    server_context_for_each_client(ctx, send_progress_fn, &b);
}

/*======== sim thread ========*/

static void *sim_thread_main(void *arg) {
    sim_manager_t *sm = (sim_manager_t*)arg;

    sm->running = 1;
    sm->stop_requested = 0;

    server_context_set_sim_state(sm->ctx, RW_WIRE_SIM_RUNNING);
    server_context_set_progress(sm->ctx, 0);

    if (worker_pool_init(&sm->pool,
                         sm->nthreads,
                         sm->queue_capacity,
                         sm->world,
                         sm->results,
                         sm->ctx->probs,
                         sm->ctx->k_max_steps) != 0) {
        die("sim_manager: worker_pool_init() failed");
                         }

    //results are acumulated over all reps -> clear at start

    results_clear(sm->results);

    uint32_t W = sm->world->size.width;
    uint32_t H = sm->world->size.height;

    for (uint32_t rep = 1; rep <= sm->ctx->total_reps; rep++) {
        if (sm->stop_requested) {
            break;
        }

        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) {
                if (sm->stop_requested) break;

                if (world_is_obstacle_xy(sm->world, (int32_t)x, (int32_t)y)) {
                    continue;
                }

                uint32_t idx = world_index(sm->world, (int32_t)x, (int32_t)y);

                rw_job_t job;
                job.cell_idx = idx;
                job.start.x = (int32_t)x;
                job.start.y = (int32_t)y;

                worker_pool_submit(&sm->pool, job);
            }
            if (sm->stop_requested) break;
        }
        //wait for all jobs to finish
        worker_pool_wait_all(&sm->pool);

        //update progress
        server_context_set_progress(sm->ctx, rep);

        //broadcast progress
        broadcast_progress(sm->ctx, rep, sm->ctx->total_reps);

        log_info("Replication %u/%u completed", rep, sm->ctx->total_reps);
    }

    worker_pool_stop(&sm->pool);
    worker_pool_destroy(&sm->pool);

    sm->running = 0;

    server_context_set_sim_state(sm->ctx, RW_WIRE_SIM_FINISHED);

    if (sm->on_end) {
        sm->on_end(sm->on_end_user, sm->stop_requested ? 1 : 0);
    }

    return NULL;
}

/*======== public API ========*/

int sim_manager_init(sim_manager_t *sm,
                     server_context_t *ctx,
                     world_t *world,
                     results_t *results,
                     int nthreads,
                     uint32_t queue_capacity) {
    if (!sm || !ctx || !world || !results) {
        return -1;
    }

    memset(sm, 0, sizeof(*sm));

    sm->ctx = ctx;
    sm->world = world;
    sm->results = results;
    sm->nthreads = (nthreads <= 0) ? 2 : nthreads;
    sm->queue_capacity = (queue_capacity == 0) ? 4096 : queue_capacity;

    sm->running = 0;
    sm->stop_requested = 0;

    sm->on_end = NULL;
    sm->on_end_user = NULL;

    return 0;
}

void sim_manager_destroy(sim_manager_t *sm) {
    if (!sm) return;

    sim_manager_request_stop(sm);

    if (sm->running) {
        pthread_join(sm->thread, NULL);
    }
}

int sim_manager_start(sim_manager_t *sm) {
    if (!sm) return -1;
    if (sm->running) return -1;

    if (pthread_create(&sm->thread, NULL, sim_thread_main, sm) != 0) {
        die("pthread_create(sim_thread) failed");
    }
    return 0;
}

void sim_manager_join(sim_manager_t *sm) {
    if (!sm) return;
    if (sm->running) {
        pthread_join(sm->thread, NULL);
        sm->running = 0;
    }
}

void sim_manager_set_on_end(sim_manager_t *sm, sim_manager_on_end_fn fn, void *user) {
    if (!sm) return;
    sm->on_end = fn;
    sm->on_end_user = user;
}

int sim_manager_restart(sim_manager_t *sm, uint32_t total_reps) {
    if (!sm) return -1;
    if (sm->running) return -1;
    if (total_reps == 0) return -1;

    sm->ctx->total_reps = total_reps;
    server_context_set_progress(sm->ctx, 0);
    server_context_set_sim_state(sm->ctx, RW_WIRE_SIM_LOBBY);

    return sim_manager_start(sm);
}

void sim_manager_request_stop(sim_manager_t *sm) {
    if (!sm) return;
    sm->stop_requested = 1;
}
