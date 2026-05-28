/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AgentOS — HAC Hypercall Interface for Linux kernel drivers
 *
 * These drivers replace traditional hardware drivers.
 * Every hardware operation goes via HVC to the HAC at EL2.
 *
 * Calling convention:
 *   x0 = HAC_HVC_* id, x1..x7 = args → hvc #0 → x0 = return value
 */
#ifndef _HAC_HYPERCALLS_H
#define _HAC_HYPERCALLS_H

#include <linux/types.h>

/* ---------------------------------------------------------------
 * HVC call IDs (mirror of hac/primitives/primitives.h)
 * --------------------------------------------------------------- */
#define HAC_HVC_VERSION         0x0000
#define HAC_HVC_GET_CAPS        0x0001
#define HAC_HVC_GRANT_CAP       0x0002
#define HAC_HVC_REVOKE_CAP      0x0003

#define HAC_HVC_DISPLAY_BLIT    0x0100
#define HAC_HVC_DISPLAY_BLANK   0x0101
#define HAC_HVC_DISPLAY_BRIGHT  0x0102
#define HAC_HVC_DISPLAY_INFO    0x0103

#define HAC_HVC_TOUCH_POLL      0x0200
#define HAC_HVC_TOUCH_ENABLE    0x0201

#define HAC_HVC_WIFI_TX         0x0300
#define HAC_HVC_WIFI_RX         0x0301
#define HAC_HVC_WIFI_SCAN       0x0302
#define HAC_HVC_WIFI_CONNECT    0x0303
#define HAC_HVC_WIFI_DISCONNECT 0x0304
#define HAC_HVC_WIFI_STATUS     0x0305

#define HAC_HVC_STORAGE_READ    0x0400
#define HAC_HVC_STORAGE_WRITE   0x0401
#define HAC_HVC_STORAGE_INFO    0x0402
#define HAC_HVC_STORAGE_FLUSH   0x0403

#define HAC_HVC_GPIO_SET        0x0500
#define HAC_HVC_GPIO_GET        0x0501
#define HAC_HVC_GPIO_DIRECTION  0x0502

#define HAC_HVC_SENSOR_READ     0x0600
#define HAC_HVC_SENSOR_ENABLE   0x0601

#define HAC_HVC_CAMERA_CAPTURE  0x0700
#define HAC_HVC_CAMERA_STREAM   0x0701
#define HAC_HVC_CAMERA_STOP     0x0702

#define HAC_HVC_AUDIO_PLAY      0x0800
#define HAC_HVC_AUDIO_RECORD    0x0801
#define HAC_HVC_AUDIO_STOP      0x0802
#define HAC_HVC_AUDIO_VOLUME    0x0803

#define HAC_HVC_POWER_SLEEP     0x0900
#define HAC_HVC_POWER_WAKE      0x0901
#define HAC_HVC_BATTERY_STATUS  0x0902

#define HAC_HVC_MODEM_AT        0x0A00

#define HAC_HVC_SEP_ENCRYPT     0x0B00
#define HAC_HVC_SEP_DECRYPT     0x0B01

#define HAC_HVC_INFER_PROMPT    0x0C00
#define HAC_HVC_INFER_STATUS    0x0C01

/* ---------------------------------------------------------------
 * Return codes
 * --------------------------------------------------------------- */
#define HAC_OK          0
#define HAC_EPERM       (-1)
#define HAC_EINVAL      (-2)
#define HAC_EBUSY       (-3)
#define HAC_ETIMEOUT    (-4)
#define HAC_ENOMEM      (-5)
#define HAC_ENODEV      (-6)

/* ---------------------------------------------------------------
 * IPC shared memory layout (mirrored from hac/ipc/ipc.h)
 * ---------------------------------------------------------------
 * Physical address HAC_IPC_SHMEM_PA = 0x84E000000 is mapped into
 * kernel virtual space by the /dev/hac driver at probe time.
 * --------------------------------------------------------------- */
#define HAC_IPC_SHMEM_PA    0x84E000000ULL
#define HAC_IPC_SHMEM_SIZE  0x00400000ULL
#define IPC_RING_CAPACITY   256
#define IPC_MSG_PAYLOAD_MAX 56

#define IPC_SHM_MAGIC       0x0A6E0510U

struct hac_ipc_msg {
    u8  type;
    u8  flags;
    u16 len;
    u32 seq;
    u8  payload[IPC_MSG_PAYLOAD_MAX];
} __packed;

struct hac_ipc_shm {
    volatile u32 el2_head, el2_tail;
    volatile u32 el1_head, el1_tail;
    u32 magic;
    u32 version;
    u8  _pad[40];
    struct hac_ipc_msg el2_ring[IPC_RING_CAPACITY];
    struct hac_ipc_msg el1_ring[IPC_RING_CAPACITY];
} __packed;

/* ---------------------------------------------------------------
 * Inline HVC call helpers
 * --------------------------------------------------------------- */
static inline long hac_hvc0(u64 id)
{
    register u64 x0 asm("x0") = id;
    asm volatile("hvc #0" : "+r"(x0) : : "x1","x2","x3","x4","x5","x6","x7","memory");
    return (long)x0;
}

static inline long hac_hvc1(u64 id, u64 a1)
{
    register u64 x0 asm("x0") = id;
    register u64 x1 asm("x1") = a1;
    asm volatile("hvc #0" : "+r"(x0) : "r"(x1) : "x2","x3","x4","x5","x6","x7","memory");
    return (long)x0;
}

static inline long hac_hvc2(u64 id, u64 a1, u64 a2)
{
    register u64 x0 asm("x0") = id;
    register u64 x1 asm("x1") = a1;
    register u64 x2 asm("x2") = a2;
    asm volatile("hvc #0" : "+r"(x0) : "r"(x1),"r"(x2) : "x3","x4","x5","x6","x7","memory");
    return (long)x0;
}

static inline long hac_hvc3(u64 id, u64 a1, u64 a2, u64 a3)
{
    register u64 x0 asm("x0") = id;
    register u64 x1 asm("x1") = a1;
    register u64 x2 asm("x2") = a2;
    register u64 x3 asm("x3") = a3;
    asm volatile("hvc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3) : "x4","x5","x6","x7","memory");
    return (long)x0;
}

static inline long hac_hvc5(u64 id, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
    register u64 x0 asm("x0") = id;
    register u64 x1 asm("x1") = a1;
    register u64 x2 asm("x2") = a2;
    register u64 x3 asm("x3") = a3;
    register u64 x4 asm("x4") = a4;
    register u64 x5 asm("x5") = a5;
    asm volatile("hvc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5) : "x6","x7","memory");
    return (long)x0;
}

#endif /* _HAC_HYPERCALLS_H */
