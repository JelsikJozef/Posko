//
// Created by Jozef Jelšík on 26/12/2025.
//

#include "worker_pool.h"

#include "../common/util.h"

#include <stdlib.h>
#include <string.h>

static void *worker_main(void *arg);

int worker_pool_init(worker_pool_t *p,
                     int nthreads,
                     uint32_t queue_capacity,
                     const world_t *world,
                     results_t *results,
                     move_probs_t probs,
                     uint32_t max_steps) {
    if (!p || !world || !results) return -1;
    if (nthreads <= 0) return -1;
    if (queue_capacity < 16) queue_capacity = 16;

    memset(p,0, sizeof(*p));
    p->nthreads = nthreads;
    p->q_cap = queue_capacity;
    p->world = world;
    p->results = results;
    p->probs = probs;
    p->max_steps = max_steps;

    p->q = (rw_job_t*)malloc(sizeof(rw_job_t) * (size_t)p->q_cap);
    p->threads = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)p->nthreads);

    if (!p->q || !p->threads) {
        worker_pool_destroy(p);
        return -1;
    }

    if (pthread_mutex_init(&p->mtx, NULL) != 0) return -1;
    if (pthread_cond_init(&p->cv_nonempty, NULL) != 0) return -1;
    if (pthread_cond_init(&p->cv_all_done, NULL) != 0) return -1;

    for (int i = 0; i < p->nthreads; i++) {
        if (pthread_create(&p->threads[i], NULL,worker_main,p) != 0) {
            die("pthread_create(worker) failed");
        }
    }

    return 0;
}

void worker_pool_stop(worker_pool_t *p) {
    if (!p) return;

    pthread_mutex_lock(&p->mtx);
    p->stop = 1;
    pthread_cond_broadcast(&p->cv_nonempty);
    pthread_mutex_unlock(&p->mtx);
}

void worker_pool_destroy(worker_pool_t *p) {
    if (!p) return;

    worker_pool_stop(p);

    for (int i = 0; i < p->nthreads; i++) {
        if (p->threads) {
            pthread_join(p->threads[i], NULL);
        }
    }

    pthread_cond_destroy(&p->cv_nonempty);
    pthread_cond_destroy(&p->cv_all_done);
    pthread_mutex_destroy(&p->mtx);

    free(p->q);
    free(p->threads);

    memset(p,0,sizeof(*p));
}

static int queue_push(worker_pool_t *p, rw_job_t job) {
    if (p->q_count >= p->q_cap) return -1;

    p->q[p->q_tail] = job;
    p->q_tail = (p->q_tail + 1) % p->q_cap;
    p->q_count++;
    return 0;
}

static int queue_pop(worker_pool_t *p, rw_job_t *out_job) {
    if (p->q_count == 0) return -1;

    *out_job = p->q[p->q_head];
    p->q_head = (p->q_head + 1) % p->q_cap;
    p->q_count--;
    return 0;
}

int worker_pool_submit(worker_pool_t *p, rw_job_t job) {
    if (!p) return -1;
    pthread_mutex_lock(&p->mtx);

    while (!p->stop && p->q_count >= p->q_cap) {
        pthread_mutex_unlock(&p->mtx);
        sched_yield();
        pthread_mutex_lock(&p->mtx);
    }
    if (p->stop) {
        pthread_mutex_unlock(&p->mtx);
        return -1;
    }

    if (queue_push(p, job) != 0) {
        pthread_mutex_unlock(&p->mtx);
        return -1;
    }

    p->in_flight++;

    pthread_cond_signal(&p->cv_nonempty);
    pthread_mutex_unlock(&p->mtx);
    return 0;
}

void worker_pool_wait_all(worker_pool_t *p) {
    if (!p) return;

    pthread_mutex_lock(&p->mtx);
    while (p->in_flight > 0) {
        pthread_cond_wait(&p->cv_all_done, &p->mtx);
    }
    pthread_mutex_unlock(&p->mtx);
}

static void job_done(worker_pool_t *p) {
    if (p->in_flight > 0) {
        p->in_flight--;
        if (p->in_flight == 0) {
            pthread_cond_signal(&p->cv_all_done);
        }
    }
}

static void *worker_main(void *arg) {
    worker_pool_t *p = (worker_pool_t *)arg;

    rw_rng_t rng;
    rw_rng_init_time_seed(&rng);

    while (1) {
        rw_job_t job;

        pthread_mutex_lock(&p->mtx);

        while (!p->stop && p->q_count == 0) {
            pthread_cond_wait(&p->cv_nonempty, &p->mtx);
        }

        if (p->stop) {
            pthread_mutex_unlock(&p->mtx);
            break;
        }

        if (queue_pop(p, &job) != 0) {
            pthread_mutex_unlock(&p->mtx);
            continue;
        }

        pthread_mutex_unlock(&p->mtx);

        uint32_t steps = 0;
        int reached = 0;
        int success = 0;

        random_walk_run(p->world, job.start, p->probs, p->max_steps,
                        &rng, &steps, &reached, &success);
        results_update(p->results, job.cell_idx, steps, reached, success);

        pthread_mutex_lock(&p->mtx);
        job_done(p);
        pthread_mutex_unlock(&p->mtx);
    }
    return NULL;
}