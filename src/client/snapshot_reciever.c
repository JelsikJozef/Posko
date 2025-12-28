#include "snapshot_reciever.h"
#include "../common/util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>

/**
 * @file snapshot_reciever.c
 * @brief Implementation of client-side snapshot reassembly and ASCII rendering.
 *
 * Design notes
 * ------------
 * - Snapshot transfer is chunked: per-field arrays are streamed in pieces.
 * - We keep one global `snapshot_state_t` instance that owns the current snapshot
 *   buffers.
 * - `client_snapshot_begin()` resets state and allocates only the fields that
 *   the server declared as included.
 * - `client_snapshot_chunk()` performs strict bounds checking before copying
 *   received bytes into the target field buffer.
 * - `client_snapshot_end()` renders the assembled view to stdout.
 */

#define SNAP_PALETTE " .:-=+*#%@"

/* Internal snapshot buffers.
 *
 * Buffers are per-field arrays in row-major order (idx = y*width + x).
 * A NULL pointer means the field was not included in the current snapshot.
 */
typedef struct {
    uint32_t snapshot_id;
    rw_wire_size_t size;
    rw_wire_world_kinds_t world_kind;
    uint32_t cell_count;
    uint32_t included_fields;

    uint8_t *obstacles;      /* cell_count */
    uint32_t *trials;        /* cell_count */
    uint64_t *sum_steps;     /* cell_count */
    uint32_t *succ_leq_k;    /* cell_count */
} snapshot_state_t;

static snapshot_state_t g_snap = {0};
static uint32_t g_k_max_steps = 0;

static void free_snapshot_buffers(void) {
    free(g_snap.obstacles);
    free(g_snap.trials);
    free(g_snap.sum_steps);
    free(g_snap.succ_leq_k);
    g_snap.obstacles = NULL;
    g_snap.trials = NULL;
    g_snap.sum_steps = NULL;
    g_snap.succ_leq_k = NULL;
}

static int field_included(uint32_t included_fields, rw_snapshot_field_t field) {
    if (field == 0) return 0;
    uint32_t bit = 1u << (field - 1); /* enum starts at 1 */
    return (included_fields & bit) != 0;
}

int client_snapshot_begin(const rw_snapshot_begin_t *begin) {
    if (!begin) return -1;

    /* Free previous snapshot. */
    free_snapshot_buffers();
    memset(&g_snap, 0, sizeof(g_snap));

    g_snap.snapshot_id = begin->snapshot_id;
    g_snap.size = begin->size;
    g_snap.world_kind = begin->world_kind;
    g_snap.cell_count = begin->cell_count;
    g_snap.included_fields = begin->included_fields;

    /* Allocate per-field buffers if included. */
    if (field_included(begin->included_fields, RW_SNAP_FIELD_OBSTACLES)) {
        g_snap.obstacles = (uint8_t *)calloc(begin->cell_count, sizeof(uint8_t));
        if (!g_snap.obstacles) goto oom;
    }
    if (field_included(begin->included_fields, RW_SNAP_FIELD_TRIALS)) {
        g_snap.trials = (uint32_t *)calloc(begin->cell_count, sizeof(uint32_t));
        if (!g_snap.trials) goto oom;
    }
    if (field_included(begin->included_fields, RW_SNAP_FIELD_SUM_STEPS)) {
        g_snap.sum_steps = (uint64_t *)calloc(begin->cell_count, sizeof(uint64_t));
        if (!g_snap.sum_steps) goto oom;
    }
    if (field_included(begin->included_fields, RW_SNAP_FIELD_SUCC_LEQ_K)) {
        g_snap.succ_leq_k = (uint32_t *)calloc(begin->cell_count, sizeof(uint32_t));
        if (!g_snap.succ_leq_k) goto oom;
    }

    return 0;

oom:
    log_error("Out of memory while allocating snapshot buffers");
    free_snapshot_buffers();
    memset(&g_snap, 0, sizeof(g_snap));
    return -1;
}

/* Copy chunk data into appropriate buffer with bounds checking.
 *
 * Bounds checks are done in bytes because chunks carry byte offsets and lengths.
 */
int client_snapshot_chunk(const rw_snapshot_chunk_t *chunk) {
    if (!chunk) return -1;
    if (chunk->snapshot_id != g_snap.snapshot_id) {
        /* Ignore stale/unknown snapshot IDs. */
        return 0;
    }

    const uint32_t offset = chunk->offset_bytes;
    const uint32_t len = chunk->data_len;

    switch ((rw_snapshot_field_t)chunk->field) {
        case RW_SNAP_FIELD_OBSTACLES: {
            if (!g_snap.obstacles) return -1;
            uint32_t total = g_snap.cell_count * (uint32_t)sizeof(uint8_t);
            if (offset > total || len > total || offset + len > total) return -1;
            memcpy(((uint8_t *)g_snap.obstacles) + offset, chunk->data, len);
            break;
        }
        case RW_SNAP_FIELD_TRIALS: {
            if (!g_snap.trials) return -1;
            uint32_t total = g_snap.cell_count * (uint32_t)sizeof(uint32_t);
            if (offset > total || len > total || offset + len > total) return -1;
            memcpy(((uint8_t *)g_snap.trials) + offset, chunk->data, len);
            break;
        }
        case RW_SNAP_FIELD_SUM_STEPS: {
            if (!g_snap.sum_steps) return -1;
            uint32_t total = g_snap.cell_count * (uint32_t)sizeof(uint64_t);
            if (offset > total || len > total || offset + len > total) return -1;
            memcpy(((uint8_t *)g_snap.sum_steps) + offset, chunk->data, len);
            break;
        }
        case RW_SNAP_FIELD_SUCC_LEQ_K: {
            if (!g_snap.succ_leq_k) return -1;
            uint32_t total = g_snap.cell_count * (uint32_t)sizeof(uint32_t);
            if (offset > total || len > total || offset + len > total) return -1;
            memcpy(((uint8_t *)g_snap.succ_leq_k) + offset, chunk->data, len);
            break;
        }
        default:
            return -1;
    }

    return 0;
}

static int cell_radius(uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, int wrap) {
    if (wrap) {
        uint32_t dx = sx;
        uint32_t dy = sy;
        uint32_t dx_wrap = (w > sx) ? (w - sx) : 0u;
        uint32_t dy_wrap = (h > sy) ? (h - sy) : 0u;
        if (dx_wrap < dx) dx = dx_wrap;
        if (dy_wrap < dy) dy = dy_wrap;
        return (int)(dx + dy);
    }
    return (int)(sx + sy);
}

static void render_radial_summary(void) {
    uint32_t w = g_snap.size.width;
    uint32_t h = g_snap.size.height;
    uint32_t cells_total = g_snap.cell_count;
    if (w == 0 || h == 0 || cells_total != w * h) {
        log_error("Invalid snapshot dimensions");
        return;
    }

    /* Distance is measured from origin (0,0). For WRAP worlds use toroidal
     * Manhattan distance; for obstacle worlds use standard Manhattan.
     */
    const int wrap = (g_snap.world_kind == RW_WIRE_WORLD_WRAP) ? 1 : 0;
    int r_max = wrap ? (int)(w / 2u + h / 2u)
                     : (int)((w ? w - 1u : 0u) + (h ? h - 1u : 0u));
    if (r_max < 0) {
        log_error("Invalid r_max");
        return;
    }
    size_t bins = (size_t)(r_max + 1);

    uint32_t *cells = (uint32_t *)calloc(bins, sizeof(uint32_t));
    uint32_t *n_used = (uint32_t *)calloc(bins, sizeof(uint32_t));
    double *sum_avg_steps = (double *)calloc(bins, sizeof(double));
    double *sum_p = (double *)calloc(bins, sizeof(double));
    uint64_t *sum_steps_r = (uint64_t *)calloc(bins, sizeof(uint64_t));
    uint32_t *succ_count_r = (uint32_t *)calloc(bins, sizeof(uint32_t));
    if (!cells || !n_used || !sum_avg_steps || !sum_p || !sum_steps_r || !succ_count_r) {
        log_error("Out of memory in radial summary");
        free(cells); free(n_used); free(sum_avg_steps); free(sum_p);
        free(sum_steps_r); free(succ_count_r);
        return;
    }

    uint32_t non_obstacle_cells = 0;
    uint32_t used_cells = 0;
    double global_max_avg = 0.0;
    int obstacles_present = 0;

    /* First pass: aggregate by radius. */
    for (uint32_t sy = 0; sy < h; ++sy) {
        for (uint32_t sx = 0; sx < w; ++sx) {
            uint32_t idx = sy * w + sx;
            int r = cell_radius(sx, sy, w, h, wrap);
            if (r < 0 || r > r_max) continue;

            int obstacle = g_snap.obstacles && g_snap.obstacles[idx];
            if (obstacle) {
                obstacles_present = 1;
            } else {
                cells[r]++;
                non_obstacle_cells++;
            }

            if (obstacle) continue;

            uint32_t trials = g_snap.trials ? g_snap.trials[idx] : 0u;
            uint32_t succ = g_snap.succ_leq_k ? g_snap.succ_leq_k[idx] : 0u;
            uint64_t sum_steps_cell = g_snap.sum_steps ? g_snap.sum_steps[idx] : 0u;
            if (trials == 0) continue;
            n_used[r]++;
            used_cells++;

            sum_steps_r[r] += sum_steps_cell;
            succ_count_r[r] += succ;

            if (g_snap.sum_steps && succ > 0) {
                double avg_i = (double)sum_steps_cell / (double)succ;
                if (avg_i > global_max_avg) global_max_avg = avg_i;
            }
            if (g_snap.succ_leq_k) {
                double p_i = (double)succ / (double)trials;
                sum_p[r] += p_i;
            }
        }
    }

    /* Compute per-ring averages. */
    double *avg_r = (double *)calloc(bins, sizeof(double));
    double *p_r = (double *)calloc(bins, sizeof(double));
    if (!avg_r || !p_r) {
        log_error("Out of memory in radial summary (avg arrays)");
        free(cells); free(n_used); free(sum_avg_steps); free(sum_p);
        free(sum_steps_r); free(succ_count_r);
        free(avg_r); free(p_r);
        return;
    }
    for (int r = 0; r <= r_max; ++r) {
        if (succ_count_r[r] > 0 && g_snap.sum_steps) {
            avg_r[r] = (double)sum_steps_r[r] / (double)succ_count_r[r];
        } else {
            avg_r[r] = NAN;
        }
        if (n_used[r] > 0 && g_snap.succ_leq_k) {
            p_r[r] = sum_p[r] / (double)n_used[r];
        } else {
            p_r[r] = NAN;
        }
    }

    /* Second pass: obstacle-induced local increases. */
    double max_increase = -INFINITY;
    int have_increase = 0;
    if (g_snap.sum_steps) {
        for (uint32_t sy = 0; sy < h; ++sy) {
            for (uint32_t sx = 0; sx < w; ++sx) {
                uint32_t idx = sy * w + sx;
                int r = cell_radius(sx, sy, w, h, wrap);
                if (r < 0 || r > r_max) continue;
                if (g_snap.obstacles && g_snap.obstacles[idx]) continue;

                uint32_t trials = g_snap.trials ? g_snap.trials[idx] : 0u;
                if (trials == 0) continue;

                double baseline = avg_r[r];
                if (isnan(baseline) || baseline == 0.0) continue;

                double avg_i = (double)g_snap.sum_steps[idx] / (double)trials;
                double inc = (avg_i - baseline) / baseline;
                if (inc > max_increase) {
                    max_increase = inc;
                    have_increase = 1;
                }
            }
        }
    }

    printf("RADIAL SUMMARY (K = %u)\n\n", (unsigned)g_k_max_steps);
    printf("r  cells  avg_steps  p(success<=K)\n");
    printf("----------------------------------\n");
    for (int r = 0; r <= r_max; ++r) {
        if (cells[r] == 0) continue;
        double avg = (n_used[r] > 0) ? avg_r[r] : NAN;
        double prob = (n_used[r] > 0) ? p_r[r] : NAN;

        printf("%-2d %5u ", r, cells[r]);
        if (isnan(avg)) {
            printf("%10s ", "0.0");
        } else {
            printf("%10.1f ", avg);
        }
        if (isnan(prob)) {
            printf("%13s", "0.0");
        } else {
            printf("%13.3f", prob);
        }
        printf("\n");
    }
    printf("\n");

    /* Heuristic summary bullets. */
    char summaries[6][128];
    int summary_count = 0;

    /* 3.1 Up to r=R, reaching origin almost certain. */
    int max_high_p_r = -1;
    for (int r = 0; r <= r_max; ++r) {
        if (!isnan(p_r[r]) && p_r[r] >= 0.95) {
            max_high_p_r = r;
        }
    }
    if (max_high_p_r >= 0 && summary_count < 6) {
        snprintf(summaries[summary_count++], sizeof(summaries[0]),
                 "Up to r=%d, reaching the origin is almost certain (>=95%%).",
                 max_high_p_r);
    }

    /* 3.2 Probability drops rapidly between r=a and r=b. */
    double max_drop = -INFINITY;
    int drop_at_r = -1;
    for (int r = 1; r <= r_max; ++r) {
        if (isnan(p_r[r]) || isnan(p_r[r - 1])) continue;
        double drop = p_r[r - 1] - p_r[r];
        if (drop > max_drop) {
            max_drop = drop;
            drop_at_r = r;
        }
    }
    if (drop_at_r >= 1 && max_drop >= 0.15 && summary_count < 6) {
        snprintf(summaries[summary_count++], sizeof(summaries[0]),
                 "Between r=%d and r=%d, probability drops rapidly.",
                 drop_at_r - 1, drop_at_r);
    }

    /* 3.3 For r>=X, success unlikely. */
    int first_low = -1;
    for (int r = 0; r <= r_max; ++r) {
        if (!isnan(p_r[r]) && p_r[r] < 0.30) {
            first_low = r;
            break;
        }
    }
    if (first_low >= 0 && summary_count < 6) {
        snprintf(summaries[summary_count++], sizeof(summaries[0]),
                 "For r>=%d, success within K steps is unlikely (<30%%).",
                 first_low);
    }

    /* 3.4 Obstacles cause local increases. */
    if (have_increase && max_increase >= 0.10 && obstacles_present && summary_count < 6) {
        int pct = (int)lround(max_increase * 100.0);
        snprintf(summaries[summary_count++], sizeof(summaries[0]),
                 "Obstacles cause local increases of avg steps by up to %d%%.", pct);
    }

    /* Coverage/fallback bullets to ensure at least 3 lines. */
    if (summary_count < 6) {
        double coverage = (non_obstacle_cells == 0) ? 0.0 :
                          (100.0 * (double)used_cells / (double)non_obstacle_cells);
        snprintf(summaries[summary_count++], sizeof(summaries[0]),
                 "Data coverage: trials on %u/%u cells (%.1f%%).",
                 used_cells, non_obstacle_cells, coverage);
    }
    if (summary_count < 3) {
        snprintf(summaries[summary_count++], sizeof(summaries[0]),
                 "Max observed avg steps (where data exists): %.1f.",
                 global_max_avg);
    }
    if (summary_count < 3) {
        snprintf(summaries[summary_count++], sizeof(summaries[0]),
                 "No additional strong patterns detected yet.");
    }

    printf("SUMMARY:\n");
    for (int i = 0; i < summary_count && i < 6; ++i) {
        printf("- %s\n", summaries[i]);
    }
    printf("\n");

    free(cells);
    free(n_used);
    free(sum_avg_steps);
    free(sum_p);
    free(sum_steps_r);
    free(succ_count_r);
    free(avg_r);
    free(p_r);
}

static void print_legend(void) {
    printf("Legend (grid preview):\n");
    printf("  ' ' : no trials for cell\n");
    printf("  '..@': increasing probability of success within K ('.' low -> '@' high)\n");
    printf("  '##': obstacle cell\n");
    printf("\n");
}

static void render_cell_grid_preview(void) {
    const uint32_t w = g_snap.size.width;
    const uint32_t h = g_snap.size.height;
    if (w == 0 || h == 0 || g_snap.cell_count != w * h) {
        log_error("Invalid snapshot dimensions for grid preview");
        return;
    }

    const uint32_t max_rows = 12u; /* keep output compact */
    const uint32_t max_cols = 24u;
    uint32_t rows = h < max_rows ? h : max_rows;
    uint32_t cols = w < max_cols ? w : max_cols;

    printf("GRID PREVIEW (top-left %ux%u of %ux%u)\n", cols, rows, w, h);
    printf("y/x");
    for (uint32_t x = 0; x < cols; ++x) {
        printf(" %2u", x);
    }
    printf("\n");

    for (uint32_t y = 0; y < rows; ++y) {
        printf("%3u", y);
        for (uint32_t x = 0; x < cols; ++x) {
            uint32_t idx = y * w + x;
            int obstacle = g_snap.obstacles ? g_snap.obstacles[idx] : 0;
            if (obstacle) {
                printf(" ##");
                continue;
            }
            uint32_t trials = g_snap.trials ? g_snap.trials[idx] : 0u;
            uint32_t succ = g_snap.succ_leq_k ? g_snap.succ_leq_k[idx] : 0u;
            char c = '.';
            if (trials == 0) {
                c = ' ';
            } else {
                double p = (succ == 0) ? 0.0 : (double)succ / (double)trials;
                size_t palette_idx = (size_t)lrint(p * (double)(strlen(SNAP_PALETTE) - 1));
                if (palette_idx >= strlen(SNAP_PALETTE)) palette_idx = strlen(SNAP_PALETTE) - 1;
                c = SNAP_PALETTE[palette_idx];
            }
            printf("  %c", c);
        }
        printf("\n");
    }
    printf("\n");
}

int client_snapshot_end(void) {
    /* Render assembled snapshot. */
    render_radial_summary();
    print_legend();
    render_cell_grid_preview();
    return 0;
}

int client_snapshot_render_last(void) {
    if (g_snap.cell_count == 0 || g_snap.size.width == 0 || g_snap.size.height == 0) {
        log_error("No snapshot available");
        return -1;
    }
    render_radial_summary();
    print_legend();
    render_cell_grid_preview();
    return 0;
}

int client_snapshot_dump_cell(uint32_t x, uint32_t y) {
    const uint32_t w = g_snap.size.width;
    const uint32_t h = g_snap.size.height;
    if (w == 0 || h == 0 || g_snap.cell_count != w * h) {
        log_error("No snapshot available");
        return -1;
    }
    if (x >= w || y >= h) {
        log_error("Cell out of bounds (x=%u y=%u)", (unsigned)x, (unsigned)y);
        return -1;
    }

    uint32_t idx = y * w + x;
    int obstacle = g_snap.obstacles ? g_snap.obstacles[idx] : 0;
    uint32_t trials = g_snap.trials ? g_snap.trials[idx] : 0u;
    uint32_t succ = g_snap.succ_leq_k ? g_snap.succ_leq_k[idx] : 0u;
    uint64_t sum_steps = g_snap.sum_steps ? g_snap.sum_steps[idx] : 0u;

    printf("SNAPSHOT CELL (%u,%u)\n", (unsigned)x, (unsigned)y);
    printf("  obstacle: %s\n", obstacle ? "yes" : "no");
    printf("  trials  : %u\n", trials);
    printf("  succ<=K : %u\n", succ);
    printf("  avg_steps_if_succ: ");
    if (succ == 0) {
        printf("n/a\n");
    } else {
        double avg = (double)sum_steps / (double)succ;
        printf("%.3f\n", avg);
    }
    if (trials > 0) {
        double p = (double)succ / (double)trials;
        printf("  p<=K   : %.6f\n", p);
    } else {
        printf("  p<=K   : n/a (no trials)\n");
    }
    printf("\n");
    return 0;
}

void client_snapshot_set_k_max(uint32_t k_max_steps) {
    g_k_max_steps = k_max_steps;
}

void client_snapshot_free(void) {
    free_snapshot_buffers();
    memset(&g_snap, 0, sizeof(g_snap));
}
