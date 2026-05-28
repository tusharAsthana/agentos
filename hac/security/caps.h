/*
 * AgentOS HAC — Capability Model
 *
 * Every EL0 container (app) and EL1 (kernel/supervisor) is identified by a
 * VMID (Virtual Machine ID). The HAC maintains a per-VMID capability bitmask
 * that controls which hardware primitives each caller may invoke.
 *
 * Default capability set:
 *   - Kernel/Supervisor (VMID 0): all capabilities
 *   - New app containers (VMID 1+): DISPLAY + TOUCH only
 *   - Supervisor explicitly grants additional caps before spawning an app
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../primitives/primitives.h"

/* ---------------------------------------------------------------
 * Capability bits (one per hardware class)
 * --------------------------------------------------------------- */
#define CAP_DISPLAY     (1U << 0)
#define CAP_TOUCH       (1U << 1)
#define CAP_WIFI        (1U << 2)
#define CAP_STORAGE     (1U << 3)
#define CAP_GPIO        (1U << 4)
#define CAP_SENSORS     (1U << 5)
#define CAP_CAMERA      (1U << 6)
#define CAP_AUDIO       (1U << 7)
#define CAP_POWER       (1U << 8)
#define CAP_MODEM       (1U << 9)
#define CAP_SEP         (1U << 10)
#define CAP_INFERENCE   (1U << 11)
#define CAP_ADMIN       (1U << 31)  /* grant/revoke caps of others */

#define CAP_DEFAULT_APP  (CAP_DISPLAY | CAP_TOUCH)
#define CAP_SUPERVISOR   (0xFFFFFFFFU)  /* all caps */

#define MAX_VMID         64

/* ---------------------------------------------------------------
 * Audit log entry
 * --------------------------------------------------------------- */
#define AUDIT_LOG_SIZE   4096

typedef struct __attribute__((packed)) {
    uint32_t vmid;
    uint32_t hvc_id;
    uint64_t arg1;
    uint64_t timestamp;   /* CPU cycle counter */
    int32_t  result;      /* HAC_OK or error */
    uint32_t _pad;
} audit_entry_t;

/* ---------------------------------------------------------------
 * API
 * --------------------------------------------------------------- */
void    caps_init(void);
void    caps_set(uint32_t vmid, uint32_t cap_mask);
uint32_t caps_get(uint32_t vmid);
void    caps_grant(uint32_t vmid, uint32_t cap_bit);
void    caps_revoke(uint32_t vmid, uint32_t cap_bit);

/* Check capability for the *current* caller (derived from VMID in VPIDR_EL2) */
bool    caps_check_current(hac_hvc_t hvc_id);
uint32_t caps_get_current(void);

/* Audit */
void    caps_audit(hac_hvc_t hvc_id, uint64_t arg1);

/* Map HVC id to required capability bit */
uint32_t caps_required_for(hac_hvc_t hvc_id);
