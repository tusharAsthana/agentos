/*
 * AgentOS HAC — IPC Ring Buffer Implementation
 */
#include "ipc.h"
#include "../core/hac.h"
#include <string.h>

volatile ipc_shm_t *g_ipc_shm = NULL;
static uint32_t g_seq = 0;

void ipc_init(volatile void *shm_pa, size_t shm_size)
{
    (void)shm_size;
    g_ipc_shm = (volatile ipc_shm_t *)shm_pa;

    /* Zero the shared region */
    volatile uint8_t *p = (volatile uint8_t *)shm_pa;
    for (size_t i = 0; i < sizeof(ipc_shm_t); i++) p[i] = 0;

    g_ipc_shm->magic   = 0xA6E0510U;  /* "AGENTOS1" compressed */
    g_ipc_shm->version = 0x00010000;
    g_ipc_shm->el2_head = g_ipc_shm->el2_tail = 0;
    g_ipc_shm->el1_head = g_ipc_shm->el1_tail = 0;

    /* Memory barrier so EL1 sees the magic before it probes */
    asm volatile("dsb sy\nisb" : : : "memory");
    hac_uart_puts("[IPC] Shared memory ring buffers initialized\r\n");
}

bool ipc_el2_push(ipc_msg_type_t type, const void *payload, uint16_t len)
{
    if (!g_ipc_shm) return false;
    uint32_t head = g_ipc_shm->el2_head;
    uint32_t next = (head + 1) % IPC_RING_CAPACITY;
    if (next == g_ipc_shm->el2_tail) return false;  /* ring full */

    volatile ipc_msg_t *msg = &g_ipc_shm->el2_ring[head];
    msg->type  = (uint8_t)type;
    msg->flags = 0;
    msg->seq   = ++g_seq;
    msg->len   = (len > IPC_MSG_PAYLOAD_MAX) ? IPC_MSG_PAYLOAD_MAX : len;

    volatile uint8_t *dst = msg->payload;
    const uint8_t    *src = (const uint8_t *)payload;
    for (uint16_t i = 0; i < msg->len; i++) dst[i] = src[i];

    asm volatile("dsb sy" : : : "memory");
    g_ipc_shm->el2_head = next;
    return true;
}

bool ipc_el1_pop(ipc_msg_t *out)
{
    if (!g_ipc_shm) return false;
    uint32_t tail = g_ipc_shm->el1_tail;
    if (tail == g_ipc_shm->el1_head) return false;  /* ring empty */

    volatile ipc_msg_t *msg = &g_ipc_shm->el1_ring[tail];
    out->type  = msg->type;
    out->flags = msg->flags;
    out->seq   = msg->seq;
    out->len   = msg->len;
    volatile uint8_t *src = msg->payload;
    uint8_t          *dst = out->payload;
    for (uint16_t i = 0; i < msg->len && i < IPC_MSG_PAYLOAD_MAX; i++) dst[i] = src[i];

    asm volatile("dsb sy" : : : "memory");
    g_ipc_shm->el1_tail = (tail + 1) % IPC_RING_CAPACITY;
    return true;
}
