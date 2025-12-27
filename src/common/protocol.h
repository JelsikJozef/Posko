//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_PROTOCOL_H
#define SEMPRACA_PROTOCOL_H

/**
 * @file protocol.h
 * @brief IPC wire protocol definitions for client <-> server communication.
 *
 * Transport: Unix domain sockets (`AF_UNIX`, `SOCK_STREAM`).
 *
 * Message format:
 * - Each message consists of a fixed-size header (`rw_msg_hdr_t`) followed by an
 *   optional payload.
 * - `rw_msg_hdr_t.payload_len` specifies the payload size in bytes.
 * - The helpers `rw_send_msg()`, `rw_recv_hdr()`, and `rw_recv_payload()` implement
 *   simple blocking I/O that reads/writes exactly the requested number of bytes.
 *
 * Snapshot streaming:
 * - Large snapshot datasets are sent as a sequence of messages:
 *   `RW_MSG_SNAPSHOT_BEGIN`, one or more `RW_MSG_SNAPSHOT_CHUNK`, and
 *   `RW_MSG_SNAPSHOT_END`.
 * - Each chunk carries a slice of one field (obstacles, trials, sum_steps, ...).
 */

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Maximum payload bytes inside one snapshot chunk message.
 */
#define RW_SNAPSHOT_CHUNK_MAX 4096u

/**
 * @brief Maximum length (including NUL) for file path strings sent over the wire.
 *
 * Kept small and fixed-size to avoid dynamic allocations and simplify parsing.
 */
#define RW_PATH_MAX 256u

/**
 * @brief Supported message types in the IPC protocol.
 */
typedef enum {
    RW_MSG_JOIN = 1,        /**< Client -> Server: join request. */
    RW_MSG_WELCOME = 2,     /**< Server -> Client: welcome response. */

    RW_MSG_SET_GLOBAL_MODE = 3,      /**< Client -> Server: request new global mode. */
    RW_MSG_GLOBAL_MODE_CHANGED = 4,  /**< Server -> Clients: global mode changed notification. */

    RW_MSG_PROGRESS = 5,    /**< Server -> Clients: progress update. */

    RW_MSG_SNAPSHOT_BEGIN = 6, /**< Server -> Client: begin snapshot transfer. */
    RW_MSG_SNAPSHOT_CHUNK = 7, /**< Server -> Client: snapshot chunk. */
    RW_MSG_SNAPSHOT_END = 8,   /**< Server -> Client: end snapshot transfer. */

    RW_MSG_STOP_SIM = 9,  /**< Client -> Server: stop simulation request. */
    RW_MSG_END = 10,      /**< Server -> Clients: simulation ended notification. */

    /* ===== Control-plane for interactive menu ===== */
    RW_MSG_QUERY_STATUS = 11,     /**< Client -> Server: request current status/state. */
    RW_MSG_STATUS = 12,           /**< Server -> Client: status response. */

    RW_MSG_CREATE_SIM = 13,       /**< Client -> Server: create a new simulation (in lobby). */
    RW_MSG_LOAD_WORLD = 14,       /**< Client -> Server: load world from file (in lobby). */
    RW_MSG_START_SIM = 15,        /**< Client -> Server: start simulation (from lobby). */

    RW_MSG_REQUEST_SNAPSHOT = 16, /**< Client -> Server: request a snapshot stream. */

    RW_MSG_RESTART_SIM = 17,      /**< Client -> Server: restart using existing world/config + new reps. */
    RW_MSG_LOAD_RESULTS = 18,     /**< Client -> Server: load results from file. */
    RW_MSG_SAVE_RESULTS = 19,     /**< Client -> Server: save results to file. */

    RW_MSG_QUIT = 20,             /**< Client -> Server: graceful disconnect / optional stop. */

    RW_MSG_ACK = 21,              /**< Server -> Client: generic ACK for a request. */

    RW_MSG_ERROR = 255    /**< Server -> Client: error message. */
} rw_msg_type_t;

/**
 * @brief Wire representation of the global simulation mode.
 */
typedef enum {
    RW_WIRE_MODE_INTERACTIVE = 1,
    RW_WIRE_MODE_SUMMARY = 2,
} rw_wire_global_mode_t;

/**
 * @brief Wire representation of supported world kinds.
 */
typedef enum {
    RW_WIRE_WORLD_WRAP = 1,      /**< World wraps around edges. */
    RW_WIRE_WORLD_OBSTACLES = 2, /**< World contains obstacles. */
} rw_wire_world_kinds_t;

/**
 * @brief Wire position.
 */
typedef struct {
    int32_t x;
    int32_t y;
} rw_wire_pos_t;

/**
 * @brief Wire world size.
 */
typedef struct {
    uint32_t width;
    uint32_t height;
} rw_wire_size_t;

/**
 * @brief Wire movement probabilities.
 */
typedef struct {
    double p_up;
    double p_down;
    double p_left;
    double p_right;
} rw_wire_move_probs_t;

/**
 * @brief Common message header sent before every payload.
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t type;        /**< Message type (`rw_msg_type_t`). */
    uint16_t reserved;    /**< Reserved for future use; must be 0. */
    uint32_t payload_len; /**< Payload byte length following this header. */
} rw_msg_hdr_t;
#pragma pack(pop)

/**
 * @brief Payload of a JOIN message.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t pid; /**< Client process id. */
} rw_join_t;
#pragma pack(pop)

/**
 * @brief Payload of a WELCOME message.
 */
#pragma pack(push, 1)
typedef struct {
    rw_wire_world_kinds_t world_kind;
    rw_wire_size_t size;

    rw_wire_move_probs_t probs;
    uint32_t k_max_steps;
    uint32_t total_reps;
    uint32_t current_rep;

    rw_wire_global_mode_t global_mode;
    rw_wire_pos_t origin;
} rw_welcome_t;
#pragma pack(pop)

/**
 * @brief Payload of a SET_GLOBAL_MODE request.
 */
#pragma pack(push, 1)
typedef struct {
    rw_wire_global_mode_t new_mode;
} rw_set_global_mode_t;

/**
 * @brief Payload of a GLOBAL_MODE_CHANGED broadcast message.
 */
typedef struct {
    rw_wire_global_mode_t new_mode;
    uint32_t changed_by_pid;
} rw_global_mode_changed_t;
#pragma pack(pop)

/**
 * @brief Payload of a PROGRESS broadcast message.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t current_rep;
    uint32_t total_reps;
} rw_progress_t;
#pragma pack(pop)

/**
 * @brief Snapshot field identifiers for chunked snapshot transfer.
 *
 * The client interprets per-cell arrays; index = y * width + x.
 */
typedef enum {
    RW_SNAP_FIELD_OBSTACLES = 1,   /**< uint8_t[]: 1 if obstacle, 0 otherwise. */
    RW_SNAP_FIELD_TRIALS = 2,      /**< uint32_t[]: number of trials per cell. */
    RW_SNAP_FIELD_SUM_STEPS = 3,   /**< uint64_t[]: sum of steps per cell. */
    RW_SNAP_FIELD_SUCC_LEQ_K = 4   /**< uint32_t[]: successes within K per cell. */
} rw_snapshot_field_t;

/**
 * @brief Payload of a SNAPSHOT_BEGIN message.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t snapshot_id;
    rw_wire_size_t size;
    rw_wire_world_kinds_t world_kind;

    uint32_t cell_count;       /**< size.width * size.height */
    uint32_t included_fields;  /**< Bitmask of `rw_snapshot_field_t` values. */
} rw_snapshot_begin_t;
#pragma pack(pop)

/**
 * @brief Payload of a SNAPSHOT_CHUNK message.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t snapshot_id;
    uint16_t field;        /**< `rw_snapshot_field_t` */
    uint16_t reserved;
    uint32_t offset_bytes; /**< Offset from start of field data. */
    uint32_t data_len;     /**< Valid data length in @ref data. */
    uint8_t data[RW_SNAPSHOT_CHUNK_MAX];
} rw_snapshot_chunk_t;
#pragma pack(pop)

/**
 * @brief Wire representation of the server simulation state.
 */
typedef enum {
    RW_WIRE_SIM_LOBBY = 1,    /**< Configurable, not running. */
    RW_WIRE_SIM_RUNNING = 2,  /**< Running replications. */
    RW_WIRE_SIM_FINISHED = 3  /**< Completed (or stopped). */
} rw_wire_sim_state_t;

#pragma pack(push, 1)
/**
 * @brief Payload of a QUERY_STATUS request.
 */
typedef struct {
    uint32_t pid;
} rw_query_status_t;

/**
 * @brief Payload of a STATUS response.
 */
typedef struct {
    rw_wire_sim_state_t state;

    uint8_t multi_user;   /**< 0=single-user, 1=multi-user */
    uint8_t can_control;  /**< 1 if the server considers this client allowed to control */
    uint16_t reserved;

    rw_wire_world_kinds_t world_kind;
    rw_wire_size_t size;

    rw_wire_move_probs_t probs;
    uint32_t k_max_steps;
    uint32_t total_reps;
    uint32_t current_rep;

    rw_wire_global_mode_t global_mode;
} rw_status_t;

/**
 * @brief Payload for CREATE_SIM.
 */
typedef struct {
    rw_wire_world_kinds_t world_kind;
    rw_wire_size_t size;

    rw_wire_move_probs_t probs;
    uint32_t k_max_steps;
    uint32_t total_reps;

    uint8_t multi_user; /**< 0=single-user, 1=multi-user */
    uint8_t reserved8[3];
} rw_create_sim_t;

/**
 * @brief Payload for LOAD_WORLD.
 */
typedef struct {
    char path[RW_PATH_MAX];
    uint8_t multi_user; /**< 0=single-user, 1=multi-user */
    uint8_t reserved8[3];
} rw_load_world_t;

/**
 * @brief Payload for RESTART_SIM.
 */
typedef struct {
    uint32_t total_reps;
} rw_restart_sim_t;

/**
 * @brief Payload for LOAD_RESULTS.
 */
typedef struct {
    char path[RW_PATH_MAX];
} rw_load_results_t;

/**
 * @brief Payload for SAVE_RESULTS.
 */
typedef struct {
    char path[RW_PATH_MAX];
} rw_save_results_t;

/**
 * @brief Payload for REQUEST_SNAPSHOT.
 */
typedef struct {
    uint32_t pid;
} rw_request_snapshot_t;

/**
 * @brief Payload for QUIT.
 */
typedef struct {
    uint32_t pid;
    uint8_t stop_if_owner; /**< if 1 and client is owner, server stops running sim */
    uint8_t reserved8[3];
} rw_quit_t;

/**
 * @brief Payload of an ACK message.
 */
typedef struct {
    uint16_t request_type; /**< original request type being acknowledged */
    uint16_t status;       /**< 0=ok, nonzero=error */
} rw_ack_t;
#pragma pack(pop)

/**
 * @brief Payload of a STOP_SIM message.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t pid; /**< Requesting client pid. */
} rw_stop_sim_t;

/**
 * @brief Payload of an END message.
 */
typedef struct {
    uint32_t reason; /**< 0=done_all_reps, 1=stopped_by_client */
} rw_end_t;

/**
 * @brief Payload of an ERROR message.
 */
typedef struct {
    uint32_t error_code;
    char error_msg[256]; /**< Null-terminated error string. */
} rw_error_t;
#pragma pack(pop)

/**
 * @brief Send a message header and optional payload.
 *
 * Blocking write: writes the complete header and then `payload_len` bytes.
 *
 * @param fd Connected socket.
 * @param type Message type.
 * @param payload Pointer to payload bytes (may be NULL only if @p payload_len is 0).
 * @param payload_len Payload size in bytes.
 * @return 0 on success, -1 on error.
 */
int rw_send_msg(int fd, rw_msg_type_t type, const void *payload, uint32_t payload_len);

/**
 * @brief Best-effort, non-blocking variant of @ref rw_send_msg.
 *
 * This is intended for broadcast-style notifications (PROGRESS/END/etc.) where
 * blocking the producer thread would be worse than dropping an update for a
 * slow client.
 *
 * Semantics:
 * - The socket is not modified (no O_NONBLOCK flag changes).
 * - Uses MSG_DONTWAIT, so it may fail with EAGAIN/EWOULDBLOCK.
 * - Returns 0 on success; -1 on any error (including would-block).
 */
int rw_send_msg_noblock(int fd, rw_msg_type_t type, const void *payload, uint32_t payload_len);

/**
 * @brief Receive a message header.
 *
 * Blocking read: reads exactly `sizeof(rw_msg_hdr_t)` bytes.
 *
 * @param fd Connected socket.
 * @param out_hdr Output header structure.
 * @return 0 on success, -1 on EOF/error.
 */
int rw_recv_hdr(int fd, rw_msg_hdr_t *out_hdr);

/**
 * @brief Receive exactly @p len payload bytes.
 *
 * @param fd Connected socket.
 * @param buf Output buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 on EOF/error.
 */
int rw_recv_payload(int fd, void *buf, uint32_t len);

#endif //SEMPRACA_PROTOCOL_H

