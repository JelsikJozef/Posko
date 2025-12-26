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
    uint32_t inlcuded_fields;  /**< Bitmask of `rw_snapshot_field_t` values. */
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

