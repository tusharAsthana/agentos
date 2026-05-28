/*
 * AgentOS HAC — EL2↔EL1 IPC Ring Buffer
 *
 * Placed in a shared physical region (HAC_IPC_SHMEM_PA) visible to both
 * EL2 (HAC) and EL1 (Linux kernel, via the /dev/hac driver).
 *
 * Two independent ring buffers:
 *   EL2→EL1 (notification ring): HAC pushes hardware events (touch, IRQ, etc.)
 *   EL1→EL2 (command ring):      Kernel pushes async hardware requests
 *
 * For synchronous HVC calls, rings are NOT used — those return immediately.
 * Rings are for unsolicited events (e.g. touch interrupt, WiFi packet ready).
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define IPC_RING_CAPACITY    256   /* entries per ring */
#define IPC_MSG_PAYLOAD_MAX  56    /* bytes of payload per message */

/* Message types */
typedef enum {
    IPC_MSG_NONE        = 0,
    /* EL2→EL1 notifications */
    IPC_MSG_TOUCH_EVENT = 0x01,  /* touch_event_t in payload */
    IPC_MSG_WIFI_RX     = 0x02,  /* WiFi packet ready at PA in payload */
    IPC_MSG_SENSOR_IRQ  = 0x03,  /* sensor_data_t in payload */
    IPC_MSG_BATTERY_LOW = 0x04,  /* battery_status_t in payload */
    IPC_MSG_CAMERA_FRAME= 0x05,  /* Frame ready at PA in payload */
    IPC_MSG_POWER_BTN   = 0x06,  /* Power button event */
    IPC_MSG_VOL_BTN     = 0x07,  /* Volume button */
    /* EL1→EL2 async requests */
    IPC_MSG_INFER_REQ   = 0x80,  /* Start LLM inference (prompt in payload) */
    IPC_MSG_INFER_RESP  = 0x81,  /* Inference complete (result PA in payload) */
    IPC_MSG_LOG         = 0xFF,  /* Debug log string */
} ipc_msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;              /* ipc_msg_type_t */
    uint8_t  flags;             /* IPC_FLAG_* */
    uint16_t len;               /* payload length */
    uint32_t seq;               /* sequence number */
    uint8_t  payload[IPC_MSG_PAYLOAD_MAX];
} ipc_msg_t;

#define IPC_FLAG_URGENT   0x01
#define IPC_FLAG_RESPONSE 0x02

/* Ring buffer header (at start of shared region) */
typedef struct __attribute__((packed)) {
    /* EL2→EL1 ring (HAC produces, kernel consumes) */
    volatile uint32_t el2_head;   /* producer write pointer */
    volatile uint32_t el2_tail;   /* consumer read pointer */
    /* EL1→EL2 ring (kernel produces, HAC consumes) */
    volatile uint32_t el1_head;
    volatile uint32_t el1_tail;
    uint32_t magic;               /* 0xAGENT05 */
    uint32_t version;
    uint8_t  _pad[40];
    /* Ring data follows immediately after header */
    ipc_msg_t el2_ring[IPC_RING_CAPACITY];
    ipc_msg_t el1_ring[IPC_RING_CAPACITY];
} ipc_shm_t;

/* ---------------------------------------------------------------
 * API
 * --------------------------------------------------------------- */
void ipc_init(volatile void *shm_pa, size_t shm_size);

/* EL2: push event to EL1 */
bool ipc_el2_push(ipc_msg_type_t type, const void *payload, uint16_t len);

/* EL2: consume command from EL1 */
bool ipc_el1_pop(ipc_msg_t *out);

/* Pointer to the shared region (for EL1 driver to use directly) */
extern volatile ipc_shm_t *g_ipc_shm;
