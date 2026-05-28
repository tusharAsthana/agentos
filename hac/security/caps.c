/*
 * AgentOS HAC — Capability Model Implementation
 */
#include "caps.h"
#include "../core/hac.h"

/* Per-VMID capability table */
static uint32_t g_caps[MAX_VMID];

/* Audit ring buffer */
static audit_entry_t g_audit[AUDIT_LOG_SIZE];
static uint32_t      g_audit_head = 0;

void caps_init(void)
{
    for (int i = 0; i < MAX_VMID; i++) g_caps[i] = 0;
    g_caps[0] = CAP_SUPERVISOR;   /* VMID 0 = kernel/supervisor: all caps */
    hac_uart_puts("[CAPS] Capability model initialized\r\n");
}

void caps_set(uint32_t vmid, uint32_t cap_mask)
{
    if (vmid >= MAX_VMID) return;
    g_caps[vmid] = cap_mask;
}

uint32_t caps_get(uint32_t vmid)
{
    if (vmid >= MAX_VMID) return 0;
    return g_caps[vmid];
}

void caps_grant(uint32_t vmid, uint32_t cap_bit)
{
    if (vmid >= MAX_VMID) return;
    g_caps[vmid] |= cap_bit;
}

void caps_revoke(uint32_t vmid, uint32_t cap_bit)
{
    if (vmid >= MAX_VMID) return;
    g_caps[vmid] &= ~cap_bit;
}

/* Read current VMID from VPIDR_EL2 (bits [7:0] = VMID on A11) */
static uint32_t current_vmid(void)
{
    /* On real hardware: read VTTBR_EL2[55:48] for VMID.
     * For now, kernel always runs as VMID 0. Apps get VMID 1+
     * when HAC configures their stage-2 context. */
    uint64_t vttbr;
    asm volatile("mrs %0, VTTBR_EL2" : "=r"(vttbr));
    return (uint32_t)((vttbr >> 48) & 0xFF);
}

uint32_t caps_get_current(void)
{
    return caps_get(current_vmid());
}

uint32_t caps_required_for(hac_hvc_t hvc_id)
{
    if (hvc_id >= HAC_HVC_DISPLAY_BLIT && hvc_id <= HAC_HVC_DISPLAY_INFO)
        return CAP_DISPLAY;
    if (hvc_id >= HAC_HVC_TOUCH_POLL && hvc_id <= HAC_HVC_TOUCH_ENABLE)
        return CAP_TOUCH;
    if (hvc_id >= HAC_HVC_WIFI_TX && hvc_id <= HAC_HVC_WIFI_STATUS)
        return CAP_WIFI;
    if (hvc_id >= HAC_HVC_STORAGE_READ && hvc_id <= HAC_HVC_STORAGE_FLUSH)
        return CAP_STORAGE;
    if (hvc_id >= HAC_HVC_GPIO_SET && hvc_id <= HAC_HVC_GPIO_DIRECTION)
        return CAP_GPIO;
    if (hvc_id >= HAC_HVC_SENSOR_READ && hvc_id <= HAC_HVC_SENSOR_ENABLE)
        return CAP_SENSORS;
    if (hvc_id >= HAC_HVC_CAMERA_CAPTURE && hvc_id <= HAC_HVC_CAMERA_STOP)
        return CAP_CAMERA;
    if (hvc_id >= HAC_HVC_AUDIO_PLAY && hvc_id <= HAC_HVC_AUDIO_VOLUME)
        return CAP_AUDIO;
    if (hvc_id >= HAC_HVC_POWER_SLEEP && hvc_id <= HAC_HVC_BATTERY_STATUS)
        return CAP_POWER;
    if (hvc_id == HAC_HVC_MODEM_AT)
        return CAP_MODEM;
    if (hvc_id >= HAC_HVC_SEP_ENCRYPT && hvc_id <= HAC_HVC_SEP_DECRYPT)
        return CAP_SEP;
    if (hvc_id >= HAC_HVC_INFER_PROMPT && hvc_id <= HAC_HVC_INFER_STATUS)
        return CAP_INFERENCE;
    if (hvc_id == HAC_HVC_GRANT_CAP || hvc_id == HAC_HVC_REVOKE_CAP)
        return CAP_ADMIN;
    return 0; /* Always allowed (VERSION, GET_CAPS) */
}

bool caps_check_current(hac_hvc_t hvc_id)
{
    uint32_t required = caps_required_for(hvc_id);
    if (required == 0) return true;
    return (caps_get_current() & required) != 0;
}

void caps_audit(hac_hvc_t hvc_id, uint64_t arg1)
{
    uint64_t ts;
    asm volatile("mrs %0, CNTVCT_EL0" : "=r"(ts));

    audit_entry_t *e = &g_audit[g_audit_head % AUDIT_LOG_SIZE];
    e->vmid      = current_vmid();
    e->hvc_id    = (uint32_t)hvc_id;
    e->arg1      = arg1;
    e->timestamp = ts;
    e->result    = 0; /* filled after dispatch */
    g_audit_head++;
}
