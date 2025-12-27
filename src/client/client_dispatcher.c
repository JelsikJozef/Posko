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
 * v1 design: single global dispatcher instance.
 *
 * Notes:
 * - This thread is the ONLY reader of the socket FD.
 * - Async notifications (PROGRESS/END/MODE_CHANGED) are consumed silently so they
 *   don't destroy the interactive menu prompt.
 * - Snapshot stream is forwarded to snapshot_reciever.* and rendered on END.
 */

typedef struct {
    int fd;
    pthread_t thread;

    pthread_mutex_t mtx;
    pthread_cond_t cv;

    int running;
    int stop;

    /* Serialize one in-flight sync op. */
    int waiting;

    rw_msg_type_t expected[3];
    size_t expected_count;

    int resp_ready;
    rw_msg_hdr_t resp_hdr;
    void *resp_payload;

    int last_err; /* 0 ok, else errno-like */
} dispatcher_t;

static dispatcher_t g_d = {0};

static int type_expected(rw_msg_type_t t) {
    for (size_t i = 0; i < g_d.expected_count; i++) {
        if (g_d.expected[i] == t) return 1;
    }
    return 0;
}

static void clear_response_slot_locked(void) {
    g_d.resp_ready = 0;
    memset(&g_d.resp_hdr, 0, sizeof(g_d.resp_hdr));
    free(g_d.resp_payload);
    g_d.resp_payload = NULL;
}

static void set_error_locked(int err) {
    g_d.last_err = err;
}

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

static void timespec_add_ms(struct timespec *ts, uint32_t ms) {
    ts->tv_sec += (time_t)(ms / 1000u);
    ts->tv_nsec += (long)((ms % 1000u) * 1000000ul);
    if (ts->tv_nsec >= 1000000000l) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000l;
    }
}

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

