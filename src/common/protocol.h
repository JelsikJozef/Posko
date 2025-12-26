//
// Created by Jozef Jelšík on 26/12/2025.
//

#ifndef SEMPRACA_PROTOCOL_H
#define SEMPRACA_PROTOCOL_H

/*
 * protocol.h - simple IPC protocol client <-> server via AF_UNIX (SOCK_STREAM).
 * Format:
 * - Each message = header + payload
 * - Header has type and length of payload
 * - Snapshots are sent as separate messages (BEGIN/CHUNK/END),
 * to allow streaming large data
 */

#include <stdint.h>
#include <stddef.h>

//Max payload for one chunk (without header)
#define RW_SNAPSHOT_CHUNK_MAX 4096u

/* ====== Message types ====== */
typedef enum {
    RW_MSG_JOIN = 1,        //Client -> Server: join request
    RW_MSG_WELCOME = 2,     //Server -> Client: welcome response

    RW_MSG_SET_GLOBAL_MODE = 3, //Client -> Server: set global mode
    RW_MSG_GLOBAL_MODE_CHANGED = 4, //Server -> all clients: global mode changed notification

    RW_MSG_PROGRESS = 5,    //Server -> all clients: progress update

    RW_MSG_SNAPSHOT_BEGIN = 6, //Server -> Client : begin snapshot transfer
    RW_MSG_SNAPSHOT_CHUNK = 7, //Server -> Client : snapshot data chunk
    RW_MSG_SNAPSHOT_END = 8,   //Server -> Client : end snapshot transfer

    RW_MSG_STOP_SIM = 9,   //Client -> Server: stop simulation request
    RW_MSG_END = 10,     //Server -> All clients: simulation ended notification

    RW_MSG_ERROR = 255     //Server -> Client: error message
}rw_msg_type_t;

/*===== Wire enums ======*/

typedef enum {
    RW_WIRE_MODE_INTERACTIVE = 1,
    RW_WIRE_MODE_SUMMARY = 2,
}rw_wire_global_mode_t;

typedef enum {
    RW_WIRE_WORLD_WRAP = 1, //World wraps around edges
    RW_WIRE_WORLD_OBSTACLES = 2, //World has obstacles
}rw_wire_world_kinds_t;

/* ====== Wire basic types ====== */
typedef struct {
    int32_t x;
    int32_t y;
}rw_wire_pos_t;

typedef struct {
    uint32_t width;
    uint32_t height;
}rw_wire_size_t;

typedef struct {
    double p_up;
    double p_down;
    double p_left;
    double p_right;
} rw_wire_move_probs_t;

/* ====== Message header ====== */

#pragma pack(push, 1)
typedef struct {
    uint16_t type;      //rw_msg_type_t
    uint16_t reserved;
    uint32_t payload_len;    //Payload length in bytes
}rw_msg_hdr_t;
#pragma pack(pop)

/* ====== Message payloads ====== */

/*JOIN: client connects to an existing server*/
#pragma pack(push, 1)
typedef struct {
    uint32_t pid;
}rw_join_t;
#pragma pack(pop)

/*WELCOME: server welcomes client with simulation parameters and state*/
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
}rw_welcome_t;
#pragma pack(pop)

/*SET_GLOBAL_MODE: client requests changing global mode for all*/
#pragma pack(push, 1)
typedef struct {
    rw_wire_global_mode_t new_mode;
}rw_set_global_mode_t;


/*GLOBAL_MODE_CHANGED: server notifies all clients of global mode change*/
typedef struct {
    rw_wire_global_mode_t new_mode;
    uint32_t changed_by_pid;
}rw_global_mode_changed_t;
#pragma pack(pop)

/*PROGRESS: server notifies all clients of simulation progress*/
#pragma pack(push, 1)
typedef struct {
    uint32_t current_rep;
    uint32_t total_reps;
}rw_progress_t;
#pragma pack(pop)

/* ====SNAPSHOT (chunked)======
 * Stats sent by fields in order
 * index = y * width + x
 *
 * Fields:
 * -obstacles[cell_count] (uint8_t): 1 if obstacle, 0 else
 * -trials[cell_count] (uint32_t): number of trials in cell
 * -sum_steps[cell_count] (uint64_t): sum of steps in cell
 * -succes_leq_k[cell_count] (uint32_t): number of successes within k steps in cell
 *
 * Client calculates:
 * avg = sum_steps / trials
 * prob = succes_leq_k / trials
 */

typedef enum {
    RW_SNAP_FIELD_OBSTACLES = 1,   //uint8_t[]
    RW_SNAP_FIELD_TRIALS = 2,      //uint32_t[]
    RW_SNAP_FIELD_SUM_STEPS = 3,   //uint64_t[]
    RW_SNAP_FIELD_SUCC_LEQ_K = 4   //uint32_t[]
}rw_snapshot_field_t;

/*SNAPSHOT_BEGIN: server begins snapshot transfer*/
#pragma pack(push, 1)
typedef struct {
    uint32_t snapshot_id;
    rw_wire_size_t size;
    rw_wire_world_kinds_t world_kind;

    uint32_t cell_count; //size.width * size.height
    uint32_t inlcuded_fields; //bitmask of rw_snapshot_field_t
}rw_snapshot_begin_t;
#pragma pack(pop)

/*SNAPSHOT_CHUNK: server sends snapshot data chunk*/
#pragma pack(push, 1)
typedef struct {
    uint32_t snapshot_id;
    uint16_t field;         //rw_snapshot_field_t
    uint16_t reserved;
    uint32_t offset_bytes; //Offset in field data
    uint32_t data_len;    //Length of data in bytes
    uint8_t data[RW_SNAPSHOT_CHUNK_MAX];
}rw_snapshot_chunk_t;
#pragma pack(pop)

/*SNAPSHOT_END: server ends snapshot transfer*/
#pragma pack(push, 1)
typedef struct {
    uint32_t pid;
}rw_stop_sim_t;

typedef struct {
    uint32_t reason; //0=done_all_reps, 1=stopped_by_client
}rw_end_t;

typedef struct {
    uint32_t error_code;
    char error_msg[256]; //Null-terminated string
}rw_error_t;
#pragma pack(pop)

/*=================
 *Send and receive helpers
 *=================*/
int rw_send_msg(int fd, rw_msg_type_t, const void *payload, uint32_t payload_len);
int rw_recv_hdr(int fd, rw_msg_hdr_t *out_hdr);
int rw_recv_payload(int fd, void *buf, uint32_t len);





#endif //SEMPRACA_PROTOCOL_H