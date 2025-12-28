//
// Created by Jozef Jelšík on 27/12/2025.
//

#include "client_dispatcher.h"

#include "../common/util.h"
#include "snapshot_reciever.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Global dispatcher state (v1: a single instance).
 *
 * Invariants:
 * - `fd` is the connected socket for the lifetime of the dispatcher.
 * - `thread` is the single reader thread.
 * - `mtx` + `cv` protect all fields below.
 * - v1 supports only one waiting synchronous request at a time.
 */
typedef struct {
    int fd;

    pthread_t thread;
    pthread_mutex_t mtx;
    pthread_cond_t cv;

    int running;     /**< 1 while reader thread is alive. */
    int stop;        /**< Request reader thread to stop. */

    int waiting;     /**< 1 while a caller is waiting in dispatcher_send_and_wait(). */

    rw_msg_type_t expected[3];
    size_t expected_count;

    int resp_ready;        /**< 1 when response slot contains a response. */
    rw_msg_hdr_t resp_hdr; /**< Header for buffered response. */
    void *resp_payload;    /**< malloc()'d payload for buffered response. */

    int last_err; /**< errno-like error code set by reader thread. */
} dispatcher_state_t;

static dispatcher_state_t g_d;

/**
 * @file client_dispatcher.c
 * @brief Single-reader socket dispatcher for the interactive client.
 *
 * ## Problem this solves
 * A Unix stream socket is a sequential byte stream. If multiple threads call
 * `rw_recv_hdr()` / `rw_recv_payload()` concurrently on the same FD, they will
 * race and corrupt message framing.
 *
 * This module enforces a **single-reader** model:
 * - exactly one background thread (`reader_main`) performs all blocking reads
 * - other code may still send requests, but synchronous request/response is done
 *   via `dispatcher_send_and_wait()`
 *
 * ## What the reader thread does
 * The reader thread reads (hdr + payload) and then routes the message:
 * - Async notifications are consumed and dropped:
 *   - `RW_MSG_PROGRESS`, `RW_MSG_END`, `RW_MSG_GLOBAL_MODE_CHANGED`
 *   (The interactive menu must not be spammed or the prompt would get corrupted.)
 * - Snapshot stream is forwarded to `snapshot_reciever.*`:
 *   - `RW_MSG_SNAPSHOT_BEGIN` -> `client_snapshot_begin()`
 *   - `RW_MSG_SNAPSHOT_CHUNK` -> `client_snapshot_chunk()`
 *   - `RW_MSG_SNAPSHOT_END`   -> `client_snapshot_end()`
 * - Sync responses for a waiting caller are delivered into a single "response slot"
 *   if the type matches the caller-provided `expected[]` list.
 * - Everything else is treated as unexpected/unhandled and is dropped.
 *
 * ## Synchronization model (v1)
 * v1 intentionally supports **only one in-flight synchronous request**.
 * `dispatcher_send_and_wait()` serializes callers with a mutex/cond-var.
 *
 * While a caller is "waiting":
 * - `expected[]` describes which response types are acceptable
 * - the reader thread copies the first acceptable response into `resp_*`
 * - the waiting caller wakes up, takes ownership of the allocated payload, and returns
 *
 * ## Memory ownership
 * - The reader thread malloc()'s `payload` for any message with payload_len > 0.
 * - For async/unhandled messages, *the reader thread frees payload*.
 * - For a sync response delivered to a waiter, ownership transfers:
 *   - reader thread stores the pointer in `g_d.resp_payload`
 *   - `dispatcher_send_and_wait()` returns it via `out_payload`
 *   - caller must free() it
 *
 * ## Error handling
 * On socket read failure the reader thread:
 * - sets `last_err` (errno-like)
 * - sets `stop = 1`
 * - signals all waiters via condition variable
 *
 * Public APIs typically return -1 on error/timeout.
 */

/**
 * @brief Return non-zero if message type @p t is in the current expected list.
 *
 * Must only be called when `g_d.expected_count` has been set for an active
 * request (i.e., while a caller is waiting).
 */
static int type_expected(rw_msg_type_t t) {
    for (size_t i = 0; i < g_d.expected_count; i++) {
        if (g_d.expected[i] == t) return 1;
    }
    return 0;
}

/**
 * @brief Reset the shared response slot and free any stored payload.
 *
 * Precondition: caller holds `g_d.mtx`.
 */
static void clear_response_slot_locked(void) {
    g_d.resp_ready = 0;
    memset(&g_d.resp_hdr, 0, sizeof(g_d.resp_hdr));
    free(g_d.resp_payload);
    g_d.resp_payload = NULL;
}

/**
 * @brief Store an error code in the dispatcher state.
 *
 * Precondition: caller holds `g_d.mtx`.
 */
static void set_error_locked(int err) {
    g_d.last_err = err;
}

/**
 * @brief Reader thread main loop.
 *
 * Responsibilities:
 * - continuously reads messages from `g_d.fd`
 * - forwards snapshot stream into snapshot receiver
 * - silently consumes async notifications
 * - delivers an expected sync response to the waiting caller (if any)
 */
static void *reader_main(void *arg) {
    (void)arg;

    while (1) {
        /* Check stop flag (without holding lock too long). */
        pthread_mutex_lock(&g_d.mtx);
        int should_stop = g_d.stop;
        pthread_mutex_unlock(&g_d.mtx);
        if (should_stop) break;

        rw_msg_hdr_t hdr;
        if (rw_recv_hdr(g_d.fd, &hdr) != 0) {
            pthread_mutex_lock(&g_d.mtx);
            set_error_locked(EPIPE);
            g_d.stop = 1;
            pthread_cond_broadcast(&g_d.cv);
            pthread_mutex_unlock(&g_d.mtx);
            break;
        }

        void *payload = NULL;
        if (hdr.payload_len > 0) {
            payload = malloc(hdr.payload_len);
            if (!payload) {
                /* Drain payload to keep framing, then signal error. */
                char buf[256];
                uint32_t left = hdr.payload_len;
                while (left > 0) {
                    uint32_t n = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;
                    if (rw_recv_payload(g_d.fd, buf, n) != 0) break;
                    left -= n;
                }

                pthread_mutex_lock(&g_d.mtx);
                set_error_locked(ENOMEM);
                pthread_cond_broadcast(&g_d.cv);
                pthread_mutex_unlock(&g_d.mtx);
                continue;
            }
            if (rw_recv_payload(g_d.fd, payload, hdr.payload_len) != 0) {
                free(payload);
                pthread_mutex_lock(&g_d.mtx);
                set_error_locked(EPIPE);
                g_d.stop = 1;
                pthread_cond_broadcast(&g_d.cv);
                pthread_mutex_unlock(&g_d.mtx);
                break;
            }
        }

        /* Dispatch */
        if (hdr.type == RW_MSG_PROGRESS && hdr.payload_len == sizeof(rw_progress_t)) {
            /* Don't print progress on client (keeps menu stable). */
            free(payload);
            continue;
        }

        if (hdr.type == RW_MSG_END && hdr.payload_len == sizeof(rw_end_t)) {
            /* Don't print end on client (keeps menu stable). */
            free(payload);
            continue;
        }

        if (hdr.type == RW_MSG_GLOBAL_MODE_CHANGED && hdr.payload_len == sizeof(rw_global_mode_changed_t)) {
            /* Don't print mode changes on client (keeps menu stable). */
            free(payload);
            continue;
        }

        if (hdr.type == RW_MSG_SNAPSHOT_BEGIN && hdr.payload_len == sizeof(rw_snapshot_begin_t)) {
            const rw_snapshot_begin_t *b = (const rw_snapshot_begin_t *)payload;
            if (client_snapshot_begin(b) != 0) {
                log_error("client_snapshot_begin() failed");
            }
            free(payload);
            continue;
        }

        if (hdr.type == RW_MSG_SNAPSHOT_CHUNK && hdr.payload_len >= (uint32_t)(sizeof(rw_snapshot_chunk_t) - RW_SNAPSHOT_CHUNK_MAX)
            && hdr.payload_len <= (uint32_t)sizeof(rw_snapshot_chunk_t)) {
            rw_snapshot_chunk_t chunk;
            memset(&chunk, 0, sizeof(chunk));
            memcpy(&chunk, payload, hdr.payload_len);
            if (client_snapshot_chunk(&chunk) != 0) {
                log_error("client_snapshot_chunk() failed");
            }
            free(payload);
            continue;
        }

        if (hdr.type == RW_MSG_SNAPSHOT_END && hdr.payload_len == 0) {
            if (client_snapshot_end() != 0) {
                log_error("client_snapshot_end() failed");
            }
            free(payload);
            continue;
        }

        /* Sync response delivery */
        pthread_mutex_lock(&g_d.mtx);
        if (g_d.waiting && !g_d.resp_ready && type_expected((rw_msg_type_t)hdr.type)) {
            clear_response_slot_locked();
            g_d.resp_hdr = hdr;
            g_d.resp_payload = payload; /* transfer ownership */
            g_d.resp_ready = 1;
            pthread_cond_broadcast(&g_d.cv);
            pthread_mutex_unlock(&g_d.mtx);
            continue;
        }
        pthread_mutex_unlock(&g_d.mtx);

        /* Unexpected/unhandled: just drop (already consumed). */
        free(payload);
    }

    pthread_mutex_lock(&g_d.mtx);
    g_d.running = 0;
    pthread_cond_broadcast(&g_d.cv);
    pthread_mutex_unlock(&g_d.mtx);
    return NULL;
}

/**
 * @brief Add @p ms milliseconds to a timespec.
 *
 * Used to compute absolute timeout for `pthread_cond_timedwait()`.
 */
static void timespec_add_ms(struct timespec *ts, uint32_t ms) {
    ts->tv_sec += (time_t)(ms / 1000u);
    ts->tv_nsec += (long)((ms % 1000u) * 1000000ul);
    if (ts->tv_nsec >= 1000000000l) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000l;
    }
}

/**
 * @brief Start the dispatcher reader thread for a connected socket.
 *
 * Behaviour:
 * - v1 supports exactly one global dispatcher instance (`g_d`).
 * - If the dispatcher is already running, this is a no-op (returns 0).
 *
 * @param fd Connected client socket FD.
 * @return 0 on success, -1 on failure.
 */
int dispatcher_start(int fd) {
    if (fd < 0) return -1;

    if (g_d.running) {
        return 0;
    }

    memset(&g_d, 0, sizeof(g_d));
    g_d.fd = fd;
    g_d.running = 1;
    g_d.stop = 0;
    g_d.waiting = 0;
    g_d.resp_ready = 0;
    g_d.resp_payload = NULL;
    g_d.last_err = 0;

    if (pthread_mutex_init(&g_d.mtx, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&g_d.cv, NULL) != 0) {
        pthread_mutex_destroy(&g_d.mtx);
        return -1;
    }

    if (pthread_create(&g_d.thread, NULL, reader_main, NULL) != 0) {
        pthread_cond_destroy(&g_d.cv);
        pthread_mutex_destroy(&g_d.mtx);
        return -1;
    }

    return 0;
}

/**
 * @brief Stop the reader thread and release resources.
 *
 * Safe to call multiple times.
 *
 * Notes:
 * - Signals the reader thread to exit and joins it.
 * - Frees any pending response payload.
 * - Destroys mutex/cond-var and clears global state.
 */
void dispatcher_stop(void) {
    if (!g_d.running) {
        return;
    }

    pthread_mutex_lock(&g_d.mtx);
    g_d.stop = 1;
    pthread_cond_broadcast(&g_d.cv);
    pthread_mutex_unlock(&g_d.mtx);

    pthread_join(g_d.thread, NULL);

    pthread_mutex_lock(&g_d.mtx);
    clear_response_slot_locked();
    g_d.waiting = 0;
    pthread_mutex_unlock(&g_d.mtx);

    pthread_cond_destroy(&g_d.cv);
    pthread_mutex_destroy(&g_d.mtx);

    memset(&g_d, 0, sizeof(g_d));
}

/**
 * @brief Send a request and synchronously wait for one of the expected response types.
 *
 * Contract (v1):
 * - Only one caller may block waiting for a response at a time; callers are serialized.
 * - The socket is still read exclusively by the reader thread.
 * - The response is matched only by *message type* (not by request id).
 *   Therefore, `expected[]` should be tight (typically {ACK, ERROR}).
 *
 * Timeout:
 * - `timeout_ms == 0` means wait forever.
 * - otherwise a timed wait is used.
 *
 * Ownership:
 * - On success, if `out_payload != NULL` and response has payload_len > 0,
 *   `*out_payload` receives a malloc()'d buffer that the caller must free().
 * - If `out_payload == NULL`, any received payload is freed internally.
 *
 * @param fd Connected socket; must match the fd passed into dispatcher_start().
 * @param req_type Request type to send.
 * @param payload Request payload pointer (may be NULL if payload_len==0).
 * @param payload_len Payload size.
 * @param expected List of acceptable response message types.
 * @param expected_count Length of @p expected (must be 1..3).
 * @param timeout_ms Timeout in milliseconds; 0 means no timeout.
 * @param out_hdr Optional output response header.
 * @param out_payload Optional output response payload (malloc()'d, caller frees).
 * @return 0 on success, -1 on timeout or error.
 */
int dispatcher_send_and_wait(
    int fd,
    rw_msg_type_t req_type,
    const void *payload,
    uint32_t payload_len,
    const rw_msg_type_t *expected,
    size_t expected_count,
    uint32_t timeout_ms,
    rw_msg_hdr_t *out_hdr,
    void **out_payload) {

    if (!expected || expected_count == 0 || expected_count > 3) return -1;
    if (fd != g_d.fd) return -1;
    if (!g_d.running) return -1;

    pthread_mutex_lock(&g_d.mtx);

    /* Serialize single in-flight request. */
    while (g_d.waiting) {
        pthread_cond_wait(&g_d.cv, &g_d.mtx);
    }
    g_d.waiting = 1;

    g_d.expected_count = expected_count;
    for (size_t i = 0; i < expected_count; i++) {
        g_d.expected[i] = expected[i];
    }

    clear_response_slot_locked();

    /* Send request while holding the lock to keep strict ordering in v1. */
    if (rw_send_msg(fd, req_type, payload, payload_len) != 0) {
        g_d.waiting = 0;
        pthread_cond_broadcast(&g_d.cv);
        pthread_mutex_unlock(&g_d.mtx);
        return -1;
    }

    int rc = 0;
    while (!g_d.resp_ready) {
        if (g_d.last_err != 0 || !g_d.running) {
            rc = -1;
            break;
        }

        if (timeout_ms == 0) {
            pthread_cond_wait(&g_d.cv, &g_d.mtx);
        } else {
            struct timespec ts;
#if defined(CLOCK_REALTIME)
            clock_gettime(CLOCK_REALTIME, &ts);
#else
            /* fallback */
            ts.tv_sec = time(NULL);
            ts.tv_nsec = 0;
#endif
            timespec_add_ms(&ts, timeout_ms);
            int w = pthread_cond_timedwait(&g_d.cv, &g_d.mtx, &ts);
            if (w == ETIMEDOUT) {
                rc = -1;
                break;
            }
        }
    }

    if (rc == 0 && g_d.resp_ready) {
        if (out_hdr) {
            *out_hdr = g_d.resp_hdr;
        }
        if (out_payload) {
            *out_payload = g_d.resp_payload; /* transfer ownership */
            g_d.resp_payload = NULL;
        } else {
            free(g_d.resp_payload);
            g_d.resp_payload = NULL;
        }
        clear_response_slot_locked();
    } else {
        if (out_payload) {
            *out_payload = NULL;
        }
    }

    g_d.waiting = 0;
    pthread_cond_broadcast(&g_d.cv);
    pthread_mutex_unlock(&g_d.mtx);

    return rc;
}
