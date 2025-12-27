#include "snapshot_reciever.h"
#include "../common/util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

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

static void render_ascii(void) {
    uint32_t w = g_snap.size.width;
    uint32_t h = g_snap.size.height;
    uint32_t cells = g_snap.cell_count;
    if (w == 0 || h == 0 || cells != w * h) {
        log_error("Invalid snapshot dimensions");
        return;
    }

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t idx = y * w + x;
            char ch = '?';

            if (g_snap.obstacles && g_snap.obstacles[idx]) {
                ch = '#';
            } else {
                /* Only render stats if trials available. */
                uint32_t trials = g_snap.trials ? g_snap.trials[idx] : 0;
                if (trials == 0) {
                    ch = '?';
                } else {
                    double value = 0.0;
                    int palette_len = (int)strlen(SNAP_PALETTE);
                    if (g_snap.sum_steps) {
                        value = (double)g_snap.sum_steps[idx] / (double)trials; /* avg steps */
                    } else if (g_snap.succ_leq_k) {
                        value = (double)g_snap.succ_leq_k[idx] / (double)trials; /* probability */
                    }

                    /* Simple clamping and scaling into palette range. */
                    if (value < 0.0) value = 0.0;
                    if (value > 1e6) value = 1e6; /* arbitrary cap to avoid overflow */

                    /* Map value into palette by logarithmic-ish scaling. */
                    double norm = value;
                    if (norm > 1.0) {
                        norm = 1.0 - (1.0 / (norm + 1.0));
                    }
                    int idx_palette = (int)(norm * (palette_len - 1));
                    if (idx_palette < 0) idx_palette = 0;
                    if (idx_palette >= palette_len) idx_palette = palette_len - 1;
                    ch = SNAP_PALETTE[idx_palette];
                }
            }

            if (x == 0 && y == 0) {
                ch = 'O'; /* origin marker */
            }
            putchar(ch);
        }
        putchar('\n');
    }

    printf("Legend: '#'=obstacle, 'O'=origin, '?'=no trials, palette='%s'\n", SNAP_PALETTE);
}

int client_snapshot_end(void) {
    /* Render assembled snapshot. */
    render_ascii();
    return 0;
}

void client_snapshot_free(void) {
    free_snapshot_buffers();
    memset(&g_snap, 0, sizeof(g_snap));
}
