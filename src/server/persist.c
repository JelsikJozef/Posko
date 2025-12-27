//
// Created by Jozef Jelšík on 27/12/2025.
//

#include "persist.h"

#include "../common/util.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define RWRES_MAGIC "RWRES\0\0\0"
#define RWRES_MAGIC_LEN 8
#define RWRES_VERSION 1u

static int write_exact(FILE *f, const void *p, size_t n) {
    return fwrite(p, 1, n, f) == n ? 0 : -1;
}

static int read_exact(FILE *f, void *p, size_t n) {
    return fread(p, 1, n, f) == n ? 0 : -1;
}

int persist_save_results(const char *path,
                         const server_context_t *ctx,
                         const world_t *world,
                         const results_t *results) {
    if (!path || !ctx || !world || !results) return -1;

    FILE *f = fopen(path, "wb");
    if (!f) {
        log_error("persist_save_results: fopen('%s') failed: %s", path, strerror(errno));
        return -1;
    }

    const char magic[RWRES_MAGIC_LEN] = RWRES_MAGIC;
    uint32_t version = RWRES_VERSION;
    uint32_t world_kind = (uint32_t)world->kind;
    uint32_t width = (uint32_t)world->size.width;
    uint32_t height = (uint32_t)world->size.height;
    double probs[4] = {ctx->probs.p_up, ctx->probs.p_down, ctx->probs.p_left, ctx->probs.p_right};
    uint32_t k_max_steps = ctx->k_max_steps;
    uint32_t total_reps = ctx->total_reps;

    uint32_t cell_count = (uint32_t)(width * height);

    int ok = 0;
    ok |= write_exact(f, magic, sizeof(magic));
    ok |= write_exact(f, &version, sizeof(version));
    ok |= write_exact(f, &world_kind, sizeof(world_kind));
    ok |= write_exact(f, &width, sizeof(width));
    ok |= write_exact(f, &height, sizeof(height));
    ok |= write_exact(f, probs, sizeof(probs));
    ok |= write_exact(f, &k_max_steps, sizeof(k_max_steps));
    ok |= write_exact(f, &total_reps, sizeof(total_reps));

    ok |= write_exact(f, world->obstacles, (size_t)cell_count * sizeof(uint8_t));
    ok |= write_exact(f, results_trials(results), (size_t)cell_count * sizeof(uint32_t));
    ok |= write_exact(f, results_sum_steps(results), (size_t)cell_count * sizeof(uint64_t));
    ok |= write_exact(f, results_success_leq_k(results), (size_t)cell_count * sizeof(uint32_t));

    if (fclose(f) != 0) {
        ok = -1;
    }

    if (ok != 0) {
        log_error("persist_save_results: write failed for '%s'", path);
        return -1;
    }
    return 0;
}

int persist_load_results(const char *path,
                         server_context_t *ctx,
                         world_t *world,
                         results_t *results) {
    if (!path || !ctx || !world || !results) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("persist_load_results: fopen('%s') failed: %s", path, strerror(errno));
        return -1;
    }

    char magic[RWRES_MAGIC_LEN];
    uint32_t version = 0;
    uint32_t world_kind = 0;
    uint32_t width = 0, height = 0;
    double probs[4];
    uint32_t k_max_steps = 0;
    uint32_t total_reps = 0;

    int ok = 0;
    ok |= read_exact(f, magic, sizeof(magic));
    ok |= read_exact(f, &version, sizeof(version));
    ok |= read_exact(f, &world_kind, sizeof(world_kind));
    ok |= read_exact(f, &width, sizeof(width));
    ok |= read_exact(f, &height, sizeof(height));
    ok |= read_exact(f, probs, sizeof(probs));
    ok |= read_exact(f, &k_max_steps, sizeof(k_max_steps));
    ok |= read_exact(f, &total_reps, sizeof(total_reps));

    if (ok != 0 || memcmp(magic, RWRES_MAGIC, RWRES_MAGIC_LEN) != 0 || version != RWRES_VERSION) {
        fclose(f);
        log_error("persist_load_results: invalid header in '%s'", path);
        return -1;
    }

    /* Re-init world/results to match file size. */
    world_kind_t wk = (world_kind == (uint32_t)WORLD_OBSTACLES) ? WORLD_OBSTACLES : WORLD_WRAP;
    world_destroy(world);
    if (world_init(world, wk, (world_size_t){(int32_t)width, (int32_t)height}) != 0) {
        fclose(f);
        return -1;
    }

    results_destroy(results);
    if (results_init(results, (world_size_t){(int32_t)width, (int32_t)height}) != 0) {
        fclose(f);
        return -1;
    }

    uint32_t cell_count = (uint32_t)(width * height);

    ok = 0;
    ok |= read_exact(f, world->obstacles, (size_t)cell_count * sizeof(uint8_t));

    /* results arrays are internal; load into them directly */
    ok |= read_exact(f, (void *)results->trials, (size_t)cell_count * sizeof(uint32_t));
    ok |= read_exact(f, (void *)results->sum_steps, (size_t)cell_count * sizeof(uint64_t));
    ok |= read_exact(f, (void *)results->success_leq_k, (size_t)cell_count * sizeof(uint32_t));

    fclose(f);

    if (ok != 0) {
        log_error("persist_load_results: read failed for '%s'", path);
        return -1;
    }

    /* Update ctx from file (base config). */
    ctx->world_kind = wk;
    ctx->world_size.width = (int32_t)width;
    ctx->world_size.height = (int32_t)height;
    ctx->probs.p_up = probs[0];
    ctx->probs.p_down = probs[1];
    ctx->probs.p_left = probs[2];
    ctx->probs.p_right = probs[3];
    ctx->k_max_steps = k_max_steps;
    ctx->total_reps = total_reps;

    return 0;
}

int persist_load_world(const char *path,
                       world_t *world,
                       server_context_t *ctx_optional) {
    if (!path || !world) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("persist_load_world: fopen('%s') failed: %s", path, strerror(errno));
        return -1;
    }

    char magic[RWRES_MAGIC_LEN];
    uint32_t version = 0;
    uint32_t world_kind = 0;
    uint32_t width = 0, height = 0;
    double probs[4];
    uint32_t k_max_steps = 0;
    uint32_t total_reps = 0;

    int ok = 0;
    ok |= read_exact(f, magic, sizeof(magic));
    ok |= read_exact(f, &version, sizeof(version));
    ok |= read_exact(f, &world_kind, sizeof(world_kind));
    ok |= read_exact(f, &width, sizeof(width));
    ok |= read_exact(f, &height, sizeof(height));
    ok |= read_exact(f, probs, sizeof(probs));
    ok |= read_exact(f, &k_max_steps, sizeof(k_max_steps));
    ok |= read_exact(f, &total_reps, sizeof(total_reps));

    if (ok != 0 || memcmp(magic, RWRES_MAGIC, RWRES_MAGIC_LEN) != 0 || version != RWRES_VERSION) {
        fclose(f);
        log_error("persist_load_world: invalid header in '%s'", path);
        return -1;
    }

    world_kind_t wk = (world_kind == (uint32_t)WORLD_OBSTACLES) ? WORLD_OBSTACLES : WORLD_WRAP;

    world_destroy(world);
    if (world_init(world, wk, (world_size_t){(int32_t)width, (int32_t)height}) != 0) {
        fclose(f);
        return -1;
    }

    uint32_t cell_count = (uint32_t)(width * height);
    if (read_exact(f, world->obstacles, (size_t)cell_count * sizeof(uint8_t)) != 0) {
        fclose(f);
        return -1;
    }

    fclose(f);

    if (ctx_optional) {
        ctx_optional->world_kind = wk;
        ctx_optional->world_size.width = (int32_t)width;
        ctx_optional->world_size.height = (int32_t)height;
        ctx_optional->probs.p_up = probs[0];
        ctx_optional->probs.p_down = probs[1];
        ctx_optional->probs.p_left = probs[2];
        ctx_optional->probs.p_right = probs[3];
        ctx_optional->k_max_steps = k_max_steps;
        ctx_optional->total_reps = total_reps;
    }

    return 0;
}

